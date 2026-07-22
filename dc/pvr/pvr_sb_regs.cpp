#include "types.h"
#include "dc/asic/asic.h"
#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/mem/sh4_mem.h"
#include "pvr_sb_regs.h"
#include "plugins/plugin_manager.h"

extern "C" int get_dma_fix_preset();

//==============================================================================
// Constants
//==============================================================================

// DMA alignment requirements
static const u32 DMA_ALIGNMENT_MASK = 0x1F;
static const u32 DMA_ALIGNMENT_SIZE = 32;

// Expected DMAOR configuration
static const u32 EXPECTED_DMAOR = 0x8201;

// Link list termination values
static const u32 LINK_ADDR_END = 1;
static const u32 LINK_ADDR_RESTART = 2;

// Link address offsets in list entry
static const u32 LINK_NEXT_OFFSET = 0x1C;
static const u32 LINK_SIZE_OFFSET = 0x18;

//==============================================================================
// Forward Declarations
//==============================================================================

static void do_pvr_dma();
static void pvr_do_sort_dma();
static u32 calculate_start_link_addr();
static inline bool validate_dma_config(u32 dmaor, u32 len);
static inline void complete_dma_transfer(u32 src, u32 len);

//==============================================================================
// Ch2 DMA Handler
//==============================================================================

void RegWrite_SB_C2DST(u32 data)
{
	if (data & 1)
	{
		SB_C2DST = 1;
		DMAC_Ch2St();
	}
}

//==============================================================================
// PVR DMA Implementation
//==============================================================================

/**
 * @brief Validate DMA configuration before transfer
 * @return true if configuration is valid, false otherwise
 *
 * DMA_FIX: PSP's do_pvr_dma only checks DMAOR — no length-alignment
 * requirement for PVR-DMA. The legacy extra 32-byte alignment check
 * (copy-pasted from the ch2-DMA validator, complete with ch2's "SB_C2DLEN"
 * wording in the error message — a tell that it was never written for this
 * channel) would silently reject ANY PVR-DMA whose length isn't 32-byte
 * aligned: a bare `return`, no error interrupt, no completion, nothing —
 * SB_PDST would stay stuck at 1 forever with no visible symptom. Skipped
 * when DMA_FIX is on, to match PSP exactly.
 */
static inline bool validate_dma_config(u32 dmaor, u32 len)
{
	// Check DMAOR settings
	if ((dmaor & DMAOR_MASK) != EXPECTED_DMAOR)
	{
		printf("\n!\tDMAC: DMAOR has invalid settings (0x%X) !\n", dmaor);
		return false;
	}

	// Legacy (pre-fix) bogus alignment check, kept for A/B comparison.
	if (!get_dma_fix_preset() && (len & DMA_ALIGNMENT_MASK))
	{
		printf("\n!\tDMAC: SB_C2DLEN not %u-byte aligned (0x%X) !\n", DMA_ALIGNMENT_SIZE, len);
		return false;
	}

	return true;
}

/**
 * @brief Complete DMA transfer and update registers
 */
static inline void complete_dma_transfer(u32 src, u32 len)
{
	// DMA_FIX: same bug class already found and fixed in DMAC_Ch2St()
	// (dc/sh4/dmac.cpp): TE (Transfer End) is how the guest learns the
	// transfer finished — a driver checking "channel enabled AND done"
	// before proceeding needs DE=1,TE=1 (real SH4 does not auto-clear DE).
	// The legacy code cleared DE and never set TE, so PVR-DMA completions
	// were invisible to any code that checked CHCR0 the same way. Matches
	// PSP exactly: only TE is set.
	if (get_dma_fix_preset())
	{
		DMAC_SAR[0]     = src + len;
		DMAC_CHCR[0].TE = 1;
		DMAC_DMATCR[0]  = 0;
	}
	else
	{
		DMAC_SAR[0]        = src + len;
		DMAC_CHCR[0].full &= 0xFFFFFFFEu;   // legacy bug: clear DE, never set TE
		DMAC_DMATCR[0]     = 0;
	}

	// Clear pending status
	SB_PDST = 0;

	// Raise interrupt to notify completion
	asic_RaiseInterrupt(holly_PVR_DMA);
}

