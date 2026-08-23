#pragma once
//
// ── Where does the SH4 thread's second actually go? ──────────────────────────
//
// nullDC4Wii is single-threaded: SH4 dynarec, ARM7/AICA, TA vertex decode and
// GX submission all run on one thread, so "emulation speed" is just that
// thread's wall clock divided up. When a game runs at 60% speed the only
// question that matters is WHICH of those buckets is eating the other 40% —
// and it is not answerable by reading the code, because the answer is per-game
// and per-scene.
//
// Buckets are EXCLUSIVE (self time): a scope subtracts whatever nested scopes
// consumed inside it, so the numbers partition the second and "other" is a
// real residual rather than an overlap artefact. See FrameProfScope below.
//
//   rend     DoRender() + PresentFramebuffer(): GX submission, texture decode,
//            EFB copy, and (under ASYNC_RENDER) the wait on the previous
//            frame's GPU work. ALWAYS measured, because the frameskip AUTO
//            controller needs it — skipping a frame removes exactly this
//            bucket and nothing else, which makes it the hard ceiling on what
//            frame skipping can ever buy. SPG.cpp turns it into
//            spg_PaceBestSpeed: the speed reachable with every render dropped.
//
//   ta       libPvr_TaSQ / libPvr_TaDMA: the TA FIFO state machine and vertex
//            decode. Scales with scene complexity like rendering does, but
//            runs while the GAME writes the FIFO, long before StartRender —
//            so frame skipping does NOT remove it. If this bucket is large,
//            the fix is either a faster decode or a no-decode TA path for
//            skipped frames (Yabause's Vdp1NoDraw trick), not a controller.
//
//   aica     UpdateAica() + UpdateArm() out of MediumUpdate(): the AICA mixer
//            and the ARM7 sound CPU. Driven by EMULATED time, so its cost per
//            wall second tracks emulation speed and is unaffected by the
//            sh4_clock underclock. The arm7_speed preset is the lever here.
//
//   pvr      UpdatePvr() — the SPG scanline/vblank machine, which also hosts
//            the frame pacer. Includes the speed limiter's usleep when the
//            game is running AHEAD of schedule, so expect this to be large on
//            a game that is comfortably fast; it means the opposite of slow.
//
//   sys      The rest of the peripheral cascade out of UpdateSystem(): TMU,
//            DMA, GD-ROM, maple, RTC, the sched preset's tick.
//
//   snd-wait Blocked in wii_audio_push_sample()'s "wait for ASND to drain the
//            staging buffer" loop. Real time the emulator spends doing
//            nothing. Non-zero means audio is pacing emulation.
//
//   other    The residual: SH4 dynarec block execution plus every memory
//            access handler it calls. A game that spins in a register-polling
//            idle loop shows up here, and so does a genuinely CPU-heavy one —
//            the sh4_clock underclock preset separates the two, because it
//            cuts executed cycles per second without touching anything else.
//
// The profile buckets are compile-time gated because their call sites are hot
// (libPvr_TaSQ runs once per 32-byte store-queue write, UpdateSystem once per
// timeslice); expect a few percent of measurement overhead, which lands inside
// the bucket being measured. Flip FRAME_PROFILE to 1, rebuild, read one run's
// log, flip it back to 0.

// MEASUREMENT COST, and it is not small: gettime() is PPCGetTickCount(), a
// mftbu/mftb/mftbu retry loop, so each scope pays ~70-90 cycles for its two
// reads plus the bookkeeping. At ~475 K TA calls/s that is ~40 M cycles/s —
// roughly 5.5% of a 729 MHz second, landing INSIDE the ta bucket. Read a
// profiled `ta:18%` as more like 12-13% unprofiled, and never A/B a speed
// change with this on: measure real speed with FRAME_PROFILE 0.
#define FRAME_PROFILE 0

// config.h poisons BIG_ENDIAN / LITTLE_ENDIAN on purpose (see its top) and
// lwp_watchdog.h re-defines them through <time.h> -> machine/endian.h. Same
// dance wii_audio.cpp does before its libogc includes.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

#include <gctypes.h>
#include <ogc/lwp_watchdog.h> // gettime()

// Wall ticks spent inside DoRender()/PresentFramebuffer() since SPG.cpp last
// sampled it. Defined in gxRend.cpp, consumed once a second by the vblank
// stats block. Always on — the AUTO controller depends on it.
extern u64 g_fp_render_ticks;

#if FRAME_PROFILE

enum FrameProfBucket
{
    FP_REND = 0,
    FP_TA,
    FP_AICA,
    FP_PVR,
    FP_SYS,
    FP_SND,
    FP_BUCKETS
};

extern u64 g_fp_acc[FP_BUCKETS];  // EXCLUSIVE ticks in the current window
extern u64 g_fp_hits[FP_BUCKETS]; // entries in the current window

// Running total of every completed scope's INCLUSIVE span. A scope records the
// value at entry and, at exit, whatever the total grew by is exactly what its
// nested scopes spent — subtract it and what is left is self time. Each scope
// then rewrites the total to (its entry value + its own span) so its parent
// sees one child span, not the whole nested tree twice.
extern u64 g_fp_child_total;

struct FrameProfScope
{
    u64 t0;
    u64 child_at_entry;
    int b;

    FrameProfScope(int bucket)
        : t0(gettime()), child_at_entry(g_fp_child_total), b(bucket) {}

    ~FrameProfScope()
    {
        u64 span     = gettime() - t0;
        u64 children = g_fp_child_total - child_at_entry;
        g_fp_acc[b] += span - children;   // self time
        g_fp_hits[b]++;
        g_fp_child_total = child_at_entry + span;
    }
};

// Times the enclosing scope into `bucket`. Safe at any return point.
#define FP_SCOPE(bucket) FrameProfScope fp_scope_##bucket(bucket)

#endif // FRAME_PROFILE

// Render-cost meter. Always feeds g_fp_render_ticks (inclusive — that is what
// the futility gate wants); under FRAME_PROFILE it doubles as the FP_REND
// scope so the partition stays complete.
struct RenderCostScope
{
    u64 t0;
#if FRAME_PROFILE
    u64 child_at_entry;
#endif

    RenderCostScope() : t0(gettime())
    {
#if FRAME_PROFILE
        child_at_entry = g_fp_child_total;
#endif
    }

    ~RenderCostScope()
    {
        u64 span = gettime() - t0;
        g_fp_render_ticks += span;
#if FRAME_PROFILE
        u64 children = g_fp_child_total - child_at_entry;
        g_fp_acc[FP_REND] += span - children;
        g_fp_hits[FP_REND]++;
        g_fp_child_total = child_at_entry + span;
#endif
    }
};

#if !FRAME_PROFILE
#define FP_SCOPE(bucket) ((void)0)
#endif
