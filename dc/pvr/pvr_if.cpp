#include "types.h"
#include "pvr_if.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

#include <malloc.h>   // memalign/free (FMV BOOST fused plane sets)
#include <gccore.h>   // DCFlushRange (fused planes are sampled by the GPU)

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
// FMV BOOST (see wii/main.cpp g_fmv_boost_preset and gxRend.cpp FMV_BOOST_*):
//   0 = OFF        legacy pass 1, byte-identical to the original code
//   1 = PASS1      optimized macroblock converter (hoisted TA_YUV_TEX_CTRL
//                  read, branchless 8x8 sub-block row pointers, u32
//                  loads/stores, direct-from-DMA when a full block is
//                  available — no YUV_tempdata staging memcpy)
//   2 = FUSED      pass 1 writes GX-ready TEV planes (I8 luma + IA8 chroma,
//                  GX tile layout) INSTEAD of packed YUYV in VRAM; gxRend
//                  binds them directly and pass 2 disappears entirely
//   3 = FUSED+HALF fused planes at half resolution (~1/4 the work)
//   4 = MAX        level 3 + frame decimation: only every 2nd FMV frame is
//                  converted (skipped frames are still consumed and
//                  acknowledged, so game timing is unchanged)
//------------------------------------------------------------------------------
extern "C" int get_fmv_boost_preset(); // defined in wii/main.cpp

// Fused-plane state published for plugs/drkPvr/gxRend.cpp. Only ever written
// here; addr != 0 means the planes describe the last COMPLETED FMV frame and
// are already DCFlushRange'd. Double-buffered so the GPU can keep sampling
// the published set while the next frame's macroblocks stream into the other.
extern "C" {
    u32 g_fmv_fused_addr = 0;              // VRAM addr of the FMV texture (0 = none)
    u32 g_fmv_fused_vw = 0, g_fmv_fused_vh = 0; // real video size in DC pixels
    u32 g_fmv_fused_pw = 0, g_fmv_fused_ph = 0; // luma plane size (halved at boost >= 3)
    u8 *g_fmv_fused_y  = 0;                // I8 luma plane  (pw x ph), GX tiled
    u8 *g_fmv_fused_uv = 0;                // IA8 chroma plane ((pw/2) x ph), GX tiled
}

static u32  YUV_ctrl = 0;          // cached TA_YUV_TEX_CTRL (hoisted read, boost >= 1)
static int  YUV_boost = 0;         // boost level latched at YUV_init
static bool YUV_skip_frame = false;// decimation: consume this frame, convert nothing
static u32  YUV_frame_count = 0;   // completed converter frames (decimation parity)
static bool YUV_fused_frame = false; // fused planes are this frame's write target
static bool YUV_fused_half = false;  // fused planes at half resolution
static u32  YUV_fused_pw = 0, YUV_fused_ph = 0; // back-set plane dims

static u8  *s_plane_set[2] = {0, 0};   // double-buffered plane sets (32B aligned)
static u32  s_plane_set_size = 0;      // bytes per set (luma + chroma)
static int  s_plane_back = 0;          // set being written this frame
static u8  *YUV_plane_y = 0;           // back-set luma write pointer
static u8  *YUV_plane_uv = 0;          // back-set chroma write pointer

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

//------------------------------------------------------------------------------
// FMV BOOST converters
//
// Source layout (one 4:2:0 macroblock, 384 bytes): 64 bytes U (8x8), 64 bytes
// V (8x8), 256 bytes Y (four 8x8 sub-blocks: TL, TR, BL, BR).
//
// Byte order: the DMA stream is guest (little-endian) data in host memory, so
// LOGICAL byte i lives at host address i^3 (see host_ptr_xor). A native
// big-endian u32 load of 4 aligned bytes therefore yields logical byte k at
// bits [8k+7:8k] — i.e. plain little-endian extraction, no per-byte XOR.
//------------------------------------------------------------------------------

// Logical bytes 0..3 -> memory bytes 0..3 (for GX tile rows, which want the
// samples in visual order): plain 32-bit byte swap.
#define YUV_BSWAP32(w) ( ((w) << 24) | (((w) & 0xFF00u) << 8) | \
                         (((w) >> 8) & 0xFF00u) | ((w) >> 24) )

