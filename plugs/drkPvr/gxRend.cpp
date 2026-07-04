// Wii Rendering

#include <cstdio>

// RUNTIME - PRESET SELECTION

// This is defined in main.cpp
extern "C" int get_accuracy_preset();

// Helper macros to check current accuracy mode
#define FAST() (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

// This is defined in main.cpp
extern "C" int get_graphism_preset();

// Helper macros to check current graphism mode
#define LOW() (get_graphism_preset() == 0)
#define NORMAL() (get_graphism_preset() == 1)
#define HIGH() (get_graphism_preset() == 2)
#define EXTRA() (get_graphism_preset() == 3)

// FULLSCREEN or 4:3 (4:3 still has bugs in some games)
extern "C" int get_ratio_preset();

#define ORIGINAL() (get_ratio_preset() == 0)
#define FULLSCREEN() (get_ratio_preset() == 1)

// ADVANCED ALPHA (may be slower but haven't really noticed any difference)
extern "C" int get_advanced_alpha_preset();
#define ADVANCED_ALPHA() (get_advanced_alpha_preset() == 1)

bool use_adv_alpha = ADVANCED_ALPHA(); // compute once

// This is defined in main.cpp
extern "C" int get_debug_message();
#define DEBUG_MESSAGE() (get_debug_message() == 1)

extern "C" int get_debug_loop();

// Specific debug for FMV
int fmv_debug = 0;

// Frame skipping
extern "C" int get_frameskip_preset();

#define NO_FRAMESKIP() (get_frameskip_preset() == 0)
#define FRAMESKIP_1() (get_frameskip_preset() == 1)
#define FRAMESKIP_2() (get_frameskip_preset() == 2)
#define FRAMESKIP_AUTO() (get_frameskip_preset() == 3)

// 4BPP palette texture management
extern "C" int get_4bpp_preset();

#define TEXTURE_4BPP_I4_STUB() (get_4bpp_preset() == 0) // I4 Stub // 34 FPS VMU Menu Daytona
#define TEXTURE_4BPP_OPTIMIZED() (get_4bpp_preset() == 1) // 4BPP Optimized // 34 FPS VMU Menu Daytona (untested)
#define TEXTURE_4BPP_CI4_FAST() (get_4bpp_preset() == 2) // CI4 (supposed to be fast) // 34 FPS VMU Menu Daytona
#define TEXTURE_4BPP_CI4() (get_4bpp_preset() == 3) // CI4 (supposed to be accurate) // 27 FPS VMU Menu Daytona
#define TEXTURE_4BPP_RGB565() (get_4bpp_preset() == 4) // RGB565 // 33 FPS VMU Menu Daytona (worse display)

// 8BPP palette texture management
extern "C" int get_8bpp_preset();

#define TEXTURE_8BPP_I8_STUB() (get_8bpp_preset() == 0) // I8 Stub
#define TEXTURE_8BPP_OPTIMIZED() (get_8bpp_preset() == 1) // 8BPP Optimized
#define TEXTURE_8BPP_CI8_FAST() (get_8bpp_preset() == 2) // CI8 (supposed to be fast)
#define TEXTURE_8BPP_CI8() (get_8bpp_preset() == 3)      // CI8 (supposed to be accurate)
#define TEXTURE_8BPP_RGB565() (get_8bpp_preset() == 4) // RGB565

// Jojo's Bizarre Adventure texture-cache fix: TLUT-reupload-skip + CACHE_FAST
extern "C" int get_jojo_fix_preset();
#define JOJO_FIX() (get_jojo_fix_preset() != 0)

// Vertex-color Intensity (Gouraud) (cars in Crazy Taxi 1)
extern "C" int get_vertex_color_fix_preset();
#define VERTEX_COLOR_FIX() (get_vertex_color_fix_preset() != 0)

// Texture cache management
extern "C" int get_texture_cache_preset();

#define CACHE_VERY_FAST()   (get_texture_cache_preset() == 0) // Original nullDC4Wii/SKMP algorithm.
#define CACHE_FAST()        (get_texture_cache_preset() == 1) // Persistent bump-allocated cache (correct sizing, cross-frame)
#define CACHE_NORMAL()      (get_texture_cache_preset() == 2) // Quality with enhancements
#define CACHE_QUALITY()     (get_texture_cache_preset() == 3) // Perfect Result (to the cost of FPS)
#define CACHE_EXTRA()       (get_texture_cache_preset() == 4) // For Debug only

// Per Polygon Z Write
extern "C" int get_ppz_write_preset();

#define PER_POLYGON_Z_WRITE() (get_ppz_write_preset() == 1) // Fix Test-drive 6 draw distance

// DecalAlpha shading fix (cars in Crazy Taxi 1)
extern "C" int get_decal_alpha_preset();
#define DECAL_ALPHA_FIX() (get_decal_alpha_preset() == 1)

// 2D Framebuffer Rendering
extern "C" int get_framebuffer_2d();

#define FRAMEBUFFER_2D() (get_framebuffer_2d() == 1) // Can get worse or better fps/visuals

// FMV Format Selection
extern "C" int get_fmv_format_preset();

#define FMV_FORMAT_CMPR()   (get_fmv_format_preset() == 0)
#define FMV_FORMAT_RGBA8()  (get_fmv_format_preset() == 1)
#define FMV_FORMAT_RGB565() (get_fmv_format_preset() == 2)

// Blend mode: per-polygon TSP SrcInstr/DstInstr blending (necessary for Resident Evil 3)
extern "C" int get_blend_mode_preset();
#define BLEND_MODE() (get_blend_mode_preset() == 1)

// Force vertex alpha opaque for both ARGB1555 (fmt0) and RGB565 (fmt1).
// Off: only ARGB1555 (fmt0) is forced opaque; RGB565 (fmt1) uses vertex alpha as-is.
extern "C" int get_rgb565_opaque_alpha_preset();
#define RGB565_OPAQUE_ALPHA() (get_rgb565_opaque_alpha_preset() == 1)

// Blend FPS boost: under ADVANCED_ALPHA()+BLEND_MODE(), skip the alpha-test/
// Gains a couple of FPS (e.g. Castlevania) at the cost of incorrect alpha on opaque/punch-through polys
extern "C" int get_blend_fps_boost_preset();
#define BLEND_FPS_BOOST() (get_blend_fps_boost_preset() == 1)

// Punch-through list fix: draw lists in OP → PT → TR order (like real PVR)
// with the PT list alpha-tested against PT_ALPHA_REF and blending off.
// Off keeps the legacy behavior: PT polys drawn last, in the TR blend state.
extern "C" int get_punch_through_preset();
#define PUNCH_THROUGH_FIX() (get_punch_through_preset() == 1)

// Offset (specular) color: real PVR shades textured polys as
// PIX = BaseCol * TEX + OffsetCol. On: the per-vertex offset color is kept at
// decode time (Vertex::spc), sent as GX_VA_CLR1 and added in a second TEV
// stage (raster color GX_COLOR1A1) — restores specular highlights on cars/
// water. Off: offset color is dropped as before (one TEV stage, 24B vertices).
extern "C" int get_offset_color_preset();
#define OFFSET_COLOR_FIX() (get_offset_color_preset() == 1)

// Translucent depth sort: real PVR autosorts translucent pixels per-pixel in
// hardware; Hollywood can't, so the TR list is normally drawn in TA submission
// order, giving wrong overlaps in alpha-heavy scenes. On: the TR strips are
// sorted back-to-front (painter's algorithm) by each strip's farthest vertex
// before drawing. Off (default): legacy submission-order draw, zero overhead.
extern "C" int get_trans_sort_preset();
#define TRANS_SORT() (get_trans_sort_preset() == 1)

// Render-to-texture: a RENDER_START whose write address (FB_W_SOF1) has bit 24
// set targets the 64-bit texture area — the game will bind the result as a
// texture (mirrors, TV screens, some menu effects). On: the scene is rendered
// normally, then the EFB is copied out with GX_CopyTex and converted back into
// emulated VRAM at FB_W_SOF1 (honoring FB_W_CTRL packmode / FB_W_LINESTRIDE)
// instead of being shown. Off (default): legacy behavior, the frame is dropped
// (or handled by the FRAMEBUFFER_2D() path when that preset is on).
extern "C" int get_render_to_texture_preset();
#define RENDER_TO_TEXTURE() (get_render_to_texture_preset() == 1)


int frame_counter;

#include "config.h"
#include "gxRend.h"
#include <gccore.h>
#include <malloc.h>
#include <stdlib.h> // qsort, for TRANS_SORT()
#include "regs.h"
#include "wii/wii_audio.h"
#include <stdio.h> // needed for log

// The FIFO is the command buffer for the GX hardware. 
// 256KB is a standard size for most homebrew applications. May need more for NullDC4Wii to avoid FIFO error ?
#define DEFAULT_FIFO_SIZE (256 * 1024)

using namespace TASplitter;

u8 *vram_buffer;

// Double buffering setup: frameBuffer[0] and [1] prevent screen tearing.
static void *frameBuffer[2] = {NULL, NULL};
static GXRModeObj *rmode;
static u8 gp_fifo[DEFAULT_FIFO_SIZE] __attribute__((aligned(32)));
static int fb = 0; // Current framebuffer index

// Set once at startup or when preset changes
static u8 min_filt, mag_filt, bias_clamp, edge_lod, aniso;
static f32 lod_bias;

void ApplyGraphismPreset() {
  switch (get_graphism_preset()) {
    case 0: min_filt = GX_NEAR; mag_filt = GX_NEAR; lod_bias = 0.0f;
            bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
    case 1: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = 0.0f;
            bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
    case 2: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = -0.5f;
            bias_clamp = GX_ENABLE; edge_lod = GX_ENABLE; aniso = GX_ANISO_2; break;
    case 3: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = -1.0f;
            bias_clamp = GX_ENABLE; edge_lod = GX_ENABLE; aniso = GX_ANISO_4; break;
    default: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = 0.0f;
              bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
  }
}

// Macro to convert ABGR (standard for some PC/PVR formats) to RGBA/Other
// #define ABGR8888(x) ((x & 0xFF00FF00) | ((x >> 16) & 0xFF) | ((x & 0xFF) << 16)) // already defined elsewhere
/*
  Texture Format Conversion Notes:
  FMT: 1555 DTP    -> RGB5A3 DP
     4444 DTP    -> RGB5A3 DP
     8888 P      -> 8888 P
     565  DTP    -> 565 DP
     YUV  DT     -> ? 565 is possibe but LQ ...
*/

// Macros for extracting color channels from Dreamcast-specific pixel formats.
#define ABGR4444_A(x) ((x) >> 12)
#define ABGR4444_R(x) ((x >> 8) & 0xF)
#define ABGR4444_G(x) ((x >> 4) & 0xF)
#define ABGR4444_B(x) ((x) & 0xF)

#define ABGR0565_R(x) ((x) >> 11)
#define ABGR0565_G(x) ((x >> 5) & 0x3F)
#define ABGR0565_B(x) ((x) & 0x1F)

#define ABGR1555_A(x) ((x >> 15))
#define ABGR1555_R(x) ((x >> 10) & 0x1F)
#define ABGR1555_G(x) ((x >> 5) & 0x1F)
#define ABGR1555_B(x) ((x) & 0x1F)
#define MAKE_555A3

// May need that later ?
// #define ABGR1555_VQ(x) (x)  // Keep the original 1555 value, alpha bit intact
// or
// #define ABGR1555_VQ(x) ((x) & 0x8000 ? (x) : 0x8000) // Make transparent pixels fully transparent (alpha=1, RGB=0)

#define MAKE_565(r, g, b, a)
#define MAKE_1555(r, g, b, a)
#define MAKE_4444(r, g, b, a)

// Dreamcast = ABGR
// Wii GX = RGB
// These macros swap R and B to correct the color output.

// RGB565 (DC: R5G6B5) → GX RGB565: identical layout, no conversion needed
#define ABGR0565(x) (x)

// Old AGBR1555(x) definition (incorrect, faster ?). Check alpha0.39 for the preset switch
// #define ABGR1555(x) ((x) & 0x8000 ? (x) : 0x0000) // Works together with the coding routing introduced in alpha 0.13 (alpha_fmt stuff)
// New AGBR1555(x) definition :
#define ABGR1555(x) ((x) & 0x8000 ? (x) : \
    ((((x) >> 11) & 0xFu) << 8) | ((((x) >> 6) & 0xFu) << 4) | (((x) >> 1) & 0xFu))

// ARGB4444 (DC: A4 R4 G4 B4) → GX RGB5A3
// ARGB4444 has 4 alpha (transparency) bits, Wii's RGB5A3 has 3 bits.
// Truncate A4 → A3, keep R4 G4 B4, force bit15=0 (blended mode).
#define ABGR4444(x) ( 0x0000u                             \
    | ((((x) & 0xF000u) >> 1) & 0x7000u)  /* A4 → A3 */  \
    | ((x) & 0x0FFFu) )                   /* R4 G4 B4 */

// ABGR8888 → RGBA8888: swap R (bits 16-23) and B (bits 0-7)
#define ABGR8888(x) ( ((x) & 0xFF00FF00u)          \
                    | (((x) & 0x00FF0000u) >> 16)   \
                    | (((x) & 0x000000FFu) << 16) )

#define colclamp(low, hi, val) \
  {                            \
    if (val < low)             \
      val = low;               \
    if (val > hi)              \
      val = hi;                \
  }


// Vertex structure used to feed the Wii's GX pipeline.
struct Vertex
{
  float u, v;        // Texture coordinates
  unsigned int col;  // Vertex color (base)
  unsigned int spc;  // Offset (specular) color, same ABGR layout as col.
                     // Always written by the decode handlers (0 when the poly
                     // has no offset color or OFFSET_COLOR_FIX() is off), only
                     // submitted to GX when OFFSET_COLOR_FIX() is on.
  float x, y, z;     // 3D coordinates
};

// Structures to manage the internal drawing lists and state 
// passed from the Dreamcast's Tile Accelerator.
struct VertexList
{
  union
  {
    Vertex *ptr;
    s32 count;
  };
};

struct PolyParam
{
  PCW pcw;
  ISP_TSP isp;

  TSP tsp;
  TCW tcw;
};

struct TextureCacheDesc
{
  GXTexObj  tex;
  GXTlutObj pal;
  u32  addr;            // orig_tex_addr this slot holds (0 = empty)
  bool has_pal;
  u32  vq_codebook_w0;  // VQ fingerprint (cb_w0 ^ idx_w0)
  u32  slot_size;       // bump allocator: total bytes of this allocation
  u32  shape_key;       // CACHE_FAST: non-address TCW/TSP bits (format/scan/stride/mip/size)
};

// -- CACHE_VERY_FAST: direct nullDC4Wii slot -------------------------------------
// Slot position = vram_buffer[tex_addr * 2] - O(1), no lookup.
//
// NOTE: this fixed mapping assumes a texture's decoded size never exceeds twice
// the VRAM distance to the next texture's address — false in general (e.g. a
// small VQ/CI4 source decodes into a much larger RGB565/RGB5A3 buffer), so two
// unrelated textures can overlap and corrupt each other ("puzzled" textures).
// CACHE_FAST below replaces this with a correctly-sized bump allocator instead.
static INLINE TextureCacheDesc* skimp_slot(u32 tex_addr)
{
  u32 cache_offset = tex_addr * 2;
  cache_offset = (cache_offset + 31) & ~31u;
  if (cache_offset < 64) cache_offset = 64;
  return (TextureCacheDesc*)&vram_buffer[cache_offset] - 1;
}

// CACHE_EXTRA / CACHE_QUALITY / CACHE_NORMAL: per-frame bump allocator
static const u32 BUMP_TOTAL = 14u * 1024u * 1024u; // 14 MB bump arena

static u32 s_bump_offset = 0;
static void bump_reset() { s_bump_offset = 0; }

// ── O(1) Hash Map for CACHE_NORMAL bump_find ─────────────────────────────────
//
// Replaces the old O(n) linear scan over the bump arena.
//
// Layout:
//   - 512 slots, power-of-two so index = hash & (TEXMAP_SIZE-1).
//   - Each slot stores the tex_addr key and the byte offset inside
//     vram_buffer[] where the TextureCacheDesc for that entry lives.
//     The pixel data immediately follows the descriptor (same layout as
//     bump_alloc), so pixel_out is trivially derived from the offset.
//   - TEXMAP_EMPTY (0xFFFFFFFF) marks a free slot. tex_addr 0xFFFFFFFF
//     is not a valid DC texture address (masked by VRAM_MASK = 0x7FFFFF),
//     so there is no false-positive risk.
//   - Collision resolution: linear probing — simple, cache-friendly, and
//     zero extra allocation. The probe wraps around with & mask.
//   - Reset cost: memset of 512*8 = 4 KB — fast on PPC with dcbz.

#define TEXMAP_SIZE  512u           // must be power-of-two
#define TEXMAP_MASK  (TEXMAP_SIZE - 1u)
#define TEXMAP_EMPTY 0xFFFFFFFFu    // sentinel: slot is free

struct TexMapSlot
{
  u32 key;          // tex_addr stored here, TEXMAP_EMPTY if free
  u32 bump_offset;  // byte offset of TextureCacheDesc in vram_buffer[]
};

// Static array: ~4 KB, lives in BSS — no heap, no malloc.
static TexMapSlot s_texmap[TEXMAP_SIZE];

// Knuth multiplicative hash — fast single-multiply on PowerPC.
// DC tex_addr is already a multiple of 8 (TexAddr<<3), so we shift
// right by 3 first to spread entropy into the low bits before hashing.
static INLINE u32 texmap_hash(u32 addr)
{
  return ((addr >> 3) * 2654435761u) >> (32u - 9u); // 9 bits → 0..511
}

// Clear the map in O(TEXMAP_SIZE). Called every frame and on full reset.
static void hash_map_reset()
{
  // memset to 0xFF fills every key field with TEXMAP_EMPTY (0xFFFFFFFF).
  memset(s_texmap, 0xFF, sizeof(s_texmap));
}

// Insert a newly allocated descriptor into the map.
// 'bump_off' is the byte offset returned by bump_alloc (s_bump_offset
// before the allocation advances).
static INLINE void hash_map_insert(u32 tex_addr, u32 bump_off)
{
  u32 idx = texmap_hash(tex_addr) & TEXMAP_MASK;
  // Linear probe: find first free slot.
  // In practice the map is reset every frame so load factor stays low.
  u32 probe = 0;
  while (s_texmap[idx].key != TEXMAP_EMPTY && probe < TEXMAP_SIZE)
  {
    idx = (idx + 1u) & TEXMAP_MASK;
    probe++;
  }
  // If map is somehow full (shouldn't happen with 512 slots per frame)
  // just silently drop the insert; bump_find will return nullptr and
  // the texture will be re-decoded — correct, just not cached this call.
  if (probe < TEXMAP_SIZE)
  {
    s_texmap[idx].key         = tex_addr;
    s_texmap[idx].bump_offset = bump_off;
  }
}

// O(1) replacement for the old O(n) bump_find linear scan.
static INLINE TextureCacheDesc* hash_map_find(u32 orig_tex_addr, u32 **pixel_out)
{
  u32 desc_sz = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 idx     = texmap_hash(orig_tex_addr) & TEXMAP_MASK;

  u32 probe = 0;
  while (probe < TEXMAP_SIZE)
  {
    u32 k = s_texmap[idx].key;
    if (k == TEXMAP_EMPTY)
      return nullptr;  // empty slot → key not present
    if (k == orig_tex_addr)
    {
      u32 off = s_texmap[idx].bump_offset;
      *pixel_out = (u32*)&vram_buffer[off + desc_sz];
      return (TextureCacheDesc*)&vram_buffer[off];
    }
    idx = (idx + 1u) & TEXMAP_MASK;
    probe++;
  }
  return nullptr;  // full table, not found (should never happen)
}

static TextureCacheDesc* bump_alloc(u32 pixel_bytes, u32 **pixel_out)
{
  u32 desc_sz   = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 pixel_sz  = (pixel_bytes + 31) & ~31u;
  u32 total     = desc_sz + pixel_sz;
  if (s_bump_offset + total > BUMP_TOTAL) s_bump_offset = 0;
  u32 alloc_off            = s_bump_offset;   // capture before advancing
  TextureCacheDesc *d      = (TextureCacheDesc*)&vram_buffer[alloc_off];
  *pixel_out               = (u32*)&vram_buffer[alloc_off + desc_sz];
  memset(d, 0, desc_sz);
  d->slot_size             = total;
  s_bump_offset           += total;
  return d;
}

// ── CACHE_FAST: persistent bump arena + hash map ──────────────────────────────
//
// Fixes the "puzzled" texture corruption from the old skimp_slot scheme (decoded
// size could overrun into the next address's slot) by giving every texture an
// allocation sized to what it actually decodes to — the same allocator shape
// CACHE_NORMAL uses. Unlike CACHE_NORMAL's arena/map, this one is NEVER cleared
// on a per-frame basis (tex_frame_reset() only touches s_bump_offset/s_texmap
// above), so a static texture still skips re-decoding on every later frame —
// that cross-frame skip is what makes CACHE_FAST fast. Reuses the same 16 MB
// vram_buffer budget as everything else; only one cache preset is ever active
// at a time, so there is no real sharing conflict, just a 14 MB sub-budget like
// CACHE_NORMAL's BUMP_TOTAL.
//
// Freshness is NOT implied by "found in the map" the way it is for CACHE_NORMAL
// (which redecodes every texture every frame): the caller still runs the usual
// CACHE_FAST sentinel/shape-key (or VQ fingerprint) check against whatever this
// map returns before trusting it.
static const u32 FAST_BUMP_TOTAL = 14u * 1024u * 1024u; // 14 MB, mirrors BUMP_TOTAL

static u32 s_fast_bump_offset = 0;

#define FASTMAP_SIZE  2048u // power-of-two; larger than TEXMAP_SIZE since this map
#define FASTMAP_MASK  (FASTMAP_SIZE - 1u) // persists across many frames instead of one
#define FASTMAP_EMPTY 0xFFFFFFFFu

static TexMapSlot s_fastmap[FASTMAP_SIZE]; // ~16 KB, lives in BSS

static void fast_cache_reset()
{
  s_fast_bump_offset = 0;
  memset(s_fastmap, 0xFF, sizeof(s_fastmap));
}

// Single linear probe that returns either the slot already holding tex_addr
// (*found = true) or the first free slot where it could be inserted
// (*found = false) — standard open-addressing lookup-or-insertion-point.
static INLINE u32 fast_map_locate(u32 tex_addr, bool *found)
{
  u32 idx = texmap_hash(tex_addr) & FASTMAP_MASK;
  u32 probe = 0;
  while (probe < FASTMAP_SIZE)
  {
    u32 k = s_fastmap[idx].key;
    if (k == tex_addr)      { *found = true;  return idx; }
    if (k == FASTMAP_EMPTY) { *found = false; return idx; }
    idx = (idx + 1u) & FASTMAP_MASK;
    probe++;
  }
  *found = false;
  return FASTMAP_SIZE; // table completely full (shouldn't happen at 2048 slots)
}

// Allocates `pixel_bytes` worth of room (descriptor+pixels), sized exactly like
// CACHE_NORMAL's bump_alloc. Wraps (discarding every existing entry) if it
// doesn't fit — rare with a 14 MB budget reused across many frames of texture
// churn, and safe: a wrap means every previously handed-out offset may now be
// overwritten, so every old map entry must be dropped, not just the new one.
static TextureCacheDesc* fast_bump_alloc(u32 pixel_bytes, u32 **pixel_out, u32 *alloc_off_out)
{
  u32 desc_sz  = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 pixel_sz = (pixel_bytes + 31) & ~31u;
  u32 total    = desc_sz + pixel_sz;
  if (s_fast_bump_offset + total > FAST_BUMP_TOTAL)
  {
    s_fast_bump_offset = 0;
    memset(s_fastmap, 0xFF, sizeof(s_fastmap));
  }
  u32 alloc_off       = s_fast_bump_offset;
  TextureCacheDesc *d = (TextureCacheDesc*)&vram_buffer[alloc_off];
  *pixel_out          = (u32*)&vram_buffer[alloc_off + desc_sz];
  memset(d, 0, desc_sz);
  d->slot_size        = total;
  s_fast_bump_offset += total;
  *alloc_off_out       = alloc_off;
  return d;
}

