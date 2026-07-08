#include "types.h"
#include "pvr_if.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

//==============================================================================
// PowerVR Interface Implementation
// Handles YUV conversion, register access, and VRAM operations
//==============================================================================

//------------------------------------------------------------------------------
// YUV Converter Constants
//------------------------------------------------------------------------------
namespace {
    constexpr u32 YUV_TEMP_BUFFER_SIZE = 512; // bytes
    constexpr u32 YUV_BLOCK_SIZE_420 = 384;   // YUV 4:2:0 format
    constexpr u32 YUV_BLOCK_SIZE_422 = 512;   // YUV 4:2:2 format
    constexpr u32 YUV_MACROBLOCK_SIZE = 16;   // 16x16 pixels per macroblock
    
    // Register addresses
    constexpr u32 TA_YUV_TEX_BASE_ADDR = 0x5F8148;
    constexpr u32 TA_YUV_TEX_CTRL_ADDR = 0x5F814C;
    constexpr u32 TA_YUV_TEX_CNT_ADDR  = 0x5F8150;
}

//------------------------------------------------------------------------------
// YUV Converter State
//------------------------------------------------------------------------------
static u32 YUV_tempdata[YUV_TEMP_BUFFER_SIZE / 4]; // Temporary conversion buffer
static u32 YUV_index = 0;          // Current position in temp buffer
static u32 YUV_dest = 0;           // Destination address in VRAM
static u32 YUV_doneblocks = 0;     // Number of completed macroblocks
static u32 YUV_blockcount = 0;     // Total macroblocks to process

static u32 YUV_x_curr = 0;         // Current X position in output
static u32 YUV_y_curr = 0;         // Current Y position in output
static u32 YUV_x_size = 0;         // Output width in pixels
static u32 YUV_y_size = 0;         // Output height in pixels

//------------------------------------------------------------------------------
// YUV Helper Functions
//------------------------------------------------------------------------------

/**
 * Write 2 pixels (4 bytes) to VRAM at current position
 * @param x X offset from current position
 * @param y Y offset from current position
 * @param pixdata YUYV packed pixel data (2 pixels)
 */
static u32 yuv_oob_logged = 0; // one [YUV] line per session, not per pixel

INLINE void YUV_putpixel2(u32 x, u32 y, u32 pixdata)
{
    u32 offset = YUV_dest + (YUV_x_curr + x + (YUV_y_curr + y) * YUV_x_size) * 2;
    // Bounds guard: YUV_dest is masked at init, but dest + texture extent
    // (blocks_x/y up to 64 -> up to 2MB of output) can still run past the
    // 8MB VRAM buffer and stomp whatever the allocator placed after
    // vram.data — host memory corruption that shows up as weird geometry
    // and, if it reaches the GX FIFO, "GFX FIFO: Unknown Opcode".
    if (offset > VRAM_MASK - 3) {
        if (!yuv_oob_logged) {
            printf("[YUV] OOB write dropped: dest=%08X x=%u y=%u xsize=%u\n",
                   YUV_dest, YUV_x_curr + x, YUV_y_curr + y, YUV_x_size);
            yuv_oob_logged = 1;
        }
        return;
    }
    *(u32*)(&vram.data[offset]) = pixdata;
}

/**
 * Extract Y (luminance) component from 4:2:0 macroblock
 * @param x X coordinate (0-15)
 * @param y Y coordinate (0-15)
 * @param base Pointer to Y component data (256 bytes)
 * @return Y value
 */
INLINE u8 GetY420(int x, int y, u8* base)
{
    // 4:2:0 format stores Y in 8x8 sub-blocks
    if (x > 7) {
        x -= 8;
        base += 64;  // Move to right 8x8 block
    }
    
    if (y > 7) {
        y -= 8;
        base += 128; // Move to bottom 8x8 block
    }
    
    return *host_ptr_xor(&base[x + y * 8]);
}

/**
 * Extract U or V (chrominance) component from 4:2:0 macroblock
 * U and V are subsampled 2:1 in both dimensions (8x8 for 16x16 block)
 * @param x X coordinate (0-15)
 * @param y Y coordinate (0-15)
 * @param base Pointer to U or V component data (64 bytes)
 * @return U or V value
 */
INLINE u8 GetUV420(int x, int y, u8* base)
{
    // Subsample coordinates
    int realx = x >> 1;
    int realy = y >> 1;

    return *host_ptr_xor(&base[realx + realy * 8]);
}

