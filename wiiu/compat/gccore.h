// wiiu/compat/gccore.h
//
// Wii U build only: wiiu/Makefile puts wiiu/compat/ first on the include path,
// so the shared core's "#include <gccore.h>" (plugins/plugin_header.h, the
// OS_WIIU branch) lands here instead of on libogc. It maps ONLY the libogc
// calls the shared core (dc/, plugins/, plugs/ minus the Wii-only plugins)
// actually makes, measured with a grep over those trees:
//
//   SYS_GetArena2Lo/Hi, SYS_SetArena2Lo/Hi   dc/mem/_vmem.cpp (_vmem_reserve)
//   IRQ_Disable / IRQ_Restore                 dc/mem/_vmem.cpp
//   DCFlushRange / ICInvalidateRange          coreinit/cache.h has these names
//   gettime(), ticks_to_*(), TB_TIMER_CLOCK   compat ogc/lwp_watchdog.h
//
// Anything else from libogc is a compile error on purpose: it means a Wii-only
// file slipped into the Wii U source list (see wiiu/Makefile's filter-out).
//
// "MEM2 arena": _vmem_reserve() bump-allocates the Dreamcast RAM, VRAM, ARAM
// and the 2x VRAM render buffer out of libogc's MEM2 arena by moving
// Arena2Lo/Hi. Cafe OS has no such arena, so wiiu/wiiu_mem.cpp carves ONE
// block from the default heap at boot (WiiU_ArenaInit) and these calls move
// the same two pointers inside it. The core's own MEM2 budget check then reads
// real numbers, and nothing in dc/mem/ needs a Wii U branch.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <malloc.h>            // memalign()
#include <coreinit/cache.h>    // DCFlushRange, DCStoreRange, ICInvalidateRange...
#include <coreinit/interrupts.h>

#include "ogc/lwp_watchdog.h"

#ifdef __cplusplus
extern "C" {
#endif

// wiiu/wiiu_mem.cpp
void *WiiU_ArenaLo(void);
void *WiiU_ArenaHi(void);
void  WiiU_SetArenaLo(void *p);
void  WiiU_SetArenaHi(void *p);

#ifdef __cplusplus
}
#endif

static inline void *SYS_GetArena2Lo(void)        { return WiiU_ArenaLo(); }
static inline void *SYS_GetArena2Hi(void)        { return WiiU_ArenaHi(); }
static inline void  SYS_SetArena2Lo(void *p)     { WiiU_SetArenaLo(p); }
static inline void  SYS_SetArena2Hi(void *p)     { WiiU_SetArenaHi(p); }

// libogc returns the previous MSR EE state as a u32 cookie; Cafe OS returns
// a BOOL with the same meaning. Only ever used as a matched pair.
static inline uint32_t IRQ_Disable(void)         { return (uint32_t)OSDisableInterrupts(); }
static inline void     IRQ_Restore(uint32_t lvl) { OSRestoreInterrupts((BOOL)lvl); }

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#endif
