/*
	Improved block manager for Dreamcast emulator on Wii - ClaudeAI improved

  blockmanager.cpp is a dynamic recompilation (dynarec) cache
	
	Optimizations:
	- Better cache locality with pre-allocated block storage
	- Reduced memory fragmentation using reserved capacity
	- Improved lookup performance with LRU caching
	- Memory-efficient design for Wii's limited RAM
	- Compatible with original structure for assembly code access
*/

#include "blockmanager.h"
#include "ngen.h"

#include "../sh4_interpreter.h"
#include "../sh4_opcode_list.h"
#include "../sh4_registers.h"
#include "../sh4_if.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "../dmac.h"
#include "dc/gdrom/gdrom_if.h"
#include "../intc.h"
#include "../tmu.h"
#include "dc/mem/sh4_mem.h"

#define bm_AddrHash(addr) (((addr)>>BM_BLOCKLIST_SHIFT)&BM_BLOCKLIST_MASK)

#ifndef HOST_NO_REC

// Use vectors for dynamic block storage (same as original, but with improvements)
typedef vector<DynarecBlock> bm_List;
static bm_List blocks[BM_BLOCKLIST_COUNT];

// Cache for fast lookups - exposed for potential assembly access (same as original)
DynarecBlock* cache[BM_BLOCKLIST_COUNT];
static DynarecBlock empty_block = {0, 0xFFFFFFFF, 0};

extern u32 rdv_FailedToFindBlock_pc;

// Statistics for debugging/profiling (can be disabled in release builds)
#ifdef BM_ENABLE_STATS
static u32 bm_stats_hits = 0;
static u32 bm_stats_misses = 0;
static u32 bm_stats_cache_hits = 0;
#endif

// Initialize the block manager
void bm_Init()
{
	empty_block.code = 0;
	empty_block.addr = 0xFFFFFFFF;
	empty_block.lookups = 0;
	
	for (u32 i = 0; i < BM_BLOCKLIST_COUNT; i++)
	{
		cache[i] = &empty_block;
		blocks[i].reserve(BM_INITIAL_CAPACITY); // Pre-allocate to reduce reallocations
	}
	
#ifdef BM_ENABLE_STATS
	bm_stats_hits = 0;
	bm_stats_misses = 0;
	bm_stats_cache_hits = 0;
#endif
}

// Fast path: inline cache check
static inline DynarecCodeEntry* bm_CheckCache(u32 addr, u32 idx)
{
	DynarecBlock* cached = cache[idx];
	if (cached->addr == addr)
	{
		cached->lookups++;
#ifdef BM_ENABLE_STATS
		bm_stats_cache_hits++;
#endif
		return cached->code;
	}
	return 0;
}

// Main lookup function with improved cache strategy
DynarecCodeEntry* FASTCALL bm_GetCode(u32 addr)
{
	u32 idx = bm_AddrHash(addr);
	
	// Fast path: check cache first
	DynarecCodeEntry* cached_code = bm_CheckCache(addr, idx);
	if (cached_code != 0)
		return cached_code;
	
	// Slow path: search in block list
	bm_List& block_list = blocks[idx];
	const u32 block_count = block_list.size();
	
	for (u32 i = 0; i < block_count; i++)
	{
		DynarecBlock& block = block_list[i];
		if (block.addr == addr)
		{
			block.lookups++;

#if BM_JIT_LOOKUP_STATS
			// Update cache if this block is accessed more frequently
			// Use a threshold to prevent thrashing (only update if significantly better)
			if (block.lookups > cache[idx]->lookups + 2)
			{
				cache[idx] = &block;
			}
#else
			// MRU promotion: the JIT fast path no longer maintains lookup
			// counts, so the threshold compare above would read a stale
			// cached count. Last slow-path hit simply takes the slot — the
			// common case (one hot block per bucket) then always hits the
			// JIT-inlined cache and never comes back here.
			cache[idx] = &block;
#endif

#ifdef BM_ENABLE_STATS
			bm_stats_hits++;
#endif
			return block.code;
		}
	}
	
	// Block not found
#ifdef BM_ENABLE_STATS
	bm_stats_misses++;
#endif
	rdv_FailedToFindBlock_pc = addr;
	return ngen_FailedToFindBlock;
}

