#include "regs.h"
#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

/*
	Basic PVR register emulation for the Dreamcast's PowerVR CLX2 ("Holly") GPU.
	Registers are memory-mapped into a flat 32KB byte array, accessed via PvrReg().

	TODO: Full TA emulation is still needed for accurate rendering.
*/

// PVR X-scaler preset (see wii/game_presets.h, wii/main.cpp).
extern "C" int get_x_scaler_preset();

u8 regs[RegSize];

u32 FASTCALL libPvr_ReadReg(u32 addr, u32 size)
{
	// The PVR only supports 32-bit aligned register accesses.
	// Sub-word reads are undefined on hardware; assert in debug builds.
	verify(size == 4);
	if (size != 4)
		return 0;

	return PvrReg(addr, u32);
}

void FASTCALL libPvr_WriteReg(u32 paddr, u32 data, u32 size)
{
	// Same constraint as reads -- PVR registers are 32-bit only.
	verify(size == 4);
	if (size != 4)
		return;

	u32 addr = paddr & RegMask;

	switch (addr)
	{
		// ---- Read-only registers: ignore writes ----
		case ID_addr:
		case REVISION_addr:
			return;

		// ---- Trigger: start the ISP/TSP rendering pipeline ----
		case STARTRENDER_addr:
			rend_start_render();
			return;

		// ---- TA_LIST_INIT: bit 31 triggers TA initialisation ----
		case TA_LIST_INIT_addr:
			if (data >> 31)
			{
				rend_list_init();
				data = 0;  // hardware clears the register after triggering
			}
			break;

		// ---- SOFTRESET: bits 0-2 control individual reset lines ----
		//   bit 0 = TA soft reset
		//   bit 1 = Core (ISP/TSP) soft reset
		//   bit 2 = SDR interface reset (ignored in emulation)
		case SOFTRESET_addr:
			if (data != 0)
			{
				if (data & 1)
					rend_list_srst();
				// bits 1 and 2 accepted but not currently emulated
				data = 0;  // hardware self-clears
			}
			break;

		// ---- TA_LIST_CONT: a write (any value) resumes TA list processing ----
		// The value is not stored; the write is purely a trigger.
		case TA_LIST_CONT_addr:
			rend_list_cont();
			return;  // do NOT write to register array

		// ---- Registers that require video-sync recalculation ----
		case FB_R_CTRL_addr:
		case SPG_CONTROL_addr:
		case SPG_LOAD_addr:
			PvrReg(addr, u32) = data;
			CalculateSync();
			return;

		// ---- SPG_VBLANK: store + diagnostic ----
		// The internal vblank event (FPS stats line, speed limiter,
		// rend_vblank) fires when the scanline counter — wrapping at
		// SPG_LOAD.vcount+1 — equals vbstart. Log every write so a game
		// programming vbstart past the wrap point is visible in ndclog.txt.
		case SPG_VBLANK_addr:
			PvrReg(addr, u32) = data;
			{
				// extern u32 spg_ScanlineCount;
				// printf("[SPG] SPG_VBLANK write: vbstart=%d vbend=%d (lines=%d)%s\n",
				// 	(int)SPG_VBLANK.vbstart, (int)SPG_VBLANK.vbend, (int)spg_ScanlineCount,
				// 	(SPG_VBLANK.vbstart >= spg_ScanlineCount) ? "  <-- vbstart OUT OF RANGE, vblank event will NEVER fire!" : "");
				// fflush(stdout);
			}
			return;

		// ---- SCALER_CTL: hscale bit feeds the renderer's canvas width ----
		// Only recalculate with the x_scaler preset on: legacy behavior never
		// resynced on this write, and preset-off must stay bit-identical.
		// (Omicron The Nomad Soul / Wacky Races set hscale for 1280-wide
		// 2:1-SSAA rendering — see CalculateSync() in SPG.cpp.)
		case SCALER_CTL_addr:
			PvrReg(addr, u32) = data;
			if (get_x_scaler_preset())
				CalculateSync();
			return;

		default:
			// Palette RAM writes: track which palette entries have changed
			// so the renderer can invalidate cached palette textures.
			if (addr >= PALETTE_RAM_START_addr && addr <= PALETTE_RAM_END_addr)
			{
				if (PvrReg(addr, u32) != data)
				{
					// TODO: implement palette dirty tracking
					// u32 pal_index = (addr - PALETTE_RAM_START_addr) >> 2;
					// pal_needs_update = true;
					// pal_rev_256[pal_index >> 8]++;
					// pal_rev_16[pal_index >> 4]++;
				}
			}
			break;
	}

	PvrReg(addr, u32) = data;
}

bool Regs_Init()
{
	// Zero all registers so we start from a clean, deterministic state
	// before Regs_Reset() applies the hardware power-on defaults.
	memset(regs, 0, sizeof(regs));
	return true;
}

void Regs_Term()
{
	// Nothing to free -- regs[] is a static array.
}

void Regs_Reset(bool Manual)
{
	// Power-on default values taken from the Dreamcast hardware manual.
	ID                  = 0x17FD11DB;
	REVISION            = 0x00000011;
	SOFTRESET           = 0x00000007;
	SPG_HBLANK_INT.full = 0x031D0000;
	SPG_VBLANK_INT.full = 0x01500104;
	FPU_PARAM_CFG       = 0x0007DF77;
	HALF_OFFSET         = 0x00000007;
	ISP_FEED_CFG        = 0x00402000;
	SDRAM_REFRESH       = 0x00000020;
	SDRAM_ARB_CFG       = 0x0000001F;
	SDRAM_CFG           = 0x15F28997;
	SPG_HBLANK.full     = 0x007E0345;
	SPG_LOAD.full       = 0x01060359;
	SPG_VBLANK.full     = 0x01500104;
	SPG_WIDTH.full      = 0x07F1933F;
	VO_CONTROL          = 0x00000108;
	VO_STARTX           = 0x0000009D;
	VO_STARTY           = 0x00000015;
	SCALER_CTL.full     = 0x00000400;
	FB_BURSTCTRL        = 0x00090639;
	PT_ALPHA_REF        = 0x000000FF;
}
