/*
	Improved block manager header for Dreamcast emulator on Wii - By ClaudeAI
	
	The extern "C" stuff is for assembly code on beagleboard/pandora/wii
*/
#include "types.h"
#pragma once

#if HOST_OS==OS_LINUX || HOST_OS==OS_WII
extern "C" {
#endif

// Block manager configuration
// Kept at 16K buckets like original, but can be tuned for Wii memory constraints
#define BM_BLOCKLIST_COUNT (16384)
#define BM_BLOCKLIST_MASK (BM_BLOCKLIST_COUNT-1)
#define BM_BLOCKLIST_SHIFT 2

// Configuration for Wii platform - adjust based on available memory
#ifndef BM_INITIAL_CAPACITY
#define BM_INITIAL_CAPACITY 16      // Initial blocks per hash bucket
#endif

#ifndef BM_MAX_BLOCKS_PER_BUCKET
#define BM_MAX_BLOCKS_PER_BUCKET 64 // Prevent excessive growth in single bucket
#endif

// Enable statistics tracking (disable in release builds for better performance)
// #define BM_ENABLE_STATS

typedef void DynarecCodeEntry();

// Core block manager functions (compatible with original API)
DynarecCodeEntry* FASTCALL bm_GetCode(u32 addr);
void bm_AddCode(u32 addr, DynarecCodeEntry* code);
void bm_Reset();

// Block structure (kept compatible with original for assembly code)
struct DynarecBlock
{
	DynarecCodeEntry* code;
	u32 addr;
	u32 lookups;
	//u32 pad;
};

// Cache array exposed for potential assembly access (like original)
extern DynarecBlock* cache[BM_BLOCKLIST_COUNT];

// Flat inline cache for the JIT dynamic-branch fast path (BCACHE preset).
// One {addr, code} pair per bucket, value-mirrored from cache[] at every
// cache[] mutation site in blockmanager.cpp, so the emitted dispatch for a
// dynamic jump touches ONE data cache line (the 8-byte entry) instead of two
// (the cache[] pointer + the DynarecBlock it points at, which lives in an
// unrelated heap vector).
// Invariants the emitted code relies on (wii_driver.cpp BET_Dynamic*):
//   * addr == 0xFFFFFFFF <=> empty (odd, so it can never match an SH4 PC)
//   * addr != 0xFFFFFFFF  => code is a valid block entry point (never 0)
//   * sizeof(entry) == 8 on the Wii (the emitted index math hardcodes idx*8)
struct BlockCacheFlatEntry
{
	u32 addr;
	DynarecCodeEntry* code;
};
extern BlockCacheFlatEntry bm_bcache[BM_BLOCKLIST_COUNT];

// New functions (not in original, but useful for Wii port)
void bm_Init();
bool bm_RemoveCode(u32 addr);
void bm_InvalidateRange(u32 start_addr, u32 end_addr);

// Statistics functions (only available when BM_ENABLE_STATS is defined)
#ifdef BM_ENABLE_STATS
void bm_GetStats(u32* hits, u32* misses, u32* cache_hits, u32* total_blocks);
void bm_ResetStats();
#endif

#if HOST_OS==OS_LINUX || HOST_OS==OS_WII
}
#endif
