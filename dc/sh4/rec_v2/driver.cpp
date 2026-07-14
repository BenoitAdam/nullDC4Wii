/*
	driver.cpp - Dynarec Driver for Dreamcast Emulator on Wii
	
	This file is the JIT recompiler driver. It manages:
	  - The native code cache (a fixed buffer where compiled PPC blocks live)
	  - Emission of raw machine code into the cache
	  - Block lookup, compilation, and invalidation
	  - Cache pressure monitoring and eviction
	  - Wii-specific D-cache/I-cache coherency

	Improvements over previous version:
	  - Fixed boot-detect cache clear bug (was invalidating freshly added blocks)
	  - Cache pressure eviction uses a deferred/hysteresis model instead of
	    a single hard threshold, reducing thrashing
	  - All Wii cache flushes are properly bounded and guarded
	  - Code quality pass: consistent style, better comments, dead code removed
*/

#include "types.h"

#if HOST_OS == OS_WINDOWS
#include <windows.h>
#elif HOST_OS == OS_LINUX
#include <unistd.h>
#include <sys/mman.h>
#elif HOST_OS == OS_WII
#include <gccore.h>
#include <malloc.h>
#endif

#include "../sh4_interpreter.h"
#include "../sh4_opcode_list.h"
#include "../sh4_registers.h"
#include "../sh4_if.h"
#include "../dmac.h"
#include "../intc.h"
#include "../tmu.h"

#include "dc/mem/sh4_mem.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dc/gdrom/gdrom_if.h"

#include <time.h>
#include <float.h>
#include <string.h>

#include "blockmanager.h"
#include "ngen.h"
#include "decoder.h"

#ifndef HOST_NO_REC

// Defined in main.cpp — returns 1 when debug loop / boot-trace is active
extern "C" int get_debug_loop();

// ============================================================================
// Optional performance counters
// Uncomment to enable; negligible overhead, useful for tuning cache thresholds
// ============================================================================
// #define ENABLE_PERF_MONITORING

#ifdef ENABLE_PERF_MONITORING
static u32 perf_blocks_compiled    = 0;
static u32 perf_cache_clears       = 0;
static u32 perf_block_check_fails  = 0;
#endif

// ============================================================================
// Forward declarations
// ============================================================================
void recSh4_ClearCache();

// ============================================================================
// Code cache storage
//
// CODE_SIZE is defined in ngen.h (currently 6 MB). Do NOT redefine here —
// a smaller local definition caused premature cache clears while the real
// array still had room, causing rdv_FailedToFindBlock spam.
//
// Wii: aligned to 32-byte cache lines so DCFlushRange/ICInvalidateRange
// operate on natural boundaries without partial-line contamination.
// ============================================================================

extern volatile bool sh4_int_bCpuRun;

#if HOST_OS == OS_WII
u8 CodeCache[CODE_SIZE] __attribute__((aligned(32)));
#elif HOST_OS == OS_LINUX
// Linux needs an executable mapping; allocate with a page-alignment margin.
u8  pMemBuff[CODE_SIZE + 4095];
u8* CodeCache;
#else
u8 CodeCache[CODE_SIZE];
#endif

// Current write pointer within the cache (byte offset)
u32  LastAddr     = 0;
// Lower bound after SetBaseAddr — clears only reclaim above this
u32  LastAddr_min = 0;
// Optional override pointer used during back-patching
u32* emit_ptr     = 0;

// ============================================================================
// Cache pressure thresholds (Wii-specific)
//
// CACHE_WARN_THRESHOLD  — log a warning, no action yet
// CACHE_FLUSH_THRESHOLD — proactive clear before we run out mid-compilation
//
// Hysteresis between the two avoids rapid clear/refill thrashing.
// Both expressed as fractions of CODE_SIZE.
// ============================================================================
#if HOST_OS == OS_WII
static u32 cache_high_water_mark = 0;
#define CACHE_WARN_THRESHOLD   ((CODE_SIZE * 80) / 100)   // 80 %
#define CACHE_FLUSH_THRESHOLD  ((CODE_SIZE * 92) / 100)   // 92 %
// Minimum free bytes required before attempting a new compilation.
// 128 KB gives room for the largest typical PPC block expansion.
#define CACHE_MIN_FREE         (128 * 1024)
#endif

