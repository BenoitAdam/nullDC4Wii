#include "types.h"
#include <string.h>
#include <stdio.h>

#include "memutil.h"
#include "sh4_mem.h"
#include "sh4_area0.h"
#include "sh4_internal_reg.h"
#include "dc/pvr/pvr_if.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/dc.h"
#include "_vmem.h"
#include "mmu.h"

#if HOST_OS == OS_WII
#include "wii/wii_mmu.h"   // Broadway MMU guest window (no-op if disabled)
#endif

// ---------------------------------------------------------------------------
// Main memory banks
// ---------------------------------------------------------------------------

VArray2  mem_b;      // Main system RAM  (16 MB)
Array<u8> bios_b;   // BIOS ROM         (2 MB)
Array<u8> flash_b;  // Flash / NVRAM    (128 KB)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void _vmem_init();
void _vmem_reset();
void _vmem_term();

// ---------------------------------------------------------------------------
// AREA 1 – VRAM  (PVR, 8 MB)
// ---------------------------------------------------------------------------

static _vmem_handler area1_32b;

static void map_area1_init()
{
	area1_32b = _vmem_register_handler(
		pvr_read_area1_8,  pvr_read_area1_16,  pvr_read_area1_32,
		pvr_write_area1_8, pvr_write_area1_16, pvr_write_area1_32);
}

static void map_area1(u32 base)
{
	// 64-bit interface  0x04xxxxxx
	_vmem_map_block(vram.data, 0x04 | base, 0x04 | base, VRAM_SIZE - 1);
	// 32-bit interface  0x05xxxxxx
	_vmem_map_handler(area1_32b, 0x05 | base, 0x05 | base);
	// Upper 32 MB mirror  0x06-0x07
	_vmem_mirror_mapping(0x06 | base, 0x04 | base, 0x02);
}

// ---------------------------------------------------------------------------
// AREA 2 – Unassigned
// ---------------------------------------------------------------------------

static void map_area2_init() {}
static void map_area2(u32 /*base*/) {}

// ---------------------------------------------------------------------------
// AREA 3 – Main RAM  (16 MB mirrored 4×)
// ---------------------------------------------------------------------------

static void map_area3_init() {}

static void map_area3(u32 base)
{
	_vmem_map_block_mirror(mem_b.data, 0x0C | base, 0x0F | base, RAM_SIZE);
}

// ---------------------------------------------------------------------------
// AREA 4 – Tile Accelerator
// ---------------------------------------------------------------------------

static void map_area4_init() {}

static void map_area4(u32 base)
{
	// Upper 32 MB mirrors lower 32 MB
	_vmem_mirror_mapping(0x12 | base, 0x10 | base, 0x02);
}

// ---------------------------------------------------------------------------
// AREA 5 – External Device  (modem / broadband adapter)
// ---------------------------------------------------------------------------

template<u32 sz, class T>
static T FASTCALL ReadMem_extdev_T(u32 addr)
{
	return (T)libExtDevice_ReadMem_A5(addr, sz);
}

template<u32 sz, class T>
static void FASTCALL WriteMem_extdev_T(u32 addr, T data)
{
	libExtDevice_WriteMem_A5(addr, data, sz);
}

static _vmem_handler area5_handler;

static void map_area5_init()
{
	area5_handler = _vmem_register_handler_Template(ReadMem_extdev_T, WriteMem_extdev_T);
}

static void map_area5(u32 base)
{
	_vmem_map_handler(area5_handler, base | 0x14, base | 0x17);
}

// ---------------------------------------------------------------------------
// AREA 6 – Unassigned
// ---------------------------------------------------------------------------

static void map_area6_init() {}
static void map_area6(u32 /*base*/) {}

// ---------------------------------------------------------------------------
// Full memory map setup
//
// SH4 address space layout:
//   U0/P0  0x00000000–0x7FFFFFFF  –  normal map (mirrored ×4 inside 512 MB)
//   P1     0x80000000–0x9FFFFFFF  –  same physical, cached
//   P2     0xA0000000–0xBFFFFFFF  –  same physical, uncached
//   P3     0xC0000000–0xDFFFFFFF  –  same physical, TLB
//   P4     0xE0000000–0xFFFFFFFF  –  SH4 internal (on-chip) registers
// ---------------------------------------------------------------------------