// ── Unified cache interface ───────────────────────────────────────────────────
static void tex_cache_init()
{
  s_bump_offset = 0;
  hash_map_reset();
  fast_cache_reset();
}

// CACHE_FAST shape_key helper: for index-only GX formats (CI4/CI8 via the
// Fast/Normal presets), the decoded buffer holds palette *indices*, not
// colors — PalSelect only picks which bank the separately-managed TLUT
// samples (see the palette setup block above), so most of it must NOT be
// part of the shape key. Otherwise the same texture redrawn with a
// different PalSelect (e.g. one glyph atlas recolored per character) is
// wrongly treated as a different texture and the whole buffer gets
// redecoded on every single draw, even within the same frame.
//
// BUT: union TCW's PAL.PalSelect (bits 21-26) overlaps the exact same bits
// as NO_PAL.StrideSel/Reserved/ScanOrder (see ta_structs.h) — PalSelect's
// top bit (bit 26, weight 32) IS the ScanOrder bit the decode switch reads
// via mod->tcw.NO_PAL.ScanOrder to choose twiddled vs linear decoding.
// Dropping that bit too would let a PalSelect change that crosses the
// 0-31/32-63 boundary reuse an index buffer decoded under the *other*
// scan-order interpretation — scrambled output. So only bits 21-25 (the
// bank-within-half selector, which doesn't affect decode shape) are
// dropped; bit 26 stays in the key.
//
// JOJO_FIX()-gated: this masking is what lets a glyph atlas recolored per
// character hit the cache instead of redecoding every draw (Jojo's Bizarre
// Adventure). Off by default — every other game keeps the plain shape_key
// (full PalSelect included) it always had.
static INLINE u32 tex_shape_key(PolyParam *mod, u32 pixel_fmt)
{
  u32 key = (mod->tcw.full & ~0x1FFFFFu) ^ (mod->tsp.full & 0x3Fu);
  if (!JOJO_FIX()) return key;
  bool index_only = (pixel_fmt == 5) ? (TEXTURE_4BPP_CI4_FAST() || TEXTURE_4BPP_CI4())
                  : (pixel_fmt == 6) ? (TEXTURE_8BPP_CI8_FAST() || TEXTURE_8BPP_CI8())
                  : false;
  if (index_only) key &= ~(0x3Fu << 21); // drop all PalSelect bits 21-26: palette decode never reads ScanOrder (bit 26 is PalSelect bit 5, not a real ScanOrder flag)
  // old : if (index_only) key &= ~(0x1Fu << 21); // drop PalSelect bits 21-25 only, keep bit 26 (ScanOrder)
  return key;
}

static void tex_frame_reset()
{
  bump_reset();       // reset arena for new frame
  hash_map_reset();   // clear hash map — O(4 KB memset), fast on PPC
  // params.vram sentinels survive across frames.
}


// VBlank() is defined after PresentFramebuffer()/ShouldSkipFrame() below, so it
// can present the VRAM framebuffer on vblanks where no PVR render occurred.

// Static arrays for vertex data to avoid frequent heap allocations.
// Limited by the Wii's MEM1/MEM2 availability.
Vertex ALIGN16 vertices[42*1024]; // Wii memory limit
VertexList ALIGN16 lists[8*1024];
PolyParam ALIGN16 listModes[8*1024];

Vertex *curVTX = vertices;
VertexList *curLST = lists;
VertexList *TransLST = 0;
// Punch-through list boundary, plus the vertex/param cursors captured when the
// TR and PT lists start: lists/vertices/listModes are parallel streams that
// can only be walked from a known cursor, and PUNCH_THROUGH_FIX() draws the PT
// range out of submission order (OP → PT → TR).
VertexList *PTLST = 0;
Vertex *TransVTX = 0;
PolyParam *TransMod = 0;
Vertex *PT_VTX = 0;
PolyParam *PT_Mod = 0;
PolyParam *curMod = listModes;
bool global_regd;
float vtx_min_Z;
float vtx_max_Z;

// Scratch face color for the polygon currently being decoded, used by
// Intensity (Col_Type==2) Gouraud shading: AppendPolyParam1/2B/4B set this
// from the TA stream, and AppendPolyVertex2/7/8/10/13A/14A consume it
// immediately while building each vertex's color, before the next polygon
// header overwrites it. Transient decode-time state only — NOT persisted
// per-entry in listModes[], which is replayed later at draw time and is
// already near the Wii's memory budget (see "Wii memory limit" above).
float curFaceColorR = 1.0f, curFaceColorG = 1.0f, curFaceColorB = 1.0f, curFaceColorA = 1.0f;

// Scratch face OFFSET color (specular), same lifetime rules as curFaceColor*:
// set by AppendPolyParam2B (the only intensity header type carrying one) and
// consumed by OFFS_INTESITY() while decoding that polygon's vertices.
float curFaceOffsR = 0.0f, curFaceOffsG = 0.0f, curFaceOffsB = 0.0f;

// 0xFFFFFFFF while the polygon being decoded has PCW.Offset set AND
// OFFSET_COLOR_FIX() is on, else 0. Lets the vertex handlers keep/drop the
// offset color with a mask (the TA vertex structs always reserve the field,
// but its content is garbage when PCW.Offset is 0).
u32 curPolyOffsMask = 0;

// Per-polygon offset color for sprites (they carry it in the header, not per
// vertex). Already ABGR-converted and masked by curPolyOffsMask.
u32 curSpriteSpc = 0;

// Cached once per frame in reset_vtx_state() instead of calling
// VERTEX_COLOR_FIX() (an uncached extern getter) on every Intensity vertex —
// the preset can't change mid-frame anyway.
bool g_vertex_color_fix_cached = false;
bool g_offset_color_fix_cached = false; // same idea, for OFFSET_COLOR_FIX()

char fps_text[512];

struct VertexDecoder;
FifoSplitter<VertexDecoder> TileAccel;

union _ISP_BACKGND_T_type
{
  struct
  {
    u32 tag_offset : 3;
    u32 tag_address : 21;
    u32 skip : 3;
    u32 shadow : 1;
    u32 cache_bypass : 1;
  };
  u32 full;
};

union _ISP_BACKGND_D_type
{
  u32 i;
  f32 f;
};

// Conversion logic to translate Dreamcast VRAM addressing (32-bit interleaved)
// into a linear 64-bit bank structure compatible with the emulator's memory map.
static INLINE u32 fast_ConvOffset32toOffset64(u32 offset32)
{
  offset32 &= VRAM_MASK;
  u32 bank = ((offset32 >> 22) & 0x1) << 2;
  u32 lower = offset32 & 0x3;
  u32 addr_shifted = (offset32 & 0x3FFFFC) << 1;
  return addr_shifted | bank | lower;
}

// Helpers to read float/int values directly from the virtualized PVR VRAM.
f32 vrf(u32 addr)
{
  return *(f32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

u32 vri(u32 addr)
{
  return *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

// Converts 16-bit PVR UV coordinates to 32-bit floats.
static f32 CVT16UV(u32 uv)
{
  uv <<= 16;
  return *(f32 *)&uv;
}

// Primary decoder that translates raw PVR vertex data into the 'Vertex' struct.
void decode_pvr_vertex(u32 base, u32 ptr, Vertex *cv)
{
  ISP_TSP isp;
  TSP tsp;
  TCW tcw;

  isp.full = vri(base);
  tsp.full = vri(base + 4);
  tcw.full = vri(base + 8);
  (void)tsp;
  (void)tcw; // Currently unused but may be needed later

  // Read coordinates: PVR positions are typically already transformed.
  // XYZ
  // UV
  // Base Col
  // Offset Col

  // XYZ are _allways_ there :)
  cv->x = vrf(ptr);  ptr += 4;
  cv->y = vrf(ptr);  ptr += 4;
  cv->z = vrf(ptr);  ptr += 4;

  // Handle Texture Coordinates (UVs)
  if (isp.Texture)
  { // Do texture , if any
    if (isp.UV_16b)
    {
      u32 uv = vri(ptr);
      cv->u = CVT16UV((u16)uv);
      cv->v = CVT16UV((u16)(uv >> 16));
      ptr += 4;
    }
    else
    {
      cv->u = vrf(ptr);  ptr += 4;
      cv->v = vrf(ptr);  ptr += 4;
    }
  }

  // Handle Vertex Color
  u32 col = vri(ptr);  ptr += 4;
  cv->col = col; // ABGR8888(col);
  cv->spc = 0;
  if (isp.Offset)
  {
    // Offset (specular) color. Stored raw like cv->col — the only caller
    // (background poly) only consumes cv->col anyway.
    cv->spc = vri(ptr);  ptr += 4;
  }
}

// Resets internal pointers for the next frame's vertex list.
void reset_vtx_state()
{
  curVTX = vertices;
  curLST = lists;
  curMod = listModes;
  // Clear the list boundaries so a frame without a TR (or PT) list doesn't
  // reuse a stale pointer from the previous frame.
  TransLST = 0;
  PTLST = 0;
  TransVTX = 0;
  TransMod = 0;
  PT_VTX = 0;
  PT_Mod = 0;
  global_regd = false;
  vtx_min_Z = 131072;
  vtx_max_Z = 0;
  tex_frame_reset(); // reset per-frame texture bump arena
  g_vertex_color_fix_cached = VERTEX_COLOR_FIX();
  g_offset_color_fix_cached = OFFSET_COLOR_FIX();
}

#define VTX_TFX(x) (x)
#define VTX_TFY(y) (y)

// The Dreamcast uses "Twiddled" (Morton Order) textures to improve cache
// input : yyyyyxxxxx // output : xyxyxyxy
// UV = x*y resolution
u32 fastcall twop(u32 x, u32 y, u32 x_sz, u32 y_sz)
{
  u32 rv = 0;
  u32 sh = 0;
  x_sz >>= 1;
  y_sz >>= 1;
  while (x_sz != 0 || y_sz != 0)
  {
    if (y_sz)
    {
      u32 temp = y & 1;
      rv |= temp << sh;
      y_sz >>= 1;
      y >>= 1;
      sh++;
    }
    if (x_sz)
    {
      u32 temp = x & 1;
      rv |= temp << sh;
      x_sz >>= 1;
      x >>= 1;
      sh++;
    }
  }
  return rv;
}

// Handler functions
// Calculates the offset for a Wii texture in TPL/GX block format.
u32 GX_TexOffs(u32 x, u32 y, u32 w)
{
  w /= 4;
  u32 xs = x & 3;
  x >>= 2;
  u32 ys = y & 3;
  y >>= 2;

  return (y * w + x) * 16 + (ys * 4 + xs);
}

// Converts YUV422 data to RGB565 for Wii compatibility.
// static + always_inline: __forceinline is a no-op on this devkitPPC build
// (see ta.h: "#define __forceinline"), so without this attribute the compiler
// is free to leave it as a real call/ret — costly since this runs per pixel
// during FMV decode (the hottest texture-conversion path in the renderer).
//
// Split into chroma/luma halves because every YUV422 source pixel pair (and
// every 2x2 twiddled block) shares one U/V pair across 2-4 luma samples.
// Computing the chroma terms (Bc/Gc/Rc) once per shared U/V and reusing them
// drops the per-pixel cost from 7 multiplies (original, computed by every
// caller that re-derived B/G/R from scratch) to 1 (just the luma term) —
// callers that still want a single all-in-one conversion can use YUV422().
static inline void __attribute__((always_inline)) YUV422_chroma(s32 Yu, s32 Yv, s32 &Bc, s32 &Gc, s32 &Rc)
{
  s32 Uc = Yu - 128;
  s32 Vc = Yv - 128;
  Bc = 132252 * Uc;
  Gc = -53281 * Vc - 25624 * Uc;
  Rc = 104595 * Vc;
}

static inline u32 __attribute__((always_inline)) YUV422_luma(s32 Y, s32 Bc, s32 Gc, s32 Rc)
{
  s32 Yc = 76283 * (Y - 16);
  s32 B = (Yc + Bc) >> (16 + 3);
  s32 G = (Yc + Gc) >> (16 + 2);
  s32 R = (Yc + Rc) >> (16 + 3);

  colclamp(0, 0x1F, B);
  colclamp(0, 0x3F, G);
  colclamp(0, 0x1F, R);

  return (R << 11) | (G << 5) | (B);  // RGB565 format
}

static inline u32 __attribute__((always_inline)) YUV422(s32 Y, s32 Yu, s32 Yv)
{
  s32 Bc, Gc, Rc;
  YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
  return YUV422_luma(Y, Bc, Gc, Rc);
}

// Infrastructure for fast pixel conversion routines using static polymorphism/templates.
#define pixelcvt_start(name, xa, yb)                                                     \
  struct name                                                                            \
  {                                                                                      \
    static const u32 xpp = xa;                                                           \
    static const u32 ypp = yb;                                                           \
    __forceinline static void fastcall Convert(u16 *pb, u32 x, u32 y, u32 pbw, u8 *data) \
    {

#define pixelcvt_end \
  }                  \
  }
#define pixelcvt_next(name, x, y) \
  pixelcvt_end;                   \
  pixelcvt_start(name, x, y)

#define pixelcvt_startVQ(name, x, y)                     \
  struct name                                            \
  {                                                      \
    static const u32 xpp = x;                            \
    static const u32 ypp = y;                            \
    __forceinline static u32 fastcall Convert(u16 *data) \
    {

#define pixelcvt_endVQ \
  }                    \
  }
#define pixelcvt_nextVQ(name, x, y) \
  pixelcvt_endVQ;                   \
  pixelcvt_startVQ(name, x, y)

inline void pb_prel(u16 *dst, u32 pbw, u32 x, u32 y, u32 col)
{
  dst[GX_TexOffs(x, y, pbw)] = col;
}


// FOR TEXTURE_4BPP_CI4()

// Write one 4-bit index into a GX CI4 block-layout buffer.
// GX CI4 tile: 8x8 = 32 bytes / tile
// High nibble = even x within tile, low nibble = odd x.
inline void ci4_prel(u8 *dst, u32 x, u32 y, u32 w, u8 idx)
{
  u32 tile     = (y / 8) * (w / 8) + (x / 8);
  u32 nibble   = tile * 64 + (y % 8) * 8 + (x % 8);
  u32 byte_off = nibble >> 1;
  if (nibble & 1)
    dst[byte_off] = (dst[byte_off] & 0xF0) | (idx & 0x0F); // low nibble
  else
    dst[byte_off] = (dst[byte_off] & 0x0F) | ((idx & 0x0F) << 4); // high nibble
}

// FOR TEXTURE_4BPP_CI8

// Write one 8-bit index into a GX CI8 block-layout buffer.
// GX CI8 tile: 8 pixels wide x 4 pixels tall = 32 bytes per tile.
inline void ci8_prel(u8 *dst, u32 x, u32 y, u32 w, u8 idx)
{
  u32 tile     = (y / 4) * (w / 8) + (x / 8);
  u32 byte_off = tile * 32 + (y % 4) * 8 + (x % 8);
  dst[byte_off] = idx;
}


// ===============
// Pixel Converters for Non-Twiddled (Planar) textures.
// ===============
pixelcvt_start(conv565_PL, 4, 1)
{
  // convert 4x1 565 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR0565(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR0565(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR0565(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR0565(p_in[3])); // 3,0
}
pixelcvt_next(conv1555_PL, 4, 1)
{
  // convert 4x1 1555 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR1555(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR1555(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR1555(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR1555(p_in[3])); // 3,0
}
pixelcvt_next(conv4444_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR4444(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR4444(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR4444(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR4444(p_in[3])); // 3,0
}
pixelcvt_next(convYUV_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u32 *p_in = (u32 *)data;

  s32 Y0 = (p_in[0] >> 8) & 255;  //
  s32 Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  s32 Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  s32 Yv = (p_in[0] >> 16) & 255; // p_in[2]
  
  pb_prel(pb, pbw, x + 0, y, YUV422(Y0, Yu, Yv)); // 0,0  
  pb_prel(pb, pbw, x + 1, y, YUV422(Y1, Yu, Yv)); // 1,0

  p_in += 1; // next 4 bytes

  Y0 = (p_in[0] >> 8) & 255;  //
  Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  Yv = (p_in[0] >> 16) & 255; // p_in[2]

    pb_prel(pb, pbw, x + 2, y, YUV422(Y0, Yu, Yv)); // 0,0
  pb_prel(pb, pbw, x + 3, y, YUV422(Y1, Yu, Yv)); // 1,0
}
pixelcvt_end;

// Pixel Converters for Twiddled textures.
pixelcvt_start(conv565_TW, 2, 2)
{
  // convert 4x1 565 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y + 0, ABGR0565(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 0, y + 1, ABGR0565(p_in[1])); // 0,1
  pb_prel(pb, pbw, x + 1, y + 0, ABGR0565(p_in[2])); // 1,0
  pb_prel(pb, pbw, x + 1, y + 1, ABGR0565(p_in[3])); // 1,1
}
pixelcvt_next(conv1555_TW, 2, 2)
{
  // convert 4x1 1555 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y + 0, ABGR1555(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 0, y + 1, ABGR1555(p_in[1])); // 0,1
  pb_prel(pb, pbw, x + 1, y + 0, ABGR1555(p_in[2])); // 1,0
  pb_prel(pb, pbw, x + 1, y + 1, ABGR1555(p_in[3])); // 1,1
}
pixelcvt_next(conv4444_TW, 2, 2)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y + 0, ABGR4444(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 0, y + 1, ABGR4444(p_in[1])); // 0,1
  pb_prel(pb, pbw, x + 1, y + 0, ABGR4444(p_in[2])); // 1,0
  pb_prel(pb, pbw, x + 1, y + 1, ABGR4444(p_in[3])); // 1,1
}
pixelcvt_next(convYUV422_TW, 2, 2)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;

  s32 Y0 = (p_in[0] >> 8) & 255; //
  s32 Yu = (p_in[0] >> 0) & 255; // p_in[0]
  s32 Y1 = (p_in[2] >> 8) & 255; // p_in[3]
  s32 Yv = (p_in[2] >> 0) & 255; // p_in[2]

  pb_prel(pb, pbw, x + 0, y + 0, YUV422(Y0, Yu, Yv)); // 0,0
  pb_prel(pb, pbw, x + 1, y + 0, YUV422(Y1, Yu, Yv)); // 1,0

  // next 4 bytes
  // p_in+=2;

  Y0 = (p_in[1] >> 8) & 255; //
  Yu = (p_in[1] >> 0) & 255; // p_in[0]
  Y1 = (p_in[3] >> 8) & 255; // p_in[3]
  Yv = (p_in[3] >> 0) & 255; // p_in[2]

  pb_prel(pb, pbw, x + 0, y + 1, YUV422(Y0, Yu, Yv)); // 0,1
  pb_prel(pb, pbw, x + 1, y + 1, YUV422(Y1, Yu, Yv)); // 1,1
}
pixelcvt_end;

// VQ Pixel Converters - Convert() averages 4 codebook pixels (legacy),
// ConvertPixel() converts one pixel DC->GX for the new VQ decompressor.
pixelcvt_startVQ(conv565_VQ, 2, 2)
{
  u32 R = ABGR0565_R(data[0]) + ABGR0565_R(data[1]) + ABGR0565_R(data[2]) + ABGR0565_R(data[3]);
  u32 G = ABGR0565_G(data[0]) + ABGR0565_G(data[1]) + ABGR0565_G(data[2]) + ABGR0565_G(data[3]);
  u32 B = ABGR0565_B(data[0]) + ABGR0565_B(data[1]) + ABGR0565_B(data[2]) + ABGR0565_B(data[3]);
  R >>= 2; G >>= 2; B >>= 2;
  return R | (G << 5) | (B << 11);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR0565(p); }
};  // closes struct

pixelcvt_startVQ(conv1555_VQ, 2, 2)
{
  u32 R = ABGR1555_R(data[0]) + ABGR1555_R(data[1]) + ABGR1555_R(data[2]) + ABGR1555_R(data[3]);
  u32 G = ABGR1555_G(data[0]) + ABGR1555_G(data[1]) + ABGR1555_G(data[2]) + ABGR1555_G(data[3]);
  u32 B = ABGR1555_B(data[0]) + ABGR1555_B(data[1]) + ABGR1555_B(data[2]) + ABGR1555_B(data[3]);
  u32 A = ABGR1555_A(data[0]) + ABGR1555_A(data[1]) + ABGR1555_A(data[2]) + ABGR1555_A(data[3]);
  R >>= 2; G >>= 2; B >>= 2; A >>= 2;
  return R | (G << 5) | (B << 10) | (A << 15);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR1555(p); }
};  // closes struct

pixelcvt_startVQ(conv4444_VQ, 2, 2)
{
  u32 R = ABGR4444_R(data[0]) + ABGR4444_R(data[1]) + ABGR4444_R(data[2]) + ABGR4444_R(data[3]);
  u32 G = ABGR4444_G(data[0]) + ABGR4444_G(data[1]) + ABGR4444_G(data[2]) + ABGR4444_G(data[3]);
  u32 B = ABGR4444_B(data[0]) + ABGR4444_B(data[1]) + ABGR4444_B(data[2]) + ABGR4444_B(data[3]);
  u32 A = ABGR4444_A(data[0]) + ABGR4444_A(data[1]) + ABGR4444_A(data[2]) + ABGR4444_A(data[3]);
  R >>= 2; G >>= 2; B >>= 2; A >>= 2;
  return R | (G << 4) | (B << 8) | (A << 12);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR4444(p); }
};  // closes struct

pixelcvt_startVQ(convYUV422_VQ, 2, 2)
{
  u16 *p_in = (u16 *)data;
  s32 Y0 = (p_in[0] >> 8) & 255;
  s32 Yu = (p_in[0] >> 0) & 255;
  s32 Y1 = (p_in[2] >> 8) & 255;
  s32 Yv = (p_in[2] >> 0) & 255;
  return YUV422(16 + ((Y0 - 16) + (Y1 - 16)) / 2.0f, Yu, Yv);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) {
    s32 Y = (p >> 8) & 255; s32 U = (p >> 0) & 255;
    return YUV422(Y, U, 128);
  }
};  // closes struct

// Redefinition of twiddle for local use. (64b words)
u32 fastcall twiddle_razi(u32 x, u32 y, u32 x_sz, u32 y_sz)
{
  // u32 rv2=twiddle_optimiz3d(raw_addr,U);
  u32 rv = 0; // raw_addr & 3;//low 2 bits are directly passed  -> needs some misc stuff to work.However
  // Pvr internaly maps the 64b banks "as if" they were twidled :p

  // verify(x_sz==y_sz);
  u32 sh = 0;
  x_sz >>= 1;
  y_sz >>= 1;
  while (x_sz != 0 || y_sz != 0)
  {
    if (y_sz)
    {
      u32 temp = y & 1;
      rv |= temp << sh;
      y_sz >>= 1; y >>= 1; sh++;
    }
    if (x_sz)
    {
      u32 temp = x & 1;
      rv |= temp << sh;
      x_sz >>= 1; x >>= 1; sh++;
    }
  }
  return rv;
}

#define twop twiddle_razi
u8 *VramWork;

// ----------------------------------------------------------------------------
// Twiddle axis-table cache.
//
// Every twiddled texture path below precomputes table_x[x]=twop(x,0,w,h) and
// table_y[y]=twop(0,y,w,h) once per call so the per-pixel loop only needs a
// cheap "table_x[x] | table_y[y]" instead of calling twop() per pixel. But a
// game reuses the same handful of texture sizes every frame, so rebuilding
// these tables from twop()'s branchy bit-interleave loop on every single
// texture decode is redundant work. Cache the raw (undivided) tables
// globally, keyed by (w,h) — twop's interleave order depends on both
// dimensions, not just one, so a non-square texture needs its own entry.
// ----------------------------------------------------------------------------
#define TWIDDLE_CACHE_SLOTS 24

struct TwiddleAxisEntry
{
  u32 w, h;           // 0,0 = unused slot
  u32 table_x[1024];  // raw twop(x,0,w,h) for x in [0,w)
  u32 table_y[1024];  // raw twop(0,y,w,h) for y in [0,h)
};

static TwiddleAxisEntry g_twiddle_cache[TWIDDLE_CACHE_SLOTS];
static u32 g_twiddle_cache_count = 0;

