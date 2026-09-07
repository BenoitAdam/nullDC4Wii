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
extern u64 TaTicks;
extern u32 TaCalls;

#if HOST_OS == OS_WII
#include <ogc/lwp_watchdog.h>
#define PERF_TICKS() gettime()
#define PERF_TICKS_US(t) ((double)ticks_to_microsecs(t))
#else
#define PERF_TICKS() ((u64)0)
#define PERF_TICKS_US(t) (0.0)
#endif

// #include "gsRend.h" // PS2
#include "gxRend.h" // Wii
// #include "nullRend.h" // PSP
// #include "glesRend.h" // DirectX 11 ? OpenGL ? PS3 ?
// #include "softRend.h" // Sofware Render

