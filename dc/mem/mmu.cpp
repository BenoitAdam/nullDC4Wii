#include "mmu.h"
#include "dc/sh4/sh4_if.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "plugins/plugin_manager.h"

#include "_vmem.h"

// Store Queue fast remap table.
// Maps 1MB virtual SQ regions (0xE0000000–0xE3FFFFFF) to physical page bases.
// Index = bits [25:20] of the virtual address (max 64 regions * 1MB = 64MB).
u32 sq_remap[64];

// Unified TLB: 64 entries (data + instructions)
TLB_Entry UTLB[64];

// Instruction TLB: 4 dedicated entries for instruction fetches
TLB_Entry ITLB[4];


// Sync a UTLB entry into the emulator's fast lookup structures.
// For SQ addresses (0xE0xx_xxxx), updates the sq_remap fast-path table.
// For normal addresses, this is where you would invalidate JIT blocks or
// update software page tables (not yet implemented).
void UTLB_Sync(u32 entry)
{
	if (entry >= 64)
	{
		printf("UTLB_Sync: entry %u out of range\n", entry);
		return;
	}

	// Check if the VPN falls in the Store Queue region (0xE0000000–0xE3FFFFFF).
	// The SQ region occupies the top 4 bits = 0xE, and bit 25 may vary per sub-region.
	// We mask off the top 6 bits (0xFC000000>>10 in VPN space) and compare to 0xE0.
	if ((UTLB[entry].Address.VPN & (0xFC000000 >> 10)) == (0xE0000000 >> 10))
	{
		// Extract the 1MB slot index from bits [25:20] of the virtual address.
		// VPN is the virtual address right-shifted by 10, so bits [15:10] of VPN
		// correspond to address bits [25:20]. Mask to 6 bits for 64 slots.
		u32 sq_index = (UTLB[entry].Address.VPN >> 10) & 0x3F;
		sq_remap[sq_index] = UTLB[entry].Data.PPN << 10;

		printf("SQ remap [%u] slot=%u : virt=0x%08X -> phys=0x%08X\n",
		       entry, sq_index,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10);
	}
	else
	{
		// Normal virtual memory mapping.
		// TODO: invalidate any JIT-compiled blocks covering this virtual range.
		printf("MEM remap [%u] : virt=0x%08X -> phys=0x%08X\n",
		       entry,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10);
	}
}

// Sync an ITLB entry. Currently only logs; extend to flush instruction caches
// or invalidate JIT blocks covering the affected virtual range as needed.
void ITLB_Sync(u32 entry)
{
	if (entry >= 4)
	{
		printf("ITLB_Sync: entry %u out of range\n", entry);
		return;
	}

	printf("ITLB remap [%u] : virt=0x%08X -> phys=0x%08X\n",
	       entry,
	       ITLB[entry].Address.VPN << 10,
	       ITLB[entry].Data.PPN << 10);
}

void MMU_Init()
{
	// Zero all TLB entries and SQ remap table on startup.
	memset(UTLB,    0, sizeof(UTLB));
	memset(ITLB,    0, sizeof(ITLB));
	memset(sq_remap, 0, sizeof(sq_remap));
}

void MMU_Reset(bool Manual)
{
	// On reset (power-on or manual), clear all translation state.
	// This matches SH4 hardware behaviour: TLBs are undefined after reset
	// and software must repopulate them.
	memset(UTLB,    0, sizeof(UTLB));
	memset(ITLB,    0, sizeof(ITLB));
	memset(sq_remap, 0, sizeof(sq_remap));

	if (Manual)
		printf("MMU: manual reset\n");
	else
		printf("MMU: power-on reset\n");
}

void MMU_Term()
{
	// Nothing to free (all storage is statically allocated).
}

// ---------------------------------------------------------------------------
// Full virtual memory translation (see mmu.h for the rationale).
// ---------------------------------------------------------------------------

// SZ1:SZ0 -> page byte-mask (low bits that are NOT part of the page compare).
// Matches the SH7091 PTEL page-size encoding: 00=1KB, 01=4KB, 10=64KB, 11=1MB.
static inline u32 PageMask(const CCN_PTEL_type& pte)
{
	switch ((pte.SZ1 << 1) | pte.SZ0)
	{
	case 0: return 0x000003FF; // 1KB
	case 1: return 0x00000FFF; // 4KB
	case 2: return 0x0000FFFF; // 64KB
	default: return 0x000FFFFF; // 1MB
	}
}

bool mmu_TranslateAddress(u32 vaddr, u32& paddr, MMU_AccessType access)
{
	const bool is_write = (access == MMU_ACCESS_WRITE);
	const u8 asid = CCN_PTEH.ASID;

	for (u32 i = 0; i < 64; i++)
	{
		const TLB_Entry& e = UTLB[i];
		if (!e.Data.V)
			continue;

		const u32 mask = PageMask(e.Data);
		if (((e.Address.VPN << 10) & ~mask) != (vaddr & ~mask))
			continue;
		if (!e.Data.SH && !CCN_MMUCR.SV && e.Address.ASID != asid)
			continue;

		// Matching entry found - check access permissions.
		// PR: 0=RO/priv, 1=RW/priv, 2=RO/priv+user, 3=RW/priv+user
		const bool user_page    = (e.Data.PR == 2 || e.Data.PR == 3);
		const bool writable_page = (e.Data.PR == 1 || e.Data.PR == 3);

		if (!sr.MD && !user_page)
		{
			// Privileged-only page accessed from user mode: no access at all.
			CCN_TEA = vaddr;
			CCN_PTEH.VPN = vaddr >> 10;
			Do_Exeption(curr_pc,
				is_write ? 0x0c0 /*DATA_TLB_PROTECTION_VIOLATION_WRITE*/
				         : 0x0a0 /*DATA_TLB_PROTECTION_VIOLATION_READ*/,
				0x100);
			return false;
		}

		if (is_write && !writable_page)
		{
			CCN_TEA = vaddr;
			CCN_PTEH.VPN = vaddr >> 10;
			Do_Exeption(curr_pc, 0x0c0 /*DATA_TLB_PROTECTION_VIOLATION_WRITE*/, 0x100);
			return false;
		}

		if (is_write && !e.Data.D)
		{
			// First write to a clean page: let the guest OS set the dirty bit.
			CCN_TEA = vaddr;
			CCN_PTEH.VPN = vaddr >> 10;
			Do_Exeption(curr_pc, 0x080 /*INITAL_PAGE_WRITE*/, 0x100);
			return false;
		}

		paddr = (e.Data.PPN << 10) | (vaddr & mask);
		return true;
	}

	// No matching entry: TLB miss. Both instruction fetches and data reads
	// share EXPEVT 0x040 on real hardware too (the OS handler disambiguates
	// them itself), so a single UTLB miss path covers both here.
	CCN_TEA = vaddr;
	CCN_PTEH.VPN = vaddr >> 10;
	Do_Exeption(curr_pc,
		is_write ? 0x060 /*DATA_TLB_MISS_WRITE*/ : 0x040 /*DATA_TLB_MISS_READ / INSTRUCTION_TLB_MISS*/,
		0x400);
	return false;
}
