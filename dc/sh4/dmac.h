#pragma once
// dmac.h - SH4 DMAC register types and public interface
// Supports both little-endian (x86) and big-endian (Wii/PPC) hosts via
// HOST_ENDIAN conditional bit-field ordering.

#include "types.h"

// ---------------------------------------------------------------------------
// DMAC_CHCR_type - Channel Control Register (one per channel, 4 channels)
// Reset value: 0x00000000
// ---------------------------------------------------------------------------
union DMAC_CHCR_type
{
	struct
	{
#if HOST_ENDIAN == ENDIAN_LITTLE
		u32 DE   : 1;   // [0]     Channel Enable
		u32 TE   : 1;   // [1]     Transfer End (set by hardware when done)
		u32 IE   : 1;   // [2]     Interrupt Enable
		u32 res0 : 1;   // [3]     reserved

		u32 TS   : 3;   // [6:4]   Transfer (word) Size: 000=8b 001=16b 010=32b 011=32bx4 100=32bx8
		u32 TM   : 1;   // [7]     Transfer Mode: 0=cycle-steal, 1=burst

		u32 RS   : 4;   // [11:8]  Resource Select (DMA request source)

		u32 SM   : 2;   // [13:12] Source address Mode: 00=fixed 10=increment 11=decrement
		u32 DM   : 2;   // [15:14] Destination address Mode: same encoding as SM

		u32 AL   : 1;   // [16]    Acknowledge Level
		u32 AM   : 1;   // [17]    Acknowledge Mode
		u32 RL   : 1;   // [18]    Request Level (CHCR0/1 only in normal mode)
		u32 DS   : 1;   // [19]    DREQ Select (CHCR0/1 normal; CHCR0-3 DDT)

		u32 res1 : 4;   // [23:20] reserved

		u32 DTC  : 1;   // [24]    Destination Address Transfer Counter enable
		u32 DSA  : 3;   // [27:25] Destination Space Attribute

		u32 STC  : 1;   // [28]    Source Address Transfer Counter enable
		u32 SSA  : 3;   // [31:29] Source Space Attribute
#else   // big-endian (Wii / PowerPC)
		u32 SSA  : 3;
		u32 STC  : 1;

		u32 DSA  : 3;
		u32 DTC  : 1;

		u32 res1 : 4;

		u32 DS   : 1;
		u32 RL   : 1;
		u32 AM   : 1;
		u32 AL   : 1;

		u32 DM   : 2;
		u32 SM   : 2;

		u32 RS   : 4;

		u32 TM   : 1;
		u32 TS   : 3;

		u32 res0 : 1;
		u32 IE   : 1;
		u32 TE   : 1;
		u32 DE   : 1;
#endif
	};
	u32 full;
};

// ---------------------------------------------------------------------------
// DMAC_DMAOR_type - DMA Operation Register (global, shared by all channels)
// Reset value: 0x00000000
// ---------------------------------------------------------------------------
union DMAC_DMAOR_type
{
	struct
	{
#if HOST_ENDIAN == ENDIAN_LITTLE
		u32 DME  : 1;   // [0]     DMA Master Enable (0=all channels disabled)
		u32 NMIF : 1;   // [1]     NMI Flag (set on NMI while DMA active; write 0 to clear)
		u32 AE   : 1;   // [2]     Address Error flag (set on address error; write 0 to clear)
		u32 res0 : 1;   // [3]     reserved

		u32 COD  : 1;   // [4]     Channel On Demand (used in DDT mode)
		u32 res1 : 3;   // [7:5]   reserved

		u32 PR0  : 1;   // [8]     Priority: 00=round-robin, 01=ch0>1>2>3, 10=ch2>0>1>3
		u32 PR1  : 1;   // [9]
		u32 res2 : 2;   // [11:10] reserved

		u32 res3 : 3;   // [14:12] reserved
		u32 DDT  : 1;   // [15]    DDT (On-Demand Data Transfer) mode enable

		u32 res4 : 16;  // [31:16] reserved
#else
		u32 res4 : 16;

		u32 DDT  : 1;
		u32 res3 : 3;

		u32 res2 : 2;
		u32 PR1  : 1;
		u32 PR0  : 1;

		u32 res1 : 3;
		u32 COD  : 1;

		u32 res0 : 1;
		u32 AE   : 1;
		u32 NMIF : 1;
		u32 DME  : 1;
#endif
	};
	u32 full;
};

// ---------------------------------------------------------------------------
// DMAC register arrays (defined in dmac.cpp)
// ---------------------------------------------------------------------------
extern u32             DMAC_SAR[4];
extern u32             DMAC_DAR[4];
extern u32             DMAC_DMATCR[4];   // lower 24 bits valid per SH4 manual
extern DMAC_CHCR_type  DMAC_CHCR[4];
extern DMAC_DMAOR_type DMAC_DMAOR;

// ---------------------------------------------------------------------------
// DMAC_DMAOR valid-bits mask
// Bits that matter when checking DMAOR for a valid Ch2-DMA trigger:
//   bit 15 (DDT=1), bit 3 (DME=1), and no error flags
// ---------------------------------------------------------------------------
#define DMAOR_MASK  0xFFFF8201u

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// On-demand / direct transfer (DDT mode) — stubs, not yet implemented
void dmac_ddt_ch0_ddt(u32 src, u32 dst, u32 count);
void dmac_ddt_ch2_direct(u32 dst, u32 count);

// Trigger a Ch2 (PVR/TA) DMA transfer — called when SB_C2DST is written
void DMAC_Ch2St();

// Lifecycle
void dmac_Init();               // register MMIO handlers
void dmac_Reset(bool Manual);   // reset registers to power-on state
void dmac_Term();               // teardown (currently a no-op)

// Cycle-accurate DMA tick (called from the main emulation loop, not yet implemented)
void UpdateDMA();

// Claude Test for Rez Fix - disproven: deferred Ch2-DMA completion IRQ was
// tested (even at a 65536-cycle delay) to see whether IRQ timing was why a
// guest driver never observed a transfer as complete. It made no observable
// difference, so DMAC_Ch2St() raises the completion IRQ synchronously again
// (matching the verified-working PSP port) and this hook is unused.
// void UpdateCh2DmaIrq(u32 cycles);
