// wii_exit.h
// Ordered shutdown for the in-game exit combo (EXIT FIX preset).
//
// Every exit combo used to call exit(0) directly from inside the emulation
// (maple DMA -> GetInput -> UpdateInputState -> exit), leaving the DSP, the
// GX FIFO, the Serial Interface and Bluetooth all live while libogc's
// SYS_ResetSystem tried to unwind them. Several steps in that unwind are
// UNBOUNDED waits, which is why the return to the Homebrew Channel sometimes
// wedges with the screen already black. See wii_exit.cpp for the details.

#ifndef WII_EXIT_H
#define WII_EXIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Quiesce the machine, then exit. Never returns.
// With the EXIT FIX preset off this is a bare exit(0) (legacy behaviour).
void WiiExitToLoader(void);

#ifdef __cplusplus
}
#endif

#endif // WII_EXIT_H