// Returns raw (undivided) per-axis twiddle tables for (w,h), building and
// caching them on first use. table_x[x] | table_y[y] == twop(x,y,w,h); both
// can be shifted (e.g. >>2 for 2x2-block formats) before or after the OR,
// since the two tables occupy disjoint bit positions.
static void get_twiddle_axis_tables(u32 w, u32 h, const u32 **out_x, const u32 **out_y)
{
  for (u32 i = 0; i < g_twiddle_cache_count; i++)
  {
    if (g_twiddle_cache[i].w == w && g_twiddle_cache[i].h == h)
    {
      *out_x = g_twiddle_cache[i].table_x;
      *out_y = g_twiddle_cache[i].table_y;
      return;
    }
  }

  // Not cached yet: claim a slot (evict slot 0 once full — in practice a
  // game's set of distinct texture sizes is small and stable, so eviction
  // essentially never triggers).
  u32 idx = (g_twiddle_cache_count < TWIDDLE_CACHE_SLOTS) ? g_twiddle_cache_count++ : 0;
  TwiddleAxisEntry &e = g_twiddle_cache[idx];
  e.w = w;
  e.h = h;
  for (u32 x = 0; x < w; x++) e.table_x[x] = twop(x, 0, w, h);
  for (u32 y = 0; y < h; y++) e.table_y[y] = twop(0, y, w, h);

  *out_x = e.table_x;
  *out_y = e.table_y;
}

// Texture untwiddling and conversion template. (Handler)
template <class PixelConvertor>
void fastcall texture_TW(u8 *p_in, u32 Width, u32 Height)
{
  u8 *pb = VramWork;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp;

  // Max DC texture size is 1024. Using fixed-size tables is safe and fast.
  u32 table_x[1024];
  u32 table_y[1024];

  // Read the cached raw per-axis tables for this (Width,Height) — avoids
  // re-running twop()'s bit-interleave loop for sizes already seen this run.
  const u32 *traw_x, *traw_y;
  get_twiddle_axis_tables(Width, Height, &traw_x, &traw_y);
  for (u32 x = 0; x < Width; x += PixelConvertor::xpp) {
    table_x[x] = traw_x[x] / divider;
  }
  for (u32 y = 0; y < Height; y += PixelConvertor::ypp) {
    table_y[y] = traw_y[y] / divider;
  }

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    u32 offset_y = table_y[y];
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {
      // Use the precomputed tables just like in texture_VQ!
      u8 *p = &p_in[(offset_y | table_x[x]) << 3];

      u16 buf[4];
      buf[0] = *host_ptr_xor((u16*)&p[0]);
      buf[1] = *host_ptr_xor((u16*)&p[2]);
      buf[2] = *host_ptr_xor((u16*)&p[4]);
      buf[3] = *host_ptr_xor((u16*)&p[6]);

      PixelConvertor::Convert((u16*)pb, x, y, Width, (u8*)buf);
    }
  }
  // NOTE: do NOT memcpy back to p_in (params.vram). Decoded pixels live in
  // VramWork (vram_buffer slot). Writing back would overwrite the raw DC
  // texture data, corrupting it for future scenes and making DEADBEEF
  // sentinel detection unreliable.
}

// VQ texture decompressor: expands compressed data into full RGB pixels in GX
// 4x4-block layout. Identical output format to texture_TW — no CI8/TLUT needed.
template <class PixelConvertor>
void fastcall texture_VQ(u8 *p_in, u32 Width, u32 Height, u8 *vq_codebook)
{
  u8 *pb = VramWork;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp; // 4 for 2x2

  u32 table_x[1024];
  u32 table_y[1024];

  const u32 *traw_x, *traw_y;
  get_twiddle_axis_tables(Width, Height, &traw_x, &traw_y);
  for (u32 x = 0; x < Width; x += PixelConvertor::xpp) {
    table_x[x] = traw_x[x] / divider;
  }
  for (u32 y = 0; y < Height; y += PixelConvertor::ypp) {
    table_y[y] = traw_y[y] / divider;
  }

  u16 *dst = (u16*)pb;

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    u32 offset_y = table_y[y];
    
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {

      u8 idx = *host_ptr_xor(&p_in[offset_y | table_x[x]]);

      u8 *cb = &vq_codebook[idx * 8];
      u16 s0 = *host_ptr_xor((u16*)&cb[0]);
      u16 s1 = *host_ptr_xor((u16*)&cb[2]);
      u16 s2 = *host_ptr_xor((u16*)&cb[4]);
      u16 s3 = *host_ptr_xor((u16*)&cb[6]);

      // Calculate the tile base address ONCE for the 2x2 block!
      // This eliminates 4 calls to pb_prel (and GX_TexOffs) per iteration.
      u32 tile_offset = ((y >> 2) * (Width >> 2) + (x >> 2)) * 16;
      u32 base = tile_offset + (y & 3) * 4 + (x & 3);

      dst[base]     = PixelConvertor::ConvertPixel(s0); // (x+0, y+0)
      dst[base + 1] = PixelConvertor::ConvertPixel(s2); // (x+1, y+0)
      dst[base + 4] = PixelConvertor::ConvertPixel(s1); // (x+0, y+1)
      dst[base + 5] = PixelConvertor::ConvertPixel(s3); // (x+1, y+1)
    }
  }
}

// Planar (Linear) texture conversion.
template <int type>
void Plannar(u8 *praw, u32 w, u32 h)
{
  // host_ptr_xor(u16* p) = (u16*)((unat)p ^ (4-sizeof(u16))) = (u16*)((unat)p ^ 2).
  // This correctly reads a DC little-endian u16 from Wii big-endian VRAM where each
  // u32 word is stored byte-swapped: the FIRST DC pixel in a u32 lives at byte+2,
  // the SECOND at byte+0.  ^2 handles that swap correctly for a FIXED address.
  //
  // BUT: the original sequential ptr++ loop reads even pixels via ^2 (correct)
  // then advances ptr by 2 bytes, so the odd pixel is read at byte+2 via ^2
  // which lands on byte+0 of the NEXT word — reading the wrong pixel.
  // Net result: every adjacent pair of pixels is swapped.
  //
  // FIX: read both pixels of each u32 word at once, extract in correct order.
  // The u32 read on Wii BE gives the correct DC u32 value directly (host_ptr_xor
  // for u32 is ^0 = identity, so *(u32*)ptr is already correct).
  // Within that u32: low 16 bits = first DC pixel, high 16 bits = second DC pixel.

  u32 *src = (u32 *)praw;  // u32 reads give correct DC values directly
  u16 *dst = (u16 *)VramWork;
  u32 x, y;

  for (y = 0; y < h; y++)
  {
    for (x = 0; x < w; x += 2)  // two pixels per u32 word
    {
      u32 word = *src++;          // correct DC u32 value: low=pix0, high=pix1
      u16 pix0 = (u16)(word & 0xFFFF);
      u16 pix1 = (u16)(word >> 16);

      switch (type)
      {
      case 1555:
        dst[GX_TexOffs(x + 0, y, w)] = ABGR1555(pix0);
        dst[GX_TexOffs(x + 1, y, w)] = ABGR1555(pix1);
        break;
      case 565:
        dst[GX_TexOffs(x + 0, y, w)] = ABGR0565(pix0);
        dst[GX_TexOffs(x + 1, y, w)] = ABGR0565(pix1);
        break;
      case 4444:
        dst[GX_TexOffs(x + 0, y, w)] = ABGR4444(pix0);
        dst[GX_TexOffs(x + 1, y, w)] = ABGR4444(pix1);
        break;
      case 422:
      {
        // DC YUV422 planar is UYVY: U,Y0,V,Y1 at DC LE bytes 0-3.
        // u32 read on Wii BE gives correct DC LE value; extract accordingly.
        s32 Yu = (word >>  0) & 255;  // U (Cb)
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V (Cr)
        s32 Y1 = (word >> 24) & 255;  // Y1
        dst[GX_TexOffs(x + 0, y, w)] = YUV422(Y0, Yu, Yv);
        dst[GX_TexOffs(x + 1, y, w)] = YUV422(Y1, Yu, Yv);
      }
      break;
      }
    }
  }
}

// =========================
// Palette management for indexed textures.
// =========================

void SetupPaletteForTexture(u32 palette_index, u32 sz)
{
  palette_index &= ~(sz - 1);
  u32 fmtpal = PAL_RAM_CTRL & 3;

  if (fmtpal < 3)
    palette_index >>= 1;

    // sceGuClutMode(PalFMT[fmtpal],0,0xFF,0);//or whatever
    // sceGuClutLoad(sz/8,&palette_lut[palette_index]);
}

// PVR Mipmap offsets (Dreamcast specific) — §3.6.2.4
//
// DC stores mipmaps small→large: TCW Texture Address = start of the 1×1 level.
// To reach the full-size level we must SKIP all the smaller levels.
//
// OtherMipPoint[n]: byte offset to add to tex_addr to reach the full-size level
//   for a texture whose log2(width) = n  (n = TexU + 3, width = 8 << TexU).
//   Index 0..2 (1x1,2x2,4x4) included for completeness; real DC textures use idx 3..10.
//   Closed form: OtherMipPoint[n] = (3 + (4^n - 1)/3) * bpp/8
//   where the "+3" accounts for the 3-texel reserved header before the 1x1 level.
//   The table below gives the TEXEL count (multiply by bpp/8 to get bytes).
//   Values: 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558
static const u32 OtherMipPoint[11] = {
    0x00003, // idx 0:  1×1   (never used for real textures)
    0x00004, // idx 1:  2×2
    0x00008, // idx 2:  4×4
    0x00018, // idx 3:  8×8
    0x00058, // idx 4:  16×16
    0x00158, // idx 5:  32×32
    0x00558, // idx 6:  64×64
    0x01558, // idx 7:  128×128
    0x05558, // idx 8:  256×256
    0x15558, // idx 9:  512×512
    0x55558  // idx 10: 1024×1024
};

// VQMipPoint[n]: byte offset FROM tex_addr (= codebook base) to reach the full-size
//   VQ index map. Codebook is always 2048 bytes; index maps then follow small→large.
//   VQMipPoint[n] = 2048 + index-map offset to the full-size level.
static const u32 VQMipPoint[11] = {
    2048 + 0x00000, // idx 0:  1×1
    2048 + 0x00001, // idx 1:  2×2
    2048 + 0x00002, // idx 2:  4×4
    2048 + 0x00006, // idx 3:  8×8
    2048 + 0x00016, // idx 4:  16×16
    2048 + 0x00056, // idx 5:  32×32
    2048 + 0x00156, // idx 6:  64×64
    2048 + 0x00556, // idx 7:  128×128
    2048 + 0x01556, // idx 8:  256×256
    2048 + 0x05556, // idx 9:  512×512
    2048 + 0x15556  // idx 10: 1024×1024
};

// Macro to encapsulate the logic for twiddled textures (VQ or standard).
//
// MIPMAP note (§3.6.2.4): TCW Texture Address = start of the 1×1 level (smallest).
// Levels are packed small→large. We must offset forward to the full-size level.
// For VQ the codebook base is NEVER offset; only the index map base moves.
// For non-VQ: add OtherMipPoint[idx] * bpp/8 bytes (16bpp → *2).
// MipMapped textures are always square → height = width (ignore TexV).
#define twidle_tex(format)                                                                \
  if (mod->tcw.NO_PAL.VQ_Comp)                                                            \
  {                                                                                       \
    /* Codebook always at tex_addr — do NOT move this for mipmaps */                      \
    vq_codebook = (u8 *)&params.vram[tex_addr];                                           \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    {                                                                                     \
      /* Jump to full-size VQ index map (codebook + small-level indices) */               \
      u32 mip_idx = mod->tsp.TexU + 3; /* log2(width): TexU 0..7 → idx 3..10 */         \
      tex_addr += VQMipPoint[mip_idx];                                                    \
      /* MipMapped = square: ignore TexV, use TexU for both dimensions */                 \
      h = w;                                                                              \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
      tex_addr += 2048; /* skip codebook to reach index map */                            \
    }                                                                                     \
    if(DEBUG_MESSAGE()) { printf("[VQ] codebook=..."); }                                  \
    texture_VQ<conv##format##_VQ> /**/ ((u8 *)&params.vram[tex_addr], w, h, vq_codebook); \
    texVQ = 1;                                                                            \
  }                                                                                       \
  else                                                                                    \
  {                                                                                       \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    {                                                                                     \
      /* Skip all smaller mip levels to reach the full-size level (16bpp → ×2 bytes) */  \
      u32 mip_idx = mod->tsp.TexU + 3; /* log2(width): TexU 0..7 → idx 3..10 */         \
      tex_addr += OtherMipPoint[mip_idx] * 2; /* 16bpp: texels × 2 bytes */              \
      /* MipMapped = square: ignore TexV, use TexU for both dimensions */                 \
      h = w;                                                                              \
    }                                                                                     \
    texture_TW<conv##format##_TW> /*TW*/ ((u8 *)&params.vram[tex_addr], w, h);            \
  }

#define norm_text(format)        \
  if (mod->tcw.NO_PAL.StrideSel) \
    w = 512;                     \
  Plannar<format>((u8 *)&params.vram[tex_addr], w, h);

  /*u32 sr;\
if (mod->tcw.NO_PAL.StrideSel)\
  {sr=(TEXT_CONTROL&31)*32;}\
        else\
  {sr=w;}\
  format((u8*)&params.vram[tex_addr],sr,h);*/

// Maps Dreamcast texture wrap/clamp modes to Wii GX constants.
int TexUV(u32 flip, u32 clamp)
{
  if (clamp)
    return GX_CLAMP;
  else if (flip)
    return GX_MIRROR;
  else
    return GX_REPEAT;
}



// -----------------------------------------------------------------
// CMPR (DXT1) block encoder
// -----------------------------------------------------------------
// GX CMPR layout: 8x8 super-tile of four 4x4 DXT1 sub-blocks: [TL][TR][BL][BR]
// Each 4x4 DXT1 block = 8 bytes:
//   [0-1] color0 RGB565 big-endian   (must be > color1 for 4-color opaque mode)
//   [2-3] color1 RGB565 big-endian
//   [4-7] 16x 2-bit selectors, row-major, MSB-first (pixel0=bits[31:30])
//         index 0=color0, 1=color1, 2=(2c0+c1)/3, 3=(c0+2c1)/3
// Strategy: min/max luma anchor selection + nearest-palette assignment per pixel.
// Exact for x < 768 (171 == floor(512/3) + 1); our callers pass x <= 189.
static inline u32 __attribute__((always_inline)) div3_u8(u32 x) { return (x * 171) >> 9; }
static void encode_cmpr_block(const u16 *src, u8 *dst)
{
  u32 best_min = 0xFFFFFFFF, best_max = 0;
  int idx_min = 0, idx_max = 0;
  for (int i = 0; i < 16; i++)
  {
    u16 p  = src[i];
    u32 r  = (p >> 11) & 0x1F;
    u32 g  = (p >>  5) & 0x3F;
    u32 b  = (p      ) & 0x1F;
    u32 lm = r * 2 + g + b * 2;
    if (lm < best_min) { best_min = lm; idx_min = i; }
    if (lm > best_max) { best_max = lm; idx_max = i; }
  }

  u16 c0 = src[idx_max]; // brightest  -> color0 (must be >= color1)
  u16 c1 = src[idx_min]; // darkest    -> color1

  // Solid-color block: nudge c0 up so GX stays in 4-color mode (c0 > c1).
  if (c0 == c1)
  {
    u32 r = (c0 >> 11) & 0x1F;
    u32 g = (c0 >>  5) & 0x3F;
    u32 b = (c0      ) & 0x1F;
    if (r < 0x1F) r++; else if (g < 0x3F) g++; else if (b < 0x1F) b++;
    c0 = (u16)((r << 11) | (g << 5) | b);
  }

  u32 r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
  u32 r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  // Fast unsigned /3 via multiply+shift (exact for inputs < 768, our inputs
  // top out at ~189): PowerPC's divwu is far slower than a multiply+shift,
  // and this runs 6x per 4x4 block for every CMPR-encoded FMV frame.
  u32 r2 = div3_u8(2*r0+r1), g2 = div3_u8(2*g0+g1), b2 = div3_u8(2*b0+b1);
  u32 r3 = div3_u8(r0+2*r1), g3 = div3_u8(g0+2*g1), b3 = div3_u8(b0+2*b1);

  u32 selectors = 0;
  for (int i = 0; i < 16; i++)
  {
    u16 p   = src[i];
    s32 pr6 = ((p >> 11) & 0x1F) << 1; // scale R/B to 6-bit for fair distance
    s32 pg  = (p >>  5) & 0x3F;
    s32 pb6 = ((p      ) & 0x1F) << 1;

    s32 dr0=pr6-(s32)(r0<<1), dg0=pg-(s32)g0, db0=pb6-(s32)(b0<<1);
    s32 dr1=pr6-(s32)(r1<<1), dg1=pg-(s32)g1, db1=pb6-(s32)(b1<<1);
    s32 dr2=pr6-(s32)(r2<<1), dg2=pg-(s32)g2, db2=pb6-(s32)(b2<<1);
    s32 dr3=pr6-(s32)(r3<<1), dg3=pg-(s32)g3, db3=pb6-(s32)(b3<<1);

    u32 d0=(u32)(dr0*dr0+dg0*dg0+db0*db0);
    u32 d1=(u32)(dr1*dr1+dg1*dg1+db1*db1);
    u32 d2=(u32)(dr2*dr2+dg2*dg2+db2*db2);
    u32 d3=(u32)(dr3*dr3+dg3*dg3+db3*db3);

    u32 best_d=d0, best_idx=0;
    if (d1<best_d){best_d=d1;best_idx=1;}
    if (d2<best_d){best_d=d2;best_idx=2;}
    if (d3<best_d){best_d=d3;best_idx=3;}

    selectors |= (best_idx << (30 - i * 2));
  }

  dst[0]=(u8)(c0>>8); dst[1]=(u8)(c0&0xFF);
  dst[2]=(u8)(c1>>8); dst[3]=(u8)(c1&0xFF);
  dst[4]=(u8)(selectors>>24); dst[5]=(u8)(selectors>>16);
  dst[6]=(u8)(selectors>> 8); dst[7]=(u8)(selectors    );
}

// Real per-row pixel stride for the planar YUV422 source, independent of the
// destination CMPR block width (which must stay == the declared GX texture
// width). Recomputed every decode from TA_YUV_TEX_CTRL — see the case-3
// YUV branch above where it's assigned — falling back to the declared width
// when that register doesn't describe a smaller real size.
int g_yuv_src_stride_override = 0;
// Real source row COUNT (height), same idea as the stride above but for the
// vertical axis (TA_YUV_TEX_CTRL bits[13:8]). 0 = no override, use declared h.
// Rows at/after this are NOT real source data — treat them the same way
// rows past the declared height already are (zero-fill), instead of reading
// past the legitimately-written buffer.
int g_yuv_src_real_h = 0;

// YUV422 planar -> GX_TF_CMPR
// DC planar YUV422: UYVY layout — DC LE bytes [U, Y0, V, Y1] at addresses [0,1,2,3].
// Each pixel pair is two u16s (Y<<8)|chroma; LE storage puts chroma at even addresses.
// Wii BE u32 read gives the DC LE u32 value; bit-shift extraction recovers channels.
static void YUV422_to_CMPR_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  static const u32 sub_ox[4] = {0,4,0,4};
  static const u32 sub_oy[4] = {0,0,4,4};

  for (u32 ty = 0; ty < h; ty += 8)
  for (u32 tx = 0; tx < w; tx += 8)
  {
    u8 *super_dst = dst + ((ty/8)*(w/8) + (tx/8)) * 32;
    for (int sub = 0; sub < 4; sub++)
    {
      u32 bx=tx+sub_ox[sub], by=ty+sub_oy[sub];
      u16 block_pixels[16];
      for (u32 row = 0; row < 4; row++)
      {
        u32 y = by + row;
        if (y >= h || y >= real_h) { for (int c=0;c<4;c++) block_pixels[row*4+c]=0; continue; }
        for (u32 col = 0; col < 4; col += 2)
        {
          // DC YUV422 planar is YUYV: Y0,U,Y1,V at DC LE bytes 0-3.
          // u32 read gives the correct DC LE value on Wii BE, so bit-shift
          // extraction recovers the right bytes. Byte-positional reads are
          // WRONG on Wii: the Wii stores bytes in reversed order within each
          // u32 (to satisfy the u32-read-equals-DC-LE invariant), so p[0]
          // would be V (DC byte 3), not U (DC byte 0).
          u32 word = *(u32*)&src_vram[(y*src_stride + bx+col) * 2];
          s32 Yu = (word >>  0) & 255;  // U (Cb) = DC UYVY byte 0
          s32 Y0 = (word >>  8) & 255;  // Y0
          s32 Yv = (word >> 16) & 255;  // V (Cr) = DC UYVY byte 2
          s32 Y1 = (word >> 24) & 255;  // Y1
          s32 Bc, Gc, Rc;
          YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
          block_pixels[row*4+col  ] = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
          block_pixels[row*4+col+1] = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
        }
      }
      encode_cmpr_block(block_pixels, super_dst + sub*8);
    }
  }
}

// YUV422 twiddled -> GX_TF_CMPR
//
// Twiddled YUV422 stores pixels in 2x2 blocks (xpp=2, ypp=2). Each 8-byte chunk
// (4x u16, after host_ptr_xor byte-swap) holds one 2x2 block, matching the
// original convYUV422_TW logic:
//   buf[0]: Y(row0,col0) high byte, U low byte
//   buf[1]: Y(row1,col0) high byte, U low byte  (same U as buf[0])
//   buf[2]: Y(row0,col1) high byte, V low byte
//   buf[3]: Y(row1,col1) high byte, V low byte  (same V as buf[2])
// host_ptr_xor for u16 is ^2 (swaps the two u16 halves within each u32 on Wii BE):
//   *host_ptr_xor((u16*)&p[0]) reads bytes [2,3] of the u32 at p[0..3]
//   *host_ptr_xor((u16*)&p[2]) reads bytes [0,1] of the u32 at p[0..3]
// So per 8-byte chunk: w0 = *(u32*)&p[0] gives buf0=(w0>>16), buf1=(w0&0xFFFF);
//                       w1 = *(u32*)&p[4] gives buf2=(w1>>16), buf3=(w1&0xFFFF).
static void YUV422_to_CMPR_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  static const u32 sub_ox[4] = {0,4,0,4};
  static const u32 sub_oy[4] = {0,0,4,4};

  // Precompute twiddle block indices per axis instead of recomputing the
  // morton code in the innermost loop: twiddle_razi(x,y,..) ==
  // twiddle_razi(x,0,..) | twiddle_razi(0,y,..) because the two axes
  // interleave into disjoint bit positions. Same trick texture_TW already
  // uses below — turns w*h/4 twiddle_razi calls into just w+h of them.
  u32 table_x[1024], table_y[1024];
  const u32 *traw_x, *traw_y;
  get_twiddle_axis_tables(w, h, &traw_x, &traw_y);
  for (u32 x = 0; x < w; x += 2) table_x[x] = traw_x[x] / 4;
  for (u32 y = 0; y < h; y += 2) table_y[y] = traw_y[y] / 4;

  for (u32 ty = 0; ty < h; ty += 8)
  for (u32 tx = 0; tx < w; tx += 8)
  {
    u8 *super_dst = dst + ((ty/8)*(w/8) + (tx/8)) * 32;
    for (int sub = 0; sub < 4; sub++)
    {
      u32 bx=tx+sub_ox[sub], by=ty+sub_oy[sub];
      u16 block_pixels[16];
      for (u32 row = 0; row < 4; row += 2)
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 x   = bx + col;
        u32 y   = by + row;
        u32 tw  = table_y[y] | table_x[x]; // 2x2 block index (divider = xpp*ypp = 4)
        u8 *p   = &src_vram[tw * 8];
        u32 w0  = *(u32*)&p[0];
        u32 w1  = *(u32*)&p[4];
        u16 buf0 = (u16)(w0 >> 16);
        u16 buf1 = (u16)(w0      );
        u16 buf2 = (u16)(w1 >> 16);
        u16 buf3 = (u16)(w1      );

        s32 Y00 = buf0 & 255; s32 Yv = (buf0 >> 8) & 255;
        s32 Y10 = buf1 & 255;
        s32 Y01 = buf2 & 255; s32 Yu = (buf2 >> 8) & 255;
        s32 Y11 = buf3 & 255;

        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        block_pixels[row*4+col    ] = (u16)YUV422_luma(Y00, Bc, Gc, Rc);
        block_pixels[row*4+col+1  ] = (u16)YUV422_luma(Y01, Bc, Gc, Rc);
        block_pixels[(row+1)*4+col  ] = (u16)YUV422_luma(Y10, Bc, Gc, Rc);
        block_pixels[(row+1)*4+col+1] = (u16)YUV422_luma(Y11, Bc, Gc, Rc);
      }
      encode_cmpr_block(block_pixels, super_dst + sub*8);
    }
  }
}

