/*
	TMU - Timer Unit (SH4)
	Three independent countdown timers used for OS scheduling, audio sync,
	and general game timing on the Sega Dreamcast.

	Each channel counts down from TCNT toward zero using a prescaled clock
	derived from the peripheral clock (Pclk). On underflow, TCNT is reloaded
	from TCOR and an interrupt is optionally raised.

	Clock prescaler options (TCR bits [2:0]):
	  0 -> Pclk/4    (shift 2)
	  1 -> Pclk/16   (shift 4)
	  2 -> Pclk/64   (shift 6)
	  3 -> Pclk/256  (shift 8)
	  4 -> Pclk/1024 (shift 10)
	  5 -> reserved
	  6 -> RTC input (unsupported on DC)
	  7 -> external  (unsupported on DC)

	Note: We accumulate in CPU (Icyc) cycles, so an extra +2 shift is applied
	to account for the Icyc-to-Pclk ratio.
*/

#include "types.h"
#include "tmu.h"
#include "intc.h"
#include "dc/mem/sh4_internal_reg.h"

// TCR register bit definitions
#define TMU_TCR_UNF		0x0100	// Underflow flag
#define TMU_TCR_UNIE	0x0020	// Underflow interrupt enable
#define TMU_TCR_TPSC	0x0007	// Timer prescaler select mask

// TSTR channel enable bits
static const u32 tmu_ch_bit[3] = { 1, 2, 4 };

// Interrupt IDs for each channel
static const InterruptID tmu_intID[3] = {
	sh4_TMU0_TUNI0,
	sh4_TMU1_TUNI1,
	sh4_TMU2_TUNI2
};

// Per-channel state
static u32  tmu_prescaler[3];		// Accumulated sub-step cycles
static u32  tmu_prescaler_shift[3];	// Bit-shift for current prescale factor
static u32  tmu_prescaler_mask[3];	// Mask to retain sub-step remainder
static u32  tmu_old_mode[3] = { 0xFFFF, 0xFFFF, 0xFFFF }; // Cached TCR prescaler bits

// Memory-mapped register backing store
u32  tmu_regs_CNT[3];	// TCNT - current countdown value
u32  tmu_regs_COR[3];	// TCOR - reload value on underflow
u16  tmu_regs_CR[3];	// TCR  - control/status register
u8   TMU_TOCR;			// Timer output control register
u8   TMU_TSTR;			// Timer start register (channel enable bits)

