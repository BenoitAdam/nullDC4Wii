#pragma once
// plugin_header.h, not types.h: this header is included from ImgReader.cpp,
// which defines its own verify() macro before anything else and would collide
// with the one types.h defines. plugin_header.h carries config.h (HOST_OS /
// OS_WII) and the u32/u64 typedefs, which is all this header actually needs.
#include "plugins/plugin_header.h"

// ---------------------------------------------------------------------------
// FRAME_PROF - per-frame tail profiler ("where does the p99 frame go?")
//
// Everything else in this emulator measures AVERAGES over one second: the
// stats line's rnd:/ta:/arm:/aica:, the [TEXC] report, [HOT], [CC], [IFB].
// Those answer "what is slow", and they are the right tool for raising the
// steady-state SPEED%. They are the WRONG tool for a stutter, because a
// stutter is by definition a small number of frames: 3 bad frames a second
// that each eat 20 ms extra move a 1-second average by 6%, which is noise
// next to the 60 good frames around them. The spike is real and visible and
// the average says nothing happened.
//
// So this one keeps every frame instead of averaging them. One record per
// EMULATED vblank goes into a ring (the last ~8.5 s at 60 Hz); every couple of
// seconds the dump sorts the frame times, prints p50/p90/p95/p99/max, and then
// does the part that actually matters:
//
//   * the mean per-bucket breakdown of a TYPICAL frame (<= p50), and
//   * the mean per-bucket breakdown of the TAIL frames (>= p95), and
//   * the DIFFERENCE between the two, ranked.
//
// That ranked difference is the flame graph of the stutter. "tail frames spend
// +9.8 ms in tex and +1.2 ms in jit" names the culprit outright, instead of
// leaving you to infer it from an average that never moved.
//
// The buckets are a DISJOINT partition of the frame, so they sum to the frame
// time and the percentages mean something:
//
//   sh4   SH4 emulation - the JIT/interpreter itself, plus anything not
//         broken out below. The residual, so bracket errors land here.
//   rend  DoRender(), net of the two things below that happen inside it
//   tex   texture decode + upload (the cache-miss path in SetTextureParams)
//   gxw   blocking waits on the GP - every GX_DrawDone/GX_WaitDrawDone in
//         gxRend.cpp goes through one wrapper, so this is the whole stall
//   ta    TA list decode           (needs TA PROFILE on; reads 0 otherwise)
//   arm   ARM7 core
//   aica  AICA synthesis, net of the pacing sleep below
//   snd   audio pacing sleep in wii_audio_push_sample()
//   jit   rdv_CompilePC() - decode + ngen_Compile + any cache clear it forces
//   disc  libGDR_ReadSector() - the SD/USB fread behind the emulated GD-ROM
//   pad   host controller reads - maple_DoDma() and the scanline-driven
//         exit-combo poll, i.e. PAD/WPAD_ScanPads and Sixaxis USB traffic
//
// Per-frame EVENT counts ride along, because a spike's size is only half the
// story and its cause is usually the count: dec= textures decoded, blk= blocks
// compiled, clr= JIT cache clears, rd= sector reads, wrap= texture-arena
// wraps, pad= host input polls.
//
// vi= is the odd one out and the reason a "tiny stutter" is sometimes NOT in
// any bucket above. The SH4 thread deliberately never VIDEO_WaitVSync()es, so
// presents free-run against the VI. Perfectly even 16.6 ms frames still judder
// if two of them land inside one VI field and the next field repeats. The VI
// post-retrace callback counts real fields between vblanks: a healthy run is
// almost all 1x, and a column of 2x/0x is a present-cadence problem that no
// amount of CPU optimisation will fix.
//
// Cost while OFF: one r13-relative load and a not-taken branch at each bracket,
// and nothing is allocated (the ring is lazily malloc'd when it first turns on,
// like the JIT HOTBLOCKS table). Cost while ON: two time-base reads per
// bracketed event - the busiest is texture decode, and a decode is thousands of
// cycles - plus ~10 reads and a 52-byte store once per emulated frame.
//
// The frame boundary is the emulated vblank in SPG.cpp, and the measured span
// deliberately EXCLUDES the speed limiter's sleep and this profiler's own
// printf: frame_prof_vblank() closes the span at the top of the vblank handler,
// frame_prof_resume() reopens it at the bottom after spg_PaceFrame(). Without
// that the limiter would pad every cheap frame out to exactly one frame period
// and flatten the very distribution we came to look at.
// ---------------------------------------------------------------------------

#if HOST_OS == OS_WII
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
// Single mftb of the low word, same trick and same wrap-exactness argument as
// PERF_TICKS32() in Renderer_if.h: only deltas are ever computed from it, and
// unsigned 32-bit subtraction is exact modulo 2^32. The time base runs at
// bus/4 = 60.75 MHz, so a bracket would have to last 70 s to be ambiguous.
#define FP_TICKS32() ((u32)PPCMftb())
#else
#define FP_TICKS32() ((u32)0)
#endif

// 0 = off, 1 = summary (percentiles + typical/tail/excess), 2 = summary plus a
// one-line dump of each individual tail frame. Defined in frame_prof.cpp, not
// in wii/main.cpp, so plugs/ and dc/ link without depending on a symbol from
// wii/ - same reasoning as g_ta_profile_preset in Renderer_if.cpp.
extern "C" int g_frame_prof_preset;

#define FRAME_PROF_ON() (__builtin_expect(g_frame_prof_preset != 0, 0))

// Live accumulators for the frame in progress. Reset by frame_prof_vblank()
// once it has folded them into the ring.
struct FrameProfAcc
{
  u32 tex;       // ticks decoding/uploading textures   (inside rend)
  u32 jit;       // ticks in rdv_CompilePC()
  u32 disc;      // ticks in libGDR_ReadSector()
  u32 pad;       // ticks reading host controllers
  u32 gxw;       // ticks blocked on the GP, all call sites
  u32 gxw_rend;  // ...of which were taken inside DoRender(), so `rend` can be
                 // reported net of them without losing the ones taken outside
  u32 n_tex;     // textures decoded
  u32 n_jit;     // blocks compiled
  u32 n_clear;   // JIT cache clears (each one costs a recompile storm after it)
  u32 n_disc;    // libGDR_ReadSector() calls
  u32 n_wrap;    // texture arena wraps (a wrap re-decodes the whole working set)
  u32 n_pad;     // host input polls
};
extern FrameProfAcc g_fp;

// Bracket helpers. Written as macros rather than a scoped RAII object so the
// OFF path is provably a load/branch with no object to construct and nothing
// live across the measured call.
#define FP_T0(v)          u32 v = FRAME_PROF_ON() ? FP_TICKS32() : 0u
#define FP_ACC(field, v)  do { if (FRAME_PROF_ON()) g_fp.field += FP_TICKS32() - (v); } while (0)
#define FP_BUMP(field)    do { if (FRAME_PROF_ON()) g_fp.field++; } while (0)

// Called from the emulated-vblank handler in SPG.cpp.
void frame_prof_vblank();          // close the frame, fold it into the ring
void frame_prof_resume();          // reopen the span after the stats + pacing
void frame_prof_dump(double secs); // no-op unless the preset is on
