#pragma once
#include "drkPvr.h"
#include "ta.h"
//#include "TexCache.h"

extern u32 VertexCount;
extern u32 StripCount;
extern u32 FrameCount;

// ---------------------------------------------------------------------------
// Render / TA time split.
//
// v/st told us WHICH half of the render loop to attack (strips average ~5.3
// vertices, so the per-strip preamble dominates the per-vertex loop). It did
// not tell us whether the render loop is worth attacking AT ALL — an older
// profiler run put the frame at sh4:63% ta:18% aica:14% rend:3.5%, and if that
// still holds then halving the strip preamble buys under 2%.
//
// RenderTicks brackets DoRender(); TaTicks brackets the two TA entry points in
// ta.cpp. PERF_TICKS() is one mftb, so the render bracket (a few per frame) is
// free. The TA bracket is per SQ block / per DMA batch — TaCalls is reported
// alongside so its own overhead stays calculable rather than hidden.
// ---------------------------------------------------------------------------
extern u64 RenderTicks;

// TA accumulators, deliberately PAIRED in one struct. libPvr_TaSQ bumps both on
// every 32-byte TA block (~500 K/s), and as two separate globals that cost two
// lis/@ha address bases per call and touched two cache lines; as one struct it
// is one base plus offsets, in one line.
//
// `ticks` is u32, not u64: SPG.cpp zeroes it once a second and the Wii time base
// runs at bus/4 = 60.75 MHz, so the accumulator has ~70 seconds of headroom
// against a ~1-second window. That keeps the hot accumulate a single add instead
// of an addc/addze pair with two loads and two stores.
struct TaPerfCounters { u32 ticks; u32 calls; };
extern TaPerfCounters TaPerf;

// TA_PROFILE: gates the per-block bracket in libPvr_TaSQ/libPvr_TaDMA, i.e.
// whether `ta:` on the stats line is measured at all. Default OFF, because the
// bracket is not free at ~500 K calls/s: with it compiled in unconditionally
// libPvr_TaSQ was 24 PowerPC instructions with a stack frame around ~36
// instructions of real vertex work; with it off the function collapses to a
// frameless tail call to TaCmd. Turn it on when you are working on the TA.
extern "C" int g_ta_profile_preset;

// Sound split, same wall-time method. armUpdateARM (plugs/vbaARM/arm_aica.cpp)
// runs BOTH the ARM7 core and the 44.1 kHz AICA synthesis, so one bracket
// cannot say which of them to optimise (ARM7 JIT vs synthesis work):
//   ArmTicks     - arm_Run() only
//   AicaTicks    - the libAICA_TimeStep() loop, INCLUDING SndWaitTicks
//   SndWaitTicks - wii_audio_push_sample()'s pacing sleep (AudioBuffers >= 1),
//                  which is idle time, so the stats line subtracts it from aica.
// Brackets: ~3 mftb reads per armUpdateARM call (~56K calls/s in ACCURATE),
// roughly 0.1-0.2% of the CPU.
extern u64 ArmTicks;
extern u64 AicaTicks;
extern u64 SndWaitTicks;

// Inside aica: where synthesis time goes, printed as vox:N%(V).
//   AicaVoxTicks    - AICA_Sample()'s channel loop only (per-voice work);
//                     aica% minus vox% is the fixed per-sample work
//                     (CDDA, master volume, timers, interrupts, buffer push)
//   AicaVoiceSum    - voices stepped, summed over samples. Counted by the
//                     aica_fast loop only, so V reads 0 with aica_fast off.
//   AicaSampleCount - samples generated, to average AicaVoiceSum
// Cost: 2 mftb reads per sample (44,100/s), well under 0.1% of the CPU.
extern u64 AicaVoxTicks;
extern u32 AicaVoiceSum;
extern u32 AicaSampleCount;

#if HOST_OS == OS_WII
#include <ogc/lwp_watchdog.h>
#define PERF_TICKS() gettime()
#define PERF_TICKS_US(t) ((double)ticks_to_microsecs(t))

// Cheap 32-bit variant, for brackets taken hundreds of thousands of times
// a second. gettime() is NOT one instruction: it is the classic 64-bit
// time-base consistency loop, mftbu/mftb/mftbu/cmpw/bne, so a bracket pays
// SIX special-register reads plus a 64-bit subtract. PPCMftb() is a single
// mftb of the low word.
//
// Safe because the only thing ever computed from it is a DELTA, and
// unsigned 32-bit subtraction is exact modulo 2^32: a bracket that happens
// to straddle a TBL wrap still yields the right interval. The time base
// runs at bus/4 = 60.75 MHz on Wii, so TBL wraps every ~70 s — far longer
// than any bracket, and the accumulators below are u64 anyway.
#define PERF_TICKS32() ((u32)PPCMftb())
#else
#define PERF_TICKS() ((u64)0)
#define PERF_TICKS_US(t) (0.0)
#define PERF_TICKS32() ((u32)0)
#endif

// #include "gsRend.h" // PS2
#include "gxRend.h" // Wii
// #include "nullRend.h" // PSP
// #include "glesRend.h" // DirectX 11 ? OpenGL ? PS3 ?
// #include "softRend.h" // Sofware Render


// STRIP_DEDUP census dump (plugs/drkPvr/gxRend.cpp). Called once a second from
// SPG.cpp's stats line alongside the other census dumps; a no-op unless the
// strip_dedup preset is on. See the STRIP_DEDUP macro block in gxRend.cpp.
void strip_dedup_dump(double tdiff);
