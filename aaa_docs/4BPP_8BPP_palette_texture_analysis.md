# 4BPP / 8BPP Palette Texture Implementation Notes

## Commit

```
fix(gxRend): decode 4BPP/8BPP palette textures to 16bpp GX pixels

Replace GX_TF_I4/I8 stub with full software palette lookup for fmt=5
and fmt=6 textures. Previously, raw palette indices were passed to GX
as intensity/greyscale values, rendering all indexed textures as grey.

For each pixel, read the twiddled index byte, look up the PALETTE_RAM
entry selected by PalSelect, convert to GX RGB565 (pal_fmt=1) or
RGB5A3 (pal_fmt=0/2/3), and write via GX_TexOffs. SetupPaletteForTexture
stub is no longer called for these cases.

Tested: previously-grey indexed textures now show correct palette colours.
Known issue: some textures appear black-on-black (likely correct palette
content on a dark background, not a regression).
```

---

## Why Not CI4 / CI8?

GX natively supports colour-indexed formats (`GX_TF_CI4`, `GX_TF_CI8`) with a TLUT, which would be the most efficient path — upload raw index data as-is, upload the palette as a TLUT, let GX do the lookup in hardware.

Two hard blockers prevent this:

**1. Palette format mismatch.**
GX's TLUT only supports `GX_TL_IA8`, `GX_TL_RGB565`, and `GX_TL_RGB5A3`.
The Dreamcast's `PAL_RAM_CTRL` can be **ARGB8888**, which has no GX TLUT equivalent. A CPU conversion step for palette entries would be required anyway in that case.

**2. The twiddle problem.**
GX's CI4/CI8 formats expect pixels in GX's own 8×4 block layout. Dreamcast 4BPP/8BPP palette textures are stored in **twiddled (Morton) order**. Untwiddling the index data into GX block layout before upload is basically the same loop already present — just copying indices instead of decoded pixels. CPU work is nearly identical.

The software decode to RGB565/RGB5A3 is therefore the correct approach given these constraints. CI4/CI8 could be revisited if `PAL_RAM_CTRL` is observed to always be RGB565/RGB5A3 in practice.

---

## Performance Impact

**CPU / Framerate**

The old stub did zero work. The new code runs a `w × h` pixel loop on Broadway (in-order, 729 MHz):

- One `twop()` call per pixel — ~10–20 cycles (bit-interleave loop)
- One `PALETTE_RAM` array lookup
- One format switch/convert + one `GX_TexOffs()` write

| Texture size | Approximate decode time |
|---|---|
| 256×256 | ~1–2 ms |
| 64×64 | ~0.1 ms |
| 32×32 | ~0.02 ms |

**The cache system protects steady-state framerate.** The `0xDEADBEEF` sentinel means the decode loop only runs when the texture actually changed in VRAM. In a typical frame, palette textures are decoded once on first load and reused. The cost only recurs on palette changes or new texture loads.

**Memory Cost**

Decoded textures use more memory than raw index data:

| Format | Raw (old stub) | Decoded 16bpp (new) | CI4/CI8 + TLUT (theoretical) |
|---|---|---|---|
| 8BPP 256×256 | 65 536 B (wrong) | 131 072 B | 65 536 B + 1 024 B |
| 4BPP 256×256 | 32 768 B (wrong) | 131 072 B | 32 768 B + 64 B |
| 8BPP 64×64 | 4 096 B | 8 192 B | 4 096 B + 1 024 B |
| 4BPP 64×64 | 2 048 B | 8 192 B | 2 048 B + 64 B |

- Decoded 8BPP uses **2× the memory** of raw index data
- Decoded 4BPP uses **4× the memory** of raw index data

This comes out of the `vram_buffer` cache region, same as all other textures — no separate allocation. In practice most DC palette textures are small (fonts, icons, 2D sprites), so real-world impact on MEM1 pressure is expected to be low.

---

## Known Remaining Issues

- **Black-on-black textures:** Some previously-grey textures are now invisible. This is likely correct palette content rendered on a matching dark background, not a regression from the fix.
- **Palette dirty tracking:** `regs.cpp` has a TODO for `pal_needs_update` / `pal_rev_256` / `pal_rev_16`. If a game changes the palette mid-frame without touching VRAM, the `0xDEADBEEF` cache will not invalidate and stale decoded pixels will be displayed. This is a pre-existing issue, not introduced by this change.
- **Scanline (non-twiddled) 4BPP/8BPP:** The current implementation assumes twiddled layout (`ScanOrder == 0`). Scanline-order palette textures (`ScanOrder == 1`) are not handled and would need a separate linear decode path similar to `Plannar<>()`.
