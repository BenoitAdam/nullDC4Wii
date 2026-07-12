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

// JIT dispatch lookup counters. When 1, the recompiler's inlined dynamic-jump
// fast path increments DynarecBlock::lookups on every cache hit (3 extra
// instructions per dynamic branch: lwz/addi/stw) and bm_GetCode promotes a
// block into cache[] only when its count exceeds the cached block's by a
// threshold. When 0, the JIT fast path skips the increment and bm_GetCode
// uses simple MRU promotion (last slow-path hit owns the cache slot); the
// C-side lookups++ is kept so bucket-overflow eviction still has a victim
// policy. Set to 1 to A/B against the old behaviour.
#define BM_JIT_LOOKUP_STATS 0

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