/**
 * Initialize YUV converter state from hardware registers
 */
void YUV_init()
{
    YUV_index = 0;
    YUV_x_curr = 0;
    YUV_y_curr = 0;
    YUV_doneblocks = 0;
    
    // Read destination address and mask to valid VRAM range
    YUV_dest = pvr_readreg_TA(TA_YUV_TEX_BASE_ADDR, 4) & VRAM_MASK;
    
    // Read texture control register
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    // Calculate total number of macroblocks
    u32 blocks_x = ((TA_YUV_TEX_CTRL >> 0) & 0x3F) + 1;
    u32 blocks_y = ((TA_YUV_TEX_CTRL >> 8) & 0x3F) + 1;
    YUV_blockcount = blocks_x * blocks_y;
    
    // Determine output size
    if ((TA_YUV_TEX_CTRL >> 16) & 1) {
        // Small texture mode (single 16x16 block)
        YUV_x_size = YUV_MACROBLOCK_SIZE;
        YUV_y_size = YUV_MACROBLOCK_SIZE;
    } else {
        // Normal mode (multiple blocks)
        YUV_x_size = blocks_x * YUV_MACROBLOCK_SIZE;
        YUV_y_size = blocks_y * YUV_MACROBLOCK_SIZE;
    }
}

/**
 * Convert one YUV macroblock (16x16 pixels) to YUYV format and write to VRAM
 */
INLINE void YUV_ConvertMacroBlock()
{
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    YUV_doneblocks++;
    YUV_index = 0;
    
    // Determine block format
    bool is_yuv420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);
    
    if (is_yuv420) {
        // YUV 4:2:0 format (384 bytes per macroblock)
        // Layout: 64 bytes U, 64 bytes V, 256 bytes Y
        u8* U = (u8*)&YUV_tempdata[0];
        u8* V = (u8*)&YUV_tempdata[64 / 4];
        u8* Y = (u8*)&YUV_tempdata[(64 + 64) / 4];
        
        u8 yuyv[4]; // Temporary buffer for 2 pixels
        
        // Convert 16x16 block
        for (u32 y = 0; y < YUV_MACROBLOCK_SIZE; y++) {
            for (u32 x = 0; x < YUV_MACROBLOCK_SIZE; x += 2) {
                // Pack as DC LE UYVY u32 (U|Y0<<8|V<<16|Y1<<24).
                // Written as Wii BE u32, this stores bytes [Y1,V,Y0,U] in
                // vram.data — the same layout CPU 32-bit stores produce —
                // so the UYVY u32 decoder (Yu=word>>0, Y0=word>>8, ...) works
                // for both TA-YUV and CPU-written YUV422 textures.
                yuyv[0] = GetY420(x + 1, y, Y);  // Y1 = Wii BE MSB = DC byte 3
                yuyv[1] = GetUV420(x, y, V);      // V  = DC byte 2
                yuyv[2] = GetY420(x, y, Y);       // Y0 = DC byte 1
                yuyv[3] = GetUV420(x, y, U);      // U  = Wii BE LSB = DC byte 0
                
                // Write 2 pixels at once
                YUV_putpixel2(x, y, *(u32*)yuyv);
            }
        }
        
        // Advance to next macroblock position
        YUV_x_curr += YUV_MACROBLOCK_SIZE;
        if (YUV_x_curr >= YUV_x_size) {
            YUV_x_curr = 0;
            YUV_y_curr += YUV_MACROBLOCK_SIZE;
            if (YUV_y_curr >= YUV_y_size) {
                YUV_y_curr = 0;
            }
        }
    } else {
        // YUV 4:2:2 format not currently implemented
        printf("YUV 4:2:2 format not supported in YUV converter\n");
    }
    
    // Check if all blocks are processed
    if (YUV_doneblocks >= YUV_blockcount) {
        YUV_init();
        // Raise interrupt to signal completion
        asic_RaiseInterrupt(holly_YUV_DMA);
    }
}

/**
 * Process incoming YUV data
 * @param data Pointer to YUV data
 * @param count Number of 32-byte blocks
 */
