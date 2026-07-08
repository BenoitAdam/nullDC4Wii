// dmac.cpp - SH4 DMAC (Direct Memory Access Controller) emulation
// Handles Ch2-DMA (PVR/GPU feed) and register I/O mapping for all 4 channels.

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/mem/sb.h"
#include "dc/mem/sh4_mem.h"
#include "dc/pvr/pvr_if.h"
#include "dmac.h"
#include "intc.h"
#include "dc/asic/asic.h"
#include "plugins/plugin_manager.h"

// ---------------------------------------------------------------------------
// DMAC register state for all 4 channels
// ---------------------------------------------------------------------------
u32              DMAC_SAR[4];
u32              DMAC_DAR[4];
u32              DMAC_DMATCR[4];   // only lower 24 bits are valid per channel
DMAC_CHCR_type   DMAC_CHCR[4];
DMAC_DMAOR_type  DMAC_DMAOR;

// ---------------------------------------------------------------------------
// Address range helpers (makes intent explicit, avoids magic numbers)
// ---------------------------------------------------------------------------
static inline bool isDstTA(u32 dst)         { return dst >= 0x10000000u && dst <= 0x10FFFFFFu; }
static inline bool isDstVRAM_LM0(u32 dst)  { return dst >= 0x11000000u && dst <= 0x11FFFFE0u; }
static inline bool isDstVRAM_LM1(u32 dst)  { return dst >= 0x13000000u && dst <= 0x13FFFFE0u; }

// LMMODE preset (wii/main.cpp): honor SB_LMMODE0/1 bus select for the
// 0x11xxxxxx / 0x13xxxxxx VRAM windows. See ch2_copy_ram_to_vram() below.
extern "C" int get_lmmode_preset();
extern "C" int get_debug_message();
#define LMMODE_FIX() (get_lmmode_preset() == 1)

