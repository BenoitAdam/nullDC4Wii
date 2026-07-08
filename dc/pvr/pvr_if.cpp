#include "types.h"
#include "pvr_if.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "dc/mem/sb.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

// LMMODE preset (wii/main.cpp): honor SB_LMMODE0/1 for the direct-VRAM TA
// windows (0x11xxxxxx / 0x13xxxxxx, reached via SQ writes and DMA). See the
// matching Ch2-DMA handling in dc/sh4/dmac.cpp for the full story.
extern "C" int get_lmmode_preset();
extern "C" int get_debug_message();
#define LMMODE_FIX() (get_lmmode_preset() == 1)

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
INLINE void YUV_putpixel2(u32 x, u32 y, u32 pixdata)
{
    u32 offset = YUV_dest + (YUV_x_curr + x + (YUV_y_curr + y) * YUV_x_size) * 2;
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

u8 FASTCALL pvr_read_area1_8(u32 addr)
{
    printf("Warning: 8-bit VRAM reads are not supported by hardware\n");
    return 0;
}

u16 FASTCALL pvr_read_area1_16(u32 addr)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    return *host_ptr_xor((u16*)&vram[addr]);
}

u32 FASTCALL pvr_read_area1_32(u32 addr)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    return *(u32*)&vram[addr];
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
// Area 4 texture-window write handlers (0x11xxxxxx / 0x13xxxxxx)
//------------------------------------------------------------------------------
// These 16MB pages had NO vmem mapping at all: SQ writes and Ch2-DMA reach the
// TA windows through dedicated code paths (TAWriteSQ / DMAC_Ch2St), but plain
// CPU stores and PVR-DMA per-word writes into the texture window fell through
// to the "not mapped" default and were dropped — any texture a game uploads
// that way stays zeros in VRAM (black/invisible polys). Mapped by map_area4()
// in sh4_mem.cpp. Behind the lmmode preset the writes land in VRAM over the
// bus SB_LMMODE0/1 selects (address bit 25 picks the register, 0x11 vs 0x13);
// preset off keeps the legacy drop, with a short log so the condition is
// visible in debug captures.

static u32 s_area4_drop_logs = 0;

static void area4_write_dropped(u32 addr)
{
    if (s_area4_drop_logs < 8)
    {
        s_area4_drop_logs++;
        printf("[pvr] Area4 texture-window write 0x%08X dropped (lmmode preset off)\n", addr);
    }
}

// Diagnostic: trace every distinct code path that can reach the TA texture
// windows (0x11/0x13), regardless of the lmmode preset — the earlier
// investigation found neither the Ch2-DMA nor the CPU-store Area4 branch
// ever fired for a texture that still read back as zeros, meaning the real
// upload must go through SQ (do_sqw -> TAWriteSQ) or bulk DMA (TAWrite),
// whose direct-VRAM branches previously had no logging at all. Rate-limited
// per call site so a real (working) upload doesn't spam every frame.
static u32 s_area4_trace_logs[4] = {0, 0, 0, 0};
enum { TR_TAWRITE = 0, TR_TAWRITESQ = 1, TR_AREA4_16 = 2, TR_AREA4_32 = 3 };
static void area4_trace(int site, const char* label, u32 addr, u32 bytes)
{
    if (!get_debug_message() || s_area4_trace_logs[site] >= 8)
        return;
    s_area4_trace_logs[site]++;
    u32 lmmode = (addr & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0;
    printf("[pvr] %s addr=0x%08X bytes=%u LMMODE=%d (bus: %s)\n",
           label, addr, bytes, (int)lmmode, (lmmode & 1) ? "32-bit" : "64-bit");
}

// Map a texture-window address to a VRAM offset over the selected bus.
static INLINE u32 area4_vram_offset(u32 addr)
{
    u32 lmmode = (addr & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0;
    u32 offset = addr & VRAM_MASK;
    if (lmmode & 1)
        offset = vramlock_ConvOffset32toOffset64(offset);
    return offset;
}

void FASTCALL pvr_write_area4_8(u32 addr, u8 data)
{
    printf("Warning: 8-bit VRAM writes are not supported by hardware\n");
}

void FASTCALL pvr_write_area4_16(u32 addr, u16 data)
{
    area4_trace(TR_AREA4_16, "Area4 CPU-store write16", addr, 2);
    if (get_lmmode_preset() != 1) { area4_write_dropped(addr); return; }
    *host_ptr_xor((u16*)&vram[area4_vram_offset(addr)]) = data;
}

void FASTCALL pvr_write_area4_32(u32 addr, u32 data)
{
    area4_trace(TR_AREA4_32, "Area4 CPU-store write32", addr, 4);
    if (get_lmmode_preset() != 1) { area4_write_dropped(addr); return; }
    *(u32*)&vram[area4_vram_offset(addr)] = data;
}

//------------------------------------------------------------------------------
// Tile Accelerator DMA Interface
//------------------------------------------------------------------------------

/**
 * Direct-VRAM write for the TA windows, honoring the SB_LMMODE bus select.
 * Window 0x11xxxxxx follows SB_LMMODE0, window 0x13xxxxxx (address bit 25)
 * follows SB_LMMODE1. LMMODE=0: 64-bit linear bus — plain memcpy, identical
 * to the legacy behavior. LMMODE=1: 32-bit interleaved bus — each word goes
 * through the 32->64 offset conversion, same as pvr_write_area1_32. Only
 * reached when the lmmode preset is on.
 */
static void vram_write_lmmode(u32 address, u32* data, u32 bytes)
{
    u32 lmmode = (address & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0;
    if (lmmode & 1) {
        // 32-bit interleaved bus: convert each word's offset
        u32 offset32 = address & VRAM_MASK;
        for (u32 i = 0; i < bytes; i += 4)
            *(u32*)&vram.data[vramlock_ConvOffset32toOffset64(offset32 + i)] = data[i >> 2];
    } else {
        // 64-bit linear bus (legacy path)
        memcpy(&vram.data[address & VRAM_MASK], data, bytes);
    }
}

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
    } else if (LMMODE_FIX()) {
        // Direct VRAM write honoring SB_LMMODE0/1 (lmmode preset on)
        area4_trace(TR_TAWRITE, "TAWrite DMA", address, count * 32);
        vram_write_lmmode(address, data, count * 32);
    } else {
        // Direct VRAM write (16MB+ range)
        // Note: This works on real hardware, respects lock modes
        area4_trace(TR_TAWRITE, "TAWrite DMA (legacy 64-bit)", address, count * 32);
        memcpy(&vram.data[address & VRAM_MASK], data, count * 32);
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
    } else if (LMMODE_FIX()) {
        // Direct VRAM write honoring SB_LMMODE0/1 (lmmode preset on)
        area4_trace(TR_TAWRITESQ, "TAWriteSQ", address, 32);
        vram_write_lmmode(address, data, 32);
    } else {
        // Direct VRAM write
        area4_trace(TR_TAWRITESQ, "TAWriteSQ (legacy 64-bit)", address, 32);
        memcpy(&vram.data[address & VRAM_MASK], data, 32);
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