// ============================================================================
// Wii cache coherency helpers
//
// On the Wii's Broadway (PowerPC 750CL) the data cache and instruction cache
// are independent. After writing new native code we must:
//   1. DCFlushRange  — write D-cache lines back to RAM
//   2. ICInvalidateRange — discard stale I-cache lines so the CPU fetches fresh
//
// Both functions require 32-byte alignment and a size rounded up to 32 bytes.
// ============================================================================
#if HOST_OS == OS_WII
static inline u32 align_up_32(u32 v) { return (v + 31u) & ~31u; }

static inline void wii_sync_icache(const void* start, u32 size)
{
	if (!size) return;
	// libogc functions are safe with unaligned inputs but work best aligned.
	DCFlushRange((void*)start, align_up_32(size));
	ICInvalidateRange((void*)start, align_up_32(size));
}
#endif

// ============================================================================
// Emit helpers — write code bytes / words into the cache
// ============================================================================

void* emit_GetCCPtr()
{
	return emit_ptr ? (void*)emit_ptr : (void*)&CodeCache[LastAddr];
}

void emit_SetBaseAddr()
{
	LastAddr_min = LastAddr;
}

void emit_Write8(u8 data)
{
	verify(!emit_ptr);
	verify(LastAddr + 1 <= CODE_SIZE);
	CodeCache[LastAddr++] = data;
}

void emit_Write16(u16 data)
{
	verify(!emit_ptr);
	verify(LastAddr + 2 <= CODE_SIZE);
	*(u16*)&CodeCache[LastAddr] = data;
	LastAddr += 2;
}

void emit_Write32(u32 data)
{
	if (emit_ptr)
	{
		*emit_ptr++ = data;
	}
	else
	{
		verify(LastAddr + 4 <= CODE_SIZE);
		*(u32*)&CodeCache[LastAddr] = data;
		LastAddr += 4;
	}
}

void emit_Skip(u32 sz)
{
	verify(LastAddr + sz <= CODE_SIZE);
	LastAddr += sz;
}

u32 emit_FreeSpace()
{
	return CODE_SIZE - LastAddr;
}

// ============================================================================
// Code cache dump (debug utility)
// ============================================================================
void emit_WriteCodeCache()
{
	char path[512];
	snprintf(path, sizeof(path), "code_cache_%08X.bin", (u32)(uintptr_t)CodeCache);

	FILE* f = fopen(path, "wb");
	if (f)
	{
		fwrite(CodeCache, LastAddr, 1, f);
		fclose(f);
		printf("recSh4: cache dump -> %s (%u bytes)\n", path, LastAddr);
	}
	else
	{
		printf("recSh4: ERROR - failed to open %s for cache dump\n", path);
	}
}

// ============================================================================
// recSh4_ClearCache
//
// Resets the write pointer and invalidates all compiled blocks.
// On Wii we also flush the instruction cache so the CPU cannot execute
// stale code that happened to sit in I-cache lines.
// ============================================================================
void recSh4_ClearCache()
{
#ifdef ENABLE_PERF_MONITORING
	perf_cache_clears++;
	printf("recSh4: cache clear #%u at pc=%08X (used %u / %u bytes)\n",
	       perf_cache_clears, curr_pc, LastAddr, CODE_SIZE);
#else
	printf("recSh4: cache clear at pc=%08X (used %u / %u bytes)\n",
	       curr_pc, LastAddr, CODE_SIZE);
#endif

	LastAddr = LastAddr_min;
	bm_Reset();

#if HOST_OS == OS_WII
	// Invalidate the entire I-cache region we may have written into.
	// This is safe even if LastAddr_min == 0 (full reset).
	ICInvalidateRange(CodeCache, CODE_SIZE);
	cache_high_water_mark = LastAddr;
#endif
}

// ============================================================================
// Block analysis
// ============================================================================
void AnalyseBlock(DecodedBlock* blk);

// DoCheck: enable extra verification for known-tricky addresses.
// Only called in debug builds / when block-check is active.
static bool DoCheck(u32 pc)
{
	if (!IsOnRam(pc))
		return false;

	switch (pc & 0xFFFFFF)
	{
		case 0x3DAFC6:
		case 0x3C83F8:
			return true;
		default:
			return false;
	}
}