// -----------------------------------------------------------------
// RGBA8 encoder
// -----------------------------------------------------------------
// GX RGBA8 tile layout: 4x4 tile = 64 bytes, split into two 32-byte sub-tiles:
//   Sub-tile 0 [0-31] : AR pairs  (A0,R0, A1,R1 ... A15,R15)
//   Sub-tile 1 [32-63]: GB pairs  (G0,B0, G1,B1 ... G15,B15)
// Pixels in row-major order within each sub-tile.

// Inline RGB565 -> RGB888 expansion (replicates MSBs into LSBs for full range).
#define RGB565_R8(p) ((u8)((((p)>>11)&0x1F)<<3|(((p)>>13)&0x7)))
#define RGB565_G8(p) ((u8)((((p)>> 5)&0x3F)<<2|(((p)>> 9)&0x3)))
#define RGB565_B8(p) ((u8)((((p)    )&0x1F)<<3|(((p)>> 2)&0x7)))

// YUV422 planar -> GX_TF_RGBA8
static void YUV422_to_RGBA8_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h  = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  u32 tiles_x = w / 4, tiles_y = h / 4;
  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u8 *tile = dst + (ty*tiles_x + tx) * 64;
    u8 *ar   = tile;
    u8 *gb   = tile + 32;
    for (u32 row = 0; row < 4; row++)
    {
      u32 y = ty*4 + row;
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 p0 = row*4+col, p1 = row*4+col+1;
        if (y >= real_h)
        {
          ar[p0*2]=0xFF; ar[p0*2+1]=0; ar[p1*2]=0xFF; ar[p1*2+1]=0;
          gb[p0*2]=0;    gb[p0*2+1]=0; gb[p1*2]=0;    gb[p1*2+1]=0;
          continue;
        }
        u32 x    = tx*4 + col;
        u32 word = *(u32*)&src_vram[(y*src_stride + x) * 2];
        s32 Yu = (word >>  0) & 255;  // U
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V
        s32 Y1 = (word >> 24) & 255;  // Y1
        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        u16 c0 = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
        u16 c1 = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
        ar[p0*2  ]=0xFF; ar[p0*2+1]=RGB565_R8(c0);
        ar[p1*2  ]=0xFF; ar[p1*2+1]=RGB565_R8(c1);
        gb[p0*2  ]=RGB565_G8(c0); gb[p0*2+1]=RGB565_B8(c0);
        gb[p1*2  ]=RGB565_G8(c1); gb[p1*2+1]=RGB565_B8(c1);
      }
    }
  }
}

// YUV422 twiddled -> GX_TF_RGBA8
// See YUV422_to_CMPR_Twiddled for full explanation of the 2x2 twiddled YUV layout
// and the host_ptr_xor (^2) byte-swap replication via raw u32 reads.
static void YUV422_to_RGBA8_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 tiles_x = w / 4, tiles_y = h / 4;

  // Same per-axis twiddle precompute as YUV422_to_CMPR_Twiddled — see there.
  u32 table_x[1024], table_y[1024];
  const u32 *traw_x, *traw_y;
  get_twiddle_axis_tables(w, h, &traw_x, &traw_y);
  for (u32 x = 0; x < w; x += 2) table_x[x] = traw_x[x] / 4;
  for (u32 y = 0; y < h; y += 2) table_y[y] = traw_y[y] / 4;

  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u8 *tile = dst + (ty*tiles_x + tx) * 64;
    u8 *ar   = tile;
    u8 *gb   = tile + 32;
    for (u32 row = 0; row < 4; row += 2)
    for (u32 col = 0; col < 4; col += 2)
    {
      u32 x  = tx*4 + col;
      u32 y  = ty*4 + row;
      u32 tw = table_y[y] | table_x[x];
      u8 *p  = &src_vram[tw * 8];
      u32 w0 = *(u32*)&p[0];
      u32 w1 = *(u32*)&p[4];
      u16 buf0 = (u16)(w0 >> 16);
      u16 buf1 = (u16)(w0      );
      u16 buf2 = (u16)(w1 >> 16);
      u16 buf3 = (u16)(w1      );

      // FIX: swap Yu/Yv source halves (was crossing R<->B, e.g. orange -> blue)
      s32 Y00 = buf0 & 255; s32 Yv = (buf0 >> 8) & 255;
      s32 Y10 = buf1 & 255;
      s32 Y01 = buf2 & 255; s32 Yu = (buf2 >> 8) & 255;
      s32 Y11 = buf3 & 255;

      s32 Bc, Gc, Rc;
      YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
      u16 c00 = (u16)YUV422_luma(Y00, Bc, Gc, Rc);
      u16 c01 = (u16)YUV422_luma(Y01, Bc, Gc, Rc);
      u16 c10 = (u16)YUV422_luma(Y10, Bc, Gc, Rc);
      u16 c11 = (u16)YUV422_luma(Y11, Bc, Gc, Rc);

      u32 idx00 = row*4+col,         idx01 = row*4+col+1;
      u32 idx10 = (row+1)*4+col,     idx11 = (row+1)*4+col+1;

      ar[idx00*2  ]=0xFF; ar[idx00*2+1]=RGB565_R8(c00);
      ar[idx01*2  ]=0xFF; ar[idx01*2+1]=RGB565_R8(c01);
      ar[idx10*2  ]=0xFF; ar[idx10*2+1]=RGB565_R8(c10);
      ar[idx11*2  ]=0xFF; ar[idx11*2+1]=RGB565_R8(c11);

      gb[idx00*2  ]=RGB565_G8(c00); gb[idx00*2+1]=RGB565_B8(c00);
      gb[idx01*2  ]=RGB565_G8(c01); gb[idx01*2+1]=RGB565_B8(c01);
      gb[idx10*2  ]=RGB565_G8(c10); gb[idx10*2+1]=RGB565_B8(c10);
      gb[idx11*2  ]=RGB565_G8(c11); gb[idx11*2+1]=RGB565_B8(c11);
    }
  }
}

#undef RGB565_R8
#undef RGB565_G8
#undef RGB565_B8

// -----------------------------------------------------------------
// RGB565 encoder (fmv_format == 2)
// -----------------------------------------------------------------
// GX RGB565 tile layout: 4x4 tile = 32 bytes, 16 pixels in row-major order,
// no AR/GB split like RGBA8 and no block compression like CMPR. YUV422()
// already returns a packed RGB565 value, so each pixel is a direct store —
// this is the cheapest of the three paths per pixel converted.

// YUV422 planar -> GX_TF_RGB565
static void YUV422_to_RGB565_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h  = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  u32 tiles_x = w / 4, tiles_y = h / 4;
  u16 *dst16  = (u16 *)dst;
  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u16 *tile = dst16 + (ty*tiles_x + tx) * 16;
    for (u32 row = 0; row < 4; row++)
    {
      u32 y = ty*4 + row;
      if (y >= real_h)
      {
        tile[row*4+0]=0; tile[row*4+1]=0; tile[row*4+2]=0; tile[row*4+3]=0;
        continue;
      }
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 x    = tx*4 + col;
        u32 word = *(u32*)&src_vram[(y*src_stride + x) * 2];
        s32 Yu = (word >>  0) & 255;  // U
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V
        s32 Y1 = (word >> 24) & 255;  // Y1
        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        tile[row*4+col  ] = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
        tile[row*4+col+1] = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
      }
    }
  }
}

// YUV422 twiddled -> GX_TF_RGB565
// See YUV422_to_CMPR_Twiddled for the twiddled YUV layout and twiddle-table precompute.
static void YUV422_to_RGB565_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 tiles_x = w / 4, tiles_y = h / 4;
  u16 *dst16  = (u16 *)dst;

  u32 table_x[1024], table_y[1024];
  const u32 *traw_x, *traw_y;
  get_twiddle_axis_tables(w, h, &traw_x, &traw_y);
  for (u32 x = 0; x < w; x += 2) table_x[x] = traw_x[x] / 4;
  for (u32 y = 0; y < h; y += 2) table_y[y] = traw_y[y] / 4;

  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u16 *tile = dst16 + (ty*tiles_x + tx) * 16;
    for (u32 row = 0; row < 4; row += 2)
    for (u32 col = 0; col < 4; col += 2)
    {
      u32 x  = tx*4 + col;
      u32 y  = ty*4 + row;
      u32 tw = table_y[y] | table_x[x];
      u8 *p  = &src_vram[tw * 8];
      u32 w0 = *(u32*)&p[0];
      u32 w1 = *(u32*)&p[4];
      u16 buf0 = (u16)(w0 >> 16);
      u16 buf1 = (u16)(w0      );
      u16 buf2 = (u16)(w1 >> 16);
      u16 buf3 = (u16)(w1      );

      s32 Y00 = buf0 & 255; s32 Yv = (buf0 >> 8) & 255;
      s32 Y10 = buf1 & 255;
      s32 Y01 = buf2 & 255; s32 Yu = (buf2 >> 8) & 255;
      s32 Y11 = buf3 & 255;

      s32 Bc, Gc, Rc;
      YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
      tile[(row  )*4+col  ] = (u16)YUV422_luma(Y00, Bc, Gc, Rc);
      tile[(row  )*4+col+1] = (u16)YUV422_luma(Y01, Bc, Gc, Rc);
      tile[(row+1)*4+col  ] = (u16)YUV422_luma(Y10, Bc, Gc, Rc);
      tile[(row+1)*4+col+1] = (u16)YUV422_luma(Y11, Bc, Gc, Rc);
    }
  }
}

// =================================
// Dreamcast Texture => Wii textures
// =================================
// Processes the Dreamcast's TCW (Texture Control Word) to initialize Wii TexObjects.
// 4 steps

