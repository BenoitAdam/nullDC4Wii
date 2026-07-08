#include "types.h"
#include "dc/asic/asic.h"
#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/mem/sh4_mem.h"
#include "pvr_sb_regs.h"
#include "plugins/plugin_manager.h"

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
 */
static inline bool validate_dma_config(u32 dmaor, u32 len)
{
	// Check DMAOR settings
	if ((dmaor & DMAOR_MASK) != EXPECTED_DMAOR)
	{
		printf("\n!\tDMAC: DMAOR has invalid settings (0x%X) !\n", dmaor);
		return false;
	}

	// Check length alignment (must be 32-byte aligned)
	if (len & DMA_ALIGNMENT_MASK)
	{
		printf("\n!\tDMAC: SB_C2DLEN has invalid size (0x%X) - must be 32-byte aligned!\n", len);
		return false;
	}

	return true;
}

/**
 * @brief Complete DMA transfer and update registers
 */
static inline void complete_dma_transfer(u32 src, u32 len)
{
	// Update source address register
	DMAC_SAR[0] = src + len;
	
	// Clear DMA enable bit
	DMAC_CHCR[0].full &= 0xFFFFFFFE;
	
	// Clear transfer count
	DMAC_DMATCR[0] = 0;
	
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
// Diagnostic trace for the Re-Volt missing-texture investigation: PVR-DMA
// (SB_PDST) is a third upload path besides Ch2-DMA and SQ, and had zero
// visibility before. Rate-limited so a busy game doesn't spam every frame.
extern "C" int get_debug_message();
static u32 s_pvr_dma_trace_logs = 0;

static void do_pvr_dma()
{
	const u32 dmaor = DMAC_DMAOR.full;
	const u32 src = SB_PDSTAR;
	const u32 dst = SB_PDSTAP;
	const u32 len = SB_PDLEN;

	if (get_debug_message() && s_pvr_dma_trace_logs < 8)
	{
		s_pvr_dma_trace_logs++;
		printf("[pvr] PVR-DMA src=0x%08X dst=0x%08X len=0x%X dir=%s\n",
		       src, dst, len, SB_PDDIR ? "PVR->RAM" : "RAM->PVR");
	}

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
 */
static void pvr_do_sort_dma()
{
	// Reset table index
	SB_SDDIV = 0;
	
	// Get initial link address
	u32 link_addr = calculate_start_link_addr();
	const u32 link_base = SB_SDBAAW;

	// Process linked list
	while (link_addr != LINK_ADDR_END)
	{
		// Apply scaling if enabled (multiply by 32)
		if (SB_SDLAS == 1)
		{
			link_addr <<= 5;  // Multiply by 32 (faster than * 32 on Wii)
		}

		// Calculate effective address and get pointer
		const u32 ea = (link_base + link_addr) & RAM_MASK;
		u32* ea_ptr = (u32*)&mem_b[ea];

		// Get next link address
		link_addr = ea_ptr[LINK_NEXT_OFFSET >> 2];

		// Submit display list to Tile Accelerator
		const u32 transfer_size = ea_ptr[LINK_SIZE_OFFSET >> 2];
		libPvr_TaDMA(ea_ptr, transfer_size);

		// Check for restart condition
		if (link_addr == LINK_ADDR_RESTART)
		{
			link_addr = calculate_start_link_addr();
		}
	}

	// Mark DMA as complete
	SB_SDST = 0;
	
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
