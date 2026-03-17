#pragma once
#include "aica.h"

void AICA_Sample();
void WriteChannelReg8(u32 channel, u32 reg);

void sgc_Init();
void sgc_Term();

// Fixed-point types — bitfield order reversed on big-endian (WII)
union fp_22_10
{
    struct
    {
#ifdef WII
        u32 ip:22;
        u32 fp:10;
#else
        u32 fp:10;
        u32 ip:22;
#endif
    };
    u32 full;
};

union fp_s_22_10
{
    struct
    {
#ifdef WII
        s32 ip:22;
        u32 fp:10;
#else
        u32 fp:10;
        s32 ip:22;
#endif
    };
    s32 full;
};

union fp_20_12
{
    struct
    {
#ifdef WII
        u32 ip:20;
        u32 fp:12;
#else
        u32 fp:12;
        u32 ip:20;
#endif
    };
    u32 full;
};

typedef s32 SampleType;

extern SampleType mixl;
extern SampleType mixr;

void ReadCommonReg(u32 reg, bool byte);
void WriteCommonReg8(u32 reg, u32 data);

#define clip(x,mn,mx)  if((x)<(mn)) (x)=(mn); if((x)>(mx)) (x)=(mx);
#define clip16(x)      clip(x,-32768,32767)