void YUV_data(u32* data, u32 count)
{
    // Initialize if not already done
    if (YUV_blockcount == 0) {
        printf("YUV_data: YUV decoder not initialized, initializing now\n");
        YUV_init();
    }
    
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    // Determine block size based on format
    u32 block_size = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0)
                     ? YUV_BLOCK_SIZE_420
                     : YUV_BLOCK_SIZE_422;

    // Guard: TA_YUV_TEX_CTRL can change between partial-block writes. If
    // block_size shrinks below the already-buffered YUV_index, the
    // "block_size - YUV_index" below underflows (u32 -> ~4G), the full-block
    // branch is skipped and the partial-block memcpy runs with an unbounded
    // count — overrunning the 512-byte YUV_tempdata static buffer into
    // neighbouring BSS (vertex arenas, formerly the GX FIFO). Drop the
    // stale partial block instead.
    if (YUV_index >= block_size) {
        printf("[YUV] stale partial block dropped: index=%u block_size=%u\n",
               YUV_index, block_size);
        YUV_index = 0;
    }

    // Convert count from 32-byte blocks to bytes
    count *= 32;
    
    // Process data in chunks
    while (count > 0) {
        u32 bytes_til_block_end = block_size - YUV_index;
        
        if (count >= bytes_til_block_end) {
            // Complete at least one full block
            memcpy(&YUV_tempdata[YUV_index >> 2], data, bytes_til_block_end);
            data += bytes_til_block_end >> 2;
            count -= bytes_til_block_end;
            YUV_ConvertMacroBlock();
        } else {
            // Partial block - buffer it
            memcpy(&YUV_tempdata[YUV_index >> 2], data, count);
            YUV_index += count;
            count = 0;
        }
    }
}

//------------------------------------------------------------------------------
// Register Access Functions
//------------------------------------------------------------------------------

/**
 * Read from PVR Tile Accelerator register
 */
u32 pvr_readreg_TA(u32 addr, u32 sz)
{
    // Handle YUV block counter register specially
    if ((addr & 0xFFFFFF) == TA_YUV_TEX_CNT_ADDR) {
        return YUV_doneblocks;
    }
    
    // Delegate to plugin implementation
    return libPvr_ReadReg(addr, sz);
}

/**
 * Write to PVR Tile Accelerator register
 */
void pvr_writereg_TA(u32 addr, u32 data, u32 sz)
{
    // Delegate to plugin implementation
    libPvr_WriteReg(addr, data, sz);
    
    // Reinitialize YUV converter if base address is written
    if ((addr & 0xFFFFFF) == TA_YUV_TEX_BASE_ADDR) {
        YUV_init();
    }
}

//------------------------------------------------------------------------------
// VRAM Access Functions (32-bit to 64-bit address conversion)
//------------------------------------------------------------------------------

// [FBREAD] debug: per-frame aggregation of CPU reads through the 32-bit VRAM
// path (0x05xxxxxx) — the linear area games use to read the framebuffer back
// (Castlevania: Resurrection HALL shadow/reflection pass). The 0x04 area is
// direct-mapped by _vmem (no handler), so reads there cannot be logged; if a
// game breaks like a readback game but [FBREAD] stays silent, that is the
// remaining suspect. Armed once per rendered frame by StartRender() while the
// DEBUG MESSAGE preset is on (fbread_frame_tick dumps the previous frame's
// window and re-arms), so the printf cost never runs in normal play.
static int fbread_armed = 0;
static u32 fbread_count16, fbread_count32;
static u32 fbread_min, fbread_max;      // 24-bit VRAM offsets, pre-conversion
static u32 fbread_first[8][2];          // first N (addr, value) pairs verbatim
static u32 fbread_first_n;

static void fbread_record(u32 addr, u32 val, u32* count)
{
    u32 a = addr & 0x00FFFFFF;
    if ((fbread_count16 | fbread_count32) == 0)
        fbread_min = fbread_max = a;
    else if (a < fbread_min) fbread_min = a;
    else if (a > fbread_max) fbread_max = a;
    if (fbread_first_n < 8) {
        fbread_first[fbread_first_n][0] = a;
        fbread_first[fbread_first_n][1] = val;
        fbread_first_n++;
    }
    (*count)++;
}

// Called by the render plugin at every StartRender(). Prints the summary of
// CPU 32-bit-path VRAM reads seen since the previous StartRender (i.e. while
// the game computed this frame) — adjacent to that frame's [PATH] line, so
// the read range can be compared against FB_W_SOF1/FB_R_SOF1 directly.
extern "C" void fbread_frame_tick(int arm)
{
    if (fbread_armed && (fbread_count16 | fbread_count32)) {
        printf("[FBREAD] n16=%u n32=%u range=%06X..%06X\n",
               fbread_count16, fbread_count32, fbread_min, fbread_max);
        for (u32 i = 0; i < fbread_first_n; i++)
            printf("[FBREAD]   #%u addr=%06X val=%08X\n",
                   i, fbread_first[i][0], fbread_first[i][1]);
    }
    fbread_armed  = arm;
    fbread_count16 = fbread_count32 = 0;
    fbread_first_n = 0;
}