static void SetTextureParams(PolyParam *mod, bool decal_alpha_fix)
{
  // decal_alpha_fix off: keep the original unconditional GX_MODULATE here so this
  // path costs exactly what it did before the fix existed. On: the caller sets
  // TEVSTAGE0's op itself based on TSP.ShadInstr right after this returns.
  if (!decal_alpha_fix)
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

  u32 tex_addr = (mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
  const u32 orig_tex_addr = tex_addr;
  u32 *ptex_skmp = (u32 *)&params.vram[tex_addr];

  // Declare all variables needed by section 1 upfront.
  bool is_vq = mod->tcw.NO_PAL.VQ_Comp;
  u32 FMT    = GX_TF_RGB565;
  u32 texVQ  = 0;
  u8 *vq_codebook;
  u32 w = 8 << mod->tsp.TexU;
  u32 h = 8 << mod->tsp.TexV;

  //// 1. Memory Management ////

  // Worst-case allocation:
  //   RGBA8   (YUV fmv_format=1) : w*h*4 bytes (32bpp)
  //   RGB565  (YUV fmv_format=2) : w*h*2 bytes (16bpp)
  //   CMPR    (YUV fmv_format=0) : w*h/2 bytes (4bpp DXT1)
  //   All other formats           : w*h*2 bytes (16bpp)
  // We check the pixel format (tcw.PixelFmt==3 => YUV422) here so the bump
  // allocator always gets enough room before any conversion runs.
  u32 decode_bytes;
  {
    bool is_yuv422 = (mod->tcw.NO_PAL.PixelFmt == 3);
    if (is_yuv422 && FMV_FORMAT_RGBA8())
      decode_bytes = (u32)w * h * 4; // RGBA8: 4 bytes/pixel
    else
      decode_bytes = (u32)w * h * 2; // 16bpp worst-case (covers CMPR and RGB565)
  }
  u32 *pixel_buf   = nullptr;
  TextureCacheDesc *pbuff = nullptr;
  bool already_decoded_this_frame = false;

  if (CACHE_VERY_FAST())
  {
    // Direct-slot cache: slot derived from tex_addr. O(1), no scan.
    pbuff     = skimp_slot(tex_addr);
    pixel_buf = (u32*)&pbuff[1];
  }
  else if (CACHE_FAST())
  {
    // Persistent bump arena + hash map: O(1) lookup like skimp_slot, but the
    // allocation is sized to what this texture actually decodes to, so it
    // can never overrun into a neighboring texture's data.
    bool found;
    u32  fidx   = fast_map_locate(tex_addr, &found);
    u32  desc_sz = (sizeof(TextureCacheDesc) + 31) & ~31u;
    if (found)
    {
      u32 off   = s_fastmap[fidx].bump_offset;
      pbuff     = (TextureCacheDesc*)&vram_buffer[off];
      pixel_buf = (u32*)&vram_buffer[off + desc_sz];
    }
    else
    {
      u32 alloc_off;
      pbuff = fast_bump_alloc(decode_bytes, &pixel_buf, &alloc_off);
      if (fidx != FASTMAP_SIZE) // table had a free slot for it
      {
        s_fastmap[fidx].key         = tex_addr;
        s_fastmap[fidx].bump_offset = alloc_off;
      }
      // pbuff->addr stays 0 from fast_bump_alloc's memset, so the validity
      // check below naturally treats this as a cache miss and decodes it.
    }
  }
  else
  {
    // NORMAL: O(1) hash map dedup (replaces old O(n) bump_find linear scan).
    if (CACHE_NORMAL())
    {
      pbuff = hash_map_find(orig_tex_addr, &pixel_buf);
      if (pbuff) already_decoded_this_frame = true;
    }
    // QUALITY: still uses the old linear scan so it can be tuned separately.
    else if (CACHE_QUALITY())
    {
      // NOTE: bump_find removed. QUALITY reuses hash_map_find for now;
      // split into its own path later if needed.
      pbuff = hash_map_find(orig_tex_addr, &pixel_buf);
      if (pbuff) already_decoded_this_frame = true;
    }
    // EXTRA: pure bump allocation, always re-decode (no dedup).
    if (!pbuff)
    {
      u32 alloc_start = s_bump_offset; // capture offset BEFORE bump_alloc advances it
      pbuff = bump_alloc(decode_bytes, &pixel_buf);
      // Register the new slot in the hash map so the next draw call
      // referencing the same tex_addr hits the O(1) fast path.
      if (CACHE_NORMAL() || CACHE_QUALITY())
        hash_map_insert(orig_tex_addr, alloc_start);
    }
  }
  u32 *dst_base = pixel_buf;

  // ── Palette TLUT setup (fmt 5 = CI4, fmt 6 = CI8) ──────────────────────────

  static u16 ATTRIBUTE_ALIGN(32) s_tlut_buf[256];

  u32 pixel_fmt = mod->tcw.NO_PAL.PixelFmt;
  // Only the CI4/CI8 presets sample through a GX TLUT. Every other 4/8BPP
  // preset (I4/I8 stubs, OPTIMIZED, RGB565 full-decode) bakes the palette
  // into the pixel data, so converting + flushing + GX_LoadTlut'ing here on
  // every single draw was pure waste for them.
  bool tlut_needed = (pixel_fmt == 5) ? (TEXTURE_4BPP_CI4_FAST() || TEXTURE_4BPP_CI4())
                   : (pixel_fmt == 6) ? (TEXTURE_8BPP_CI8_FAST() || TEXTURE_8BPP_CI8())
                   : false;
  if (tlut_needed)
  {
    u32 pal_fmt = PAL_RAM_CTRL & 3; // 0=ARGB1555 1=RGB565 2=ARGB4444 3=ARGB8888
    u8  gx_tlut_fmt = (pal_fmt == 1) ? GX_TL_RGB565 : GX_TL_RGB5A3; // u8, not a named enum

    u32 n_entries, pal_base;
    if (pixel_fmt == 5) { // 4BPP: 16-entry palette
      // PalSelect[5:0] selects one of 64 possible 16-entry groups.
      // Absolute PALETTE_RAM entry index = PalSelect * 16 = PalSelect << 4.
      // (Old code `& ~15u` zeroed bits[3:0], discarding sub-bank selection.)
      n_entries = 16;
      pal_base  = mod->tcw.PAL.PalSelect << 4;
    } else {              // 8BPP: 256-entry palette
      n_entries = 256;
      pal_base  = (mod->tcw.PAL.PalSelect >> 4) << 8;
    }
    u32 *pal = PALETTE_RAM + pal_base;

    // JOJO_FIX(): GX_TLUT0 is the only TLUT slot in use, so re-uploading is
    // only ever needed when the bytes actually bound for it differ from
    // whatever is already sitting in that slot. Cheap source-side checksum
    // lets us skip the conversion loop + DCFlushRange + GX_LoadTlut entirely
    // on a repeat — this is what eliminates the bulk of GX_LoadTlut calls
    // when a scene hammers many draws that reuse the same palette bank
    // back-to-back (e.g. text/sprite glyphs sharing one font palette).
    // Off by default: every other game keeps the old behavior of reloading
    // the TLUT unconditionally on every textured-and-palettized draw.
    bool need_reupload = true;
    static u32 s_last_tlut_checksum = 0xFFFFFFFFu;
    if (JOJO_FIX())
    {
      u32 checksum = pal_fmt ^ (n_entries << 16) ^ pal_base;
      for (u32 i = 0; i < n_entries; i++)
        checksum = checksum * 31u + pal[i];
      need_reupload = (checksum != s_last_tlut_checksum);
      if (need_reupload) s_last_tlut_checksum = checksum;
    }

    if (need_reupload)
    {
      for (u32 i = 0; i < n_entries; i++)
      {
        u32 pe = pal[i];
        u16 px;
        switch (pal_fmt)
        {
          case 1:  px = (u16)(pe & 0xFFFF); break;                   // RGB565  -> RGB565
          case 2:  px = ABGR4444((u16)(pe & 0xFFFF)); break;         // ARGB4444-> RGB5A3
          case 3:  {                                                  // ARGB8888-> RGB5A3
            u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
            px = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            break;
          }
          default: px = ABGR1555((u16)(pe & 0xFFFF)); break;         // ARGB1555-> RGB5A3
        }
        s_tlut_buf[i] = px;
      }

      DCFlushRange(s_tlut_buf, n_entries * sizeof(u16));
      GXTlutObj tlut_obj;
      GX_InitTlutObj(&tlut_obj, s_tlut_buf, gx_tlut_fmt, (u16)n_entries);
      GX_LoadTlut(&tlut_obj, GX_TLUT0);
    }
  }

  // === End of Palette TLUT setup ===
  

  //// 2. Cache Check ////

  if (already_decoded_this_frame)
  {
    GX_LoadTexObj(&pbuff->tex, GX_TEXMAP0);
    return;
  }

  bool cache_valid = false;

  if (CACHE_VERY_FAST())
  {
    cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr)
                && !(mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder);
  }
  else if (CACHE_FAST())
  {
    // VQ: the codebook lives at tex_addr itself, so the VERY_FAST trick of
    // stomping tex_addr with the sentinel would corrupt codebook entry 0,
    // and watching only the codebook misses index-only updates (animated
    // VQ textures reusing the same palette). Use the same non-destructive
    // codebook+index fingerprint as CACHE_NORMAL instead — a couple of
    // extra word reads, still O(1), no VRAM writes for VQ.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      // Same sentinel trick as VERY_FAST, plus a shape key (format, scan
      // order, stride, mip, size bits) so a different texture reusing the
      // same address/stride combo can't be served stale data — this is
      // the gap VERY_FAST plugs by disabling its cache entirely for
      // stride+scan-order textures. Validating the shape instead lets
      // CACHE_FAST keep caching those textures safely.
      u32 shape_key = tex_shape_key(mod, pixel_fmt);
      cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr)
                  && (pbuff->shape_key == shape_key);
    }
  }
  else if (CACHE_NORMAL())
  {
    // NORMAL: corrected sentinel at orig_tex_addr.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == orig_tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      cache_valid = (*ptex == 0xDEADBEEF)
                   && (pbuff->addr == orig_tex_addr)
                   && !(mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder);
    }
  }
  else if (CACHE_QUALITY())
  {
    // QUALITY: duplicate of NORMAL for now.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == orig_tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      cache_valid = (*ptex == 0xDEADBEEF)
                   && (pbuff->addr == orig_tex_addr)
                   && !(mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder);
    }
  }
  // CACHE_EXTRA: cache_valid stays false, always re-decode.

  if (!cache_valid)
  {
    u32 *dst = dst_base;
    VramWork  = (u8*)dst;
    pbuff->has_pal = false;
    pbuff->addr    = (CACHE_VERY_FAST() || CACHE_FAST()) ? tex_addr : orig_tex_addr;

    if (is_vq && !CACHE_VERY_FAST())
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      pbuff->vq_codebook_w0 = cb_w0 ^ idx_w0;
    }
    else if (CACHE_FAST())
    {
      // Non-VQ: remember the format/scan/stride/mip/size bits alongside the
      // sentinel so a future draw at the same address with a different
      // shape (e.g. a stride-selected texture) can't be served this slot's
      // stale data — see the CACHE_FAST validity check above.
      pbuff->shape_key = tex_shape_key(mod, pixel_fmt);
    }

    if(DEBUG_MESSAGE()) {
      printf("[TEX] addr=%06X fmt=%d vq=%d scan=%d mip=%d w=%d h=%d\n",
        orig_tex_addr, (int)mod->tcw.NO_PAL.PixelFmt, (int)mod->tcw.NO_PAL.VQ_Comp,
        (int)mod->tcw.NO_PAL.ScanOrder, (int)mod->tcw.NO_PAL.MipMapped, w, h);
    }

    //// 3. Format Conversion ////

    switch (mod->tcw.NO_PAL.PixelFmt)
    {
    case 0:
    case 7:
      // 0	1555 value: 1 bit; RGB values: 5 bits each
      // 7	Reserved	Regarded as 1555
     if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        norm_text(1555);
      }
      else
      {
        // verify(tsp.TexU==tsp.TexV);
        twidle_tex(1555);
      }
      FMT = GX_TF_RGB5A3;
      break;

      // redo_argb:

    case 1:
      // 565 Format  R value: 5 bits; G value: 6 bits; B value: 5 bits
      if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        norm_text(565);
        //(&pbt,(u16*)&params.vram[sa],w,h);
      }
      else
      {
        // verify(tsp.TexU==tsp.TexV);
        twidle_tex(565);
      }
      FMT = GX_TF_RGB565;
      break;

    case 2:
      // 4444 Format: 4 bits; RGB values: 4 bits each
      if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        // argb4444to8888(&pbt,(u16*)&params.vram[sa],w,h);
        norm_text(4444);
      }
      else
      {
        twidle_tex(4444);
      }
      FMT = GX_TF_RGB5A3;
      break;
      
    case 3:
    {
      // YUV422: 32 bits per 2 pixels (UYVY — U, Y0, V, Y1, 8 bits each).
      if (mod->tcw.NO_PAL.MipMapped)
      {
        tex_addr += OtherMipPoint[mod->tsp.TexU + 3] * 2; // YUV422: 2 bytes/pixel
        h = w; // mipmapped textures are always square (ignore TexV), as elsewhere
      }
      if (mod->tcw.NO_PAL.StrideSel)
        w = 512;

      // The declared texture width (w) may not match the actual video width
      // which is stored in the YUV converter's TA_YUV_TEX_CTRL register (bits[5:0] = (real_width/16 - 1)).
      // Using this register fixes texture issues without hardcoding values.
      u32 yuv_real_w = ((TA_YUV_TEX_CTRL & 0x3F) + 1) * 16;
      if (yuv_real_w == 0 || yuv_real_w > w)
        yuv_real_w = w; // sane fallback: register unset/irrelevant for this texture
      g_yuv_src_stride_override = (int)yuv_real_w;

      // Same idea, vertical axis: TA_YUV_TEX_CTRL bits[13:8] = (real_height/16 - 1).
      u32 yuv_real_h = (((TA_YUV_TEX_CTRL >> 8) & 0x3F) + 1) * 16;
      if (yuv_real_h == 0 || yuv_real_h > h)
        yuv_real_h = h;
      g_yuv_src_real_h = (int)yuv_real_h;

      u8 *yuv_src = &params.vram[tex_addr];

      // FMV Debug
      if (fmv_debug)
      {
        printf("[YUV] ---- new YUV422 texture ----\n");
        printf("[YUV] tex_addr=%06X w=%u h=%u scan=%u mip=%u stride=%u fmv_format=%d "
               "TA_YUV_TEX_CTRL=%08X real_w=%u real_h=%u\n",
               tex_addr, w, h, (unsigned)mod->tcw.NO_PAL.ScanOrder,
               (unsigned)mod->tcw.NO_PAL.MipMapped,
               (unsigned)mod->tcw.NO_PAL.StrideSel, get_fmv_format_preset(),
               (u32)TA_YUV_TEX_CTRL, yuv_real_w, yuv_real_h);

        // Dump raw bytes at the start of the source (first 16 bytes = 4 u32 words).
        u32 *raw32 = (u32*)yuv_src;
        printf("[YUV] raw words: %08X %08X %08X %08X\n",
               raw32[0], raw32[1], raw32[2], raw32[3]);

        // Decode the first 2 pixels exactly the way the planar path does,
        // so we can see if U/V/Y extraction and the YUV422() formula look sane.
        {
          u32 w0 = raw32[0];
          s32 Yu = (w0 >>  0) & 255;  // U
          s32 Y0 = (w0 >>  8) & 255;  // Y0
          s32 Yv = (w0 >> 16) & 255;  // V
          s32 Y1 = (w0 >> 24) & 255;  // Y1
          u16 c0 = (u16)YUV422(Y0, Yu, Yv);
          u16 c1 = (u16)YUV422(Y1, Yu, Yv);
          printf("[YUV] planar-style decode word0: Y0=%d Y1=%d U=%d V=%d\n", Y0, Y1, Yu, Yv);
          printf("[YUV]   -> RGB565 c0=%04X (R=%d G=%d B=%d)  c1=%04X (R=%d G=%d B=%d)\n",
                 c0, (c0>>11)&0x1F, (c0>>5)&0x3F, c0&0x1F,
                 c1, (c1>>11)&0x1F, (c1>>5)&0x3F, c1&0x1F);
        }

        // Decode the first 2x2 block exactly the way the twiddled path does,
        // so we can compare against the planar interpretation above.
        {
          u32 tw = twop(0, 0, w, h) / 4;
          u8 *p  = &yuv_src[tw * 8];
          u32 tw0 = *(u32*)&p[0];
          u32 tw1 = *(u32*)&p[4];
          u16 buf0 = (u16)(tw0 >> 16);
          u16 buf1 = (u16)(tw0      );
          u16 buf2 = (u16)(tw1 >> 16);
          u16 buf3 = (u16)(tw1      );
          s32 Y00 = buf0 & 255; s32 Yv = (buf0 >> 8) & 255;
          s32 Y10 = buf1 & 255;
          s32 Y01 = buf2 & 255; s32 Yu = (buf2 >> 8) & 255;
          s32 Y11 = buf3 & 255;
          printf("[YUV] twiddled-style decode block0: tw_idx=%u raw=%08X %08X\n", tw, tw0, tw1);
          printf("[YUV]   Y00=%d Y10=%d Y01=%d Y11=%d U=%d V=%d\n", Y00, Y10, Y01, Y11, Yu, Yv);
        }

        // ASCII brightness thumbnail of the FULL raw source (every cell is a
        // real sampled pixel, not averaged) — dense enough to actually show
        // structure like dashed lines, instead of the previous 64px-step scan
        // which could easily land entirely between thin dashes every time.
        // This directly answers whether tiling/repetition is already present
        // in the raw VRAM bytes BEFORE any CMPR/RGBA8 encode or GX upload.
        {
          u32 *raw32t = (u32*)yuv_src;
          const int COLS = 100, ROWS = 32;
          printf("[YUV] thumbnail %dx%d (luma 0-9, low..high):\n", COLS, ROWS);
          for (int r = 0; r < ROWS; r++)
          {
            char line[COLS + 1];
            u32 sy = (u32)((u64)r * h / ROWS);
            for (int c = 0; c < COLS; c++)
            {
              u32 sx = (u32)((u64)c * w / COLS) & ~1u; // even: UYVY pixel pairs
              u8 *p = &((u8*)raw32t)[(sy * w + sx) * 2];
              s32 yy = p[1]; // luma of the first pixel in the pair (byte1 = Y0)
              int level = (yy * 10) / 256; if (level > 9) level = 9; if (level < 0) level = 0;
              line[c] = (char)('0' + level);
            }
            line[COLS] = 0;
            printf("[YUV]  %s\n", line);
          }
        }
      }

      if (FMV_FORMAT_RGBA8())
      {
        // ---- RGBA8 path ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_RGBA8_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_RGBA8_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_RGBA8;
      }
      else if (FMV_FORMAT_RGB565())
      {
        // ---- RGB565 path — cheapest per-pixel cost (direct store, no
        // block encoding, no 8-bit channel expansion) ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_RGB565_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_RGB565_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_RGB565;
      }
      else
      {
        // ---- CMPR (DXT1) path — default (fmv_format == 0) ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_CMPR_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_CMPR_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_CMPR;
      }

      if (DEBUG_MESSAGE())
      {
        // Dump the first few bytes of the ENCODED output too, so we can see
        // if the CMPR/RGBA8/RGB565 packing itself produced something sane.
        u8 *out = (u8*)VramWork;
        if (FMT == GX_TF_CMPR)
        {
          printf("[YUV] CMPR block0 bytes: %02X%02X %02X%02X  sel=%02X%02X%02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        }
        else if (FMT == GX_TF_RGBA8)
        {
          printf("[YUV] RGBA8 tile0 AR bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
          printf("[YUV] RGBA8 tile0 GB bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[32], out[33], out[34], out[35], out[36], out[37], out[38], out[39]);
        }
        else // GX_TF_RGB565
        {
          printf("[YUV] RGB565 tile0 bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        }
      }
      break;
    }
      // 4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
    case 5: 
    {
      if(TEXTURE_4BPP_I4_STUB()){ // I4 Stub (initial code)
        // 5	4 BPP Palette	Palette texture with 4 bits/pixel
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 16);

        FMT = GX_TF_I4; // wha? the ?
        }
      else if(TEXTURE_4BPP_OPTIMIZED()){ // BEST: LUT-baked palette decode -> GX RGB565/RGB5A3
        // Same accuracy as a full palette decode (correct ARGB8888/4444/1555
        // palettes, correct PalSelect bank — fixes counters/HUDs that CI4+TLUT
        // can't render right), but the palette is converted to GX format ONCE
        // per texture into a 16-entry u16 LUT. The inner loop is then a pure
        // table fetch: no per-pixel format switch, no conversion macros.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally, no branch needed.
        //
        // Other speedups vs the previous OPTIMIZED code:
        //   - Twiddle tables used via direct cached pointers (the old code
        //     memcpy'd 2x 4 KB of tables on EVERY texture decode).
        //   - Morton bit0 is Y's LSB (twop() interleaves y first), so rows
        //     y and y+1 share each source byte:
        //       nib(x, y even) = ty | tx    -> LOW  nibble
        //       nib(x, y+1)    = nib + 1    -> HIGH nibble, SAME byte
        //     Decoding two rows per pass halves the reads + table lookups.
        //   - GX tile addressing: the per-row tile/sub-tile math (y>>2, y&3,
        //     w/4) is hoisted out of the x loop.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        u32  pal_fmt  = PAL_RAM_CTRL & 3;
        u32  pal_base = mod->tcw.PAL.PalSelect << 4; // PalSelect selects a 16-entry bank
        u32 *pal      = PALETTE_RAM + pal_base;

        FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

        // Bake the palette: 16 conversions total instead of w*h.
        u16 gx_pal[16];
        switch (pal_fmt)
        {
          case 1: // RGB565 -> RGB565 (direct)
            for (u32 i = 0; i < 16; i++) gx_pal[i] = (u16)(pal[i] & 0xFFFF);
            break;
          case 2: // ARGB4444 -> RGB5A3
            for (u32 i = 0; i < 16; i++) gx_pal[i] = ABGR4444((u16)(pal[i] & 0xFFFF));
            break;
          case 3: // ARGB8888 -> RGB5A3
            for (u32 i = 0; i < 16; i++)
            {
              u32 pe = pal[i];
              u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
              gx_pal[i] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            }
            break;
          default: // ARGB1555 -> RGB5A3
            for (u32 i = 0; i < 16; i++) gx_pal[i] = ABGR1555((u16)(pal[i] & 0xFFFF));
            break;
        }

        u8  *src     = (u8 *)&params.vram[tex_addr];
        u16 *dst16   = (u16 *)VramWork;
        u32  tiles_x = w / 4; // GX 16bpp tile = 4x4 pixels

        // Twiddled decode (unconditional — see note above).
        const u32 *table_x, *table_y;
        get_twiddle_axis_tables(w, h, &table_x, &table_y);

        for (u32 y = 0; y < h; y += 2) // h is pow2 >= 8, always even
        {
          u32 row_base0 = ((y    ) >> 2) * tiles_x * 16 + ((y    ) & 3) * 4;
          u32 row_base1 = ((y + 1) >> 2) * tiles_x * 16 + ((y + 1) & 3) * 4;
          u32 ty        = table_y[y]; // even y -> Morton bit0 clear
          for (u32 x = 0; x < w; x++)
          {
            u32 nib = ty | table_x[x];     // nibble index for (x, y)
            u8  raw = src[(nib >> 1) ^ 3]; // ^3: 32-bit BE VRAM correction
            u32 col = (x >> 2) * 16 + (x & 3);
            dst16[row_base0 + col] = gx_pal[raw & 0xF]; // row y   (low nibble)
            dst16[row_base1 + col] = gx_pal[raw >> 4];  // row y+1 (high nibble)
          }
        }

        pbuff->has_pal = false; // palette baked in, GPU doesn't need a TLUT
      }
      else if (TEXTURE_4BPP_CI4_FAST()) { // 4BPP palette -> GX_TF_CI4 (Fast)
        // TLUT already loaded above — only the index nibbles are reordered here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally.
        //
        // Paired-nibble loop: 2 pixels per iteration, both nibbles of each
        // GX CI4 output byte filled in a single store (no ci4_prel calls).
        // Per-axis Morton tables come from the shared cache (O(w+h) once per
        // size, zero per-decode rebuild/copy).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x += 2)
            {
              u32 tw0  = ty | table_x[x];                         // nibble index for x
              u32 tw1  = ty | table_x[x + 1];                     // nibble index for x+1
              u8  raw0 = src[(tw0 >> 1) ^ 3];                     // ^3: 32-bit BE correction
              u8  raw1 = src[(tw1 >> 1) ^ 3];
              u8  idx0 = (tw0 & 1) ? (raw0 >> 4) : (raw0 & 0xF); // palette index for x
              u8  idx1 = (tw1 & 1) ? (raw1 >> 4) : (raw1 & 0xF); // palette index for x+1

              // Pack both indices into one GX CI4 tile byte
              // (even -> high nibble, odd -> low nibble).
              u32 tile     = (y / 8) * (w / 8) + (x / 8);
              u32 nibble   = tile * 64 + (y % 8) * 8 + (x % 8); // points at even pixel
              idst[nibble >> 1] = (u8)((idx0 << 4) | (idx1 & 0x0F));
            }
          }
        }

        FMT = GX_TF_CI4;
        pbuff->has_pal = true;
      }

      else if (TEXTURE_4BPP_CI4()) { // 4BPP palette -> GX_TF_CI4 (Accurate)
        // Untwiddle index nibbles into GX CI4 block layout via ci4_prel.
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally.
        //
        // DC nibble convention: even pixel -> LOW nibble, odd pixel -> HIGH nibble.
        // VRAM byte addressing: DC stores data in 32-bit little-endian words,
        // but the Wii's big-endian SH4 emulator stores them with full 32-bit
        // reversal (DC byte B lives at vram[B ^ 3]).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;
          memset(idst, 0, w * h / 2); // clear required: nibbles are OR'd in

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u32 tw_nibble = ty | table_x[x];                          // nibble index
              u8  raw       = src[(tw_nibble >> 1) ^ 3];                // ^3: BE correction
              u8  idx       = (tw_nibble & 1) ? (raw >> 4) : (raw & 0xF);
              ci4_prel(idst, x, y, w, idx);
            }
          }
        }

        FMT = GX_TF_CI4;
        pbuff->has_pal = true;
      }
      else if (TEXTURE_4BPP_RGB565()) {
          verify(mod->tcw.PAL.VQ_Comp == 0);
          if (mod->tcw.NO_PAL.MipMapped)
              tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

          u32  pal_fmt  = PAL_RAM_CTRL & 3;
          u32  pal_base = mod->tcw.PAL.PalSelect & ~15u;
          u32 *pal      = PALETTE_RAM + pal_base;

          FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

          u8  *src = (u8 *)&params.vram[tex_addr];
          u16 *dst = (u16 *)VramWork;

          // Precompute per-axis Morton contributions once (O(w+h)) instead of
          // calling twop(x,y,w,h) per pixel (O(w*h) — same fix applied to the
          // other 4BPP paths above; this loop was still doing it per pixel).
          u32 table_x[1024];
          u32 table_y[1024];
          const u32 *traw_x, *traw_y;
          get_twiddle_axis_tables(w, h, &traw_x, &traw_y);
          for (u32 x = 0; x < w; x++) table_x[x] = traw_x[x];
          for (u32 y = 0; y < h; y++) table_y[y] = traw_y[y];

          // We pull the switch OUTSIDE the loops.
          // The compiler can now optimize each loop specifically for the format.
          switch (pal_fmt) {
              case 1: // RGB565
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = (u16)(pal[idx] & 0xFFFF);
                      }
                  }
                  break;

              case 2: // ARGB4444
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = ABGR4444((u16)(pal[idx] & 0xFFFF));
                      }
                  }
                  break;

              case 3: // ARGB8888 -> RGB5A3
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          u32 pe  = pal[idx];
                          u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
                          dst[GX_TexOffs(x, y, w)] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
                      }
                  }
                  break;

              default: // ARGB1555
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = ABGR1555((u16)(pal[idx] & 0xFFFF));
                      }
                  }
                  break;
          }

          pbuff->has_pal = false; // We baked the palette in, so the GPU doesn't need it.
      }
      break;
    }
    case 6: // 8BPP Palette: 8 bits per pixel, 256-entry palette.
    {
      if(TEXTURE_8BPP_I8_STUB()) {// I8 Stub (initial code)
        // 8 BPP Palette	Palette texture with 8 bits/pixel
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 256);

        FMT = GX_TF_I8; // wha? the ? FUCK!

      }
      else if(TEXTURE_8BPP_OPTIMIZED()) {
        // BEST: LUT-baked palette decode -> GX RGB565/RGB5A3.
        // Same output as TEXTURE_8BPP_RGB565 (the path that renders the
        // Crazy Taxi / Bomberman counters correctly) but faster:
        //   - Palette converted ONCE into a 256-entry u16 LUT: the inner loop
        //     is gx_pal[idx], no per-pixel format switch / ARGB8888 unpack.
        //   - Twiddle tables via direct cached pointers (old code memcpy'd
        //     8 KB of tables per decode), GX tile row addressing hoisted.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        u32  pal_fmt  = PAL_RAM_CTRL & 3;
        u32  pal_base = (mod->tcw.PAL.PalSelect >> 4) << 8; // bits[5:4] pick a 256-entry bank
        u32 *pal      = PALETTE_RAM + pal_base;

        FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

        // Bake the palette: 256 conversions total instead of w*h.
        static u16 gx_pal[256]; // static: keep it off the stack
        switch (pal_fmt)
        {
          case 1: // RGB565 -> RGB565 (direct)
            for (u32 i = 0; i < 256; i++) gx_pal[i] = (u16)(pal[i] & 0xFFFF);
            break;
          case 2: // ARGB4444 -> RGB5A3
            for (u32 i = 0; i < 256; i++) gx_pal[i] = ABGR4444((u16)(pal[i] & 0xFFFF));
            break;
          case 3: // ARGB8888 -> RGB5A3
            for (u32 i = 0; i < 256; i++)
            {
              u32 pe = pal[i];
              u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
              gx_pal[i] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            }
            break;
          default: // ARGB1555 -> RGB5A3
            for (u32 i = 0; i < 256; i++) gx_pal[i] = ABGR1555((u16)(pal[i] & 0xFFFF));
            break;
        }

        u8  *src     = (u8 *)&params.vram[tex_addr];
        u16 *dst16   = (u16 *)VramWork;
        u32  tiles_x = w / 4; // GX 16bpp tile = 4x4 pixels

        // Twiddled decode (unconditional — see note above).
        // 1 byte = 1 pixel in Morton order.
        const u32 *table_x, *table_y;
        get_twiddle_axis_tables(w, h, &table_x, &table_y);

        for (u32 y = 0; y < h; y++)
        {
          u32 row_base = (y >> 2) * tiles_x * 16 + (y & 3) * 4;
          u32 ty       = table_y[y];
          for (u32 x = 0; x < w; x++)
          {
            u8 idx = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE VRAM correction
            dst16[row_base + (x >> 2) * 16 + (x & 3)] = gx_pal[idx];
          }
        }

        pbuff->has_pal = false; // palette baked in, GPU doesn't need a TLUT
      }
      else if(TEXTURE_8BPP_CI8_FAST()) {
        // 8BPP palette -> GX_TF_CI8  [FAST path]
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        //
        // Inlined tile write (no ci8_prel call); y_tile_base hoisted out of
        // the x loop; per-axis Morton tables from the shared cache.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          u32 tiles_x = w / 8;
          for (u32 y = 0; y < h; y++)
          {
            u32 y_tile_base = (y / 4) * tiles_x * 32 + (y % 4) * 8;
            u32 ty          = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u8  idx      = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE correction
              u32 byte_off = y_tile_base + (x / 8) * 32 + (x % 8);
              idst[byte_off] = idx;
            }
          }
        }

        FMT = GX_TF_CI8;
        pbuff->has_pal = true;
      }
      else if(TEXTURE_8BPP_CI8()) {
        // 8BPP palette -> GX_TF_CI8  [ACCURATE path]
        // Untwiddle index bytes into GX CI8 block layout via ci8_prel.
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        //
        // ^3 = full 32-bit BE word reversal (DC byte B lives at vram[B ^ 3]).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u8 idx = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE correction
              ci8_prel(idst, x, y, w, idx);
            }
          }
        }

        FMT = GX_TF_CI8;
        pbuff->has_pal = true;
      }
      else if (TEXTURE_8BPP_RGB565()){
        // Fully decode each indexed pixel into a GX 16bpp pixel.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u32  pal_fmt  = PAL_RAM_CTRL & 3;
          // PalSelect[5:4] selects the 256-entry bank (each bank = 256 u32 entries)
          u32  pal_base = (mod->tcw.PAL.PalSelect >> 4) << 8;
          u32 *pal      = PALETTE_RAM + pal_base;

          FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

          u8  *src = (u8 *)&params.vram[tex_addr];
          u16 *dst = (u16 *)VramWork;

          // 8BPP twiddled: 1 byte = 1 pixel in Morton order. Precompute
          // per-axis Morton contributions once (O(w+h)) instead of calling
          // twop(x,y,w,h) per pixel (O(w*h) — same fix as the 4BPP paths above).
          u32 table_x[1024];
          u32 table_y[1024];
          const u32 *traw_x, *traw_y;
          get_twiddle_axis_tables(w, h, &traw_x, &traw_y);
          for (u32 x = 0; x < w; x++) table_x[x] = traw_x[x];
          for (u32 y = 0; y < h; y++) table_y[y] = traw_y[y];

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u32 tw  = ty | table_x[x];
              u8  idx = src[tw ^ 3];  // ^3: full 32-bit BE word correction (matches CI8/I8 paths)

              u32 pe = pal[idx];
              u16 px;
              switch (pal_fmt)
              {
                case 1:  px = (u16)(pe & 0xFFFF); break;                    // RGB565  → RGB565
                case 2:  px = ABGR4444((u16)(pe & 0xFFFF)); break;          // ARGB4444→ RGB5A3
                case 3:                                                      // ARGB8888→ RGB5A3
                {
                  u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
                  px = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
                  break;
                }
                default: px = ABGR1555((u16)(pe & 0xFFFF)); break;          // ARGB1555→ RGB5A3
              }
              dst[GX_TexOffs(x, y, w)] = px;
            }
          }
        }
      }
      break;
    }
    default:
      printf("Unhandled texture\n");
      // memset(temp_tex_buffer,0xFFEFCFAF,w*h*4);
    }

    if (texVQ)
    {
      // texture_VQ now fully decompresses into standard 16bpp RGB pixels —
      // same format as texture_TW. No CI8/TLUT. w,h stay full size.
      pbuff->has_pal = false;
    }

    //// 4. Hardware Handover ////

    void *gx_pixels = (void*)dst;

    // Flush pixel data from CPU cache to RAM for GX.
    {
      u32 flush_sz;
      if      (FMT == GX_TF_CI4 || FMT == GX_TF_I4) flush_sz = w * h / 2;
      else if (FMT == GX_TF_CI8 || FMT == GX_TF_I8) flush_sz = w * h;
      else if (FMT == GX_TF_RGBA8)                   flush_sz = w * h * 4; // 32bpp
      else if (FMT == GX_TF_CMPR)                    flush_sz = w * h / 2; // DXT1 4bpp
      else                                            flush_sz = w * h * 2; // 16bpp default
      DCFlushRange(gx_pixels, (flush_sz + 31) & ~31u);
    }

    if (pbuff->has_pal)
    {
      GX_InitTexObjCI(&pbuff->tex, gx_pixels, (u16)w, (u16)h, (u8)FMT,
                      (u8)TexUV(mod->tsp.FlipU, mod->tsp.ClampU),
                      (u8)TexUV(mod->tsp.FlipV, mod->tsp.ClampV),
                      GX_FALSE, GX_TLUT0);
    }
    else
    {
      // GX_TRUE for mip maps would require ALL mip levels (base down to 1x1)
      // packed sequentially in GX block format in gx_pixels. We only decoded
      // the base level (skipping smaller mips via MipPoint offset), so passing
      // GX_TRUE causes GX to read garbage data → corrupted / checkerboard texture.
      // Always pass GX_FALSE: the DC mip offset already selected the correct
      // (largest) level, and GX filtering handles the rest without mip data.
      GX_InitTexObj(&pbuff->tex, gx_pixels, w, h, FMT,
                    TexUV(mod->tsp.FlipU, mod->tsp.ClampU),
                    TexUV(mod->tsp.FlipV, mod->tsp.ClampV), GX_FALSE);
    }

    GX_InitTexObjLOD(&pbuff->tex, min_filt, mag_filt,
                  0.0f, 10.0f, lod_bias,
                  bias_clamp, edge_lod, aniso);

    // Write sentinel.
    if (CACHE_VERY_FAST())
    {
      *ptex_skmp = 0xDEADBEEF;
    }
    else if (CACHE_FAST())
    {
      // VQ validity is tracked purely via the codebook+index fingerprint
      // (stored above) — no VRAM write, so the codebook/index data the
      // decoder just read stays intact. Only stomp the sentinel for
      // non-VQ formats, where tex_addr == orig_tex_addr (untouched by the
      // VQ mip-offset math) and a real overwrite naturally invalidates it.
      if (!is_vq)
      {
        u32 *ptex_fast = (u32*)&params.vram[tex_addr];
        *ptex_fast = 0xDEADBEEF;
      }
    }
    else if (!is_vq)
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      *ptex = 0xDEADBEEF;
    }

    if(DEBUG_MESSAGE()){
      printf("Texture:%d %d %dx%d %08X --> %08X\n",
        mod->tcw.NO_PAL.PixelFmt, mod->tcw.NO_PAL.ScanOrder,
        8 << mod->tsp.TexU, 8 << mod->tsp.TexV,
        orig_tex_addr, (u32)gx_pixels);
    }
  }

  GX_LoadTexObj(&pbuff->tex, GX_TEXMAP0);
}

static bool s_did_3d_render = false;

// Set by SPG.cpp's CalculateSync() from SPG_CONTROL/SCALER_CTL whenever the
// video timing changes. 240p (non-interlaced NTSC/PAL) modes and SCALER_CTL's
// hscale bit halve the active resolution on real hardware, so the TA-rendered
// scene only spans a fraction of the nominal 640x480 canvas. DoRender() uses
// these to shrink the projected canvas size accordingly.
static float g_fb_scale_x = 1.0f;
static float g_fb_scale_y = 1.0f;

void SetFbScale(float x, float y)
{
  g_fb_scale_x = x;
  g_fb_scale_y = y;
}

