#include "types.h"
#include "sh4_internal_reg.h"

#include "dc/sh4/bsc.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/cpg.h"
#include "dc/sh4/dmac.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/rtc.h"
#include "dc/sh4/sci.h"
#include "dc/sh4/scif.h"
#include "dc/sh4/tmu.h"
#include "dc/sh4/ubc.h"
#include "_vmem.h"
#include "mmu.h"


// 64 bytes of store queue buffer (256-byte aligned for hardware compatibility)
ALIGN(256) u8 sq_both[64];



Array<u8> OnChipRAM;

// All registers are 4-byte aligned in their arrays.
// Array sizes are slightly oversized to avoid off-by-one issues.
Array<RegisterStruct> CCN(16, true);   // CCN  : 14 registers (cache/MMU control)
Array<RegisterStruct> UBC(9,  true);   // UBC  : 9 registers  (user breakpoint)
Array<RegisterStruct> BSC(19, true);   // BSC  : 18 registers (bus state control)
Array<RegisterStruct> DMAC(17, true);  // DMAC : 17 registers (DMA controller)
Array<RegisterStruct> CPG(5,  true);   // CPG  : 5 registers  (clock/power)
Array<RegisterStruct> RTC(16, true);   // RTC  : 16 registers (real-time clock)
Array<RegisterStruct> INTC(4, true);   // INTC : 4 registers  (interrupt controller)
Array<RegisterStruct> TMU(12, true);   // TMU  : 12 registers (timer unit)
Array<RegisterStruct> SCI(8,  true);   // SCI  : 8 registers  (serial comm interface)
Array<RegisterStruct> SCIF(10, true);  // SCIF : 10 registers (serial comm FIFO)


// ---------------------------------------------------------------------------
// Register read/write helpers
// ---------------------------------------------------------------------------
// Template parameter 'size' is the access size in bytes (1, 2, or 4).
// 'offset' is the byte offset within the module's register block.

template <u32 size>
INLINE u32 RegSRead(Array<RegisterStruct>& reg, u32 offset)
{
#ifdef TRACE
// Minimum alignment is 4 bytes for all SH4 internal registers
	if (offset & 3)
	{
		// EMUERROR("Unaligned register read");
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("Unaligned register read\n");
	}
#endif

	offset >>= 2; // Convert byte offset to register index

#ifdef TRACE
	if (reg[offset].flags & size)
	{
#endif
		if (reg[offset].flags & REG_READ_DATA)
		{
			if (size == 4)      return *reg[offset].data32;
			else if (size == 2) return *reg[offset].data16;
			else                return *reg[offset].data8;
		}
		else
		{
			if (reg[offset].readFunction)
				return reg[offset].readFunction();
			else if (!(reg[offset].flags & REG_NOT_IMPL))
			{
				// EMUERROR("Read from write-only register");
				// printf instead of EMUERROR because tiny freeze otherwise
				printf("Read from write-only register\n");
			}
		}
#ifdef TRACE
	}
	else
	{
		if (!(reg[offset].flags & REG_NOT_IMPL))
		{
			// EMUERROR("Wrong size read on register");
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("Wrong size read on register\n");
		}
	}
#endif

	if (reg[offset].flags & REG_NOT_IMPL)
	{
		// EMUERROR2("Read from unimplemented internal register, offset=0x%x", offset);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("Read from unimplemented internal register, offset=0x%x\n", offset);
	}

	return 0;
}