/**
 * @brief Execute PVR DMA transfer
 * 
 * Transfers data between system RAM and PVR memory.
 * Direction is controlled by SB_PDDIR register.
 */
static void do_pvr_dma()
{
	const u32 dmaor = DMAC_DMAOR.full;
	const u32 src = SB_PDSTAR;
	const u32 dst = SB_PDSTAP;
	const u32 len = SB_PDLEN;

	// Validate configuration
	if (!validate_dma_config(dmaor, len))
	{
		return;
	}

	// Perform transfer based on direction
	if (SB_PDDIR)
	{
		// PVR -> System RAM
		// Must read from PVR memory with proper synchronization
		for (u32 offset = 0; offset < len; offset += 4)
		{
			u32 value = ReadMem32_nommu(dst + offset);
			WriteMem32_nommu(src + offset, value);
		}
	}
	else
	{
		// System RAM -> PVR
		// Optimized block write when possible
		u32* src_ptr = (u32*)GetMemPtr(src, len);
		if (src_ptr != NULL)
		{
			WriteMemBlock_nommu_ptr(dst, src_ptr, len);
		}
		else
		{
			// Fallback to word-by-word transfer
			for (u32 offset = 0; offset < len; offset += 4)
			{
				u32 value = ReadMem32_nommu(src + offset);
				WriteMem32_nommu(dst + offset, value);
			}
		}
	}

	// Complete the transfer
	complete_dma_transfer(src, len);
}

void RegWrite_SB_PDST(u32 data)
{
	if (data & 1)
	{
		SB_PDST = 1;
		do_pvr_dma();
	}
}

//==============================================================================
// Sort DMA Implementation
//==============================================================================

/**
 * @brief Calculate next link address from sort table
 * @return Next link address or termination value
 */
static u32 calculate_start_link_addr()
{
	const u8* base = &mem_b[SB_SDSTAW & RAM_MASK];
	u32 link_addr;

	if (SB_SDWLT == 0)
	{
		// 16-bit width mode
		const u16* table = (const u16*)base;
		link_addr = table[SB_SDDIV];
	}
	else
	{
		// 32-bit width mode
		const u32* table = (const u32*)base;
		link_addr = table[SB_SDDIV];
	}

	// Increment index for next read
	SB_SDDIV++;

	return link_addr;
}

/**
 * @brief Execute Sort DMA operation
 *
 * Processes linked list of display lists and submits them
 * to the Tile Accelerator. Used primarily by Windows CE games.
 *
 * DMA_FIX: LINK_ADDR_END (1) and LINK_ADDR_RESTART (2) were used with their
 * roles EXACTLY SWAPPED versus PSP in the legacy code. PSP terminates the
 * whole chain on 2 and treats 1 as "fetch the next start link and continue";
 * the legacy code terminated on 1 and treated 2 as "continue". So the real
 * end-of-chain marker (2) made the loop restart instead of stopping, and the
 * real continue-to-next-display-list marker (1) made it stop instead of
 * continuing — a WinCE-built game's sort-DMA chain would drop or misorder
 * every display list after the first. Rez is built on Sega's WinCE devkit.
 * Also restores two PSP behaviors the legacy code lacked entirely: masking
 * link_addr to 32-byte alignment when SB_SDLAS is not set, and advancing
 * SB_SDSTAW by 32 bytes after the chain completes (needed for the next
 * sort-DMA batch in a double-buffered submission scheme).
 */
