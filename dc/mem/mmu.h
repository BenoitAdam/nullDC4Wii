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

// Invalidate all ITLB/UTLB entries and the derived sq_remap table (MMUCR.TI)
void mmu_InvalidateTLB();

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