void mem_map_defualt()
{
	_vmem_init();

	// Register all area handlers
	map_area0_init();
	map_area1_init();
	map_area2_init();
	map_area3_init();
	map_area4_init();
	map_area5_init();
	map_area6_init();
	map_area7_init();

	// Mirror the 512 MB P0–P3 window (0x00–0xDF) seven times
	for (int i = 0x0; i < 0xE; i += 0x2)
	{
		const u32 base = (u32)i << 4;
		map_area0(base);  // BIOS, flash, I/O regs, ext. device, sound RAM
		map_area1(base);  // VRAM
		map_area2(base);  // (unassigned)
		map_area3(base);  // Main RAM
		map_area4(base);  // TA / PowerVR
		map_area5(base);  // External device
		map_area6(base);  // (unassigned)
		map_area7(base);  // SH4 on-chip regs
	}

	// P4 – on-chip / internal registers
	map_p4();

#if HOST_OS == OS_WII
	// With the software map built, mirror RAM/VRAM into the hardware MMU
	// guest window at EA 0x00000000+ and run the self-test (one-shot).
	WMMU_OnMemMapped();
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void mem_Init()
{
	bios_b.Resize(BIOS_SIZE, false);
	flash_b.Resize(FLASH_SIZE, false);

	sh4_area0_Init();
	sh4_internal_reg_Init();
	MMU_Init();
}

void mem_Reset(bool Manual)
{
	if (!Manual)
	{
		// Hard reset – clear all RAM and reload firmware
		mem_b.Zero();
		bios_b.Zero();
		flash_b.Zero();
		LoadBiosFiles();
	}

	sh4_area0_Reset(Manual);
	sh4_internal_reg_Reset(Manual);
	MMU_Reset(Manual);
}

void mem_Term()
{
	MMU_Term();
	sh4_internal_reg_Term();
	sh4_area0_Term();

	// Persist flash (NVRAM) back to disk
	char* temp_path = GetEmuPath("data/");
	strcat(temp_path, "dc_flash_wb.bin");
	SaveSh4FlashromToFile(temp_path);
	free(temp_path);

	flash_b.Free();
	bios_b.Free();

	_vmem_term();
}

// ---------------------------------------------------------------------------
// Block memory transfer helpers
// ---------------------------------------------------------------------------

void MEMCALL WriteMemBlock_nommu_dma(u32 dst, u32 src, u32 size)
{
	// Word-at-a-time DMA copy through the normal memory accessors
	verify((size & 3) == 0);   // must be 4-byte aligned
	for (u32 i = 0; i < size; i += 4)
		WriteMem32_nommu(dst + i, ReadMem32_nommu(src + i));
}

void MEMCALL WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size)
{
	verify((size & 3) == 0);
	for (u32 i = 0; i < size; i += 4)
		WriteMem32_nommu(dst + i, src[i >> 2]);
}

void MEMCALL WriteMemBlock_ptr(u32 addr, u32* data, u32 size)
{
	verify((size & 3) == 0);
	for (u32 i = 0; i < size; i += 4)
		WriteMem32(addr + i, data[i >> 2]);
}

// ---------------------------------------------------------------------------
// Debugger / dynarec helpers
// ---------------------------------------------------------------------------

/**
 * GetMemPtr – return a host pointer into a known memory region, or NULL.
 *
 * Supported regions:
 *   Area 3  (bits 28:26 == 3)  –  main RAM
 *   Area 0  (bits 28:26 == 0)  –  BIOS ROM (first 2 MB of area 0)
 *
 * P4 (bits 31:29 == 7) is never accessible this way.
 */
u8* GetMemPtr(u32 Addr, u32 /*size*/)
{
	// P4 – internal registers, never directly addressable
	if (((Addr >> 29) & 0x7) == 7)
	{
		printf("GetMemPtr: P4 address 0x%08X is not directly mapped\n", Addr);
		return nullptr;
	}

	switch ((Addr >> 26) & 0x7)
	{
	case 3:
		return &mem_b[Addr & RAM_MASK];

	case 0:
		Addr &= 0x01FFFFFF;
		if (Addr <= 0x001FFFFF)
			return &bios_b[Addr];

		printf("GetMemPtr: Area 0 address 0x%08X is outside BIOS range\n", Addr);
		return nullptr;

	default:
		printf("GetMemPtr: unsupported area for address 0x%08X\n", Addr);
		return nullptr;
	}
}

bool IsOnRam(u32 addr)
{
	return (((addr >> 26) & 0x7) == 3) &&
	       (((addr >> 29) & 0x7) != 7);
}

u32 FASTCALL GetRamPageFromAddress(u32 RamAddress)
{
	verify(IsOnRam(RamAddress));
	return (RamAddress & RAM_MASK) / PAGE_SIZE;
}
