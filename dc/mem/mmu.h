#pragma once
#include "types.h"
#include "dc/sh4/ccn.h"

// TLB entry: pairs a virtual address (PTEH) with its physical mapping (PTEL)
struct TLB_Entry
{
	CCN_PTEH_type Address;  // Virtual Page Number + ASID
	CCN_PTEL_type Data;     // Physical Page Number + protection bits
};

// SH4 Unified TLB: 64 entries for data + instruction translation
extern TLB_Entry UTLB[64];

// SH4 Instruction TLB: 4 entries, dedicated for instruction fetches
extern TLB_Entry ITLB[4];

// Store Queue fast remap table: caches physical addresses for 0xE0000000-0xE3FFFFFF
// Each entry covers a 1MB region (64 entries * 1MB = 64MB max SQ space)
extern u32 sq_remap[64];

// Sync a single UTLB entry into the fast remap tables
// entry: index 0-63; call after writing a new UTLB entry
void UTLB_Sync(u32 entry);

// Sync a single ITLB entry (currently logs only)
// entry: index 0-3
void ITLB_Sync(u32 entry);

void MMU_Init();
void MMU_Reset(bool Manual);
void MMU_Term();

// Translate a Store Queue write address using the fast remap table.
// adr: virtual address in 0xE0000000-0xE3FFFFFF range
// Returns the physical address, preserving the low 20 bits (page offset).
// The index into sq_remap is bits [25:20] of the address (6 bits -> 64 slots).
#define mmu_TranslateSQW(adr) (sq_remap[((adr) >> 20) & 0x3F] | ((adr) & 0xFFFFF))

// ---------------------------------------------------------------------------
// Full virtual memory translation (P0/U0 and P3 address spaces only).
//
// Needed for Windows CE-based Dreamcast titles: unlike most Katana-SDK
// games (which run with MMUCR.AT=0 and access RAM directly through the
// always-physical P1/P2 windows), WinCE enables AT=1 and relies on the SH4
// TLB-miss/protection-violation exceptions to drive its own page tables.
// ---------------------------------------------------------------------------

enum MMU_AccessType { MMU_ACCESS_READ, MMU_ACCESS_WRITE };

// True if 'vaddr' is currently subject to TLB translation, i.e. MMUCR.AT==1
// and the address falls in P0/U0 (0x00000000-0x7FFFFFFF) or P3
// (0xC0000000-0xDFFFFFFF). P1, P2 and P4 are always physically mapped
// regardless of AT, exactly like on real hardware.
static inline bool mmu_NeedsTranslation(u32 vaddr)
{
	if (!CCN_MMUCR.AT)
		return false;
	return (vaddr < 0x80000000u) || ((vaddr & 0xE0000000u) == 0xC0000000u);
}

// Looks up 'vaddr' in the UTLB and, on success, writes the translated
// physical address to 'paddr' and returns true.
// On TLB miss / protection violation / initial-page-write, raises the
// matching SH4 exception (sets TEA/PTEH, redirects next_pc to the
// handler via Do_Exeption) and returns false. The caller must abandon the
// access without performing it; the faulting instruction will be retried
// once the guest exception handler fixes up the page tables.
bool mmu_TranslateAddress(u32 vaddr, u32& paddr, MMU_AccessType access);