// ============================================================================
// rdv_CompileBlock
//
// Decodes, analyses, and compiles one SH4 basic block into native PPC code.
// Returns a pointer to the entry point, or NULL on failure.
//
// Cache pressure check:
//   We use CACHE_MIN_FREE as a hard floor. If we are below it we clear first.
//   Between CACHE_WARN_THRESHOLD and CACHE_FLUSH_THRESHOLD we log a warning
//   but continue — giving time for blocks that are rarely re-used to be
//   displaced naturally before forcing a full eviction.
// ============================================================================
DynarecCodeEntry* rdv_CompileBlock(u32 bpc)
{
#if HOST_OS == OS_WII
	// Update high-water mark
	if (LastAddr > cache_high_water_mark)
	{
		cache_high_water_mark = LastAddr;

		if (cache_high_water_mark >= CACHE_WARN_THRESHOLD &&
		    cache_high_water_mark < CACHE_FLUSH_THRESHOLD)
		{
			printf("recSh4: cache %u%% full (warning zone)\n",
			       (cache_high_water_mark * 100u) / CODE_SIZE);
		}
	}

	// Hard floor: clear if we cannot safely fit another block
	if (emit_FreeSpace() < CACHE_MIN_FREE)
	{
		printf("recSh4: cache below min-free threshold, clearing (free=%u bytes)\n",
		       emit_FreeSpace());
		recSh4_ClearCache();
	}
	// Proactive clear at high-water threshold
	else if (LastAddr >= CACHE_FLUSH_THRESHOLD)
	{
		printf("recSh4: cache above flush threshold (%u%%), clearing\n",
		       (LastAddr * 100u) / CODE_SIZE);
		recSh4_ClearCache();
	}
#else
	// Non-Wii: simple floor check (64 KB)
	if (emit_FreeSpace() < 65536)
	{
		printf("recSh4: WARNING - low cache space (%u bytes), clearing\n",
		       emit_FreeSpace());
		recSh4_ClearCache();
	}
#endif

	// Decode
	DecodedBlock* blk = dec_DecodeBlock(bpc, fpscr, SH4_TIMESLICE / 2);
	if (!blk)
	{
		printf("recSh4: ERROR - decode failed at %08X\n", bpc);
		return 0;
	}

	AnalyseBlock(blk);

	// Remember where code for this block starts (for I-cache flush)
#if HOST_OS == OS_WII
	void* code_start = emit_GetCCPtr();
#endif

	DynarecCodeEntry* rv = ngen_Compile(blk, DoCheck(blk->start));

#if HOST_OS == OS_WII
	if (rv)
	{
		u32 code_size = (u32)((u8*)emit_GetCCPtr() - (u8*)code_start);
		wii_sync_icache(code_start, code_size);
	}
#endif

	dec_Cleanup();

#ifdef ENABLE_PERF_MONITORING
	if (rv) perf_blocks_compiled++;
#endif

	return rv;
}

// ============================================================================
// rdv_CompilePC
//
// Compiles the block at next_pc, registers it with the block manager, and
// returns its entry point.
//
// Boot detection note (BUG FIX):
//   The previous version called recSh4_ClearCache() AFTER bm_AddCode(),
//   which immediately invalidated the block we just added — causing
//   rdv_FailedToFindBlock to be called on the very next execution of that
//   address, wasting a full compile cycle every boot iteration.
//
//   Fix: boot-triggered cache clears are now DEFERRED — we set a flag and
//   clear at the top of the NEXT compile cycle, after the current block has
//   been safely executed at least once.
// ============================================================================

// Set to true when a boot address is detected; acted on next compile cycle.
static bool s_deferred_cache_clear = false;
// PC that triggered the deferred clear (for logging)
static u32  s_deferred_clear_pc    = 0;

u32 rdv_FailedToFindBlock_pc;

DynarecCodeEntry* rdv_CompilePC()
{
	u32 pc = next_pc;

	// Handle any deferred cache clear from a previous boot-detect event.
	// This happens at the START of a new compile, not after adding a block,
	// so we never immediately invalidate the block we are about to create.
	if (s_deferred_cache_clear)
	{
		printf("recSh4: deferred cache clear (triggered by boot at %08X)\n",
		       s_deferred_clear_pc);
		s_deferred_cache_clear = false;
		s_deferred_clear_pc    = 0;
		recSh4_ClearCache();
	}

	DynarecCodeEntry* rv = rdv_CompileBlock(pc);

	if (!rv)
	{
		// First attempt failed (decode/ngen error). Retry with a clean cache.
		printf("recSh4: WARNING - compile failed at %08X, retrying after cache clear\n", pc);
		recSh4_ClearCache();
		rv = rdv_CompileBlock(pc);

		if (!rv)
		{
			printf("recSh4: FATAL - compile still failed at %08X after cache clear\n", pc);
			return 0;
		}
	}

	bm_AddCode(pc, rv);

	// Boot detection — schedule a cache clear for NEXT compile, not now.
	// This preserves the block we just registered so the current execution
	// context can use it without hitting rdv_FailedToFindBlock immediately.
	if (get_debug_loop() == 1)
	{
		u32 masked = pc & 0xFFFFFF;
		if (masked == 0x08300 || masked == 0x10000)
		{
			printf("recSh4: boot address %08X detected, scheduling deferred cache clear\n", pc);
			s_deferred_cache_clear = true;
			s_deferred_clear_pc    = pc;
		}
	}

	return rv;
}