// Copy a Ch2 transfer from system RAM into VRAM through the chosen bus.
// SB_LMMODEn selects the VRAM bus for the window: 0 = 64-bit linear (area
// 0x04, the legacy behavior), 1 = 32-bit interleaved (area 0x05, whose
// pvr_write_area1_32 handler applies the 32->64 offset conversion per word).
// A game that uploads textures with LMMODE=1 (common in WinCE titles, but any
// game can program it) gets them written linearly by the legacy path, landing
// the data at roughly half the intended 64-bit offset, so the textures read
// back as zeros. Gated behind the lmmode preset: with it off the caller
// always passes lmmode=0 and this path is byte-identical to the old code.
// Returns the advanced src address for the register update below.
static u32 ch2_copy_ram_to_vram(u32 src, u32 dst, u32 len, u32 lmmode)
{
	u32 vram_dst = (dst & 0x00FFFFFFu) | (lmmode ? 0xA5000000u : 0xA4000000u);

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

// ---------------------------------------------------------------------------
// DMAC_Ch2St - Channel 2 DMA: feeds the PowerVR TA / VRAM
// Called when SB_C2DST is written to trigger a Ch2 transfer.
// ---------------------------------------------------------------------------
void DMAC_Ch2St()
{
	const u32 dmaor  = DMAC_DMAOR.full;
	const u32 dst    = SB_C2DSTAT;
	u32       src    = DMAC_SAR[2];
	u32       len    = SB_C2DLEN;

	// Validate DMAOR: must have DME set and DDT mode, no error flags
	if ((dmaor & DMAOR_MASK) != 0x8201u)
	{
		printf("DMAC: Ch2 abort - DMAOR invalid (0x%08X)\n", dmaor);
		return;
	}

	// SB_C2DLEN must be a multiple of 32 bytes
	if (len & 0x1Fu)
	{
		printf("DMAC: Ch2 abort - SB_C2DLEN not 32-byte aligned (0x%X)\n", len);
		return;
	}

	// Guard against zero-length transfer
	if (len == 0)
	{
		printf("DMAC: Ch2 abort - SB_C2DLEN is zero\n");
		return;
	}

	//printf("DMAC: Ch2 DMA  SRC=0x%08X  DST=0x%08X  LEN=0x%X\n", src, dst, len);

	// --- Transfer to TA (display list) address space ---
	if (isDstTA(dst))
	{
		while (len)
		{
			const u32 p_addr  = src & RAM_MASK;
			const u32 avail   = RAM_SIZE - p_addr;

			if (avail < len)
			{
				// Transfer up to the RAM wrap boundary, then loop
				u32 *sys_buf = (u32 *)GetMemPtr(src, avail);
				if (sys_buf)
					TAWrite(dst, sys_buf, avail / 32);
				len -= avail;
				src += avail;
			}
			else
			{
				u32 *sys_buf = (u32 *)GetMemPtr(src, len);
				if (sys_buf)
					TAWrite(dst, sys_buf, len / 32);
				src += len;
				break;
			}
		}
	}
	// --- Transfer to VRAM via LMMODE0 window (0x11xxxxxx) ---
	else if (isDstVRAM_LM0(dst))
	{
		u32 lmmode = LMMODE_FIX() ? (SB_LMMODE0 & 1) : 0;
		if (get_debug_message())
			printf("DMAC: Ch2 VRAM DST=0x%08X LEN=0x%X LMMODE0=%d (bus: %s)\n",
			       dst, len, (int)SB_LMMODE0, lmmode ? "32-bit" : "64-bit");
		src = ch2_copy_ram_to_vram(src, dst, len, lmmode);
	}
	// --- Transfer to VRAM via LMMODE1 window (0x13xxxxxx) ---
	else if (isDstVRAM_LM1(dst))
	{
		if (LMMODE_FIX())
		{
			u32 lmmode = SB_LMMODE1 & 1;
			if (get_debug_message())
				printf("DMAC: Ch2 VRAM DST=0x%08X LEN=0x%X LMMODE1=%d (bus: %s)\n",
				       dst, len, (int)SB_LMMODE1, lmmode ? "32-bit" : "64-bit");
			src = ch2_copy_ram_to_vram(src, dst, len, lmmode);
		}
		else
		{
			// Legacy: LMMODE1 path not implemented; advance src to keep state consistent
			printf("DMAC: Ch2 LMMODE1 transfer (stub) DST=0x%08X LEN=0x%X\n", dst, len);
			src += len;
		}
	}
	else
	{
		printf("DMAC: Ch2 invalid SB_C2DSTAT address (0x%08X)\n", dst);
		src += len;
	}

	// --- Update registers to reflect completed transfer ---
	DMAC_SAR[2]       = src;
	DMAC_CHCR[2].full &= ~0x1u;   // clear DE (channel enable)
	DMAC_DMATCR[2]    = 0x00000000u;

	SB_C2DST          = 0x00000000u;
	SB_C2DLEN         = 0x00000000u;
	SB_C2DSTAT        = src;

	// Raise Ch2-DMA end interrupt (ISTNRM bit 19: DTDE2INT)
	asic_RaiseInterrupt(holly_CH2_DMA);
}

// ---------------------------------------------------------------------------
// On-demand data transfer stubs (not yet implemented)
// ---------------------------------------------------------------------------
void dmac_ddt_ch0_ddt(u32 src, u32 dst, u32 count)
{
	// TODO: Ch0 on-demand data transfer
	(void)src; (void)dst; (void)count;
}

void dmac_ddt_ch2_direct(u32 dst, u32 count)
{
	// TODO: Ch2 direct data transfer (DDT mode)
	(void)dst; (void)count;
}

// ---------------------------------------------------------------------------
// UpdateDMA - cycle-accurate DMA tick (not yet implemented)
// ---------------------------------------------------------------------------
void UpdateDMA()
{
	// TODO: implement cycle-accurate / timesliced DMA
	// Requirements:
	//   - DMAOR.DME must be set and DMAOR.AE must be clear
	//   - DMAOR.DDT must be 1 for DDT mode
	//   - For each active channel (CHCR[ch].DE==1 && CHCR[ch].TE==0):
	//       RS field must be in range 0x0-0x7 for normal mode, 0x0-0x3 for some modes
	//   - Transfer in 22 KB chunks (704 x 32 bytes = 22528 bytes)
}

// ---------------------------------------------------------------------------
// CHCR / DMAOR write handlers (used as callbacks in the register map)
// ---------------------------------------------------------------------------
template<u32 ch>
static void WriteCHCR(u32 data)
{
	DMAC_CHCR[ch].full = data;
	//printf("DMAC: Write CHCR%u = 0x%08X\n", ch, data);
}

static void WriteDMAOR(u32 data)
{
	DMAC_DMAOR.full = data;
	//printf("DMAC: Write DMAOR = 0x%08X\n", data);
}

// ---------------------------------------------------------------------------
// dmac_Init - register all DMAC MMIO registers into the SH4 register map
// Registers span 0xFFA00000 (P4) / 0x1FA00000 (physical), 4-byte aligned.
// ---------------------------------------------------------------------------

// Convenience macro to reduce the repetitive register-map boilerplate
#define DMAC_REG_RW(addr, ptr) \
	do { \
		DMAC[((addr) & 0xFF) >> 2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA; \
		DMAC[((addr) & 0xFF) >> 2].readFunction  = 0; \
		DMAC[((addr) & 0xFF) >> 2].writeFunction = 0; \
		DMAC[((addr) & 0xFF) >> 2].data32        = (ptr); \
	} while(0)

#define DMAC_REG_CHCR(addr, ptr, fn) \
	do { \
		DMAC[((addr) & 0xFF) >> 2].flags         = REG_32BIT_READWRITE | REG_READ_DATA; \
		DMAC[((addr) & 0xFF) >> 2].readFunction  = 0; \
		DMAC[((addr) & 0xFF) >> 2].writeFunction = (fn); \
		DMAC[((addr) & 0xFF) >> 2].data32        = (ptr); \
	} while(0)

void dmac_Init()
{
	// Channel 0
	DMAC_REG_RW  (DMAC_SAR0_addr,   &DMAC_SAR[0]);
	DMAC_REG_RW  (DMAC_DAR0_addr,   &DMAC_DAR[0]);
	DMAC_REG_RW  (DMAC_DMATCR0_addr,&DMAC_DMATCR[0]);
	DMAC_REG_CHCR(DMAC_CHCR0_addr,  &DMAC_CHCR[0].full, WriteCHCR<0>);

	// Channel 1
	DMAC_REG_RW  (DMAC_SAR1_addr,   &DMAC_SAR[1]);
	DMAC_REG_RW  (DMAC_DAR1_addr,   &DMAC_DAR[1]);
	DMAC_REG_RW  (DMAC_DMATCR1_addr,&DMAC_DMATCR[1]);
	DMAC_REG_CHCR(DMAC_CHCR1_addr,  &DMAC_CHCR[1].full, WriteCHCR<1>);

	// Channel 2
	DMAC_REG_RW  (DMAC_SAR2_addr,   &DMAC_SAR[2]);
	DMAC_REG_RW  (DMAC_DAR2_addr,   &DMAC_DAR[2]);
	DMAC_REG_RW  (DMAC_DMATCR2_addr,&DMAC_DMATCR[2]);
	DMAC_REG_CHCR(DMAC_CHCR2_addr,  &DMAC_CHCR[2].full, WriteCHCR<2>);

	// Channel 3
	DMAC_REG_RW  (DMAC_SAR3_addr,   &DMAC_SAR[3]);
	DMAC_REG_RW  (DMAC_DAR3_addr,   &DMAC_DAR[3]);
	DMAC_REG_RW  (DMAC_DMATCR3_addr,&DMAC_DMATCR[3]);
	DMAC_REG_CHCR(DMAC_CHCR3_addr,  &DMAC_CHCR[3].full, WriteCHCR<3>);

	// DMAOR (global DMA operation register)
	DMAC[((DMAC_DMAOR_addr) & 0xFF) >> 2].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
	DMAC[((DMAC_DMAOR_addr) & 0xFF) >> 2].readFunction  = 0;
	DMAC[((DMAC_DMAOR_addr) & 0xFF) >> 2].writeFunction = WriteDMAOR;
	DMAC[((DMAC_DMAOR_addr) & 0xFF) >> 2].data32        = &DMAC_DMAOR.full;
}

#undef DMAC_REG_RW
#undef DMAC_REG_CHCR

// ---------------------------------------------------------------------------
// dmac_Reset - reset all channel registers to power-on state
// ---------------------------------------------------------------------------
void dmac_Reset(bool Manual)
{
	for (int ch = 0; ch < 4; ch++)
	{
		// SAR/DAR/DMATCR are "undefined" after reset per SH4 manual — leave them
		DMAC_CHCR[ch].full = 0x00000000u;
	}
	DMAC_DMAOR.full = 0x00000000u;
}

// ---------------------------------------------------------------------------
// dmac_Term - nothing to release currently
// ---------------------------------------------------------------------------
void dmac_Term()
{
}
