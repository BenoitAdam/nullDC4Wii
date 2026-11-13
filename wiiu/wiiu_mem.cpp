// wiiu/wiiu_mem.cpp
//
// Memory setup for the Wii U build: the "MEM2 arena" stand-in that the shared
// core's _vmem_reserve() (dc/mem/_vmem.cpp) bump-allocates the Dreamcast RAM,
// VRAM, ARAM and render buffer from, through the libogc-named calls in
// wiiu/compat/gccore.h (SYS_GetArena2Lo/Hi, SYS_SetArena2Lo/Hi).
//
// libogc's arena is "whatever MEM2 IOS left us"; here it is one block carved
// from the Cafe OS default heap at boot, WIIU_ARENA_SIZE bytes. The core then
// moves Lo up / Hi down exactly as it does on the Wii, its own budget check
// sees real numbers, and dc/mem/ needs no Wii U branch.
//
// No fastmem replacement yet: the Wii's fastmem (wii/wii_fastmem.cpp) is an
// MMU page table plus a DSI handler, and both need the SH4 recompiler, which
// the Wii U build does not have (interpreter only, see config.h _WIIU). The
// interpreter goes through the normal ReadMem/WriteMem handlers.

#include <stdio.h>
#include <stdint.h>
#include <coreinit/memdefaultheap.h>

#include "wiiu/wiiu_config.h"
#include "wiiu/wiiu_mem.h"

static uint8_t *s_arena_base = nullptr;
static uint8_t *s_arena_lo   = nullptr;
static uint8_t *s_arena_hi   = nullptr;

extern "C" bool WiiU_ArenaInit(void)
{
    if (s_arena_base)
        return true;

    // 64 KB alignment: _vmem_reserve() rounds Lo up to 64 KB anyway, so this
    // just keeps the first carve from wasting most of a 64 KB step.
    s_arena_base = (uint8_t *)MEMAllocFromDefaultHeapEx(WIIU_ARENA_SIZE, 0x10000);
    if (!s_arena_base)
    {
        printf("[wiiu_mem] arena: %u MB allocation FAILED\n", WIIU_ARENA_SIZE >> 20);
        return false;
    }

    s_arena_lo = s_arena_base;
    s_arena_hi = s_arena_base + WIIU_ARENA_SIZE;
    printf("[wiiu_mem] arena: %p - %p (%u MB)\n",
           (void *)s_arena_lo, (void *)s_arena_hi, WIIU_ARENA_SIZE >> 20);
    return true;
}

extern "C" void WiiU_ArenaTerm(void)
{
    // Only called on the way out (wiiu/wiiu_exit.cpp), after the emulator has
    // stopped touching the Dreamcast memory living in here.
    if (s_arena_base)
        MEMFreeToDefaultHeap(s_arena_base);
    s_arena_base = s_arena_lo = s_arena_hi = nullptr;
}

// dc/mem/_vmem.cpp calls this first thing in _vmem_reserve() to place the Wii
// fastmem page table. No fastmem on Wii U (see the header): nothing to
// reserve, and "1" lets the boot carry on.
extern "C" int WiiFastmem_ReserveHtab()
{
    return 1;
}

// --- libogc arena API stand-ins (wiiu/compat/gccore.h) ----------------------
// Before WiiU_ArenaInit() both ends are null, so the core's budget check reads
// 0 bytes free and _vmem_reserve() fails cleanly instead of writing anywhere.

extern "C" void *WiiU_ArenaLo(void)        { return s_arena_lo; }
extern "C" void *WiiU_ArenaHi(void)        { return s_arena_hi; }
extern "C" void  WiiU_SetArenaLo(void *p)  { s_arena_lo = (uint8_t *)p; }
extern "C" void  WiiU_SetArenaHi(void *p)  { s_arena_hi = (uint8_t *)p; }