/**
 * Boost >= 1: optimized 4:2:0 macroblock -> legacy packed-YUYV VRAM writes.
 * Produces exactly the bytes the boost-0 loop below produces, but:
 *   - no per-pixel GetY420/GetUV420 branches: the four fixed 8x8 Y sub-blocks
 *     are walked directly with row pointers
 *   - u32 loads/stores instead of four byte loads + a rebuild per pixel pair
 *   - the destination advances with a running row pointer instead of the
 *     full offset multiply YUV_putpixel2 does per pixel pair
 */
static void YUV_Block420_to_YUYV_Fast(const u8* in)
{
    const u32* Uw = (const u32*)in;          // 64 bytes U (8x8)
    const u32* Vw = (const u32*)(in + 64);   // 64 bytes V (8x8)
    const u32* Yw = (const u32*)(in + 128);  // 256 bytes Y (4x 8x8 sub-blocks)

    u8* dst0  = &vram.data[YUV_dest + (YUV_x_curr + YUV_y_curr * YUV_x_size) * 2];
    u32 pitch = YUV_x_size * 2;

    for (u32 by = 0; by < 2; by++)
    for (u32 bx = 0; bx < 2; bx++)
    {
        const u32* Yb = Yw + by*32 + bx*16;      // 8x8 luma sub-block, 2 words/row
        u8* drow = dst0 + (by*8) * pitch + bx*16; // 8 pixels * 2 bytes
        for (u32 y = 0; y < 8; y++)
        {
            // One chroma u32 = 4 logical U (or V) bytes = the 4 pixel pairs
            // of this half-row; chroma rows are shared by two luma rows.
            u32 cidx = (by*4 + (y >> 1)) * 2 + bx;
            u32 uw = Uw[cidx];
            u32 vw = Vw[cidx];
            u32 w0 = Yb[y*2 + 0];   // logical luma bytes 0-3
            u32 w1 = Yb[y*2 + 1];   // logical luma bytes 4-7

            // DC LE UYVY u32 per pixel pair (U | Y0<<8 | V<<16 | Y1<<24),
            // stored as one Wii BE u32 — same bytes YUV_putpixel2 produced.
            u32* out = (u32*)drow;
            out[0] = ( uw        & 0xFF) | ((w0 & 0x00FF) <<  8) |
                     (( vw        & 0xFF) << 16) | ((w0 & 0xFF00) << 16);
            out[1] = ((uw >>  8) & 0xFF) | ((w0 >>  8) & 0xFF00) |
                     (((vw >>  8) & 0xFF) << 16) | ( w0 & 0xFF000000u);
            out[2] = ((uw >> 16) & 0xFF) | ((w1 & 0x00FF) <<  8) |
                     (((vw >> 16) & 0xFF) << 16) | ((w1 & 0xFF00) << 16);
            out[3] = ( uw >> 24)         | ((w1 >>  8) & 0xFF00) |
                     (( vw >> 24)         << 16) | ( w1 & 0xFF000000u);
            drow += pitch;
        }
    }
}

/**
 * Boost >= 2 (full-res): 4:2:0 macroblock -> GX-ready TEV planes.
 * Luma -> I8 tiles (8x4 texels = 32 bytes): each 8x8 luma sub-block is
 * exactly two vertically stacked tiles, so a tile row is two byte-swapped
 * u32 stores. Chroma -> IA8 tiles (4x4 texels, 8 bytes/row), texel = [V|U]
 * bytes, one texel per luma pixel pair; each 4:2:0 chroma row covers two
 * output rows (duplicated — GX_LINEAR interpolates on top of that).
 * No color math at all: pass 2's YUV->RGB runs on the GPU (gxRend TEV).
 */