template <u32 size>
INLINE void RegSWrite(Array<RegisterStruct>& reg, u32 offset, u32 data)
{
#ifdef TRACE
	if (offset & 3)
	{
		// EMUERROR("Unaligned register write");
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("Unaligned register write\n");
	}
#endif

	offset >>= 2;

#ifdef TRACE
	if (reg[offset].flags & size)
	{
#endif
		if (reg[offset].flags & REG_WRITE_DATA)
		{
			if (size == 4)      *reg[offset].data32 = data;
			else if (size == 2) *reg[offset].data16 = (u16)data;
			else                *reg[offset].data8  = (u8)data;
			return;
		}
		else
		{
			if (reg[offset].flags & REG_CONST)
			{
				// EMUERROR("Write to read-only (const) register");
				// printf instead of EMUERROR because tiny freeze otherwise
				printf("Write to read-only (const) register\n");
			}
			else if (reg[offset].writeFunction)
			{
				reg[offset].writeFunction(data);
				return;
			}
			else if (!(reg[offset].flags & REG_NOT_IMPL))
			{
				// EMUERROR("Write to read-only register (no write function)");
				// printf instead of EMUERROR because tiny freeze otherwise
				printf("Write to read-only register (no write function)\n");
			}
		}
#ifdef TRACE
	}
	else
	{
		if (!(reg[offset].flags & REG_NOT_IMPL))
		{
			// EMUERROR4("Wrong size write on register; offset=0x%x, data=0x%x, sz=%d", offset, data, size);
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("Wrong size write on register; offset=0x%x, data=0x%x, sz=%d\n", offset, data, size);
		}
	}
#endif

	if (reg[offset].flags & REG_NOT_IMPL)
	{
		// EMUERROR3("Write to unimplemented internal register, offset=0x%x, data=0x%x", offset, data);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("Write to unimplemented internal register, offset=0x%x, data=0x%x\n", offset, data);
	}
}


// ---------------------------------------------------------------------------
// P4 Region (0xE0000000 - 0xFFFFFFFF): privileged SH4 control space
// Covers store queues, cache/TLB arrays, and area7 (on-chip peripheral regs).
// ---------------------------------------------------------------------------

// Page-size mask table for TLB associative write (SZ field encoding)
// SZ: 0=1KB, 1=4KB, 2=64KB, 3=1MB
static const u32 mmu_mask_2[4] =
{
	((0xFFFFFFFF) >> 10) << 0,   // 1 KB page
	((0xFFFFFFFF) >> 12) << 2,   // 4 KB page
	((0xFFFFFFFF) >> 16) << 6,   // 64 KB page
	((0xFFFFFFFF) >> 20) << 10,  // 1 MB page
};

template <u32 sz, class T>
T FASTCALL ReadMem_P4(u32 addr)
{
	switch ((addr >> 24) & 0xFF)
	{
	// Store queues (0xE0-0xE3): fallback, normally handled by vmem block map
	case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		return 0;

	// Instruction cache address array (F0): not emulated
	case 0xF0:
		return 0;

	// Instruction cache data array (F1): not emulated
	case 0xF1:
		return 0;

	// ITLB address array (F2)
	case 0xF2:
		{
			u32 entry = (addr >> 8) & 3;
			return (T)(ITLB[entry].Address.reg_data | (ITLB[entry].Data.V << 8));
		}

	// ITLB data arrays 1 and 2 (F3)
	case 0xF3:
		{
			u32 entry = (addr >> 8) & 3;
			return (T)ITLB[entry].Data.reg_data;
		}

	// Operand cache address array (F4): not emulated
	case 0xF4:
		return 0;

	// Operand cache data array (F5): not emulated
	case 0xF5:
		return 0;

	// UTLB address array (F6)
	case 0xF6:
		{
			u32 entry = (addr >> 8) & 63;
			u32 rv = UTLB[entry].Address.reg_data;
			rv |= UTLB[entry].Data.D << 9;
			rv |= UTLB[entry].Data.V << 8;
			return (T)rv;
		}

	// UTLB data arrays 1 and 2 (F7)
	case 0xF7:
		{
			u32 entry = (addr >> 8) & 63;
			return (T)UTLB[entry].Data.reg_data;
		}

	// Area7 (FF): should be routed via area7 handler, not here
	case 0xFF:
		return 0;

	default:
		// Outside defined P4 sub-regions (e.g. 0xECxxxxxx).
		// Not a real SH4 address — likely a guest bug. Return 0 safely.
		return 0;
	}
}