// Add a new compiled block
void bm_AddCode(u32 addr, DynarecCodeEntry* code)
{
	u32 idx = bm_AddrHash(addr);
	bm_List& block_list = blocks[idx];
	
	// Check if block already exists (shouldn't happen, but be safe)
	const u32 block_count = block_list.size();
	for (u32 i = 0; i < block_count; i++)
	{
		if (block_list[i].addr == addr)
		{
			// Update existing block
			block_list[i].code = code;
			block_list[i].lookups = 0;
			return;
		}
	}
	
	// Prevent unlimited growth in pathological cases
	if (block_count >= BM_MAX_BLOCKS_PER_BUCKET)
	{
		// Find and replace least used block
		u32 min_lookups_idx = 0;
		u32 min_lookups = block_list[0].lookups;
		
		for (u32 i = 1; i < block_count; i++)
		{
			if (block_list[i].lookups < min_lookups)
			{
				min_lookups = block_list[i].lookups;
				min_lookups_idx = i;
			}
		}
		
		// Replace the least used block
		block_list[min_lookups_idx].code = code;
		block_list[min_lookups_idx].addr = addr;
		block_list[min_lookups_idx].lookups = 0;
		
		// Invalidate cache if we replaced the cached block
		if (cache[idx] == &block_list[min_lookups_idx])
			cache[idx] = &empty_block;
	}
	else
	{
		// Add new block (same as original)
		DynarecBlock new_block;
		new_block.code = code;
		new_block.addr = addr;
		new_block.lookups = 0;
		block_list.push_back(new_block);
		// Point cache at the new block immediately - it was just compiled
		// so it will almost certainly be needed right away.
		cache[idx] = &block_list.back();
	}
	// NOTE: do NOT reset cache to empty_block here when evicting.
	// The eviction path already handles cache invalidation above.
}

// Remove a specific block (useful for invalidation)
bool bm_RemoveCode(u32 addr)
{
	u32 idx = bm_AddrHash(addr);
	bm_List& block_list = blocks[idx];
	
	for (u32 i = 0; i < block_list.size(); i++)
	{
		if (block_list[i].addr == addr)
		{
			// Invalidate cache if we're removing the cached block
			if (cache[idx] == &block_list[i])
				cache[idx] = &empty_block;
			
			// Swap with last element and pop (faster than erase)
			if (i < block_list.size() - 1)
				block_list[i] = block_list[block_list.size() - 1];
			block_list.pop_back();
			
			return true;
		}
	}
	
	return false;
}

// Reset all blocks
void bm_Reset()
{
	ngen_ResetBlocks();
	
	for (u32 i = 0; i < BM_BLOCKLIST_COUNT; i++)
	{
		cache[i] = &empty_block;
		blocks[i].clear();
		// Optionally shrink memory if we're low on RAM
		// blocks[i].shrink_to_fit();
	}
	
#ifdef BM_ENABLE_STATS
	bm_stats_hits = 0;
	bm_stats_misses = 0;
	bm_stats_cache_hits = 0;
#endif
}

// Invalidate blocks in a memory range (useful for SMC - self-modifying code)
void bm_InvalidateRange(u32 start_addr, u32 end_addr)
{
	// Calculate hash range
	u32 start_idx = bm_AddrHash(start_addr);
	u32 end_idx = bm_AddrHash(end_addr);
	
	// If range spans multiple buckets, invalidate all affected buckets
	if (start_idx == end_idx)
	{
		// Same bucket - check individual blocks
		bm_List& block_list = blocks[start_idx];
		for (u32 i = 0; i < block_list.size(); )
		{
			if (block_list[i].addr >= start_addr && block_list[i].addr <= end_addr)
			{
				if (cache[start_idx] == &block_list[i])
					cache[start_idx] = &empty_block;
				
				// Swap and pop
				if (i < block_list.size() - 1)
					block_list[i] = block_list[block_list.size() - 1];
				block_list.pop_back();
			}
			else
			{
				i++;
			}
		}
	}
	else
	{
		// Conservative: invalidate all blocks in affected hash buckets
		// This is simpler and faster than checking each block's actual address
		u32 idx = start_idx;
		while (true)
		{
			cache[idx] = &empty_block;
			blocks[idx].clear();
			
			if (idx == end_idx)
				break;
			idx = (idx + 1) & BM_BLOCKLIST_MASK;
		}
	}
}

// Get statistics (for debugging)
#ifdef BM_ENABLE_STATS
void bm_GetStats(u32* hits, u32* misses, u32* cache_hits, u32* total_blocks)
{
	if (hits) *hits = bm_stats_hits;
	if (misses) *misses = bm_stats_misses;
	if (cache_hits) *cache_hits = bm_stats_cache_hits;
	
	if (total_blocks)
	{
		u32 count = 0;
		for (u32 i = 0; i < BM_BLOCKLIST_COUNT; i++)
			count += blocks[i].size();
		*total_blocks = count;
	}
}

void bm_ResetStats()
{
	bm_stats_hits = 0;
	bm_stats_misses = 0;
	bm_stats_cache_hits = 0;
}
#endif

#endif //#ifndef HOST_NO_REC