static void pvr_do_sort_dma()
{
	// Reset table index
	SB_SDDIV = 0;

	// Get initial link address
	u32 link_addr = calculate_start_link_addr();
	const u32 link_base = SB_SDBAAW;

	const bool dma_fix = get_dma_fix_preset() != 0;
	// Fixed: terminate on LINK_ADDR_RESTART, continue-fetch on LINK_ADDR_END.
	// Legacy (swapped): terminate on LINK_ADDR_END, continue-fetch on LINK_ADDR_RESTART.
	const u32 loop_term = dma_fix ? LINK_ADDR_RESTART : LINK_ADDR_END;
	const u32 loop_next = dma_fix ? LINK_ADDR_END : LINK_ADDR_RESTART;

	// Process linked list
	while (link_addr != loop_term)
	{
		if (SB_SDLAS == 1)
			link_addr <<= 5;      // Multiply by 32 (faster than * 32 on Wii)
		else if (dma_fix)
			link_addr &= ~31u;   // 32-byte align (PSP: link_addr &= ~31)

		// Calculate effective address and get pointer
		const u32 ea = (link_base + link_addr) & RAM_MASK;
		u32* ea_ptr = (u32*)&mem_b[ea];

		// Get next link address
		link_addr = ea_ptr[LINK_NEXT_OFFSET >> 2];

		// Submit display list to Tile Accelerator
		const u32 transfer_size = ea_ptr[LINK_SIZE_OFFSET >> 2];
		libPvr_TaDMA(ea_ptr, transfer_size);

		// Fetch the next start link and continue
		if (link_addr == loop_next)
		{
			link_addr = calculate_start_link_addr();
		}
	}

	// Mark DMA as complete
	SB_SDST = 0;
	if (dma_fix)
		SB_SDSTAW += 32;

	// Raise interrupt
	asic_RaiseInterrupt(holly_PVR_SortDMA);
}

void RegWrite_SB_SDST(u32 data)
{
	if (data & 1)
	{
		pvr_do_sort_dma();
	}
}

//==============================================================================
// Initialization & Cleanup
//==============================================================================

/**
 * @brief Register DMA control register handler
 * @param addr Register address
 * @param write_func Write callback function
 * @param data_ptr Pointer to register data
 */
static inline void register_dma_control(u32 addr, 
                                        void (*write_func)(u32), 
                                        u32* data_ptr)
{
	const u32 index = (addr - SB_BASE) >> 2;
	
	sb_regs[index].flags = REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[index].readFunction = NULL;
	sb_regs[index].writeFunction = write_func;
	sb_regs[index].data32 = data_ptr;
}

void pvr_sb_Init()
{
	// Register PVR DMA start control (0x005F7C18)
	register_dma_control(SB_PDST_addr, RegWrite_SB_PDST, &SB_PDST);

	// Register Ch2 DMA start control (0x005F6808)
	register_dma_control(SB_C2DST_addr, RegWrite_SB_C2DST, &SB_C2DST);

	// Register Sort DMA start control (0x005F6820)
	register_dma_control(SB_SDST_addr, RegWrite_SB_SDST, &SB_SDST);
}

void pvr_sb_Term()
{
	// Currently no resources to free
	// Placeholder for future cleanup if needed
}

void pvr_sb_Reset(bool Manual)
{
	// Reset DMA state registers
	SB_PDST = 0;
	SB_C2DST = 0;
	SB_SDST = 0;
	SB_SDDIV = 0;
	
	// Note: Manual parameter currently unused but reserved
	// for future functionality (e.g., different reset behaviors)
}

//==============================================================================
// Register Read/Write Stubs
//==============================================================================

u32 pvr_sb_readreg_Pvr(u32 addr, u32 sz)
{
	// Currently handled by generic register system
	// Placeholder for future direct register reads if needed
	return 0;
}

void pvr_sb_writereg_Pvr(u32 addr, u32 data, u32 sz)
{
	// Currently handled by generic register system
	// Placeholder for future direct register writes if needed
}