// ============================================================================
// Block lookup and fallback handlers
// ============================================================================

DynarecCodeEntry* rdv_FailedToFindBlock()
{
	next_pc = rdv_FailedToFindBlock_pc;

	if (get_debug_loop() == 1)
		printf("recSh4: rdv_FailedToFindBlock at %08X\n", next_pc);

	return rdv_CompilePC();
}

DynarecCodeEntry* FASTCALL rdv_BlockCheckFail(u32 pc)
{
	next_pc = pc;

#ifdef ENABLE_PERF_MONITORING
	perf_block_check_fails++;
#endif

	printf("recSh4: block check fail at %08X, recompiling\n", pc);

	bm_RemoveCode(pc);
	return rdv_CompilePC();
}

DynarecCodeEntry* rdv_FindCode()
{
	DynarecCodeEntry* rv = bm_GetCode(next_pc);
	return (rv == ngen_FailedToFindBlock) ? 0 : rv;
}

DynarecCodeEntry* rdv_FindOrCompile()
{
	DynarecCodeEntry* rv = bm_GetCode(next_pc);
	if (rv == ngen_FailedToFindBlock)
		rv = rdv_CompilePC();
	return rv;
}

// ============================================================================
// Main execution loop
// ============================================================================

void recSh4_Run()
{
	sh4_int_bCpuRun = true;

	// Latch the accuracy-preset timing parameters BEFORE ngen_mainloop():
	// the mainloop is emitted once, on first entry, with the timeslice baked
	// into the generated code. Without this the JIT always ran the ACCURATE
	// 448-cycle timeslice while UpdateSystem advanced peripherals by
	// s_timeslice — consistent only because the preset was never applied in
	// rec mode. Now FAST/BALANCED cut the UpdateSystem call rate 4x/2x.
	Sh4_ApplyAccuracyPreset();

	printf("recSh4: starting dynarec execution\n");

#ifdef ENABLE_PERF_MONITORING
	u32 blocks_at_start = perf_blocks_compiled;
#endif

	ngen_mainloop();

#ifdef ENABLE_PERF_MONITORING
	printf("recSh4: execution stopped — compiled %u blocks this session\n",
	       perf_blocks_compiled - blocks_at_start);
#endif

	sh4_int_bCpuRun = false;
}

// ============================================================================
// Emulator control
// ============================================================================

void recSh4_Stop()  { Sh4_int_Stop(); }
void recSh4_Step()  { Sh4_int_Step(); }
void recSh4_Skip()  { Sh4_int_Skip(); }

void recSh4_Reset(bool Manual)
{
	Sh4_int_Reset(Manual);
	recSh4_ClearCache();
}

// ============================================================================
// Initialization
// ============================================================================

