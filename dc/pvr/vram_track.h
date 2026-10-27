#pragma once

#include "types.h"

//==============================================================================
// VRAM Write Tracking (tex_cache=normal_plus)
//==============================================================================
// The renderer's persistent texture caches validate a texture by its FIRST
// word (the 0xDEADBEEF sentinel). A game that updates part of a texture - a
// sprite atlas getting this frame's animation tiles, as Street Fighter III and
// Guilty Gear do - leaves that word alone, so the stale tiles keep being drawn.
// normal_plus stamps every VRAM page a write lands on with the current render
// epoch instead; the renderer re-decodes a texture when any page under it
// carries a stamp at least as new as the render that decoded it.
//
// Two levels so the check is cheap: a 64 KB block that nothing touched since
// the decode clears all 32 of its 2 KB pages with one load.
//
// Offsets are vram.data offsets, i.e. the 64-bit (texture) layout. Everything
// here is a no-op while vram_wtrack is false, which is every preset but
// normal_plus. plugs/drkPvr/gxRend.cpp's DoRender() owns vram_wtrack and
// vram_wepoch; dc/pvr/pvr_if.cpp holds the storage.
//
// Shared by dc/ (the write paths) and gxRend.cpp (the check), which is why it
// is its own header: gxRend.cpp does not pull in pvr_if.h.
#define VRAM_WPAGE_SHIFT   11  // 2 KB pages:   4096 stamps
#define VRAM_WCOARSE_SHIFT 16  // 64 KB blocks:  128 stamps, checked first
#define VRAM_WPAGES   (VRAM_SIZE >> VRAM_WPAGE_SHIFT)
#define VRAM_WCOARSE  (VRAM_SIZE >> VRAM_WCOARSE_SHIFT)

extern "C" u32  vram_wstamp[VRAM_WPAGES];
extern "C" u32  vram_wstamp_coarse[VRAM_WCOARSE];
extern "C" u32  vram_wepoch;
extern "C" bool vram_wtrack;

// One word at a 64-bit-layout offset (the 32-bit area handlers).
static INLINE void vram_wmark(u32 off64)
{
    if (!vram_wtrack)
        return;
    off64 &= VRAM_MASK;
    vram_wstamp[off64 >> VRAM_WPAGE_SHIFT]          = vram_wepoch;
    vram_wstamp_coarse[off64 >> VRAM_WCOARSE_SHIFT] = vram_wepoch;
}

// A run of bytes at a 64-bit-layout offset. Wraps at the end of VRAM the way
// the per-word store path does.
extern "C" void vram_wmark_range(u32 off64, u32 len);

// Bulk copies into the 64-bit area (0x04/0x06, direct-mapped, so no handler
// sees them): store-queue flushes, ch2-DMA to 0x11, PVR-DMA. The 32-bit area
// goes through pvr_write_area1_* word by word, which stamps by itself.
static INLINE void vram_wmark_block(u32 addr, u32 len)
{
    if (vram_wtrack && ((addr >> 26) & 7) == 1 && !(addr & 0x01000000))
        vram_wmark_range(addr & VRAM_MASK, len);
}
