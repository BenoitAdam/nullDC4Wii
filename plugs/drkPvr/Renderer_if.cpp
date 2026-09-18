#include "Renderer_if.h"


u32 VertexCount=0;
u32 StripCount=0;
u32 FrameCount=0;

u64 RenderTicks=0;
TaPerfCounters TaPerf={0,0};

// TA_PROFILE preset (wii/main.cpp menu, `ta_profile` cfg key). Lives here, not
// in wii/main.cpp, so plugs/ links without depending on a symbol from wii/ --
// same reasoning as g_jit_ccalls_preset in dc/sh4/rec_v2/shil.cpp.
// 0 = off (default), 1 = on.
extern "C" { int g_ta_profile_preset = 0; }

u64 ArmTicks=0;
u64 AicaTicks=0;
u64 SndWaitTicks=0;

u64 AicaVoxTicks=0;
u32 AicaVoiceSum=0;
u32 AicaSampleCount=0;