template <u32 sz, class T>
void FASTCALL WriteMem_P4(u32 addr, T data)
{
	switch ((addr >> 24) & 0xFF)
	{
	// Store queues: fallback, normally handled by vmem block map
	case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		return;

	// Instruction cache address array (F0): ignore writes (no cache emulation)
	case 0xF0:
		return;

	// Instruction cache data array (F1): ignore writes
	case 0xF1:
		return;

	// ITLB address array (F2)
	case 0xF2:
		{
			u32 entry = (addr >> 8) & 3;
			ITLB[entry].Address.reg_data = data & 0xFFFFFEFF;
			ITLB[entry].Data.V = ((u32)data >> 8) & 1;
			ITLB_Sync(entry);
			return;
		}

	// ITLB data arrays (F3)
	case 0xF3:
		{
			u32 entry = (addr >> 8) & 3;
			ITLB[entry].Data.reg_data = data;
			ITLB_Sync(entry);
			return;
		}

	// Operand cache address array (F4): ignore writes
	case 0xF4:
		return;

	// Operand cache data array (F5): ignore writes
	case 0xF5:
		return;

	// UTLB address array (F6)
	case 0xF6:
		{
			if (addr & 0x80)
			{
				// Associative write: match VPN+ASID across all TLB entries
				CCN_PTEH_type t;
				t.reg_data = data;

				for (int i = 0; i < 64; i++)
				{
					u32 tsz  = UTLB[i].Data.SZ1 * 2 + UTLB[i].Data.SZ0;
					u32 mask = mmu_mask_2[tsz];
					u32 vpn  = t.VPN & mask;

					if (((UTLB[i].Address.VPN & mask) == vpn) &&
					    (UTLB[i].Address.ASID == CCN_PTEH.ASID))
					{
						UTLB[i].Data.V = ((u32)data >> 8) & 1;
						UTLB[i].Data.D = ((u32)data >> 9) & 1;
					}
				}

				for (int i = 0; i < 4; i++)
				{
					u32 tsz  = ITLB[i].Data.SZ1 * 2 + ITLB[i].Data.SZ0;
					u32 mask = mmu_mask_2[tsz];
					u32 vpn  = t.VPN & mask;

					if (((ITLB[i].Address.VPN & mask) == vpn) &&
					    (ITLB[i].Address.ASID == CCN_PTEH.ASID))
					{
						ITLB[i].Data.V = ((u32)data >> 8) & 1;
						ITLB[i].Data.D = ((u32)data >> 9) & 1;
					}
				}
			}
			else
			{
				u32 entry = (addr >> 8) & 63;
				UTLB[entry].Address.reg_data = data & 0xFFFFFCFF;
				UTLB[entry].Data.D = ((u32)data >> 9) & 1;
				UTLB[entry].Data.V = ((u32)data >> 8) & 1;
				UTLB_Sync(entry);
			}
			return;
		}

	// UTLB data arrays (F7)
	case 0xF7:
		{
			u32 entry = (addr >> 8) & 63;
			UTLB[entry].Data.reg_data = data;
			UTLB_Sync(entry);
			return;
		}

	// Area7 (FF): fallback
	case 0xFF:
		return;

	default:
		// Outside defined P4 sub-regions — discard silently
		return;
	}
}


// ---------------------------------------------------------------------------
// Store Queue access (32-bit only per SH4 spec)
// ---------------------------------------------------------------------------

template <u32 sz, class T>
T FASTCALL ReadMem_sq(u32 addr)
{
	if (sz != 4)
		return 0xDE;

	u32 offset = addr & 0x3C; // 8 dwords, 4 bytes each = 32 bytes per queue
	return (T) *(u32*)&sq_both[offset];
}

template <u32 sz, class T>
void FASTCALL WriteMem_sq(u32 addr, T data)
{
	if (sz != 4)
		return;

	u32 offset = addr & 0x3C;
	*(u32*)&sq_both[offset] = (u32)data;
}


// ---------------------------------------------------------------------------
// Area 7 (0x1Fxxxxxx): on-chip peripheral register dispatch
// Registers are grouped by module, hashed by address bits [23:16].
// ---------------------------------------------------------------------------

// Special-case address seen in Shenmue; harmless read
static const u32 SHENMUE_UNKNOWN_ADDR = 0x1F100008;

