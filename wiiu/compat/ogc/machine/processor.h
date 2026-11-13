// wiiu/compat/ogc/machine/processor.h
//
// Wii U build only. The two libogc processor helpers the shared core uses:
// PPCMftb() (one mftb of the time base low word, for cheap 32-bit deltas) and
// PPCUsToTicks(). Espresso is a 750-family core, so the instruction is the same.
#pragma once

#include <stdint.h>
#include <coreinit/time.h>

static inline uint32_t PPCMftb(void)
{
    uint32_t tbl;
    __asm__ __volatile__("mftb %0" : "=r"(tbl));
    return tbl;
}

static inline uint64_t PPCUsToTicks(uint64_t us)
{
    return (us * (uint64_t)(OSTimerClockSpeed / 1000u)) / 1000ull;
}