u8 FASTCALL pvr_read_area1_8(u32 addr)
{
    printf("Warning: 8-bit VRAM reads are not supported by hardware\n");
    return 0;
}

u16 FASTCALL pvr_read_area1_16(u32 addr)
{
    u32 raw = addr;
    addr = vramlock_ConvOffset32toOffset64(addr);
    u16 data = *host_ptr_xor((u16*)&vram[addr]);
    if (fbread_armed)
        fbread_record(raw, data, &fbread_count16);
    return data;
}

u32 FASTCALL pvr_read_area1_32(u32 addr)
{
    u32 raw = addr;
    addr = vramlock_ConvOffset32toOffset64(addr);
    u32 data = *(u32*)&vram[addr];
    if (fbread_armed)
        fbread_record(raw, data, &fbread_count32);
    return data;
}

void FASTCALL pvr_write_area1_8(u32 addr, u8 data)
{
    printf("Warning: 8-bit VRAM writes are not supported by hardware\n");
}

void FASTCALL pvr_write_area1_16(u32 addr, u16 data)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    *host_ptr_xor((u16*)&vram[addr]) = data;
}

void FASTCALL pvr_write_area1_32(u32 addr, u32 data)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    *(u32*)&vram[addr] = data;
}

//------------------------------------------------------------------------------
// Tile Accelerator DMA Interface
//------------------------------------------------------------------------------

/**
 * Write data to Tile Accelerator
 * Routes data based on target address:
 * - 0x000000-0x7FFFFF: TA polygon data
 * - 0x800000-0xFFFFFF: YUV converter
 * - 0x1000000+:        Direct VRAM write
 */
void FASTCALL TAWrite(u32 address, u32* data, u32 count)
{
    u32 address_masked = address & 0x1FFFFFF;
    
    if (address_masked < 0x800000) {
        // TA polygon data (0-8MB range)
        libPvr_TaDMA(data, count);
    } else if (address_masked < 0x1000000) {
        // YUV converter (8-16MB range)
        YUV_data(data, count);
    } else {
        // Direct VRAM write (16MB+ range)
        // Note: This works on real hardware, respects lock modes
        // Clamp: the base is masked but count*32 was not — a burst starting
        // near the top of VRAM would memcpy past the buffer (host memory
        // corruption -> weird geometry / GX FIFO desync).
        u32 vaddr = address & VRAM_MASK;
        u32 bytes = count * 32;
        if (bytes > VRAM_SIZE - vaddr) {
            printf("[VRAM] TAWrite clamped: addr=%08X count=%u\n", address, count);
            bytes = VRAM_SIZE - vaddr;
        }
        memcpy(&vram.data[vaddr], data, bytes);
    }
}

/**
 * Write single 32-byte block via Store Queue
 * Optimized path for SH4 store queue operations
 */
void FASTCALL TAWriteSQ(u32 address, u32* data)
{
    u32 address_masked = address & 0x1FFFFFF;
    
    if (address_masked < 0x800000) {
        // TA polygon data
        libPvr_TaSQ(data);
    } else if (address_masked < 0x1000000) {
        // YUV converter
        YUV_data(data, 1);
    } else {
        // Direct VRAM write (same clamp as TAWrite: masked base + fixed 32
        // bytes can still cross the end of the buffer by up to 31 bytes)
        u32 vaddr = address & VRAM_MASK;
        if (vaddr + 32 <= VRAM_SIZE)
            memcpy(&vram.data[vaddr], data, 32);
    }
}

//------------------------------------------------------------------------------
// Module Lifecycle
//------------------------------------------------------------------------------

void pvr_Init()
{
    // Reserved for future initialization
}

void pvr_Term()
{
    // Reserved for future cleanup
}

void pvr_Reset(bool Manual)
{
    // Clear VRAM on automatic reset only
    if (!Manual) {
        vram.Zero();
    }
    
    // Reset YUV converter state
    YUV_index = 0;
    YUV_dest = 0;
    YUV_doneblocks = 0;
    YUV_blockcount = 0;
    YUV_x_curr = 0;
    YUV_y_curr = 0;
    YUV_x_size = 0;
    YUV_y_size = 0;
}
