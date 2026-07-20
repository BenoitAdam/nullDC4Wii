// wii_timeprof.cpp – see wii_timeprof.h for the design notes.

#include <stdio.h>
#include "wii_timeprof.h"

typedef unsigned int u32;
typedef unsigned long long u64;

volatile u32 tprof_acc[TP_NBUCKETS];

void tprof_print(void)
{
#if TPROF_ENABLE
    static u32 last[TP_NBUCKETS];
    static u32 last_tb;

    const u32 now  = tprof_now();
    const u32 wall = now - last_tb;

    if (last_tb == 0)
    {
        // First call: just take the baseline.
        last_tb = now;
        for (int i = 0; i < TP_NBUCKETS; i++)
            last[i] = tprof_acc[i];
        return;
    }
    if (wall == 0)
        return;
    last_tb = now;

    u32 pct[TP_NBUCKETS];
    u32 sum = 0;
    for (int i = 0; i < TP_NBUCKETS; i++)
    {
        const u32 cur = tprof_acc[i];
        const u32 d   = cur - last[i];
        last[i] = cur;
        pct[i]  = (u32)(((u64)d * 100u) / wall);
        sum    += pct[i];
    }

    printf("[TIME] ta=%u%% render=%u%% aica=%u%% arm7=%u%% sysmisc=%u%% audio=%u%% "
           "compile=%u%% sh4+other=%u%%\n",
           pct[TP_TA], pct[TP_RENDER], pct[TP_AICA], pct[TP_ARM7], pct[TP_SYSMISC],
           pct[TP_AUDIO], pct[TP_COMPILE],
           sum <= 100 ? 100 - sum : 0);
#endif
}
