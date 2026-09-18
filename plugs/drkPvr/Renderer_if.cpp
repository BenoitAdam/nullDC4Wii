#include "Renderer_if.h"


u32 VertexCount=0;
u32 StripCount=0;
u32 FrameCount=0;

u64 RenderTicks=0;
TaPerfCounters TaPerf={0,0};

u64 ArmTicks=0;
u64 AicaTicks=0;
u64 SndWaitTicks=0;

u64 AicaVoxTicks=0;
u32 AicaVoiceSum=0;
u32 AicaSampleCount=0;

