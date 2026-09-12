#pragma once
#include "types.h"

// ---------------------------------------------------------------------------
// C CALL-OUT CENSUS  (JIT_CCALLS preset, default OFF)
//
// "Check the number of C/helper function call-outs per frame, and reduce the
// heck outta those too."  -- dave
//
// Every time translated SH4 code leaves the dynarec and enters a C function it
// pays the call, whatever the callee costs, and the return. This counts how
// often each of those hand-offs actually happens, per second and per emulated
// frame, so the next optimisation is aimed at measured traffic instead of at
// whatever looks expensive when reading the emitter.
//
// That distinction is not academic here. The only JIT change in this codebase
// that ever moved a game (jit_fschg, +10% in ChuChu) was found by a counter
// showing 1.3 M sync_fpscr/s. Every change argued from static instruction
// counts -- superblocks, bcache, jit_align, hot-block clustering, dyn_ic,
// jit_cr0 -- has landed between 0% and 3%. So: count first.
//
// WHERE THE COUNTERS LIVE: inside the C functions themselves, not in emitted
// code. A callee that is already paying a function call does not notice one
// predicted-not-taken branch, so this perturbs far less than JIT_HOTBLOCKS
// (which costs a measured 12.6% because it bumps a counter per BLOCK ENTRY).
// Rates from a census run can be trusted; treat the SPEED% from one as
// very slightly pessimistic and re-measure speed with the preset off.
//
// NOT COUNTED HERE: shop_ifb (that is IFB_PROBE's job, and it is already
// closed -- measured at 2 calls/SECOND on hardware), and the fastmem DSI
// trampolines, which are patched-in branches rather than C calls.
//
// READ CC_READMEM / CC_WRITEMEM WITH CARE. Those counters sit on the shared
// _vmem dispatchers, which the interpreter, DMA and the debugger also use, so
// they are an UPPER BOUND on the JIT's own memory call-outs rather than an
// exact figure. Every other counter here is reached only from translated code
// (or from the mainloop that drives it), so those are exact.
// ---------------------------------------------------------------------------

enum CCallSite
{
	CC_TIMESLICE = 0,  // UpdateSystem_no_event  - the per-timeslice peripheral tick
	CC_INTERRUPT,      // UpdateSystem_handle_event / UpdateINTC - a real dispatch
	CC_GETCODE,        // bm_GetCode - dynamic-branch cache miss, or timeslice resume
	CC_SQW,            // do_sqw - store-queue flush (this is the TA submission path)
	CC_SYNC_SR,        // UpdateSR - SR write side effects
	CC_SYNC_FPSCR,     // UpdateFPSCR - FULL path only; jit_fschg bypasses it
	CC_FSQRT,          // rec_fsqrt -> libm sqrtf (the 750 has no hardware fsqrt)
	CC_FSRRA,          // rec_fsrra -> 1.0f/sqrtf
	CC_READMEM,        // _vmem_ReadMem*  - the inline/fastmem fast path was missed
	CC_WRITEMEM,       // _vmem_WriteMem*
	CC_MAC_SAT,        // sh4_mac_l / sh4_mac_w - the rare SR.S saturation path
	CC_COUNT
};

// Defined in dc/sh4/rec_v2/shil.cpp so every target links without needing a
// symbol out of wii/main.cpp; the Wii menu and game_presets just write to it.
extern "C" u32 g_ccall[CC_COUNT];
extern "C" int g_jit_ccalls_preset;

// One load + one predicted-not-taken branch when the preset is off.
#define CCALL(site) do { if (g_jit_ccalls_preset) g_ccall[site]++; } while (0)

// ---------------------------------------------------------------------------
// Per-AREA breakdown for the memory dispatchers.
//
// The first census run on Crazy Taxi showed 1.1 M writemem/s against 2.7 K
// readmem/s -- a 412x asymmetry -- which says the two are hitting completely
// different address ranges, not that the write path is broken. Which range
// decides whether anything can be done about it:
//
//   area 1 (VRAM)  -> could be an inlined store plus a texture-cache dirty
//                     mark, so the C dispatcher would go away entirely
//   area 4 (TA)    -> must stay, every write drives the TA state machine
//   area 0 (MMIO)  -> must stay
//   area 3 (RAM)   -> would mean FASTMEM is failing on addresses it should
//                     be handling directly, i.e. a real bug
//
// SH4 area = bits 26..28 of the 29-bit physical address, so this is one
// rlwinm's worth of work on the counted path.
// ---------------------------------------------------------------------------
enum { CCA_COUNT = 8 };
extern "C" u32 g_ccall_area[2][CCA_COUNT];   // [0]=read, [1]=write

// ---------------------------------------------------------------------------
// Caller-region split for the memory dispatchers.
//
// The per-area split says WHERE the writes go (95% system RAM). It cannot say
// HOW they arrive. WriteMem32 is a plain #define onto _vmem_WriteMem32, so the
// return address IS the real caller, and one range test sorts them into:
//
//   * a back-patched FASTMEM trampoline, living in s_fm_pool
//   * ordinary emitted code in CodeCache
//   * anything else -- i.e. the emulator's own C code
//
// This is what closed the question. On Crazy Taxi, 95.6% of system-RAM writes
// came back "other": block transfers (do_sqw and DMA) running the dispatcher
// once per 32-bit word from plain C, nothing to do with the JIT at all. See
// WriteMemBlock_nommu_ptr() and _vmem_GetBlockPtr() in dc/mem. Keep the split
// around -- "which side of the JIT boundary is this traffic even on" turned
// out to be the question worth asking first.
// ---------------------------------------------------------------------------
enum { CCS_TRAMP = 0, CCS_CODECACHE = 1, CCS_OTHER = 2, CCS_COUNT = 3 };
extern "C" u32 g_ccall_src[2][CCS_COUNT];

// Published by the Wii driver at cache-clear time; null everywhere else, which
// simply classifies everything as CCS_OTHER rather than misreporting.
extern "C" const u8* g_ccall_tramp_lo;
extern "C" const u8* g_ccall_tramp_hi;
extern "C" const u8* g_ccall_cc_lo;
extern "C" const u8* g_ccall_cc_hi;

static inline u32 ccall_src_of(const void* ra)
{
	const u8* q = (const u8*)ra;
	if (q >= g_ccall_tramp_lo && q < g_ccall_tramp_hi) return CCS_TRAMP;
	if (q >= g_ccall_cc_lo    && q < g_ccall_cc_hi)    return CCS_CODECACHE;
	return CCS_OTHER;
}

#define CCALL_MEM(site, rw, addr) \
	do { \
		if (g_jit_ccalls_preset) \
		{ \
			g_ccall[site]++; \
			g_ccall_area[rw][((u32)(addr) >> 26) & 7]++; \
			g_ccall_src[rw][ccall_src_of(__builtin_return_address(0))]++; \
		} \
	} while (0)

// Prints the census and resets it. Called once a second from the SPG stats
// block (plugs/drkPvr/SPG.cpp) so it shares the stats line's time base and
// fflush. `vbs` is vblanks/sec, used for the per-frame column.
extern "C" void ccall_census_dump(double seconds, double vbs);
