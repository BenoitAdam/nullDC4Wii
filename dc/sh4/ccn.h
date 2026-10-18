#pragma once

#include "types.h"

// ============================================================
// CCN: SH4 Cache and TLB Controller
// Emulates the on-chip MMU and cache control registers
// of the Hitachi SH7091 (SH4) processor used in the Dreamcast.
// ============================================================

void ccn_Init();
void ccn_Reset(bool Manual);
void ccn_Term();

// PTEH — Page Table Entry High (0xFF000000)
//   VPN : Virtual Page Number (bits 31-10)
//   ASID: Address Space Identifier (bits 7-0)
union CCN_PTEH_type
{
    struct {
#if HOST_ENDIAN == ENDIAN_LITTLE
        u32 ASID : 8;   // bits  7- 0 — Address Space ID
        u32 res  : 2;   // bits  9- 8 — reserved
        u32 VPN  : 22;  // bits 31-10 — Virtual Page Number
#else
        u32 VPN:22; //10-31 VPN
        u32 res:2;  //8,9 reserved
		    u32 ASID:8; //0-7 ASID
#endif
    };
    u32 reg_data;
};

// PTEL — Page Table Entry Low (0xFF000004)
//   PPN: Physical Page Number (bits 28-10)
//   PR : Protection bits, SZ: page size, C: cacheable,
//   D  : dirty, SH: shared, WT: write-through, V: valid
union CCN_PTEL_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 WT:1;
		u32 SH:1;
		u32 D :1;
		u32 C :1;
		
		u32 SZ0:1;
		u32 PR :2;
		u32 SZ1:1;

		u32 V:1;
		u32 res_0:1;
		u32 PPN:19;//PPN 10-28
		u32 res_1:3;
#else
		u32 res_1:3;
		u32 PPN:19;//PPN 10-28
		u32 res_0:1;
		u32 V:1;

		u32 SZ1:1;
		u32 PR :2;
		u32 SZ0:1;

		u32 C :1;
		u32 D :1;
		u32 SH:1;
		u32 WT:1;
#endif
	};
	u32 reg_data;
};

// MMUCR — MMU Control Register (0xFF000010)
//   AT  : Address Translation enable (0=off, 1=on)
//   TI  : TLB Invalidate — self-clearing on write
//   SV  : Single Virtual mode
//   SQMD: Store Queue Mode
//   URC/URB/LRUI: TLB replacement control
//
// WRONG ON BIG ENDIAN: the fields below sum to 28 bits, not 32 -- the manual's
// two reserved gaps (bits 17-16 and 25-24) are missing. GCC on PowerPC fills
// from bit 31 down, so only LRUI lands on its real bits; AT reads bit 4, TI
// bit 6, SV bit 12, URC bits 19-14 (confirmed from devkitPPC -S output). Read
// the fields with the MMUCR_* masks / mmucr_*() accessors further down, never
// the members. Kept for reg_data and for MMUCR_FIX=0 (legacy A/B).
union CCN_MMUCR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 AT:1;
		u32 res:1;
		u32 TI:1;
		u32 res_2:5;
		u32 SV:1;
		u32 SQMD:1;
		u32 URC:6;
		u32 URB:6;
		u32 LRUI:6;
#else
		u32 LRUI:6;
		u32 URB:6;
		u32 URC:6;
		u32 SQMD:1;
		u32 SV:1;
		u32 res_2:5;
		u32 TI:1;
		u32 res:1;
		u32 AT:1;
#endif
	};
	u32 reg_data;
};

union CCN_PTEA_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 SA:3;
		u32 TC:1;
		u32 res:28;
#else
		u32 res:28;
		u32 TC:1;
		u32 SA:3;
#endif
	};
	u32 reg_data;
};

// CCR — Cache Control Register (0xFF00001C)
//   OCE/ICE : Operand/Instruction cache enable
//   OCI/ICI : Cache Invalidate — self-clearing on write
//   ORA     : OC used as RAM
//   OIX/IIX : Index mode selectors
//   WT/CB   : Write-through / Copy-back
union CCN_CCR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 OCE:1;
		u32 WT:1;
		u32 CB:1;
		u32 OCI:1;
		u32 res:1;
		u32 ORA:1;
		u32 res_1:1;
		u32 OIX:1;
		u32 ICE:1;
		u32 res_2:2;
		u32 ICI:1;
		u32 res_3:3;
		u32 IIX:1;
		u32 res_4:16;
#else
		u32 res_4:16;
		u32 IIX:1;
		u32 res_3:3;
		u32 ICI:1;
		u32 res_2:2;
		u32 ICE:1;
		u32 OIX:1;
		u32 res_1:1;
		u32 ORA:1;
		u32 res:1;
		u32 OCI:1;
		u32 CB:1;
		u32 WT:1;
		u32 OCE:1;
#endif
	};
	u32 reg_data;
};

union CCN_QACR_type
{
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 res:2;
		u32 Area:3;
		u32 res_1:27;
#else
		u32 res_1:27;
		u32 Area:3;
		u32 res:2;
#endif
	};
	u32 reg_data;
};


//Types
extern CCN_PTEH_type CCN_PTEH;
extern CCN_PTEL_type CCN_PTEL;
extern u32 CCN_TTB;
extern u32 CCN_TEA;
extern CCN_MMUCR_type CCN_MMUCR;

// MMUCR fields at the manual's bits (see the bitfield warning above).
#define MMUCR_AT(v)    ((v) & 1u)
#define MMUCR_TI(v)    (((v) >> 2) & 1u)
#define MMUCR_SV(v)    (((v) >> 8) & 1u)
#define MMUCR_SQMD(v)  (((v) >> 9) & 1u)
#define MMUCR_URC(v)   (((v) >> 10) & 63u)
#define MMUCR_URB(v)   (((v) >> 18) & 63u)
#define MMUCR_LRUI(v)  (((v) >> 26) & 63u)
#define MMUCR_TI_BIT   0x00000004u
// The bits a write keeps (Flycast CCN_MMUCR_write).
#define MMUCR_WMASK    0xFCFCFF05u

// MMUCR_FIX preset (wii/main.cpp): 0 = legacy bitfield reads, 1 = mask reads +
// Flycast write handler, 2 = 1 + the TLB rules that go with it (mmu.cpp,
// sh4_internal_reg.cpp). Ported from flycastPS3 rez_fix.md.
extern "C" int get_mmucr_fix_preset();

static inline u32 mmucr_at()  { return get_mmucr_fix_preset() ? MMUCR_AT(CCN_MMUCR.reg_data)  : (u32)CCN_MMUCR.AT;  }
static inline u32 mmucr_urc() { return get_mmucr_fix_preset() ? MMUCR_URC(CCN_MMUCR.reg_data) : (u32)CCN_MMUCR.URC; }
static inline u32 mmucr_urb() { return get_mmucr_fix_preset() ? MMUCR_URB(CCN_MMUCR.reg_data) : (u32)CCN_MMUCR.URB; }

extern u8 CCN_BASRA;
extern u8 CCN_BASRB;
extern CCN_CCR_type CCN_CCR;
extern u32 CCN_TRA;
extern u32 CCN_EXPEVT;
extern u32 CCN_INTEVT;
extern CCN_PTEA_type CCN_PTEA;
extern CCN_QACR_type CCN_QACR0;
extern CCN_QACR_type CCN_QACR1;