template <u32 sz, class T>
T FASTCALL ReadMem_area7(u32 addr)
{
	addr &= 0x1FFFFFFF;

	switch (A7_REG_HASH(addr))
	{
	// Shenmue reads this address frequently; return 0 silently
	case A7_REG_HASH(SHENMUE_UNKNOWN_ADDR):
		if (addr == SHENMUE_UNKNOWN_ADDR)
			return 0;
		// EMUERROR2("Unknown read from Area7, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("Unknown read from Area7, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(CCN_BASE_addr):
		if (addr <= 0x1F00003C)
			return (T)RegSRead<sz>(CCN, addr & 0xFF);
		// EMUERROR2("CCN register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("CCN register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(UBC_BASE_addr):
		if (addr <= 0x1F200020)
			return (T)RegSRead<sz>(UBC, addr & 0xFF);
		// EMUERROR2("UBC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("UBC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(BSC_BASE_addr):
		if (addr <= 0x1F800048)
			return (T)RegSRead<sz>(BSC, addr & 0xFF);
		else if (addr >= BSC_SDMR2_addr && addr <= 0x1F90FFFF)
		{
			// EMUERROR("Read from write-only SDMR2 (DRAM settings)");
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("Read from write-only SDMR2 (DRAM settings)\n");
		}
		else if (addr >= BSC_SDMR3_addr && addr <= 0x1F94FFFF)
		{
			// EMUERROR("Read from write-only SDMR3 (DRAM settings)");
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("Read from write-only SDMR3 (DRAM settings)\n");
		}
		else
		{
			// EMUERROR2("BSC register out of range, addr=0x%x", addr);
			// printf instead of EMUERROR because tiny freeze otherwise
			printf("BSC register out of range, addr=0x%x\n", addr);
		}
		break;

	case A7_REG_HASH(DMAC_BASE_addr):
		if (addr <= 0x1FA00040)
			return (T)RegSRead<sz>(DMAC, addr & 0xFF);
		// EMUERROR2("DMAC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("DMAC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(CPG_BASE_addr):
		if (addr <= 0x1FC00010)
			return (T)RegSRead<sz>(CPG, addr & 0xFF);
		// EMUERROR2("CPG register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("CPG register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(RTC_BASE_addr):
		if (addr <= 0x1FC8003C)
			return (T)RegSRead<sz>(RTC, addr & 0xFF);
		// EMUERROR2("RTC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("RTC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(INTC_BASE_addr):
		if (addr <= 0x1FD0000C)
			return (T)RegSRead<sz>(INTC, addr & 0xFF);
		// EMUERROR2("INTC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("INTC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(TMU_BASE_addr):
		if (addr <= 0x1FD8002C)
			return (T)RegSRead<sz>(TMU, addr & 0xFF);
		// EMUERROR2("TMU register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("TMU register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(SCI_BASE_addr):
		if (addr <= 0x1FE0001C)
			return (T)RegSRead<sz>(SCI, addr & 0xFF);
		// EMUERROR2("SCI register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("SCI register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(SCIF_BASE_addr):
		if (addr <= 0x1FE80024)
			return (T)RegSRead<sz>(SCIF, addr & 0xFF);
		// EMUERROR2("SCIF register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("SCIF register out of range, addr=0x%x\n", addr);
		break;

	// UDI (User Debug Interface): not present on Dreamcast, ignore silently
	case A7_REG_HASH(UDI_BASE_addr):
		switch (addr)
		{
		case UDI_SDIR_addr: return (T)0xFFFF; // Reset value (truncated to T width)
		case UDI_SDDR_addr: return 0;
		}
		break;
	}

	// EMUERROR2("ReadMem_area7 not implemented, addr=0x%x", addr);
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("ReadMem_area7 not implemented, addr=0x%x\n", addr);
	return 0;
}

