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
    
    return base[realx + realy * 8];
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
                // Pack YUYV format: Y0 U Y1 V
                yuyv[1] = GetY420(x, y, Y);      // Y0
                yuyv[0] = GetUV420(x, y, U);     // U (shared)
                yuyv[3] = GetY420(x + 1, y, Y);  // Y1
                yuyv[2] = GetUV420(x, y, V);     // V (shared)
                
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
        // TA polygon data range.
        // Also mirror to vram.data for addresses >= 0x200000 (texture space).
        // The polygon object list lives at low addresses (< 0x200000) and must
        // not be corrupted. VQ textures uploaded via TA DMA land at addresses
        // like 0x5B0080 which are above the threshold and need to be in vram.data
        // so the renderer can read the codebook/indices at tex_addr.
        if (address_masked >= 0x200000)
            memcpy(&vram.data[address_masked], data, count * 32);
        libPvr_TaDMA(data, count);
    } else if (address_masked < 0x1000000) {
        // YUV converter (8-16MB range)
        YUV_data(data, count);
    } else {
        // Direct VRAM write (16MB+ range)
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
        if (address_masked >= 0x200000)
            memcpy(&vram.data[address_masked], data, 32);
        libPvr_TaSQ(data);
    } else if (address_masked < 0x1000000) {
        YUV_data(data, 1);
    } else {
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
