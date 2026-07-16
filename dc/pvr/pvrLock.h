#pragma once

#include "types.h"

//==============================================================================
// PowerVR VRAM Address Conversion
//==============================================================================
// The Dreamcast has 8 MB of VRAM (16 MB for NAOMI) organized in an interleaved
// 64-bit architecture. Memory can be accessed in two modes:
//
// 64-bit mode (0xA4000000-0xA4FFFFFF): Interleaved banks, higher bandwidth
// 32-bit mode (0xA5000000-0xA5FFFFFF): Sequential access
//
// Memory Layout (8MB Dreamcast):
//   VRAM_SIZE = 8MB (0x800000)
//   VRAM_MASK = 0x7FFFFF
//
// Bank Interleaving (64-bit mode):
//   Bank 0: 0xA4000000-0xA4000003 → 0xA5000000-0xA5000003
//   Bank 1: 0xA4000004-0xA4000007 → 0xA5400000-0xA5400003
//   Bank 0: 0xA4000008-0xA400000B → 0xA5000004-0xA5000007
//   (Pattern repeats every 8 bytes)
//==============================================================================

/**
 * Check if address uses 64-bit interleaved bus mode
 * @param addr SH4 memory address
 * @return Non-zero if 64-bit mode (0xA4xxxxxx), zero if 32-bit mode (0xA5xxxxxx)
 */
#define Is_64_Bit(addr) (((addr) & 0x1000000) == 0)

//==============================================================================
// Function Declarations (implemented in pvrLock.cpp)
//==============================================================================

/**
 * Convert SH4 address to 32-bit VRAM offset
 */
u32 vramlock_ConvAddrtoOffset32(u32 Address);

/**
 * Convert 64-bit VRAM offset to 32-bit VRAM offset
 */
u32 vramlock_ConvOffset64toOffset32(u32 offset64);

/**
 * Convert SH4 address to 64-bit VRAM offset
 */
u32 vramlock_ConvAddrtoOffset64(u32 Address);

/**
 * Convert 32-bit VRAM offset to 64-bit VRAM offset
 */
u32 vramlock_ConvOffset32toOffset64(u32 offset32);

//==============================================================================
// Debug/Validation Macros (disabled in release builds ("RELEASE" in Makefile))
//==============================================================================
#ifndef RELEASE
	#define VRAM_VALIDATE_ADDRESS(addr) \
		do { \
			u32 _a = (addr) & 0xFF000000; \
			if (_a != 0xA4000000 && _a != 0xA5000000) { \
				/* EMUERROR2("Invalid VRAM address: 0x%08X", (addr)); */ \
				/* printf instead of EMUERROR because tiny freeze otherwise */ \
				printf("Invalid VRAM address: 0x%08X\n", (addr)); \
			} \
		} while(0)
#else
	#define VRAM_VALIDATE_ADDRESS(addr) ((void)0)
#endif