template <u32 sz, class T>
void FASTCALL WriteMem_area7(u32 addr, T data)
{
	addr &= 0x1FFFFFFF;

	switch (A7_REG_HASH(addr))
	{
	case A7_REG_HASH(CCN_BASE_addr):
		if (addr <= 0x1F00003C)
		{
			RegSWrite<sz>(CCN, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("CCN register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("CCN register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(UBC_BASE_addr):
		if (addr <= 0x1F200020)
		{
			RegSWrite<sz>(UBC, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("UBC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("UBC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(BSC_BASE_addr):
		if (addr <= 0x1F800048)
		{
			RegSWrite<sz>(BSC, addr & 0xFF, data);
			return;
		}
		else if (addr >= BSC_SDMR2_addr && addr <= 0x1F90FFFF)
			return; // DRAM settings 2 — write-only, no effect needed
		else if (addr >= BSC_SDMR3_addr && addr <= 0x1F94FFFF)
			return; // DRAM settings 3 — write-only, no effect needed
		// EMUERROR2("BSC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("BSC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(DMAC_BASE_addr):
		if (addr <= 0x1FA00040)
		{
			RegSWrite<sz>(DMAC, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("DMAC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("DMAC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(CPG_BASE_addr):
		if (addr <= 0x1FC00010)
		{
			RegSWrite<sz>(CPG, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("CPG register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("CPG register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(RTC_BASE_addr):
		if (addr <= 0x1FC8003C)
		{
			RegSWrite<sz>(RTC, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("RTC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("RTC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(INTC_BASE_addr):
		if (addr <= 0x1FD0000C)
		{
			RegSWrite<sz>(INTC, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("INTC register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("INTC register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(TMU_BASE_addr):
		if (addr <= 0x1FD8002C)
		{
			RegSWrite<sz>(TMU, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("TMU register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("TMU register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(SCI_BASE_addr):
		if (addr <= 0x1FE0001C)
		{
			RegSWrite<sz>(SCI, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("SCI register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("SCI register out of range, addr=0x%x\n", addr);
		break;

	case A7_REG_HASH(SCIF_BASE_addr):
		if (addr <= 0x1FE80024)
		{
			RegSWrite<sz>(SCIF, addr & 0xFF, data);
			return;
		}
		// EMUERROR2("SCIF register out of range, addr=0x%x", addr);
		// printf instead of EMUERROR because tiny freeze otherwise
		printf("SCIF register out of range, addr=0x%x\n", addr);
		break;

	// UDI: not present on Dreamcast, ignore writes silently
	case A7_REG_HASH(UDI_BASE_addr):
		switch (addr)
		{
		case UDI_SDIR_addr: return;
		case UDI_SDDR_addr: return;
		}
		break;
	}

	// EMUERROR4("WriteMem_area7 not implemented, addr=0x%x, data=0x%x, sz=%d", addr, (u32)data, sz);
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("WriteMem_area7 not implemented, addr=0x%x, data=0x%x, sz=%d\n", addr, (u32)data, sz);
}


// ---------------------------------------------------------------------------
// On-Chip RAM (scratchpad, 8KB)
// Only active when CCN_CCR.ORA bit is set.
// ---------------------------------------------------------------------------

template <u32 sz, class T>
T FASTCALL ReadMem_area7_OCR_T(u32 addr)
{
	if (!CCN_CCR.ORA)
		return (T)0xDE;

	u32 ofs = addr & OnChipRAM_MASK;

	if (sz == 1) return (T)OnChipRAM[ofs];
	if (sz == 2) return (T)host_to_le<sz>(*(u16*)&OnChipRAM[ofs]);
	if (sz == 4) return (T)host_to_le<sz>(*(u32*)&OnChipRAM[ofs]);

	return (T)0xDE;
}

template <u32 sz, class T>
void FASTCALL WriteMem_area7_OCR_T(u32 addr, T data)
{
	if (!CCN_CCR.ORA)
		return;

	u32 ofs = addr & OnChipRAM_MASK;

	if (sz == 1)      OnChipRAM[ofs]               = (u8)data;
	else if (sz == 2) *(u16*)&OnChipRAM[ofs]        = (u16)host_to_le<sz>((u32)data);
	else if (sz == 4) *(u32*)&OnChipRAM[ofs]        = host_to_le<sz>((u32)data);
}


// ---------------------------------------------------------------------------
// Lifecycle: Init / Reset / Term
// ---------------------------------------------------------------------------

void sh4_internal_reg_Init()
{
	OnChipRAM.Resize(OnChipRAM_SIZE, false);

  // Mark all register slots as unimplemented by default.
	// Individual module Init() calls will override the ones they handle.
	for (u32 i = 0; i < 30; i++)
	{
		if (i < CCN.Size)  CCN[i].flags  = REG_NOT_IMPL;
		if (i < UBC.Size)  UBC[i].flags  = REG_NOT_IMPL;
		if (i < BSC.Size)  BSC[i].flags  = REG_NOT_IMPL;
		if (i < DMAC.Size) DMAC[i].flags = REG_NOT_IMPL;
		if (i < CPG.Size)  CPG[i].flags  = REG_NOT_IMPL;
		if (i < RTC.Size)  RTC[i].flags  = REG_NOT_IMPL;
		if (i < INTC.Size) INTC[i].flags = REG_NOT_IMPL;
		if (i < TMU.Size)  TMU[i].flags  = REG_NOT_IMPL;
		if (i < SCI.Size)  SCI[i].flags  = REG_NOT_IMPL;
		if (i < SCIF.Size) SCIF[i].flags = REG_NOT_IMPL;
	}

  // Initialize each peripheral module's register structs
	bsc_Init();
	ccn_Init();
	cpg_Init();
	dmac_Init();
	intc_Init();
	rtc_Init();
	sci_Init();
	scif_Init();
	tmu_Init();
	ubc_Init();
}

void sh4_internal_reg_Reset(bool Manual)
{
	OnChipRAM.Zero();

	bsc_Reset(Manual);
	ccn_Reset(Manual);
	cpg_Reset(Manual);
	dmac_Reset(Manual);
	intc_Reset(Manual);
	rtc_Reset(Manual);
	sci_Reset(Manual);
	scif_Reset(Manual);
	tmu_Reset(Manual);
	ubc_Reset(Manual);
}

void sh4_internal_reg_Term()
{
  // Terminate in reverse init order for safety
	ubc_Term();
	tmu_Term();
	scif_Term();
	sci_Term();
	rtc_Term();
	intc_Term();
	dmac_Term();
	cpg_Term();
	ccn_Term();
	bsc_Term();
	OnChipRAM.Free();
}


// ---------------------------------------------------------------------------
// Virtual memory handler registration and mapping
// ---------------------------------------------------------------------------

_vmem_handler area7_handler;
_vmem_handler area7_orc_handler;

void map_area7_init()
{
	area7_handler     = _vmem_register_handler_Template(ReadMem_area7,     WriteMem_area7);
	area7_orc_handler = _vmem_register_handler_Template(ReadMem_area7_OCR_T, WriteMem_area7_OCR_T);
}

void map_area7(u32 base)
{
	if (base == 0x60)
		_vmem_map_handler(area7_orc_handler, 0x1C | base, 0x1F | base);
	else
		_vmem_map_handler(area7_handler, 0x1F | base, 0x1F | base);
}

void map_p4()
{
  // Register a catch-all P4 handler for 0xE0000000-0xFFFFFFFF
	_vmem_handler p4_handler = _vmem_register_handler_Template(ReadMem_P4, WriteMem_P4);
	_vmem_map_handler(p4_handler, 0xE0, 0xFF);

  // Store queues: direct block-mapped to sq_both buffer (fast path, no handler overhead)
	// Each of the four 256MB segments 0xE0-0xE3 maps to the same 64-byte buffer.
	_vmem_map_block(sq_both, 0xE0, 0xE0, 63);
	_vmem_map_block(sq_both, 0xE1, 0xE1, 63);
	_vmem_map_block(sq_both, 0xE2, 0xE2, 63);
	_vmem_map_block(sq_both, 0xE3, 0xE3, 63);

  // Area7 handler overlays the 0xFFxxxxxx region, overriding the P4 catch-all
	map_area7(0xE0);
}