// ============================
// Framebuffer-present tracking (devcast-style, magic-word variant)
// ============================
//
// Some titles (notably MIL-CD / CDI selfboot 2D content) write their image
// directly into VRAM and present it via the SPG scanout, without ever issuing
// a PVR RENDER_START.  In that case StartRender() never runs and the screen
// would stay stale.  devcast handles this by presenting the VRAM framebuffer
// at vblank whenever no 3D render happened that frame (render_called / fb_dirty
// gate in Renderer_if.cpp / RenderFramebuffer()).
//
// Here we use an in-VRAM magic word instead of a host-side flag so the signal
// is tied to the actual framebuffer contents:
//
//   * When the PVR produces a frame (DoRender), we stamp FB_MAGIC into VRAM at
//     the framebuffer *write* start address (FB_W_SOF1).  This marks "the image
//     currently in VRAM was produced by the PVR and is already on screen".
//   * At vblank (VBlank) we read the magic from the framebuffer *read* start
//     address (FB_R_SOF1).  If it is still there, the on-screen image is the
//     PVR's render and there is nothing new to show.  If it is gone, the CPU
//     (or any non-PVR writer) has overwritten the framebuffer, so we present
//     the VRAM framebuffer contents.
//
// The magic is written at the very first pixel of the framebuffer; a real frame
// will normally overwrite pixel 0, which is exactly the "needs present" signal.
#define FB_PRESENT_MAGIC 0xDC4F0FB0u   // "DC4 0FB" – framebuffer-presented marker

// Linear 32-bit VRAM accessor (DC byte address -> interleaved 64-bit bank).
// Mirrors vri()/vrf() but lets us write as well as read the magic word.
static INLINE u32 fb_vram_read32(u32 addr)
{
  return *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

static INLINE void fb_vram_write32(u32 addr, u32 val)
{
  *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)] = val;
}

// Stamp the "already presented" marker into the framebuffer.  Called:
//   * after a PVR 3D render, at the write origin (FB_W_SOF1) — the address the
//     PVR just produced into and that the game will scan out from;
//   * after a VRAM framebuffer present, at the read origin (FB_R_SOF1) — the
//     address that was just shown.
// In both cases the marker means "the current framebuffer contents are on
// screen".  Any subsequent CPU/2D write to the framebuffer overwrites pixel 0
// (and thus the marker), which is exactly the "needs present" signal.
static INLINE void fb_stamp_present_magic_at(u32 addr)
{
  fb_vram_write32(addr & 0x00FFFFFF, FB_PRESENT_MAGIC);
}

static INLINE void fb_stamp_present_magic()
{
  fb_stamp_present_magic_at(FB_W_SOF1);
}

// True when the framebuffer read origin no longer holds our marker, i.e. the
// framebuffer was written by something other than the present path and the new
// contents still need to be shown.
static INLINE bool fb_needs_present()
{
  return fb_vram_read32(FB_R_SOF1 & 0x00FFFFFF) != FB_PRESENT_MAGIC;
}

void PresentFramebuffer();

// ============================
// Frame-skip state
// ============================

// Wall-clock time (seconds) when the last DoRender() call started.
// os_GetSeconds() is the platform timer already used by SPG.cpp — no extra
// headers or libogc-specific constants required.
static double s_render_start_time = 0.0;
static double s_last_render_time  = 0.0;   // seconds taken by the previous rendered frame

#define VBLANK_BUDGET_SEC  (1.0 / 60.0) // AUTO Mode (NTSC)


static bool ShouldSkipFrame() // Returns true when the current frame should be dropped
{
    if (NO_FRAMESKIP())
        return false;

    if (FRAMESKIP_1())
    {
        frame_counter = (frame_counter + 1) & 1;
        return frame_counter != 0;
    }
    if (FRAMESKIP_2())
    {
        frame_counter = (frame_counter + 1) % 3;
        return frame_counter != 0;
    }
    if (FRAMESKIP_AUTO())
    {

        return s_last_render_time > VBLANK_BUDGET_SEC;
    }

    return false;
}

// ── TRANS_SORT(): per-strip back-to-front sort of the translucent list ──────
// One record per TR strip, built by walking the lists/vertices/listModes
// parallel streams once (the same walk the draw loop does), then qsort'ed.
// Vertex::z holds W (view depth, larger = farther — see vert_base()), so
// back-to-front means descending far_w. Ties keep TA submission order via the
// vtx pointer (monotonically increasing in submission order) so 2D UI layers
// stacked at the same depth are not reshuffled. Sized like lists[] (worst
// case: every strip is translucent) — 8K * 16B = 128KB of BSS.
struct TransStripRec
{
  float far_w;     // max Vertex::z of the strip (its farthest point)
  Vertex *vtx;     // first vertex of the strip
  PolyParam *mod;  // render state in effect for this strip
  u16 count;       // vertex count (sign bit already consumed)
  u16 _pad;
};

static TransStripRec trans_sort_recs[8 * 1024];

static int trans_strip_cmp(const void *a, const void *b)
{
  const TransStripRec *ra = (const TransStripRec *)a;
  const TransStripRec *rb = (const TransStripRec *)b;
  if (ra->far_w > rb->far_w) return -1; // farther strips draw first
  if (ra->far_w < rb->far_w) return 1;
  // stable tie-break: original submission order
  if (ra->vtx < rb->vtx) return -1;
  if (ra->vtx > rb->vtx) return 1;
  return 0;
}

// Fixed GX scratch texture, always 640x480 RGB565 (4x4 tiled), 32-byte aligned
// as EFB-copy destinations must be. Shared (file scope) between
// PresentFramebuffer() below — which decodes the DC framebuffer into it — and
// the RENDER_TO_TEXTURE() write-back — which uses it as the GX_CopyTex
// destination. Both users finish with the buffer within their own call and
// both run on the SH4 thread, so sharing is safe and saves 600 KB of BSS.
#define FB2D_W 640
#define FB2D_H 480
static u16 fb2d_tex[FB2D_W * FB2D_H] ATTRIBUTE_ALIGN(32);

// ── RENDER_TO_TEXTURE() state ────────────────────────────────────────────────
// Set by StartRender() around the DoRender() call for an RTT frame; DoRender()
// swaps its canvas/viewport to the RTT target size and hands its tail to
// rtt_copy_efb_to_vram() instead of copying to the display.
static bool s_rtt_pass = false;
static u32  s_rtt_w = 0;   // RTT target size, from FB_X_CLIP / FB_Y_CLIP
static u32  s_rtt_h = 0;

// Copy the finished EFB scene back into emulated VRAM at FB_W_SOF1, the way
// the real PVR writes an RTT frame, then leave the EFB cleared for the next
// frame (the display path gets that clear from GX_CopyDisp's clear flag).
//
// Addressing: bit 24 of FB_W_SOF1 marks the 64-bit texture area, which is
// exactly the linear params.vram[] layout the texture decoders read — so the
// pixels are written linearly (no 32->64 offset conversion), and the first
// u32 written at the target address stomps the tex-cache 0xDEADBEEF sentinel,
// which is precisely the "re-decode this texture" signal.
//
// Pixel layout: Plannar() reads each native u32 word as low16 = left pixel,
// high16 = right pixel, so pairs are written as (p1 << 16) | p0.
static void rtt_copy_efb_to_vram()
{
  // Pad the copy rect to the 4x4 tile grid of GX_TF_RGB565 so the tile pitch
  // used below is exact (EFB copies also require even sizes).
  u32 copy_w = (s_rtt_w + 3) & ~3u;
  u32 copy_h = (s_rtt_h + 3) & ~3u;

  // Grab the RTT pixels without clearing yet — the EFB is cleared as a whole
  // below so color AND depth start fresh everywhere, not just in this rect.
  GX_SetTexCopySrc(0, 0, (u16)copy_w, (u16)copy_h);
  GX_SetTexCopyDst((u16)copy_w, (u16)copy_h, GX_TF_RGB565, GX_FALSE);
  GX_CopyTex(fb2d_tex, GX_FALSE);
  GX_DrawDone(); // wait for the copy to land in main memory
  DCInvalidateRange(fb2d_tex, copy_w * copy_h * 2);

  u32 packmode = FB_W_CTRL & 7;
  if (packmode <= 3)
  {
    // fb_kval bit 7 (FB_W_CTRL bit 15) becomes pixel bit 15 for 0555/1555.
    u16 kbit    = (u16)(FB_W_CTRL & 0x8000);
    u32 stride  = (FB_W_LINESTRIDE & 0x1FF) * 8; // register unit = 8 bytes/line
    if (stride == 0)
      stride = s_rtt_w * 2;
    u32 base = FB_W_SOF1 & VRAM_MASK & ~3u;
    u32 tiles_per_row = copy_w >> 2;

    for (u32 y = 0; y < s_rtt_h; y++)
    {
      const u16 *trow = &fb2d_tex[((y >> 2) * tiles_per_row) * 16 + (y & 3) * 4];
      u32 line = base + y * stride;
      for (u32 x = 0; x < s_rtt_w; x += 2)
      {
        // x is even, so x and x+1 share a 4x4 tile row (x&3 is 0 or 2).
        const u16 *tile = &trow[(x >> 2) * 16];
        u16 t0 = tile[(x & 3) + 0];
        u16 t1 = tile[(x & 3) + 1];
        u16 p0, p1;
        switch (packmode)
        {
        case 1: // RGB565: EFB copy format, pass through
          p0 = t0;
          p1 = t1;
          break;
        case 2: // ARGB4444 (alpha forced opaque: RGB565 copies carry no alpha)
          p0 = (u16)(0xF000 | ((t0 >> 4) & 0x0F00) | ((t0 >> 3) & 0x00F0) | ((t0 >> 1) & 0x000F));
          p1 = (u16)(0xF000 | ((t1 >> 4) & 0x0F00) | ((t1 >> 3) & 0x00F0) | ((t1 >> 1) & 0x000F));
          break;
        default: // 0 = KRGB0555, 3 = ARGB1555: drop G LSB, K/A from fb_kval
          p0 = (u16)(kbit | ((t0 >> 1) & 0x7FE0) | (t0 & 0x001F));
          p1 = (u16)(kbit | ((t1 >> 1) & 0x7FE0) | (t1 & 0x001F));
          break;
        }
        *(u32 *)&params.vram[(line + x * 2) & VRAM_MASK] = ((u32)p1 << 16) | p0;
      }
    }
  }
  else if (DEBUG_MESSAGE())
  {
    // 24/32-bit packmodes (4/5/6) are not written back: no DC texture format
    // reads them, so a game doing this is using the buffer some other way.
    printf("[RTT] unsupported FB_W_CTRL packmode %d, write-back skipped\n", (int)packmode);
  }

  // Clear the whole EFB (color + Z) for the next frame, chunked so each
  // discard-copy destination fits the scratch buffer. With fbWidth=640 the
  // chunk is 480 lines, i.e. a single copy on every common video mode.
  u32 chunk = ((u32)(FB2D_W * FB2D_H) / rmode->fbWidth) & ~3u;
  for (u32 y = 0; y < rmode->efbHeight; y += chunk)
  {
    u32 ch = rmode->efbHeight - y;
    if (ch > chunk)
      ch = chunk;
    GX_SetTexCopySrc(0, (u16)y, rmode->fbWidth, (u16)ch);
    GX_SetTexCopyDst(rmode->fbWidth, (u16)ch, GX_TF_RGB565, GX_FALSE);
    GX_CopyTex(fb2d_tex, GX_TRUE); // data discarded, only the clear matters
  }
  // Wait for the discard-copies: fb2d_tex must not be pending as a GP copy
  // destination when PresentFramebuffer() next fills it from the CPU side.
  GX_DrawDone();
}

// ============================
// The main rendering loop. Executes GX commands to draw the stored vertex lists.
// ============================