static void YUV_Block420_to_TEV_Full(const u8* in)
{
    const u32* Uw = (const u32*)in;
    const u32* Vw = (const u32*)(in + 64);
    const u32* Yw = (const u32*)(in + 128);
    u32 X0 = YUV_x_curr, Y0 = YUV_y_curr;

    u32 tiles_x = YUV_fused_pw / 8;
    for (u32 by = 0; by < 2; by++)
    for (u32 bx = 0; bx < 2; bx++)
    {
        const u32* Yb = Yw + by*32 + bx*16;
        u32 tx = (X0 + bx*8) >> 3;
        u32 ty = (Y0 + by*8) >> 2;
        u8* t0 = YUV_plane_y + (ty * tiles_x + tx) * 32;
        u8* t1 = t0 + tiles_x * 32;
        for (u32 r = 0; r < 4; r++)
        {
            u32 w0 = Yb[r*2], w1 = Yb[r*2 + 1];
            ((u32*)(t0 + r*8))[0] = YUV_BSWAP32(w0);
            ((u32*)(t0 + r*8))[1] = YUV_BSWAP32(w1);
            w0 = Yb[(r+4)*2]; w1 = Yb[(r+4)*2 + 1];
            ((u32*)(t1 + r*8))[0] = YUV_BSWAP32(w0);
            ((u32*)(t1 + r*8))[1] = YUV_BSWAP32(w1);
        }
    }

    u32 ctiles_x = (YUV_fused_pw / 2) / 4;
    for (u32 cy = 0; cy < 8; cy++)
    {
        u32 uw0 = Uw[cy*2], uw1 = Uw[cy*2 + 1];
        u32 vw0 = Vw[cy*2], vw1 = Vw[cy*2 + 1];
        // IA8 texel pair k,k+1 packed BE: V(k)<<24 | U(k)<<16 | V(k+1)<<8 | U(k+1)
        u32 o0 = ((vw0 & 0xFF) << 24) | ((uw0 & 0xFF) << 16) |
                 ( vw0 & 0xFF00)      | ((uw0 >>  8) & 0xFF);
        u32 o1 = ((vw0 & 0xFF0000) << 8) | ( uw0 & 0xFF0000) |
                 ((vw0 >> 16) & 0xFF00)  | ( uw0 >> 24);
        u32 o2 = ((vw1 & 0xFF) << 24) | ((uw1 & 0xFF) << 16) |
                 ( vw1 & 0xFF00)      | ((uw1 >>  8) & 0xFF);
        u32 o3 = ((vw1 & 0xFF0000) << 8) | ( uw1 & 0xFF0000) |
                 ((vw1 >> 16) & 0xFF00)  | ( uw1 >> 24);

        u32 yy = Y0 + cy*2;                  // yy&3 is 0 or 2, so yy and yy+1
        u8* trow = YUV_plane_uv              // always land in the same tile
                 + ((yy >> 2) * ctiles_x + (X0 >> 3)) * 32 + (yy & 3) * 8;
        ((u32*)trow)[0] = o0;        ((u32*)trow)[1] = o1;        // row yy, texels 0-3
        ((u32*)(trow +  8))[0] = o0; ((u32*)(trow +  8))[1] = o1; // row yy+1 (dup)
        ((u32*)(trow + 32))[0] = o2; ((u32*)(trow + 32))[1] = o3; // next tile, texels 4-7
        ((u32*)(trow + 40))[0] = o2; ((u32*)(trow + 40))[1] = o3;
    }
}

/**
 * Boost >= 3 (half-res): 4:2:0 macroblock -> half-resolution TEV planes.
 * Only even source rows/columns are kept: each macroblock shrinks to an 8x8
 * luma block (two stacked I8 tiles) and 4x8 chroma texels (one IA8 tile
 * column). Conversion work, cache flush, and texture upload all drop ~4x;
 * GX bilinear upscaling hides most of the resolution loss.
 */