void recSh4_Init()
{
	printf("recSh4: initializing dynarec\n");

	Sh4_int_Init();
	bm_Init();

	s_deferred_cache_clear = false;
	s_deferred_clear_pc    = 0;

#ifdef ENABLE_PERF_MONITORING
	perf_blocks_compiled   = 0;
	perf_cache_clears      = 0;
	perf_block_check_fails = 0;
#endif

	// Platform-specific: make the code cache executable
#if HOST_OS == OS_WINDOWS
	DWORD old;
	VirtualProtect(CodeCache, CODE_SIZE, PAGE_EXECUTE_READWRITE, &old);
	printf("recSh4: code cache at %p (%u KB, Windows)\n",
	       CodeCache, CODE_SIZE / 1024);

#elif HOST_OS == OS_LINUX
	CodeCache = (u8*)(((uintptr_t)pMemBuff + 4095) & ~(uintptr_t)4095);
	printf("recSh4: code cache at %p (aligned from %p, Linux)\n",
	       CodeCache, pMemBuff);

	if (mprotect(CodeCache, CODE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE))
	{
		perror("recSh4: ERROR - mprotect failed");
		verify(false);
	}
	memset(CodeCache, 0xCD, CODE_SIZE);

#elif HOST_OS == OS_WII
	printf("recSh4: code cache at %p (%u KB, Wii)\n",
	       CodeCache, CODE_SIZE / 1024);

	memset(CodeCache, 0x00, CODE_SIZE);

	// Ensure the zeroed data is visible to the instruction fetch unit
	// before any code is written or executed.
	DCFlushRange(CodeCache, CODE_SIZE);
	ICInvalidateRange(CodeCache, CODE_SIZE);

	cache_high_water_mark = 0;
	printf("recSh4: Wii code cache flushed\n");
#endif

	LastAddr     = 0;
	LastAddr_min = 0;
	emit_ptr     = 0;

	printf("recSh4: initialization complete (cache=%u KB)\n",
	       CODE_SIZE / 1024);
}

// ============================================================================
// Termination
// ============================================================================

void recSh4_Term()
{
	printf("recSh4: terminating dynarec\n");

#ifdef ENABLE_PERF_MONITORING
	printf("recSh4 stats:\n");
	printf("  blocks compiled   : %u\n", perf_blocks_compiled);
	printf("  cache clears      : %u\n", perf_cache_clears);
	printf("  block check fails : %u\n", perf_block_check_fails);
	printf("  cache usage       : %u / %u bytes (%.1f%%)\n",
	       LastAddr, CODE_SIZE, (LastAddr * 100.0f) / CODE_SIZE);
#if HOST_OS == OS_WII
	printf("  high water mark   : %u bytes (%.1f%%)\n",
	       cache_high_water_mark,
	       (cache_high_water_mark * 100.0f) / CODE_SIZE);
#endif

#ifdef BM_ENABLE_STATS
	u32 hits, misses, cache_hits, total_blocks;
	bm_GetStats(&hits, &misses, &cache_hits, &total_blocks);
	printf("  bm hits           : %u\n", hits);
	printf("  bm misses         : %u\n", misses);
	printf("  bm cache hits     : %u\n", cache_hits);
	printf("  bm total blocks   : %u\n", total_blocks);
	if (hits + cache_hits > 0)
		printf("  bm efficiency     : %.2f%%\n",
		       (cache_hits * 100.0f) / (hits + cache_hits));
#endif
#endif // ENABLE_PERF_MONITORING

	Sh4_int_Term();

#if HOST_OS == OS_WII
	printf("recSh4: Wii cleanup complete\n");
#endif
}

bool recSh4_IsCpuRunning()
{
	return Sh4_int_IsCpuRunning();
}

// ============================================================================
// Debug utilities
// ============================================================================

void recSh4_GetCacheStats(u32* total_size, u32* used_size, u32* free_size)
{
	if (total_size) *total_size = CODE_SIZE;
	if (used_size)  *used_size  = LastAddr;
	if (free_size)  *free_size  = CODE_SIZE - LastAddr;
}

#ifdef ENABLE_PERF_MONITORING
void recSh4_GetPerfStats(u32* blocks_compiled, u32* cache_clears, u32* check_failures)
{
	if (blocks_compiled) *blocks_compiled = perf_blocks_compiled;
	if (cache_clears)    *cache_clears    = perf_cache_clears;
	if (check_failures)  *check_failures  = perf_block_check_fails;
}

void recSh4_ResetPerfStats()
{
	perf_blocks_compiled   = 0;
	perf_cache_clears      = 0;
	perf_block_check_fails = 0;
}
#endif // ENABLE_PERF_MONITORING

#endif // HOST_NO_REC

// ============================================================================
// Interface table
// ============================================================================

void Get_Sh4Recompiler(sh4_if* rv)
{
#ifdef HOST_NO_REC
	Get_Sh4Interpreter(rv);
#else
	rv->Run           = recSh4_Run;
	rv->Stop          = recSh4_Stop;
	rv->Step          = recSh4_Step;
	rv->Skip          = recSh4_Skip;
	rv->Reset         = recSh4_Reset;
	rv->Init          = recSh4_Init;
	rv->Term          = recSh4_Term;
	rv->IsCpuRunning  = recSh4_IsCpuRunning;
	rv->ResetCache    = recSh4_ClearCache;
#endif
}