void DoRender()
{
  /* commented to help prevent FIFO 
  if(get_debug_loop() == 1) {
    printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
    printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);
  }
    */

  // 240p (non-interlaced NTSC/PAL) and SCALER_CTL.hscale modes only use a
  // fraction of the nominal 640x480 canvas (real hardware halves the active
  // scanline/pixel count) — shrink the canvas so geometry submitted in that
  // smaller space still fills the whole output instead of a quarter/half of it.
  float dc_width = 640.f * g_fb_scale_x;
  float dc_height = 480.f * g_fb_scale_y;
  if (s_rtt_pass)
  {
    // Render-to-texture: geometry was submitted in the RTT target's own
    // coordinate space, so the canvas is the target size (no fb scale).
    dc_width  = (float)s_rtt_w;
    dc_height = (float)s_rtt_h;
  }

  VIDEO_SetBlack(FALSE);
  // Set viewport to a centred 4:3 sub-region of the 16:9 framebuffer.
  // NDC [-1..+1] maps to this viewport, so all DC geometry (which is
  // already in 4:3 screen-space) displays with correct proportions.
  // In fullscreen mode use the whole width (stretched 16:9).
  if (s_rtt_pass)
  {
    // 1:1 pixels into the EFB top-left corner; rtt_copy_efb_to_vram() reads
    // exactly this rect back out.
    GX_SetViewport(0, 0, (float)s_rtt_w, (float)s_rtt_h, 0, 1);
  }
  else if (FULLSCREEN())
  {
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
  }
  else
  {
    const float ratio  = (4.f / 3.f) / (16.f / 9.f); // 0.75
    const float vp_w   = rmode->fbWidth * ratio;
    const float vp_x   = (rmode->fbWidth - vp_w) * 0.5f;
    GX_SetViewport(vp_x, 0, vp_w, rmode->efbHeight, 0, 1);
  }
  GX_InvVtxCache();
  GX_InvalidateTexAll();

  // GX ACCURATE
  // Single vertex format, always 24 bytes/vertex (POS+CLR0+TEX0).
  // VCD never changes mid-stream to avoid CP packet FIFO misalignment.
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);

  // OLD GX ACCURATE (better in some cases ?)
  // Define the vertex format for the GX pipeline.
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

  // Note : making an if/else statement here also cause FIFO (CPU desynchronisation)
																	 
  

  // Offset (specular) color support. Read once per frame like the other
  // presets; the setting cannot change while a game is running, so the VCD
  // stays identical from frame to frame (see the FIFO warnings above).
  const bool offset_fix = OFFSET_COLOR_FIX();

  GX_SetNumChans(offset_fix ? 2 : 1);
  GX_SetNumTexGens(1);

  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

  if (offset_fix)
  {
    // 28 bytes/vertex: POS+CLR0+CLR1+TEX0. Every vertex this frame submits
    // CLR1 (spc, 0 when unused) so the VCD never changes mid-stream.
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    // Channel 1: plain vertex color, no lighting.
    GX_SetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    // TEV stage 1 implements the PVR offset term: PIX = stage0 + OffsetCol.
    //   color = CPREV + RASC(COLOR1A1)   (a=RASC, b=0, c=0, d=CPREV)
    //   alpha = APREV                    (offset alpha is unused on real PVR)
    // Configured once here; the per-polygon loop below only toggles the
    // stage count between 1 and 2, so polys without offset color keep the
    // exact single-stage output (and fill rate) they had before.
    GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
    GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_RASC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  }
  GX_SetNumTevStages(1);

  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

  // Background polygon handling
  u32 param_base = PARAM_BASE & 0xF00000;
  _ISP_BACKGND_D_type bg_d;

  bg_d.i = ISP_BACKGND_D & ~(0xF);
  (void)bg_d; // Currently unused but may be needed later

  bool PSVM = FPU_SHAD_SCALE & 0x100; // double parameters for volumes

  // ISP_BACKGND_T bitfield layout (from DC hardware spec, LSB first):
  //   bits [2:0]   tag_offset  (3 bits)
  //   bits [23:3]  tag_address (21 bits)
  //   bits [26:24] skip        (3 bits)
  //   bit  [27]    shadow      (1 bit)
  //   bit  [28]    cache_bypass(1 bit)
  // The Wii is big-endian so we must extract manually instead of
  // relying on the C bitfield struct (which packs in the wrong order).
  u32 raw_bgt        = ISP_BACKGND_T;
  u32 real_tag_offset  = (raw_bgt >> 0)  & 0x7;
  u32 real_tag_address = (raw_bgt >> 3)  & 0x1FFFFF;
  u32 real_skip        = (raw_bgt >> 24) & 0x7;
  u32 real_shadow      = (raw_bgt >> 27) & 0x1;

  // Get the strip base
  u32 strip_base = param_base + real_tag_address * 4;
  // Calculate the vertex size
  u32 strip_vs = (3 + real_skip) * 4;
  u32 strip_vert_num = real_tag_offset;

  if (PSVM && real_shadow)
  {
    strip_vs += real_skip * 4; // 2x the size needed for shadow volumes
  }
  // Get vertex ptr
  u32 vertex_ptr = strip_vert_num * strip_vs + strip_base + 3 * 4;
  // now , all the info is ready :p

  Vertex BGTest;

  decode_pvr_vertex(strip_base, vertex_ptr, &BGTest);

  // BGTest.col is in ARGB (Dreamcast PVR) format.
  // GXColor expects RGBA, so swap R and B channels before passing.
  u32 raw_col = BGTest.col;
  GXColor bgColor = {
    (u8)((raw_col >> 16) & 0xFF), // R (was at B position in ARGB)
    (u8)((raw_col >>  8) & 0xFF), // G
    (u8)((raw_col >>  0) & 0xFF), // B (was at R position in ARGB)
    (u8)((raw_col >> 24) & 0xFF)  // A
  };
  // Use the background vertex color as the EFB clear color.
  GX_SetCopyClear(bgColor, 0x00000000);

  GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE);
  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GX_SetAlphaUpdate(GX_TRUE);
  GX_SetColorUpdate(GX_TRUE);


  /*
  // This algorithm performs 4x4 matrix vertex transformation followed by W-buffer depth normalization.
  // keep in mind that the Wii's Paired Singles (floating point unit) is very fast at these $4 \times 4$ multiplications!
  // Still needed ? we have "Mtx44 mtx" code, it that the same stuff ?
  
  x'=x*xx + y*xy + z* xz + xw
  y'=x*yx + y*yy + z* yz + yw
  z'=x*zx + y*zy + z* zz + zw
  w'=x*wx + y*wy + z* wz + ww

  cx=x'/w'
  cy=y'/w'
  cz=z'/w'

  invW  = [+inf,0]
  w     = [0,+inf]

  post transform : -1 1
  w'=w
  z = w
  z' = A*w+B

  z'/w = A + B/w

  A + B*invW

  mapped to [0 1]
  min(invW)=1/max(W)
  max(invW)=1/min(W)

  A + B * max(invW) = vnear
  A + B * min(invW) = vfar

  A=-B*max(invW) + vnear

  -B*max(invW) + B*min(invW) = vfar
  B*(min(invW)-max(invW))=vfar
  B=vfar/(min(invW)-max(invW))

*/

  // sanitise values
  // Coordinate Projection Logic: Converts Dreamcast 1/W into Wii depth.

  // Allow W to be much smaller to push the far plane out for massive environments (like racing games)
  // Important : Keep 0.0001f ! 0.001f is not enough
  if (vtx_min_Z <= 0.0001f)
    vtx_min_Z = 0.0001f;

  if (vtx_max_Z < 0 || vtx_max_Z > 128 * 1024)
    vtx_max_Z = 100000.0f;

  // Extra guard: if EFB garbage or NaN slipped through...
  // Z range so that min >= max, the projection math below produces NaN/inf
  // which desyncs the GX FIFO ("GFX Fifo Opcode unknown 0x64").
  // Reset to a safe default range so the frame renders without a crash.
  // Don't remove this part or Bomberman intro FMV will not display on real hardware
  if (vtx_min_Z >= vtx_max_Z || vtx_max_Z != vtx_max_Z || vtx_min_Z != vtx_min_Z)
  {
    vtx_min_Z = 0.0001f;
    vtx_max_Z = 100000.0f;  
  }

  // extend range
  vtx_max_Z *= 1.001; // to not clip vtx_max verts
  // vtx_min_Z*=0.999;

  // convert to [-1 .. 0]
  float p6 = -1 / (1 / vtx_max_Z - 1 / vtx_min_Z);
  float p5 = p6 / vtx_min_Z;

  // The projection matrix maps DC screen-space coords to GX clip space.
  // X aspect ratio is NOT corrected here — DC vertices are already in screen
  // space (x=[0..640]) and z is 1/W (depth), so a perspective matrix would
  // incorrectly couple x and z. X is remapped at vertex submission instead.
  Mtx44 mtx =
      {
          {(2.f / dc_width), 0, +1, 0},
          {0, -(2.f / dc_height), -1, 0},
          {0, 0, p5, p6},
          {0, 0, -1, 0}
        };

  // load the matrix to GX
  GX_LoadProjectionMtx(mtx, GX_PERSPECTIVE);

  // clear out other matrixes
  Mtx modelview;
  guMtxIdentity(modelview);
  GX_LoadPosMtxImm(modelview, GX_PNMTX0);

  Vertex *drawVTX = vertices;
  VertexList *drawLST = lists;
  PolyParam *drawMod = listModes;

  const VertexList *const crLST = curLST; // hint to the compiler that sceGUM cant edit this value !

  // FMV Strip Debug
  // commented to prevent unecessary if/else in main render loop
  /*
  if (fmv_debug)
  {
    // Full, unfiltered dump of every strip this frame: catches anything a
    // narrower instrumentation point (e.g. only logging inside EndPolyStrip)
    // could miss, and ties each strip to its actual texture/format state.
    Vertex *dvtx = vertices;
    PolyParam *dmod = listModes;
    u32 strip_idx = 0;
    for (const VertexList *dlst = lists; dlst != crLST; dlst++, strip_idx++)
    {
      s32 raw_cnt   = dlst->count;
      bool new_param = raw_cnt < 0;
      s32 vcnt      = raw_cnt & 0x7FFF;
      bool textured = dmod->pcw.Texture != 0;
      u32 fmt       = textured ? (u32)dmod->tcw.NO_PAL.PixelFmt : 0xFF;
      u32 addr      = textured ? ((dmod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK) : 0;

      float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
      for (s32 i = 0; i < vcnt; i++)
      {
        if (dvtx[i].x < minx) minx = dvtx[i].x;
        if (dvtx[i].x > maxx) maxx = dvtx[i].x;
        if (dvtx[i].y < miny) miny = dvtx[i].y;
        if (dvtx[i].y > maxy) maxy = dvtx[i].y;
      }

      printf("[STRIP] #%u %s vtx=%d new_param=%d textured=%d fmt=%u addr=%06X x=[%.1f..%.1f] y=[%.1f..%.1f]\n",
             strip_idx, (dlst == TransLST) ? "TRANS" : "OPAQUE", vcnt, (int)new_param,
             (int)textured, fmt, addr, minx, maxx, miny, maxy);

      if (new_param) dmod++;
      dvtx += vcnt;
    }
    printf("[FRAME] total strips=%u total verts=%u\n", strip_idx, (u32)(curVTX - vertices));
  }
  */

  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  int last_textured = -1;  // track texture state to skip redundant GX calls
  int last_tev_stages = 1; // offset_fix only: 2 while drawing offset-color polys
  int last_alpha_fmt = -1; // -1 = unset
  int last_shad_instr = -1; // -1 = unset; tracks the GX op currently set on TEVSTAGE0 for textured polys
  const bool decal_alpha_fix = DECAL_ALPHA_FIX(); // read once per frame, not per polygon
  bool force_vtx_alpha_opaque = false; // true for 1555/4444: vertex alpha must not kill tex alpha
  bool last_z_write = true; // Per Polygon Z Write algorythm (Beta, untested)

  // DC TSP blend factor → GX blend factor lookup.
  // SrcInstr: "OtherColor" for a source factor means the framebuffer (dst) color.
  // DstInstr: "OtherColor" for a dest factor means the incoming (src) color.
  static const u8 dc_src_to_gx[8] = {
    GX_BL_ZERO,        // 0 = Zero
    GX_BL_ONE,         // 1 = One
    GX_BL_DSTCLR,      // 2 = OtherColor  (dst color used as src factor)
    GX_BL_INVDSTCLR,   // 3 = InvOtherColor
    GX_BL_SRCALPHA,    // 4 = SrcAlpha
    GX_BL_INVSRCALPHA, // 5 = InvSrcAlpha
    GX_BL_DSTALPHA,    // 6 = DstAlpha
    GX_BL_INVDSTALPHA, // 7 = InvDstAlpha
  };
  static const u8 dc_dst_to_gx[8] = {
    GX_BL_ZERO,        // 0 = Zero
    GX_BL_ONE,         // 1 = One
    GX_BL_SRCCLR,      // 2 = OtherColor  (src color used as dst factor)
    GX_BL_INVSRCCLR,   // 3 = InvOtherColor
    GX_BL_SRCALPHA,    // 4 = SrcAlpha
    GX_BL_INVSRCALPHA, // 5 = InvSrcAlpha
    GX_BL_DSTALPHA,    // 6 = DstAlpha
    GX_BL_INVDSTALPHA, // 7 = InvDstAlpha
  };
  bool in_trans_list = false;
  int last_src_blend = -1;
  int last_dst_blend = -1;

  // TRANS_SORT() state. While ts_active, strips are fetched from
  // trans_sort_recs[] (sorted back-to-front) instead of the sequential
  // lists/vertices/listModes walk; ts_end_* are the walk cursors at the end
  // of the TR range, restored when the sorted range is done so any strips
  // after it (legacy path: a PT list submitted after TR) still line up.
  const bool trans_sort = TRANS_SORT(); // read once per frame
  bool ts_active = false;
  int ts_idx = 0;
  int ts_count = 0;
  Vertex *ts_end_vtx = 0;
  PolyParam *ts_end_mod = 0;
  const PolyParam *ts_last_mod = 0; // last state applied while sorted-drawing


  // ── Draw segments ──────────────────────────────────────────────────────────
  // Legacy: one pass over every list in TA submission order (OP, TR, PT), so
  // punch-through polys get drawn last, in the translucent blend state.
  // PUNCH_THROUGH_FIX() with a PT list present splits the walk into three
  // ranges drawn OP → PT → TR, like real PVR: PT polys render as opaque ones
  // gated by a GX alpha test against PT_ALPHA_REF, and translucent polys can
  // then blend over them. lists/vertices/listModes are parallel streams, so
  // each range restarts from the cursors captured in StartList().
  struct DrawSeg
  {
    VertexList *begin;
    const VertexList *end;
    Vertex *vtx;
    PolyParam *mod;
    bool punch_through;
  };
  DrawSeg segs[3];
  int seg_count = 0;

  if (PUNCH_THROUGH_FIX() && PTLST)
  {
    // Opaque: everything before the first of the TR/PT boundaries.
    const VertexList *op_end = crLST;
    if (TransLST && TransLST < op_end) op_end = TransLST;
    if (PTLST < op_end) op_end = PTLST;
    segs[seg_count].begin = lists;    segs[seg_count].end = op_end;
    segs[seg_count].vtx = vertices;   segs[seg_count].mod = listModes;
    segs[seg_count].punch_through = false; seg_count++;

    segs[seg_count].begin = PTLST;
    segs[seg_count].end = (TransLST && TransLST > PTLST) ? TransLST : crLST;
    segs[seg_count].vtx = PT_VTX;     segs[seg_count].mod = PT_Mod;
    segs[seg_count].punch_through = true; seg_count++;

    if (TransLST)
    {
      segs[seg_count].begin = TransLST;
      segs[seg_count].end = (PTLST > TransLST) ? PTLST : crLST;
      segs[seg_count].vtx = TransVTX; segs[seg_count].mod = TransMod;
      segs[seg_count].punch_through = false; seg_count++;
    }
  }
  else
  {
    segs[seg_count].begin = lists;  segs[seg_count].end = crLST;
    segs[seg_count].vtx = vertices; segs[seg_count].mod = listModes;
    segs[seg_count].punch_through = false; seg_count++;
  }

  for (int seg = 0; seg < seg_count; seg++)
  {
    const bool seg_is_pt = segs[seg].punch_through;
    const VertexList *const seg_end = segs[seg].end;
    drawLST = segs[seg].begin;
    drawVTX = segs[seg].vtx;
    drawMod = segs[seg].mod;
    ts_active = false; // each segment restarts from its own captured cursors

    if (seg_is_pt)
    {
      // Punch-through: opaque-style draw (no blending, Z write on) with the
      // hardware alpha test real PVR applies to the PT list — a pixel passes
      // only if its final alpha >= PT_ALPHA_REF. ZCompLoc must be FALSE so
      // discarded pixels don't stamp the Z buffer.
      GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
      GX_SetAlphaCompare(GX_GEQUAL, (u8)(PT_ALPHA_REF & 0xFF), GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_FALSE);
    }

    for (; drawLST != seg_end; drawLST++)
    {
      if (ts_active && ts_idx == ts_count)
      {
        // Sorted TR range fully drawn — resume the sequential walk right
        // after it (only reached when strips follow the TR range in this
        // segment, i.e. legacy path with a PT list submitted after TR).
        ts_active = false;
        drawVTX = ts_end_vtx;
        drawMod = ts_end_mod;
      }

      if (drawLST == TransLST)
      {
        // Enable blending for the translucent list. Blend factors are set
        // per-polygon below based on TSP.SrcInstr / DstInstr.
        in_trans_list = true;
        last_src_blend = -1; // force first per-polygon update
        last_dst_blend = -1;
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

        // TODO: per-polygon ISP state (isp.ZWriteDis, isp.CullMode, isp.DepthMode)
        // is not yet emulated. Translucent polys may incorrectly stamp the Z-buffer.
        // See below
        //
        // ClaudeAI: attempted on branch feature/per-poly-isp-state — added
        // per-poly isp.DepthMode (GX depth func) and isp.CullMode (GX cull
        // mode) behind ISP_DEPTH()/ISP_CULL() presets, merged into the same
        // GX_SetZMode() call as the existing ZWriteDis handling. Measured a
        // FPS regression and visual glitches in testing, so it was not
        // merged to main. Kept on the branch for future investigation
        // (suspects: per-poly GX state churn, and/or the DC CullMode->GX
        // FRONT/BACK winding pairing being backwards for this projection).

        if (trans_sort)
        {
          // Build one record per TR strip by pre-walking the range with the
          // same cursor rules as the draw loop (drawVTX/drawMod are exactly
          // TransVTX/TransMod here), then sort back-to-front. The TR range
          // ends where the PT list starts if PT was submitted after TR
          // (legacy path — with PUNCH_THROUGH_FIX() the segment split already
          // ends this segment there), else at the end of the stream.
          const VertexList *tr_end = (PTLST && PTLST > TransLST) ? PTLST : crLST;
          if (tr_end > seg_end)
            tr_end = seg_end;

          Vertex *wvtx = drawVTX;
          PolyParam *wmod = drawMod;
          // Param in effect if the first TR strip carries no header (should
          // not happen — TA lists start with a global param — but a valid
          // fallback beats reading a NULL mod).
          PolyParam *cur_mod = (wmod > listModes) ? wmod - 1 : wmod;
          int n = 0;
          for (const VertexList *l = drawLST; l != tr_end; l++)
          {
            s32 c = l->count;
            if (c < 0)
              cur_mod = wmod++;
            c &= 0x7FFF;

            float far_w = 0.0f;
            for (s32 i = 0; i < c; i++)
              if (wvtx[i].z > far_w)
                far_w = wvtx[i].z;

            trans_sort_recs[n].far_w = far_w;
            trans_sort_recs[n].vtx = wvtx;
            trans_sort_recs[n].mod = cur_mod;
            trans_sort_recs[n].count = (u16)c;
            n++;
            wvtx += c;
          }

          if (n > 1)
            qsort(trans_sort_recs, n, sizeof(TransStripRec), trans_strip_cmp);

          ts_idx = 0;
          ts_count = n;
          ts_end_vtx = wvtx;   // walk-end cursors, restored after the range
          ts_end_mod = wmod;
          ts_last_mod = 0;     // force state application on the first strip
          ts_active = n > 0;
        }
      }

      s32 count;
      PolyParam *stripMod = 0; // non-NULL → apply this polygon's render state below
      if (ts_active)
      {
        // Sorted translucent strip: fetched out of submission order, so the
        // count sign bit can't drive state changes anymore — re-apply render
        // state whenever the strip's param differs from the last one applied.
        const TransStripRec &r = trans_sort_recs[ts_idx++];
        count = r.count;
        drawVTX = r.vtx;
        if (r.mod != ts_last_mod)
        {
          stripMod = r.mod;
          ts_last_mod = r.mod;
        }
      }
      else
      {
        count = drawLST->count;
        if (count < 0)
        {
          stripMod = drawMod++;
          count &= 0x7FFF;
        }
      }

      if (stripMod)
      {
        int is_textured = stripMod->pcw.Texture ? 1 : 0;
        if (is_textured != last_textured)
        {
          if (is_textured)
          {
            GX_SetNumTexGens(1);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
          }
          else
          {
            GX_SetNumTexGens(0);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            last_shad_instr = -1; // force TEVSTAGE0 op to be reapplied on the next textured poly
          }
          last_textured = is_textured;
        }

        // Enable TEV stage 1 (offset color add, configured once above) only
        // for textured polys that actually carry an offset color, so
        // everything else keeps single-stage fill rate.
        if (offset_fix)
        {
          int tev_stages = (is_textured && stripMod->pcw.Offset) ? 2 : 1;
          if (tev_stages != last_tev_stages)
          {
            GX_SetNumTevStages(tev_stages);
            last_tev_stages = tev_stages;
          }
        }

        if (is_textured)
        {
          SetTextureParams(stripMod, decal_alpha_fix);

          // TSP.ShadInstr==2 (DecalAlpha) needs GX_DECAL instead of GX_MODULATE on
          // TEVSTAGE0 (see DECAL_ALPHA_FIX() above for why). Gated on decal_alpha_fix
          // so the off path never reads tsp.ShadInstr here — SetTextureParams() above
          // already set GX_MODULATE unconditionally either way.
          if (decal_alpha_fix)
          {
            int tev_op = (stripMod->tsp.ShadInstr == 2) ? GX_DECAL : GX_MODULATE;
            if (tev_op != last_shad_instr)
            {
              GX_SetTevOp(GX_TEVSTAGE0, tev_op);
              last_shad_instr = tev_op;
            }
          }

          // Real PVR hardware decides this per-polygon via TSP.UseAlpha, not pixel format.
          // UseAlpha==0 means the hardware forces vertex alpha to 1.0 before modulation
          // (vertex alpha must not be allowed to multiply texture alpha to zero).
          // UseAlpha==1 means vertex alpha is used as supplied.
          // Regression fix: ARGB1555 (fmt 0) and RGB565 (fmt 1) textures are commonly
          // drawn with vertex alpha left at 0 even when UseAlpha==1 (e.g. Test Drive 6's
          // menu/HUD text, an ARGB1555 cutout font): the game relies on the texture's
          // own alpha, not the vertex alpha, to gate visibility. Always force opaque for
          // these two formats; defer to TSP.UseAlpha for everything else (e.g. ARGB4444).
          u32 fmt = stripMod->tcw.NO_PAL.PixelFmt;
          if (RGB565_OPAQUE_ALPHA())
            force_vtx_alpha_opaque = (fmt == 0 || fmt == 1) || !stripMod->tsp.UseAlpha;
          else
            force_vtx_alpha_opaque = (fmt == 0) || !stripMod->tsp.UseAlpha;

          // This is more accurate for alpha. May cost CPU cycles.
          // Skipped in the punch-through segment: its alpha test (GEQUAL
          // PT_ALPHA_REF, set once at segment start) must not be overwritten.
          if (ADVANCED_ALPHA() && !seg_is_pt)
          {
            int alpha_fmt = 0;
            if (BLEND_MODE()) {
              // fps_boost gains even 2 FPS in Castlevania but the alpha isn't correct anymore
              bool blend_fps_boost = BLEND_FPS_BOOST();
              bool blend_alpha_independent = (stripMod->tsp.SrcInstr < 4 && stripMod->tsp.DstInstr < 4);
              if (blend_fps_boost && drawLST != TransLST) { // New in alpha 0.39
                  alpha_fmt = 0;
              } else if (decal_alpha_fix && stripMod->tsp.ShadInstr == 2) { // Was there in alpha0.38
                  alpha_fmt = 0;
              } else if (blend_alpha_independent) { // New in alpha 0.39
                  alpha_fmt = 0;
              } else if (stripMod->tsp.IgnoreTexA) { // Was there in alpha0.38
                  alpha_fmt = 0;
              } else {
                  alpha_fmt = 1; // Was 0 before
              }
            } else { // Legacy Behavior (correct for Chuchurocket & max FPS)
              alpha_fmt = (decal_alpha_fix && stripMod->tsp.ShadInstr == 2) ? 0 : (stripMod->tsp.IgnoreTexA ? 0 : 1);
            }

            if (alpha_fmt != last_alpha_fmt)
            {
              if (alpha_fmt)
              {
                GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
                GX_SetZCompLoc(GX_FALSE);
              }
              else
              {
                GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
                GX_SetZCompLoc(GX_TRUE);
              }
              last_alpha_fmt = alpha_fmt;
            }
          }
        }
        else
        {
          // Untextured polygon — vertex alpha is meaningful as-is, never override it.
          force_vtx_alpha_opaque = false;
        }

        // ── Per-polygon Z write (ISP.ZWriteDis) ──────────────────────────────
        // Real DC hardware honors ZWriteDis per polygon. Fix sprites with ZWriteDis=1
        if(PER_POLYGON_Z_WRITE())
        {
          bool z_write = !stripMod->isp.ZWriteDis;
          if (z_write != last_z_write)
          {
            GX_SetZMode(GX_TRUE, GX_GEQUAL, z_write ? GX_TRUE : GX_FALSE);
            last_z_write = z_write;
          }
        }

        // Per-polygon blend mode: match TSP.SrcInstr / DstInstr for the translucent
        // Absolutely necessary for Resident Evil 3
        if (BLEND_MODE() && in_trans_list)
        {
          int src = (int)stripMod->tsp.SrcInstr;
          int dst = (int)stripMod->tsp.DstInstr;
          if (src != last_src_blend || dst != last_dst_blend)
          {
            GX_SetBlendMode(GX_BM_BLEND, dc_src_to_gx[src], dc_dst_to_gx[dst], GX_LO_CLEAR);
            last_src_blend = src;
            last_dst_blend = dst;
          }
        }
      }

      if (count)
      {
        GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, count);
        if (offset_fix)
        {
          // VCD carries CLR1 this frame: every vertex must submit it, in
          // attribute order POS, CLR0, CLR1, TEX0.
          while (count--)
          {
            GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
            u32 vcol = drawVTX->col;
            if (force_vtx_alpha_opaque)
              vcol |= 0xFF000000u;
            GX_Color1u32(HOST_TO_LE32(vcol));
            GX_Color1u32(HOST_TO_LE32(drawVTX->spc));
            GX_TexCoord2f32(drawVTX->u, drawVTX->v);
            drawVTX++;
          }
        }
        else
        while (count--)
        {
          GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
          // For ARGB1555/4444 textures the DC game may leave vertex alpha = 0,
          // expecting the hardware to ignore it. Force alpha = 0xFF so GX_MODULATE
          // does not multiply the texture alpha away.
          u32 vcol = drawVTX->col;
          if (force_vtx_alpha_opaque)
            vcol |= 0xFF000000u;
          GX_Color1u32(HOST_TO_LE32(vcol));
          GX_TexCoord2f32(drawVTX->u, drawVTX->v);
          drawVTX++;
        }
        GX_End();
      }

      // sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,0,drawVTX);

      //			drawVTX+=count;
    }

    if (seg_is_pt)
    {
      // Undo the PT alpha test before the translucent segment — and before the
      // next frame when there is no translucent list, since GX alpha-compare
      // state persists across frames. This matches the alpha_fmt==0 state of
      // the ADVANCED_ALPHA() block above, so tell its cache about it.
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      last_alpha_fmt = 0;
    }
  }

  reset_vtx_state();

  GX_DrawDone();

  if (s_rtt_pass)
  {
    // Render-to-texture frame: nothing reaches the display. Copy the EFB back
    // into emulated VRAM at FB_W_SOF1 (and leave the EFB cleared) instead.
    // No present, no fb_stamp_present_magic(): FB_W_SOF1 is a texture here,
    // not the scanout framebuffer.
    rtt_copy_efb_to_vram();
    return;
  }

  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  VIDEO_SetNextFramebuffer(frameBuffer[fb]);
  VIDEO_Flush();

  s_did_3d_render = true;

  // Mark the VRAM framebuffer as "produced by the PVR / already on screen".
  // VBlank() checks this magic word and only presents the VRAM framebuffer
  // when it has been overwritten by a non-PVR (CPU/2D) writer.
  fb_stamp_present_magic();

  wii_audio_frame();
  // VIDEO_WaitVSync() // vsync speed limit removed — don't block the SH4 thread
}

// ============================
// FRAMEBUFFER PRESENT (VRAM scanout -> GX)
// ============================
//
// Reads the Dreamcast framebuffer out of VRAM (at FB_R_SOF1) as a 640x480
// image and draws it as a fullscreen textured quad, then copies to the Wii
// framebuffer and presents.  Shared by the StartRender 2D-blit path and the
// vblank-driven present in VBlank().
// The DC source framebuffer may be smaller/narrower than the fb2d_tex scratch
// texture (declared at file scope above DoRender, shared with the
// RENDER_TO_TEXTURE() write-back); we decode the real region into the top
// area of it and present only that sub-rect via UVs.

void PresentFramebuffer()
{
  // ── Derive the source framebuffer geometry from FB_R_SIZE / FB_R_CTRL ──────
  // (devcast RenderFramebuffer).  FB_R_SIZE is a raw u32 here:
  //   bits  0-9  fb_x_size, 10-19 fb_y_size, 20-29 fb_modulus.
  u32 fb_x_size  =  FB_R_SIZE        & 0x3FF;
  u32 fb_y_size  = (FB_R_SIZE >> 10) & 0x3FF;
  u32 fb_modulus = (FB_R_SIZE >> 20) & 0x3FF;
  u32 fb_depth   = FB_R_CTRL.fb_depth;

  if(DEBUG_MESSAGE()) printf("[FB] PresentFramebuffer FB_R_SIZE=%08X (x=%u y=%u mod=%u) depth=%u SOF1=%08X\n",
    (u32)FB_R_SIZE, fb_x_size, fb_y_size, fb_modulus, fb_depth, FB_R_SOF1);

  if (fb_x_size == 0 || fb_y_size == 0)
    return;   // nothing configured to scan out

  // width/modulus expressed in pixels for the 16-bit (0555/565) formats.
  int src_w   = (int)((fb_x_size + 1) << 1);   // pixels per line (16bpp word == 1 pixel)
  int src_h   = (int)(fb_y_size + 1);
  int mod_px  = (int)((fb_modulus > 0 ? (fb_modulus - 1) : 0) << 1); // gap pixels per line
  int bpp     = 2;                              // bytes per pixel (16bpp paths)

  // For 24/32bpp scanout the stride math differs; fall back to treating the
  // pixels as 16bpp so we at least show something rather than a torn line.
  // (DC selfboot 2D content is virtually always 0555/565.)

  // Clamp the decoded region to the scratch texture.
  int cpy_w = src_w > FB2D_W ? FB2D_W : src_w;
  int cpy_h = src_h > FB2D_H ? FB2D_H : src_h;

  u32 addr = (FB_R_SOF1 & 0x00FFFFFF);

  // Decode line-by-line into the 4x4-tiled RGB565 scratch texture, honoring the
  // real source stride (line width + modulus gap).
  for (int y = 0; y < cpy_h; y++)
  {
    u32 line_addr = addr;
    for (int x = 0; x < cpy_w; x++)
    {
      u32 off64 = fast_ConvOffset32toOffset64(line_addr);
      u16 px = *host_ptr_xor((u16*)&params.vram[off64]);

      // Convert 1555 -> 565 when needed so the GX texture format is uniform.
      if (fb_depth != 1) // fbde_0555 (or non-565): RGB555 -> RGB565
      {
        u16 r = (px >> 10) & 0x1F;
        u16 g = (px >>  5) & 0x1F;
        u16 b =  px        & 0x1F;
        px = (u16)((r << 11) | (g << 6) | b);
      }

      // 4x4 tile address inside fb2d_tex.
      u16 *dst = &fb2d_tex[((y / 4) * (FB2D_W / 4) + (x / 4)) * 16
                           + (y % 4) * 4 + (x % 4)];
      *dst = px;

      line_addr += bpp;
    }
    // Advance to the next source line by the full source width + modulus gap,
    // regardless of how much we clamped into the scratch texture.
    addr += (u32)((src_w + mod_px) * bpp);
  }
  DCFlushRange(fb2d_tex, sizeof(fb2d_tex));

  // Texture is uniformly RGB565 now.
  GXTexObj texobj;
  GX_InitTexObj(&texobj, fb2d_tex, FB2D_W, FB2D_H, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&texobj, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GX_LoadTexObj(&texobj, GX_TEXMAP0);

  // UV sub-rect: only the decoded cpy_w x cpy_h region holds real pixels.
  float u1 = (float)cpy_w / (float)FB2D_W;
  float v1 = (float)cpy_h / (float)FB2D_H;

  // VTXFMT1: XY+UV only, leaves VTXFMT0 (3D path) undisturbed.
  GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS,  GX_POS_XY,  GX_F32, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST,  GX_F32, 0);
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GX_SetNumChans(0);
  // The 3D path may have left TEV stage 1 active (OFFSET_COLOR_FIX with an
  // offset poly drawn last); with 0 channels its raster input would be junk.
  GX_SetNumTevStages(1);
  GX_SetNumTexGens(1);
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
  GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

  Mtx44 ortho;
  guOrtho(ortho, 0, 480, 0, 640, 0, 1);
  GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
  Mtx mv;
  guMtxIdentity(mv);
  GX_LoadPosMtxImm(mv, GX_PNMTX0);

  // In 4:3 mode, draw the 2D framebuffer into a centred 4:3 sub-rectangle
  // so logo screens and 2D content have the same correct framing as 3D.
  float x0_2d, x1_2d;
  if (FULLSCREEN())
  {
    x0_2d = 0.f;
    x1_2d = 640.f;
  }
  else
  {
    // 4:3 inside 16:9: side bars are (1 - 3/4) / 2 * 640 = 80 px each side
    const float ratio = (4.f / 3.f) / (16.f / 9.f); // 0.75
    float margin = (640.f * (1.f - ratio)) * 0.5f;   // 80 px
    x0_2d = margin;
    x1_2d = 640.f - margin;
  }

  GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
    GX_Position2f32(x0_2d,   0); GX_TexCoord2f32(0,  0);
    GX_Position2f32(x1_2d,   0); GX_TexCoord2f32(u1, 0);
    GX_Position2f32(x1_2d, 480); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x0_2d, 480); GX_TexCoord2f32(0,  v1);
  GX_End();

  GX_DrawDone();
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  VIDEO_SetNextFramebuffer(frameBuffer[fb]);
  VIDEO_Flush();
  wii_audio_frame();
  // VIDEO_WaitVSync() // Not necessary here (don't block the SH4 thread)

  // Mark these framebuffer contents as shown.  The texture has already been
  // sampled from VRAM above, so stamping the read origin now only affects what
  // the *next* VBlank sees: it will re-present only if the game overwrites the
  // framebuffer (clobbering the marker) before then.
  fb_stamp_present_magic_at(FB_R_SOF1);
}

// ============================
// VBLANK (vblank-driven framebuffer present)
// ============================
//
// Called at the start of every vblank (from SPG.cpp).  devcast presents the
// VRAM framebuffer here whenever no 3D render happened that frame
// (Renderer_if.cpp rend_vblank -> RenderFramebuffer).  We do the same, but
// detect the "needs present" condition via the in-VRAM magic word:
//
//   * If the framebuffer read origin (FB_R_SOF1) still holds FB_PRESENT_MAGIC,
//     the on-screen image is the PVR's last render -> nothing new to show.
//   * If the magic is gone, a non-PVR writer (CPU / 2D blit) has refreshed the
//     framebuffer -> scan it out of VRAM and present it.
void VBlank()
{
  if (!FB_R_CTRL.fb_enable)
    return;   // display output disabled

  if (!fb_needs_present())
    return;   // on-screen image is still the PVR render; nothing new

  if (ShouldSkipFrame())
    return;

  if(DEBUG_MESSAGE()) printf("[PATH] VBlank-FB present: FB_R_SOF1=%08X fb_depth=%d\n",
    FB_R_SOF1, (int)FB_R_CTRL.fb_depth);

  PresentFramebuffer();
  s_did_3d_render = false;
  FrameCount++;
}

