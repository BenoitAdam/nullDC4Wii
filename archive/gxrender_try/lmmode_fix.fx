================================================================================
LMMODE FIX — archived 2026-07-15 (removed from the tree, kept here for later)
================================================================================

WHAT IT IS
----------
SB_LMMODE0 / SB_LMMODE1 (0x005F6884 / 0x005F6888) select which VRAM bus the
two "direct texture path" windows go through:

    0x11xxxxxx window  -> bus selected by SB_LMMODE0
    0x13xxxxxx window  -> bus selected by SB_LMMODE1

    bus 0 = 64-bit linear layout   (same as CPU stores to 0x04000000)
    bus 1 = 32-bit bank-interleaved (same as CPU stores to 0x05000000)

The emulator ignored both registers:
  1. Ch2-DMA to 0x11xxxxxx always wrote through the 64-bit linear window,
     so a game that sets LMMODE0=1 gets its texture data word-scrambled
     (it reads back as garbage/zeros from the 64-bit address the TCW names).
  2. Ch2-DMA to 0x13xxxxxx was a stub that DROPPED the data entirely.
  3. TAWrite / TAWriteSQ direct-VRAM writes (address >= 16MB inside the TA
     range) always did a linear memcpy, same problem as (1).

This is correct-hardware behavior, but no game we tested so far actually
needs it (Hokuto no Ken, the original suspect, uses LMMODE0=0 everywhere —
confirmed by the [LMMODE] diagnostic). It was implemented preset-gated as
"lmmode" (default off).

The 32->64 interleave conversion already exists in the tree:
vramlock_ConvOffset32toOffset64() (pvrLock), and the 0xA5000000 write
handler applies it per word — that is what the fix leans on.

================================================================================
1) dc/sh4/dmac.cpp
================================================================================
Near the isDstVRAM_* helpers, add:

// lmmode preset (game_presets: "lmmode", default off = legacy).
// SB_LMMODE0/1 select which VRAM bus the 0x11/0x13 DMA windows go through:
// 0 = 64-bit (linear texture layout), 1 = 32-bit (bank-interleaved, same
// layout CPU stores to 0x05000000 produce). Legacy code ignored them and
// always wrote linearly, so every texture uploaded with LMMODE=1 lands
// word-scrambled and reads back as zeros from the 64-bit address its TCW
// points at.
extern "C" int get_lmmode_preset();
#define LMMODE_FIX() (get_lmmode_preset() == 1)

// Copy a Ch2 transfer from system RAM into a VRAM window, honoring the RAM
// wrap boundary. Goes word-wise through the memory accessors, so a 0xA5
// (32-bit window) destination gets the 32->64 interleave applied by its
// handler. Returns the advanced source address.
static u32 Ch2CopyToVram(u32 src, u32 vram_dst, u32 len)
{
	while (len)
	{
		const u32 p_addr = src & RAM_MASK;
		const u32 avail  = RAM_SIZE - p_addr;
		const u32 chunk  = (avail < len) ? avail : len;

		u32 *sys_buf = (u32 *)GetMemPtr(src, chunk);
		if (sys_buf)
			WriteMemBlock_nommu_ptr(vram_dst, sys_buf, chunk);

		len      -= chunk;
		src      += chunk;
		vram_dst += chunk;
	}
	return src;
}

In DMAC_Ch2St(), REPLACE the isDstVRAM_LM0(dst) body (the manual RAM-wrap
while-loop over WriteMemBlock_nommu_ptr with hardcoded 0xA4000000) with:

	else if (isDstVRAM_LM0(dst))
	{
		u32 vram_win = (LMMODE_FIX() && SB_LMMODE0 == 1) ? 0xA5000000u : 0xA4000000u;
		src = Ch2CopyToVram(src, (dst & 0x00FFFFFFu) | vram_win, len);
	}

and REPLACE the isDstVRAM_LM1(dst) stub ("LMMODE1 transfer (stub)",
src += len) with:

	else if (isDstVRAM_LM1(dst))
	{
		if (LMMODE_FIX())
		{
			// Same VRAM, same rules as the 0x11 window — only the bus-select
			// register differs.
			u32 vram_win = (SB_LMMODE1 == 1) ? 0xA5000000u : 0xA4000000u;
			src = Ch2CopyToVram(src, (dst & 0x00FFFFFFu) | vram_win, len);
		}
		else
		{
			// Legacy: window not implemented, data dropped.
			printf("DMAC: Ch2 LMMODE1 transfer (stub) DST=0x%08X LEN=0x%X\n", dst, len);
			src += len;
		}
	}