static void YUV_Block420_to_TEV_Half(const u8* in)
{
    const u32* Uw = (const u32*)in;
    const u32* Vw = (const u32*)(in + 64);
    const u32* Yw = (const u32*)(in + 128);
    u32 ox = YUV_x_curr >> 1, oy = YUV_y_curr >> 1; // multiples of 8

    u32 tiles_x = YUV_fused_pw / 8;
    u8* t0 = YUV_plane_y + ((oy >> 2) * tiles_x + (ox >> 3)) * 32;
    u8* t1 = t0 + tiles_x * 32;
    for (u32 r = 0; r < 8; r++)
    {
        u32 sy = r * 2;                                     // even source row
        const u32* YbL = Yw + (sy >> 3) * 32 + (sy & 7) * 2; // left 8x8 sub-block row
        const u32* YbR = YbL + 16;                           // right sub-block, same row
        u32 l0 = YbL[0], l1 = YbL[1];
        u32 r0 = YbR[0], r1 = YbR[1];
        // Even luma samples only (logical bytes 0 and 2 of each word).
        u32 oA = ((l0 & 0xFF) << 24) | (l0 & 0xFF0000) |
                 ((l1 & 0xFF) <<  8) | ((l1 >> 16) & 0xFF);
        u32 oB = ((r0 & 0xFF) << 24) | (r0 & 0xFF0000) |
                 ((r1 & 0xFF) <<  8) | ((r1 >> 16) & 0xFF);
        u8* drow = (r < 4) ? (t0 + r*8) : (t1 + (r-4)*8);
        ((u32*)drow)[0] = oA;
        ((u32*)drow)[1] = oB;
    }

    u32 ctiles_x = (YUV_fused_pw / 2) / 4;
    u32 ctx = ox >> 3; // chroma texel base = ox/2, tile index = ox/8
    for (u32 r = 0; r < 8; r++)
    {
        u32 uw0 = Uw[r*2], uw1 = Uw[r*2 + 1];
        u32 vw0 = Vw[r*2], vw1 = Vw[r*2 + 1];
        // 4 texels per row: chroma samples 0,2 (word 0) and 4,6 (word 1).
        u32 o0 = ((vw0 & 0xFF) << 24) | ((uw0 & 0xFF) << 16) |
                 (((vw0 >> 16) & 0xFF) << 8) | ((uw0 >> 16) & 0xFF);
        u32 o1 = ((vw1 & 0xFF) << 24) | ((uw1 & 0xFF) << 16) |
                 (((vw1 >> 16) & 0xFF) << 8) | ((uw1 >> 16) & 0xFF);
        u32 yy = oy + r;
        u32* d = (u32*)(YUV_plane_uv
                 + ((yy >> 2) * ctiles_x + ctx) * 32 + (yy & 3) * 8);
        d[0] = o0;
        d[1] = o1;
    }
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

    // ── FMV BOOST frame setup ────────────────────────────────────────────────
    YUV_ctrl  = TA_YUV_TEX_CTRL;              // hoisted per-block register read
    YUV_boost = get_fmv_boost_preset();       // latched once per FMV frame

    // Frame decimation (boost 4): convert only every 2nd frame. Skipped
    // frames are still fully consumed and acknowledged (block counter +
    // interrupt), so the game's decode loop keeps its timing; the display
    // simply reuses the previous frame (fused planes / VRAM stay untouched,
    // so gxRend's texture cache keeps serving the last converted frame).
    YUV_skip_frame = (YUV_boost >= 4) && (YUV_frame_count & 1);

    // Fused planes (boost >= 2), 4:2:0 streams only (FMVs always are; the
    // 4:2:2 block path was never implemented in this converter anyway).
    YUV_fused_frame = false;
    if (YUV_boost >= 2 && ((TA_YUV_TEX_CTRL & (1 << 24)) == 0)
        && YUV_x_size >= 16 && YUV_y_size >= 16
        && YUV_x_size <= 1024 && YUV_y_size <= 1024)
    {
        YUV_fused_half = (YUV_boost >= 3);
        u32 pw = YUV_fused_half ? YUV_x_size / 2 : YUV_x_size;
        u32 ph = YUV_fused_half ? YUV_y_size / 2 : YUV_y_size;
        u32 need = pw * ph * 2; // luma pw*ph + chroma (pw/2)*ph*2, contiguous

        if (need > s_plane_set_size)
        {
            // Growing: both sets move, so the published pointers must be
            // withdrawn BEFORE freeing (gxRend keys on g_fmv_fused_addr).
            g_fmv_fused_addr = 0;
            free(s_plane_set[0]);
            free(s_plane_set[1]);
            s_plane_set[0] = (u8*)memalign(32, need);
            s_plane_set[1] = (u8*)memalign(32, need);
            if (s_plane_set[0] && s_plane_set[1]) {
                s_plane_set_size = need;
            } else {
                free(s_plane_set[0]);
                free(s_plane_set[1]);
                s_plane_set[0] = s_plane_set[1] = 0;
                s_plane_set_size = 0;
                printf("FMV BOOST: plane alloc failed (%u bytes), fused path off\n",
                       (unsigned)need);
            }
        }

        if (s_plane_set_size >= need)
        {
            YUV_fused_frame = true;
            YUV_fused_pw = pw;
            YUV_fused_ph = ph;
            YUV_plane_y  = s_plane_set[s_plane_back];
            YUV_plane_uv = YUV_plane_y + pw * ph;
        }
    }
}

/**
 * Convert one YUV macroblock (16x16 pixels) and write it to the current
 * target: packed YUYV in VRAM (boost 0/1) or GX-ready TEV planes (boost 2+).
 * @param in Macroblock source: YUV_tempdata, or the DMA stream directly
 */
