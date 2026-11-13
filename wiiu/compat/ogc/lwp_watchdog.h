// wiiu/compat/ogc/lwp_watchdog.h
//
// Wii U build only (wiiu/Makefile puts wiiu/compat/ first on the include path).
// The shared core includes <ogc/lwp_watchdog.h> for libogc's time-base helpers
// (gettime(), ticks_to_*(), TB_TIMER_CLOCK). This maps them onto Cafe OS's
// time base, which is the same PowerPC TB register, just clocked differently:
// bus/4 = 62.15625 MHz on Wii U vs 60.75 MHz on Wii. Everything that only ever
// takes DELTAS of gettime() (the stats brackets, SOUND_CLOCK real time) works
// unchanged as long as TB_TIMER_CLOCK and the ticks_to_*() conversions agree,
// and here they both come from OSTimerClockSpeed.
#pragma once

#include <stdint.h>
#include <coreinit/time.h>

// libogc: ticks per MILLISECOND (kHz). A runtime value on Wii U (read from
// OSGetSystemInfo()), so it must only be used in expressions, never in #if.
#ifndef TB_TIMER_CLOCK
#define TB_TIMER_CLOCK ((uint32_t)(OSTimerClockSpeed / 1000u))
#endif

static inline uint64_t gettime(void)
{
    return (uint64_t)OSGetSystemTime();
}

static inline uint32_t diff_sec(uint64_t start, uint64_t end)
{
    return (uint32_t)((end - start) / (uint64_t)OSTimerClockSpeed);
}

static inline uint32_t diff_msec(uint64_t start, uint64_t end)
{
    return (uint32_t)((end - start) / (uint64_t)TB_TIMER_CLOCK);
}

static inline uint32_t diff_usec(uint64_t start, uint64_t end)
{
    return (uint32_t)(((end - start) * 1000ull) / (uint64_t)TB_TIMER_CLOCK);
}

static inline uint64_t diff_ticks(uint64_t start, uint64_t end)
{
    return end - start;
}

// Same rounding as libogc's macros (integer division, truncating).
static inline uint64_t ticks_to_secs(uint64_t ticks)      { return ticks / (uint64_t)OSTimerClockSpeed; }
static inline uint64_t ticks_to_millisecs(uint64_t ticks) { return ticks / (uint64_t)TB_TIMER_CLOCK; }
static inline uint64_t ticks_to_microsecs(uint64_t ticks) { return (ticks * 1000ull) / (uint64_t)TB_TIMER_CLOCK; }
static inline uint64_t ticks_to_nanosecs(uint64_t ticks)  { return (ticks * 1000000ull) / (uint64_t)TB_TIMER_CLOCK; }

static inline uint64_t secs_to_ticks(uint64_t s)          { return s * (uint64_t)OSTimerClockSpeed; }
static inline uint64_t millisecs_to_ticks(uint64_t ms)    { return ms * (uint64_t)TB_TIMER_CLOCK; }
static inline uint64_t microsecs_to_ticks(uint64_t us)    { return (us * (uint64_t)TB_TIMER_CLOCK) / 1000ull; }

#include "ogc/machine/processor.h"