(Optional diagnostic, fires regardless of preset, self-caps at 8 lines per
window — put before the copy in each branch:)

		static int s_lm0_log = 0;
		if (s_lm0_log < 8)
		{
			printf("[LMMODE] Ch2 DMA dst=%08X len=%X LMMODE0=%d fix=%d\n",
			       dst, len, (int)SB_LMMODE0, LMMODE_FIX() ? 1 : 0);
			s_lm0_log++;
		}

================================================================================
2) dc/pvr/pvr_if.cpp
================================================================================
Add near the top (needs #include "dc/mem/sb.h"):

// lmmode preset (game_presets: "lmmode", default off = legacy). See dmac.cpp:
// SB_LMMODE0/1 select the VRAM bus (0 = 64-bit linear, 1 = 32-bit
// bank-interleaved) for the 0x11/0x13 direct texture-path windows.
extern "C" int get_lmmode_preset();

// Write `words` u32s to VRAM through the 32-bit (interleaved) bus, starting
// at 32-bit-space offset `off32` — the layout CPU stores to 0x05000000
// produce. Used by the direct texture path when SB_LMMODE selects bus 1.
static void vram_write_area1_words(u32 off32, const u32* data, u32 words)
{
    for (u32 i = 0; i < words; i++, off32 += 4)
        *(u32*)&vram.data[vramlock_ConvOffset32toOffset64(off32)] = data[i];
}

In TAWrite(), replace the direct-VRAM-else-branch memcpy:

        // Direct VRAM write (16MB+ range). The 0x11 window's bus is selected
        // by SB_LMMODE0, the 0x13 window's by SB_LMMODE1 (address bit 25
        // tells them apart). Bus 1 = 32-bit interleaved; legacy always wrote
        // linearly (64-bit layout). Gated by the lmmode preset.
        u32 lm = (address & 0x02000000u) ? SB_LMMODE1 : SB_LMMODE0;
        if (get_lmmode_preset() == 1 && lm == 1) {
            vram_write_area1_words(address & VRAM_MASK, data, count * 8);
        } else {
            memcpy(&vram.data[address & VRAM_MASK], data, count * 32);
        }

In TAWriteSQ(), same replacement for its else-branch memcpy (fixed 32 bytes):

        u32 lm = (address & 0x02000000u) ? SB_LMMODE1 : SB_LMMODE0;
        if (get_lmmode_preset() == 1 && lm == 1) {
            vram_write_area1_words(address & VRAM_MASK, data, 8);
        } else {
            memcpy(&vram.data[address & VRAM_MASK], data, 32);
        }

================================================================================
3) wii/main.cpp
================================================================================
int g_lmmode_preset = 0;
// 0=off (legacy: Ch2-DMA/TA direct VRAM writes always use the 64-bit bus,
//   LMMODE1 window dropped), 1=on (honor SB_LMMODE0/1 bus select — 32-bit
//   interleaved uploads land where textures read them)

extern "C" {
  int get_lmmode_preset() { return g_lmmode_preset; }
}

================================================================================
4) wii/game_presets.cpp  (standard preset plumbing)
================================================================================
extern int g_lmmode_preset;                              // extern list
int lmmode;                                              // struct GamePreset
else if (key_eq(key, "lmmode")) p->lmmode = parse_bool(val);   // apply_kv
cur->lmmode = -1;                                        // preset_clear
if (p->lmmode >= 0) { g_lmmode_preset = p->lmmode; printf("  lmmode -> %d\n", p->lmmode); }  // preset_apply_fields

================================================================================
5) apps/discs/game_presets.cfg
================================================================================
;;   lmmode          = on | off  (honor SB_LMMODE0/1 VRAM bus select for the
;;                             0x11/0x13 DMA + TA direct texture windows.
;;                             off (default): legacy 64-bit-only writes,
;;                             0x13 window dropped. Correct HW behavior but
;;                             no tested game needs it yet.)

and under [default]:
lmmode=off