INLINE void YUV_ConvertMacroBlock(const u8* in)
{
    // Boost 0 keeps the original live register read per block; boost >= 1
    // uses the value cached at YUV_init / CTRL register write instead
    // (saves a plugin register-read call per macroblock).
    u32 TA_YUV_TEX_CTRL = (YUV_boost >= 1)
                        ? YUV_ctrl
                        : pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);

    YUV_doneblocks++;
    YUV_index = 0;

    // Determine block format
    bool is_yuv420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);

    if (is_yuv420) {
        if (YUV_skip_frame) {
            // Decimated frame: data consumed, nothing converted.
        }
        else if (YUV_fused_frame) {
            if (YUV_fused_half)
                YUV_Block420_to_TEV_Half(in);
            else
                YUV_Block420_to_TEV_Full(in);
        }
        else if (YUV_boost >= 1) {
            YUV_Block420_to_YUYV_Fast(in);
        }
        else {
            // Legacy path (boost 0) — byte-identical to the original code.
            // YUV 4:2:0 format (384 bytes per macroblock)
            // Layout: 64 bytes U, 64 bytes V, 256 bytes Y
            u8* U = (u8*)in;
            u8* V = (u8*)in + 64;
            u8* Y = (u8*)in + 128;

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
        // Publish the completed fused plane set for gxRend, then flip
        // buffers. Skipped (decimated) frames publish nothing — the
        // previously published set stays on screen.
        if (YUV_fused_frame && !YUV_skip_frame) {
            u32 luma_sz = YUV_fused_pw * YUV_fused_ph;
            DCFlushRange(YUV_plane_y, luma_sz * 2); // luma + chroma, contiguous
            g_fmv_fused_y   = YUV_plane_y;
            g_fmv_fused_uv  = YUV_plane_uv;
            g_fmv_fused_pw  = YUV_fused_pw;
            g_fmv_fused_ph  = YUV_fused_ph;
            g_fmv_fused_vw  = YUV_x_size;
            g_fmv_fused_vh  = YUV_y_size;
            g_fmv_fused_addr = YUV_dest; // published LAST — gxRend keys on it
            s_plane_back ^= 1;
        }
        YUV_frame_count++;
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

    u32 TA_YUV_TEX_CTRL = (YUV_boost >= 1)
                        ? YUV_ctrl
                        : pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);

    // Determine block size based on format
    u32 block_size = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0)
                     ? YUV_BLOCK_SIZE_420
                     : YUV_BLOCK_SIZE_422;

    // Convert count from 32-byte blocks to bytes
    count *= 32;

    // Process data in chunks
    while (count > 0) {
        // Boost >= 1: a whole macroblock is available in the DMA source and
        // nothing is buffered — convert straight from it, skipping the
        // YUV_tempdata staging memcpy entirely.
        if (YUV_boost >= 1 && YUV_index == 0 && count >= block_size) {
            YUV_ConvertMacroBlock((const u8*)data);
            data  += block_size >> 2;
            count -= block_size;
            continue;
        }

        u32 bytes_til_block_end = block_size - YUV_index;

        if (count >= bytes_til_block_end) {
            // Complete at least one full block
            memcpy(&YUV_tempdata[YUV_index >> 2], data, bytes_til_block_end);
            data += bytes_til_block_end >> 2;
            count -= bytes_til_block_end;
            YUV_ConvertMacroBlock((const u8*)YUV_tempdata);
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
    // Keep the hoisted TA_YUV_TEX_CTRL cache coherent with writes that land
    // after the base-address write (YUV_init already re-reads it).
    else if ((addr & 0xFFFFFF) == TA_YUV_TEX_CTRL_ADDR) {
        YUV_ctrl = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
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
        // TA polygon data (0-8MB range)
        libPvr_TaDMA(data, count);
    } else if (address_masked < 0x1000000) {
        // YUV converter (8-16MB range)
        YUV_data(data, count);
    } else {
        // Direct VRAM write (16MB+ range)
        // Note: This works on real hardware, respects lock modes
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
    } else {
        // Direct VRAM write
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

    // Reset FMV BOOST state (plane buffers are kept allocated)
    YUV_ctrl = 0;
    YUV_boost = 0;
    YUV_skip_frame = false;
    YUV_frame_count = 0;
    YUV_fused_frame = false;
    g_fmv_fused_addr = 0;
}
