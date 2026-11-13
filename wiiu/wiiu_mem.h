// wiiu/wiiu_mem.h
// Wii U memory setup (wiiu/wiiu_mem.cpp): the arena _vmem_reserve() carves the
// Dreamcast memory from. The libogc-named accessors are in wiiu/compat/gccore.h.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Allocate the arena. Must run before EmuMain() (i.e. before _vmem_reserve()).
bool WiiU_ArenaInit(void);
// Free it. Exit path only, after emulation has stopped.
void WiiU_ArenaTerm(void);

#ifdef __cplusplus
}
#endif