// ---------------------------------------------------------------------------
// UpdateTMU_chan<ch>
//   Advance channel ch by clc CPU cycles.
//   Handles single and multiple underflows correctly.
// ---------------------------------------------------------------------------
template<u32 ch>
INLINE void UpdateTMU_chan(u32 clc)
{
	// Skip if channel is not running
	if ((TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return;

	tmu_prescaler[ch] += clc;
	u32 steps = tmu_prescaler[ch] >> tmu_prescaler_shift[ch];

	// Remove consumed sub-steps from accumulator
	tmu_prescaler[ch] &= tmu_prescaler_mask[ch];

	if (steps == 0)
		return;

	if (steps > tmu_regs_CNT[ch])
	{
		// At least one underflow occurred
		steps -= tmu_regs_CNT[ch];

		// Signal underflow interrupt
		tmu_regs_CR[ch] |= TMU_TCR_UNF;
		InterruptPend(tmu_intID[ch], 1);

		// Absorb any additional full-period underflows.
		// This is rare in practice (only if UpdateTMU is called very infrequently
		// relative to a very fast timer), but must be handled correctly.
		const u32 cor = tmu_regs_COR[ch];
		if (cor > 0 && steps > cor)
			steps %= cor;

		// Reload counter from TCOR
		tmu_regs_CNT[ch] = cor;
	}

	tmu_regs_CNT[ch] -= steps;
}

void UpdateTMU(u32 Cycles)
{
	UpdateTMU_chan<0>(Cycles);
	UpdateTMU_chan<1>(Cycles);
	UpdateTMU_chan<2>(Cycles);
}

// ---------------------------------------------------------------------------
// UpdateTMUCounts
//   Called whenever TCR is written. Syncs interrupt state and recalculates
//   the prescaler shift/mask when the prescaler mode changes.
// ---------------------------------------------------------------------------
void UpdateTMUCounts(u32 reg)
{
	// Sync interrupt pending/mask state to current TCR flags
	InterruptPend(tmu_intID[reg],  tmu_regs_CR[reg] & TMU_TCR_UNF);
	InterruptMask(tmu_intID[reg],  tmu_regs_CR[reg] & TMU_TCR_UNIE);

	const u32 mode = tmu_regs_CR[reg] & TMU_TCR_TPSC;
	if (tmu_old_mode[reg] == mode)
		return;
	tmu_old_mode[reg] = mode;

	u32 shift;
	switch (mode)
	{
		case 0: shift = 2;  break;	// Pclk / 4
		case 1: shift = 4;  break;	// Pclk / 16
		case 2: shift = 6;  break;	// Pclk / 64
		case 3: shift = 8;  break;	// Pclk / 256
		case 4: shift = 10; break;	// Pclk / 1024
		case 5:
			printf("TMU ch%u: TCR prescaler mode 5 is reserved\n", reg);
			return;
		case 6:
			printf("TMU ch%u: TCR mode RTC (6) not supported on Dreamcast\n", reg);
			return;
		case 7:
			printf("TMU ch%u: TCR mode External (7) not supported on Dreamcast\n", reg);
			return;
		default:
			return;
	}

	// +2 because we count in Icyc (CPU core) cycles; TMU is clocked from Pclk
	// and Icyc = 4 * Pclk on the DC's SH4 (200 MHz Icyc, 50 MHz Pclk).
	shift += 2;
	tmu_prescaler_shift[reg] = shift;
	tmu_prescaler_mask[reg]  = (1u << shift) - 1u;
	tmu_prescaler[reg]       = 0;
}

// ---------------------------------------------------------------------------
// TCR write handlers (one per channel — required by the register dispatch)
// ---------------------------------------------------------------------------
void TMU_TCR0_write(u32 data) { tmu_regs_CR[0] = (u16)data; UpdateTMUCounts(0); }
void TMU_TCR1_write(u32 data) { tmu_regs_CR[1] = (u16)data; UpdateTMUCounts(1); }
void TMU_TCR2_write(u32 data) { tmu_regs_CR[2] = (u16)data; UpdateTMUCounts(2); }

// ---------------------------------------------------------------------------
// TCPR2 - channel 2 input capture register.
// Not used / not connected on Dreamcast hardware.
// ---------------------------------------------------------------------------
u32 TMU_TCPR2_read()
{
	// EMUERROR("TMU_TCPR2 read — register not used on Dreamcast");
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("TMU_TCPR2 read — register not used on Dreamcast\n");
	return 0;
}

void TMU_TCPR2_write(u32 data)
{
	// EMUERROR2("TMU_TCPR2 write (data=0x%08X) — register not used on Dreamcast", data);
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("TMU_TCPR2 write (data=0x%08X) — register not used on Dreamcast\n", data);
}

// ---------------------------------------------------------------------------
// tmu_Init  — register memory-mapped I/O bindings
// ---------------------------------------------------------------------------
void tmu_Init()
{
	// Helper macro to reduce repetition in the register table setup
#define TMU_REG(addr, _flags, rfn, wfn, field) \
	do { \
		auto& r = TMU[(addr & 0xFF) >> 2]; \
		r.flags         = (_flags); \
		r.readFunction  = (rfn); \
		r.writeFunction = (wfn); \
		r.field; \
	} while(0)

	// TOCR - Timer output control (8-bit, R/W)
	TMU_REG(TMU_TOCR_addr,  REG_8BIT_READWRITE  | REG_READ_DATA | REG_WRITE_DATA, 0, 0, data8  = &TMU_TOCR);

	// TSTR - Timer start (8-bit, R/W)
	TMU_REG(TMU_TSTR_addr,  REG_8BIT_READWRITE  | REG_READ_DATA | REG_WRITE_DATA, 0, 0, data8  = &TMU_TSTR);

	// Channel 0
	TMU_REG(TMU_TCOR0_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_COR[0]);
	TMU_REG(TMU_TCNT0_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_CNT[0]);
	TMU_REG(TMU_TCR0_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR0_write, data16 = &tmu_regs_CR[0]);

	// Channel 1
	TMU_REG(TMU_TCOR1_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_COR[1]);
	TMU_REG(TMU_TCNT1_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_CNT[1]);
	TMU_REG(TMU_TCR1_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR1_write, data16 = &tmu_regs_CR[1]);

	// Channel 2
	TMU_REG(TMU_TCOR2_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_COR[2]);
	TMU_REG(TMU_TCNT2_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,              data32 = &tmu_regs_CNT[2]);
	TMU_REG(TMU_TCR2_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR2_write, data16 = &tmu_regs_CR[2]);

	// TCPR2 - input capture (not connected on DC)
	TMU_REG(TMU_TCPR2_addr, REG_32BIT_READWRITE, TMU_TCPR2_read, TMU_TCPR2_write, data32 = 0);

#undef TMU_REG
}

// ---------------------------------------------------------------------------
// tmu_Reset  — restore power-on defaults
// ---------------------------------------------------------------------------
void tmu_Reset(bool Manual)
{
	TMU_TOCR = TMU_TSTR = 0;

	for (int i = 0; i < 3; i++)
	{
		tmu_regs_COR[i] = 0xFFFFFFFF;
		tmu_regs_CNT[i] = 0xFFFFFFFF;
		tmu_regs_CR[i]  = 0;
		tmu_old_mode[i] = 0xFFFF;	// Force prescaler recalculation
		tmu_prescaler[i] = 0;
	}

	UpdateTMUCounts(0);
	UpdateTMUCounts(1);
	UpdateTMUCounts(2);
}

void tmu_Term()
{
	// Nothing to release; all state is statically allocated.
}