// ============================
// START RENDERING
// ============================

void StartRender()
{
  u32 VtxCnt = curVTX - vertices;
  VertexCount += VtxCnt;

  render_end_pending_cycles = VtxCnt * 15;
  if (render_end_pending_cycles < 50000)
    render_end_pending_cycles = 50000;

  // ── Frame-skip early-out ─────────────────────────────────────────────────
  // Must come AFTER render_end_pending_cycles is set so the game's timing
  // loop receives the render-done interrupt on schedule even for skipped frames.
  if (ShouldSkipFrame())
  {
    reset_vtx_state();   // discard this frame's geometry; free the buffers
    return;              // no GX calls — saves ~10-15 ms on a heavy frame
  }

  if (FB_W_SOF1 & 0x1000000)
  {
    // Render-to-texture: bit 24 in the write address targets the 64-bit
    // texture area (mirrors, TV screens, some menu effects). With the preset
    // on and real geometry submitted, render the frame and copy it back into
    // VRAM at FB_W_SOF1 instead of dropping it / treating it as a 2D present.
    if (RENDER_TO_TEXTURE() && VtxCnt > 0)
    {
      // Target size from the pixel clip registers (extracted manually — see
      // the ISP_BACKGND_T note in DoRender() about bitfields on the Wii).
      u32 xclip = FB_X_CLIP.full;
      u32 yclip = FB_Y_CLIP.full;
      u32 rtt_w = ((xclip >> 16) & 0x7FF) + 1 - (xclip & 0x7FF); // clip max is inclusive
      u32 rtt_h = ((yclip >> 16) & 0x3FF) + 1 - (yclip & 0x3FF);
      if ((s32)rtt_w < 8 || rtt_w > (u32)rmode->fbWidth || rtt_w > FB2D_W)
        rtt_w = 8;
      if ((s32)rtt_h < 8 || rtt_h > (u32)rmode->efbHeight || rtt_h > FB2D_H)
        rtt_h = 8;

      if(DEBUG_MESSAGE()) printf("[PATH] RTT: FB_W_SOF1=%08X %ux%u packmode=%d stride=%d VtxCnt=%d\n",
        FB_W_SOF1, rtt_w, rtt_h, (int)(FB_W_CTRL & 7), (int)((FB_W_LINESTRIDE & 0x1FF) * 8), VtxCnt);

      s_rtt_w = rtt_w;
      s_rtt_h = rtt_h;
      s_rtt_pass = true;
      DoRender();
      s_rtt_pass = false;
      return; // not a presented frame: no FrameCount++, display untouched
    }

    if(FRAMEBUFFER_2D()){
      if (s_did_3d_render)
      {
        if(DEBUG_MESSAGE()) printf("[PATH] 2D-after-3D: FB_W_SOF1=%08X FB_R_SOF1=%08X fb_depth=%d VtxCnt=%d\n",
          FB_W_SOF1, FB_R_SOF1, (int)FB_R_CTRL.fb_depth, VtxCnt);
        s_did_3d_render = false;
        GX_DrawDone();
        GX_CopyDisp(frameBuffer[fb], GX_TRUE);
        VIDEO_SetNextFramebuffer(frameBuffer[fb]);
        VIDEO_Flush();
        wii_audio_frame();
        // VIDEO_WaitVSync() // Not necessary here (don't block the SH4 thread)
        FrameCount++;
        return;
      }

      if(DEBUG_MESSAGE()) printf("[PATH] 2D-blit: FB_W_SOF1=%08X FB_R_SOF1=%08X fb_depth=%d VtxCnt=%d\n",
        FB_W_SOF1, FB_R_SOF1, (int)FB_R_CTRL.fb_depth, VtxCnt);

      PresentFramebuffer();
      FrameCount++;
      return;
    } else {
      return; // just return
    }
  }

  if(DEBUG_MESSAGE()) printf("[PATH] 3D: FB_W_SOF1=%08X FB_R_SOF1=%08X VtxCnt=%d lists=%d\n",
    FB_W_SOF1, FB_R_SOF1, VtxCnt, (int)(curLST - lists));

  s_render_start_time = os_GetSeconds();
  DoRender();
  s_last_render_time  = os_GetSeconds() - s_render_start_time;

  FrameCount++;
}

void EndRender() {}



// ============================
// VertexDecoder struct: Handles the accumulation of vertex data into strips.
// ============================

struct VertexDecoder
{
  // list handling
  __forceinline static void StartList(u32 ListType)
  {
    if (ListType == ListType_Translucent)
    {
      TransLST = curLST;
      TransVTX = curVTX;
      TransMod = curMod;
    }
    else if (ListType == ListType_Punch_Through)
    {
      PTLST = curLST;
      PT_VTX = curVTX;
      PT_Mod = curMod;
    }
  }
  __forceinline static void EndList(u32 ListType) {}

  static u32 FLCOL(float *col)
  {
    u32 A = col[0] * 255;
    u32 R = col[1] * 255;
    u32 G = col[2] * 255;
    u32 B = col[3] * 255;
    if (A > 255)
      A = 255;
    if (R > 255)
      R = 255;
    if (G > 255)
      G = 255;
    if (B > 255)
      B = 255;

    return (A << 24) | (B << 16) | (G << 8) | R;
  }
  // Real PVR Intensity (Gouraud) mode: each vertex carries a scalar intensity;
  // the final RGB is the polygon's FaceColor (curFaceColor*, set by
  // AppendPolyParam1/2B/4B) scaled by that intensity. Alpha comes straight
  // from curFaceColorA, not from the intensity scalar. Gated behind
  // g_vertex_color_fix_cached (refreshed once per frame in reset_vtx_state(),
  // see its decl above) — off keeps the original flat grayscale behavior
  // untouched for every game that hasn't opted in.
  static u32 INTESITY(float inte)
  {
    if (!g_vertex_color_fix_cached)
    {
      u32 C = inte * 255;
      if (C > 255)
        C = 255;
      return (0xFF << 24) | (C << 16) | (C << 8) | (C);
    }

    s32 R = (s32)(curFaceColorR * inte * 255.0f);
    s32 G = (s32)(curFaceColorG * inte * 255.0f);
    s32 B = (s32)(curFaceColorB * inte * 255.0f);
    s32 A = (s32)(curFaceColorA * 255.0f);
    colclamp(0, 255, R);
    colclamp(0, 255, G);
    colclamp(0, 255, B);
    colclamp(0, 255, A);
    return ((u32)A << 24) | ((u32)B << 16) | ((u32)G << 8) | (u32)R;
  }

  // Offset-color flavor of INTESITY(): the vertex's offset intensity scalar
  // scales the polygon's FaceOffset color (curFaceOffs*, set by
  // AppendPolyParam2B). Alpha is left 0 — the offset TEV stage only adds RGB
  // and passes the previous stage's alpha through. Callers only invoke this
  // when curPolyOffsMask is set, so OFFSET_COLOR_FIX() is already known on.
  static u32 OFFS_INTESITY(float inte)
  {
    s32 R = (s32)(curFaceOffsR * inte * 255.0f);
    s32 G = (s32)(curFaceOffsG * inte * 255.0f);
    s32 B = (s32)(curFaceOffsB * inte * 255.0f);
    colclamp(0, 255, R);
    colclamp(0, 255, G);
    colclamp(0, 255, B);
    return ((u32)B << 16) | ((u32)G << 8) | (u32)R;
  }

  // Polys
#define glob_param_bdc                 \
  if ((curVTX - vertices) > 38 * 1024) \
    reset_vtx_state();                 \
  global_regd = true;                  \
  curMod->pcw = pp->pcw;               \
  curMod->isp = pp->isp;               \
  curMod->tsp = pp->tsp;               \
  curMod->tcw = pp->tcw;               \
  curPolyOffsMask = (g_offset_color_fix_cached && pp->pcw.Offset) ? 0xFFFFFFFFu : 0u;

  __forceinline static void fastcall AppendPolyParam0(TA_PolyParam0 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam1(TA_PolyParam1 *pp)
  {
    glob_param_bdc;
    curFaceColorR = pp->FaceColorR;
    curFaceColorG = pp->FaceColorG;
    curFaceColorB = pp->FaceColorB;
    curFaceColorA = pp->FaceColorA;
    // Type 1 carries no face offset color — clear the scratch so a stale
    // value from an earlier Type 2 polygon can't leak into OFFS_INTESITY().
    curFaceOffsR = curFaceOffsG = curFaceOffsB = 0.0f;
  }
  __forceinline static void fastcall AppendPolyParam2A(TA_PolyParam2A *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam2B(TA_PolyParam2B *pp)
  {
    curFaceColorR = pp->FaceColorR;
    curFaceColorG = pp->FaceColorG;
    curFaceColorB = pp->FaceColorB;
    curFaceColorA = pp->FaceColorA;
    // Face offset (specular) color, consumed by OFFS_INTESITY() when
    // OFFSET_COLOR_FIX() is on. Offset alpha is not used by the hardware.
    curFaceOffsR = pp->FaceOffsetR;
    curFaceOffsG = pp->FaceOffsetG;
    curFaceOffsB = pp->FaceOffsetB;
  }
  __forceinline static void fastcall AppendPolyParam3(TA_PolyParam3 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam4A(TA_PolyParam4A *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam4B(TA_PolyParam4B *pp)
  {
    // Only volume 0's face color is used: AppendPolyVertex10/13A/14A (the
    // intensity vertex handlers for two-volume polys) only ever read
    // BaseInt0, never blending in volume 1.
    curFaceColorR = pp->FaceColor0R;
    curFaceColorG = pp->FaceColor0G;
    curFaceColorB = pp->FaceColor0B;
    curFaceColorA = pp->FaceColor0A;
    // Type 4 carries no face offset color (see Type 1 note above).
    curFaceOffsR = curFaceOffsG = curFaceOffsB = 0.0f;
  }

  // Poly Strip handling
  // UPDATE SPRITES ON EDIT !
  __forceinline static void StartPolyStrip()
  {
    curLST->ptr = curVTX;
  }

  __forceinline static void EndPolyStrip()
  {
    curLST->count = (curVTX - curLST->ptr);
    if (global_regd)
    {
      curLST->count |= 0x80000000;
      global_regd = false;
      curMod++;
    }
    curLST++;
  }

// Standard vertex projection macro. PVR 'Z' is actually 1/W.
//
// Guard against zero/negative/NaN Z values that can arrive when the game reads
// back EFB (framebuffer) data that the emulator hasn't rendered yet (e.g.
// Castlevania: Resurrection shadow/reflection pass).  A bad Z causes W=1/Z to
// become infinity or NaN, which then corrupts vtx_min_Z / vtx_max_Z, blows up
// the projection matrix in DoRender(), and ultimately desyncs the GX FIFO
// producing "GFX Fifo Opcode unknown (0x64)".
//
// Clamping to 0.0001f keeps W finite and pushes the vertex safely to the far
// plane where it is invisible but harmless.
//
// NOTE: the comparison below is intentionally "_z >= 0.0001f" (not "_z < 0.0001f")
// so that NaN falls into the clamp. Per IEEE-754, every relational comparison
// against NaN is false, so "NaN < 0.0001f" is false and a "<"-based ternary lets
// NaN pass through unclamped, defeating the whole point of this guard.
#define vert_base(dst, _x, _y, _z) /*VertexCount++;*/         \
  float _safe_z = (_z >= 0.0001f) ? _z : 0.0001f;            \
  float W = 1.0f / _safe_z;                                   \
  curVTX[dst].x = VTX_TFX(_x) * W;                           \
  curVTX[dst].y = VTX_TFY(_y) * W;                           \
  if (W > 0.0f && W < vtx_min_Z)                             \
    vtx_min_Z = W;                                            \
  if (W > 0.0f && W > vtx_max_Z)                             \
    vtx_max_Z = W;                                            \
  curVTX[dst].z = W; /*Linearly scaled later*/

  // Poly Vertex handlers
#define vert_cvt_base vert_base(0, vtx->xyz[0], vtx->xyz[1], vtx->xyz[2])

  // Handlers for various PVR vertex types (Packed color, Float color, Intensity, etc.)
  //(Non-Textured, Packed Color)
  __forceinline static void AppendPolyVertex0(TA_Vertex0 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = 0; // non-textured: no offset color on PVR

    curVTX++;
  }

  //(Non-Textured, Floating Color)
  __forceinline static void AppendPolyVertex1(TA_Vertex1 *vtx)
  {
    vert_cvt_base;
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Non-Textured, Intensity)
  __forceinline static void AppendPolyVertex2(TA_Vertex2 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Textured, Packed Color)
  __forceinline static void AppendPolyVertex3(TA_Vertex3 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = ABGR8888(vtx->OffsCol) & curPolyOffsMask;

    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX++;
  }

  //(Textured, Packed Color, 16bit UV)
  __forceinline static void AppendPolyVertex4(TA_Vertex4 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = ABGR8888(vtx->OffsCol) & curPolyOffsMask;

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);

    curVTX++;
  }

  //(Textured, Floating Color)
  __forceinline static void AppendPolyVertex5A(TA_Vertex5A *vtx)
  {
    vert_cvt_base;

    curVTX->u = vtx->u;
    curVTX->v = vtx->v;
  }
  __forceinline static void AppendPolyVertex5B(TA_Vertex5B *vtx)
  {
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX->spc = curPolyOffsMask ? FLCOL(&vtx->OffsA) : 0;
    curVTX++;
  }

  //(Textured, Floating Color, 16bit UV)
  __forceinline static void AppendPolyVertex6A(TA_Vertex6A *vtx)
  {
    vert_cvt_base;

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);
  }
  __forceinline static void AppendPolyVertex6B(TA_Vertex6B *vtx)
  {
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX->spc = curPolyOffsMask ? FLCOL(&vtx->OffsA) : 0;
    curVTX++;
  }

  //(Textured, Intensity)
  __forceinline static void AppendPolyVertex7(TA_Vertex7 *vtx)
  {
    vert_cvt_base;
    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt) : 0;

    curVTX++;
  }

  //(Textured, Intensity, 16bit UV)
  __forceinline static void AppendPolyVertex8(TA_Vertex8 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt) : 0;

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);

    curVTX++;
  }

  //(Non-Textured, Packed Color, with Two Volumes)
  __forceinline static void AppendPolyVertex9(TA_Vertex9 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol0);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Non-Textured, Intensity,	with Two Volumes)
  __forceinline static void AppendPolyVertex10(TA_Vertex10 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt0);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Textured, Packed Color,	with Two Volumes)
  __forceinline static void AppendPolyVertex11A(TA_Vertex11A *vtx)
  {
    vert_cvt_base;

    curVTX->u = vtx->u0;
    curVTX->v = vtx->v0;

    curVTX->col = ABGR8888(vtx->BaseCol0);
    curVTX->spc = ABGR8888(vtx->OffsCol0) & curPolyOffsMask;
  }
  __forceinline static void AppendPolyVertex11B(TA_Vertex11B *vtx)
  {
    curVTX++;
  }

  //(Textured, Packed Color, 16bit UV, with Two Volumes)
  __forceinline static void AppendPolyVertex12A(TA_Vertex12A *vtx)
  {
    vert_cvt_base;

    curVTX->u = CVT16UV(vtx->u0);
    curVTX->v = CVT16UV(vtx->v0);

    curVTX->col = ABGR8888(vtx->BaseCol0);
    curVTX->spc = ABGR8888(vtx->OffsCol0) & curPolyOffsMask;
  }
  __forceinline static void AppendPolyVertex12B(TA_Vertex12B *vtx)
  {
    curVTX++;
  }

  //(Textured, Intensity,	with Two Volumes)
  __forceinline static void AppendPolyVertex13A(TA_Vertex13A *vtx)
  {
    vert_cvt_base;
    curVTX->u = vtx->u0;
    curVTX->v = vtx->v0;
    curVTX->col = INTESITY(vtx->BaseInt0);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt0) : 0;
  }
  __forceinline static void AppendPolyVertex13B(TA_Vertex13B *vtx)
  {
    curVTX++;
  }

  //(Textured, Intensity, 16bit UV, with Two Volumes)
  __forceinline static void AppendPolyVertex14A(TA_Vertex14A *vtx)
  {
    vert_cvt_base;
    curVTX->u = CVT16UV(vtx->u0);
    curVTX->v = CVT16UV(vtx->v0);
    curVTX->col = INTESITY(vtx->BaseInt0);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt0) : 0;
  }
  __forceinline static void AppendPolyVertex14B(TA_Vertex14B *vtx)
  {
    curVTX++;
  }

  // Sprites
  __forceinline static void AppendSpriteParam(TA_SpriteParam *spr)
  {
    TA_SpriteParam *pp = spr;
    glob_param_bdc;
    // Sprites carry one packed offset color in the header, shared by all 4
    // vertices (masked to 0 when PCW.Offset is off or the fix is disabled).
    curSpriteSpc = ABGR8888(pp->OffsCol) & curPolyOffsMask;
  }

// Sprite Vertex Handlers
/*
__forceinline
static void AppendSpriteVertex0A(TA_Sprite0A* sv)
{

}
__forceinline
static void AppendSpriteVertex0B(TA_Sprite0B* sv)
{

}
*/
#define sprite_uv(indx, u_name, v_name) \
  curVTX[indx].u = CVT16UV(sv->u_name); \
  curVTX[indx].v = CVT16UV(sv->v_name);

  // Sprites are converted to 4-vertex triangle strips.
  __forceinline static void AppendSpriteVertexA(TA_Sprite1A *sv)
  {
    
    StartPolyStrip();
    curVTX[0].col = 0xFFFFFFFF;
    curVTX[1].col = 0xFFFFFFFF;
    curVTX[2].col = 0xFFFFFFFF;
    curVTX[3].col = 0xFFFFFFFF;
    curVTX[0].spc = curSpriteSpc;
    curVTX[1].spc = curSpriteSpc;
    curVTX[2].spc = curSpriteSpc;
    curVTX[3].spc = curSpriteSpc;

    {
      vert_base(2, sv->x0, sv->y0, sv->z0);
    }
    {
      vert_base(3, sv->x1, sv->y1, sv->z1);
    }

    curVTX[1].x = sv->x2;
  }
  __forceinline static void AppendSpriteVertexB(TA_Sprite1B *sv)
  {

    {
      vert_base(1, curVTX[1].x, sv->y2, sv->z2);
    }
    {
      vert_base(0, sv->x3, sv->y3, sv->z2);
    }

    sprite_uv(2, u0, v0);
    sprite_uv(3, u1, v1);
    sprite_uv(1, u2, v2);
    sprite_uv(0, u0, v2); // or sprite_uv(u2,v0); ?

    curVTX += 4;
    //			VertexCount+=4;

    // EndPolyStrip();
    curLST->count = 4;
    if (global_regd)
    {
      curLST->count |= 0x80000000;
      global_regd = false;
      curMod++;
    }
    curLST++;
  }

  // ModVolumes
  __forceinline static void AppendModVolParam(TA_ModVolParam *modv)
  {
  }

  // ModVol Strip handling
  __forceinline static void StartModVol(TA_ModVolParam *param)
  {
  }
  __forceinline static void ModVolStripEnd()
  {
  }

  // Mod Volume Vertex handlers
  __forceinline static void AppendModVolVertexA(TA_ModVolA *mvv)
  {
  }
  __forceinline static void AppendModVolVertexB(TA_ModVolB *mvv)
  {
  }
  __forceinline static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax)
  {
  }
  __forceinline static void TileClipMode(u32 mode)
  {
  }
  // Misc
  __forceinline static void ListCont()
  {
  }
  __forceinline static void ListInit()
  {
  // reset_vtx_state();
  }
  __forceinline static void SoftReset()
  {
  // reset_vtx_state();
  }
};
// Setup related

// ============================
// Vertex Decoding-Converting
// ============================

void SetFpsText(char *text)
{
  strcpy(fps_text, text);
  printf(text);
  // if (!IsFullscreen)
  {
    // SetWindowText((HWND)emu.GetRenderTarget(), fps_text);
  }
}

// ============================
// Initialize the Wii Video and GX subsystem.
// ============================

bool InitRenderer()
{
  // Diagnostic file logger
  printf("Log to file check ndclog.txt\n");
  freopen("/ndclog.txt", "w", stdout);
  printf("[Init Renderer] Logging started\n");
  // Obtain the preferred video mode from the system
  // This will correspond to the settings in the Wii menu
  rmode = VIDEO_GetPreferredMode(NULL);

  // Adjust video mode based on region.
  switch (rmode->viTVMode >> 2)
  {
  case VI_NTSC: // 480 lines (NTSC 60hz)
    break;
  case VI_PAL: // 576 lines (PAL 50hz)
    rmode = &TVPal576IntDfScale;
    rmode->xfbHeight = 480;
    rmode->viYOrigin = (VI_MAX_HEIGHT_PAL - 480) / 2;
    rmode->viHeight = 480;
    break;
  default: // 480 lines (PAL 60Hz)
    break;
  }

  // Force single-sample, full 24-bit colour. rmode->aa selects a
  // 16-bit (RGB565) EFB further below, which dithers to hide banding —
  // that dither noise was previously masked by the deflicker blur and
  // becomes visible once the blur is removed. Pixel accuracy matters
  // more here than the TV anti-aliasing this mode is meant for.
  // rmode->aa = GX_FALSE;

  // Set up the video registers with the chosen mode (devkit wii example)
  VIDEO_Configure(rmode);

  // Allocate memory for the display in the uncached region (devkit wii example)
  // allocate 2 framebuffers for double buffering
  frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  
  // Set up the video registers with the chosen mode (devkit wii example)
  VIDEO_Configure(rmode);
  // Tell the video hardware where our display memory is (devkit wii example)
  VIDEO_SetNextFramebuffer(frameBuffer[fb]);
  // Make the display visible or invisible (devkit wii example)
  VIDEO_SetBlack(TRUE); // FALSE doesn't seems to change anything here
  // Flush the video register changes to the hardware (devkit wii example)
  VIDEO_Flush();
  // Wait for Video setup to complete
  VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // fb = fb ^ 1; (invert value)
  fb ^= 1;

  // setup the fifo and then init the flipper
  memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);

  GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);
  ApplyGraphismPreset();  // LOW/NORMAL/HIGH/EXTRA

  printf("vram_buffer: %08X\n", (u32)vram_buffer);

  // clears the bg to color and clears the z buffer
  // GX_SetCopyClear(background, 0x00ffffff);

  // other gx setup
  // Standard GX display/copy settings.
  GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
  f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
  u32 xfbHeight = GX_SetDispCopyYScale(yscale);
  GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter); // GX_FALSE or GX_TRUE ?
  GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

  if (rmode->aa)
    GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
  else
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  GX_SetCullMode(GX_CULL_NONE);
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  // setup the vertex descriptor
  // tells the flipper to expect direct data

  printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
  printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);

  printf("sizeof TextureCacheDesc: %d\n", sizeof(TextureCacheDesc));
  printf("sizeof GXTexObj: %d\n", sizeof(GXTexObj));
  printf("sizeof GXTlutObj: %d\n", sizeof(GXTlutObj));

  tex_cache_init(); // must be after vram_buffer is allocated by _vmem_reserve()

  return TileAccel.Init();
}

void TermRenderer()
{
  TileAccel.Term();
}

// ============================
// RESET RENDERER
// ============================

void ResetRenderer(bool Manual)
{
  TileAccel.Reset(Manual);
  tex_cache_init(); // clear texture cache on reset
  VertexCount = 0;
  FrameCount = 0;
}

// Thread Start
bool ThreadStart()
{
  return true;
}

// Thread End
void ThreadEnd()
{
}

// List Cont
void ListCont()
{
  TileAccel.ListCont();
}

// List Init
void ListInit()
{
  TileAccel.ListInit();
}

// Soft Reset
void SoftReset()
{
  TileAccel.SoftReset();
}

// VRAM locked Write
void VramLockedWrite(vram_block *bl)
{
}

#include <vector>
#include <string>
using namespace std;

// ==============
// Attempts to find and boot a 'boot.cdi' image from the SD/USB root.
// ==============

int GetFile(char *szFileName, char *szParse = 0, u32 flags = 0)
{
  if (FILE *f = fopen("/boot.cdi", "rb"))
  {
    fclose(f);
    strcpy(szFileName, "/boot.cdi");
    return 1;
  }
  else
    return 0;
}