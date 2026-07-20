#pragma once
// ---------------------------------------------------------------------------
// wii_timeprof – coarse CPU-time buckets for the single Broadway core.
//
// Answers "where does the frame time go" directly: a few top-level phases
// are bracketed with timebase (mftb, bus/4 = 60.75 MHz) accumulators and
// tprof_print() reports each bucket as a percentage of wall time on the SPG
// 2-second stats tick:
//
//   [TIME] ta=..% render=..% sys=..% audio=..% compile=..% sh4+other=..%
//
//   ta      – TA FIFO parsing + vertex conversion (libPvr_TaSQ/TaDMA;
//             runs on the SH4 thread as the guest submits geometry)
//   render  – StartRender/DoRender: GX submission + present (SH4 thread)
//   sys     – peripheral cascade (AICA, ARM7, DMA, GDRom, Maple; MediumUpdate)
//   audio   – ASND voice callback (IRQ context)
//   compile – dynarec block compilation (ngen_Compile)
//   sh4+other = remainder ≈ executing translated SH4 code (+ emulator misc)
//
// Motivation (2026-07-19): JIT_PROFILE proved gameplay has zero interpreter/
// canonical fallbacks and ~zero slow-memory hits, so the remaining split
// between translated-code execution and the vertex pipeline decides where
// optimization effort goes. See memory: jit-profile-counters.
//
// Overhead: 2 mftb + 1 add per bracket (~10 cycles); the hottest bracket is
// TaSQ at one call per 32-byte burst — well under 1% total.
//
// Header is self-contained (no types.h dependency) so any Wii-only TU —
// core or plugin — can include it directly. Buckets are plain u32 wrapping
// counters; the printer works on deltas, so wrap is harmless.
// ---------------------------------------------------------------------------

#define TPROF_ENABLE 1

enum
{
    TP_TA,
    TP_RENDER,
    TP_AICA,      // AICA DSP/sample generation (UpdateAica)
    TP_ARM7,      // ARM7 sound-CPU interpreter (UpdateArm)
    TP_SYSMISC,   // DMA + GDRom + Maple + RTC (rest of the cascade)
    TP_AUDIO,
    TP_COMPILE,
    TP_NBUCKETS
};

#ifdef __cplusplus
extern "C" {
#endif

extern volatile unsigned int tprof_acc[TP_NBUCKETS];

// Print the split since the previous call (call from the 2s stats tick).
void tprof_print(void);

#ifdef __cplusplus
}
#endif

#if TPROF_ENABLE

static inline unsigned int tprof_now(void)
{
    unsigned int v;
    __asm__ __volatile__("mftb %0" : "=r"(v));
    return v;
}

static inline void tprof_leave(int bucket, unsigned int t0)
{
    tprof_acc[bucket] += tprof_now() - t0;
}

#else

static inline unsigned int tprof_now(void)                { return 0; }
static inline void tprof_leave(int bucket, unsigned int t0) { (void)bucket; (void)t0; }

#endif
