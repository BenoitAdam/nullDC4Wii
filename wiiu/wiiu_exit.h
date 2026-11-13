// wiiu/wiiu_exit.h
// Exit handling for the Wii U build (wiiu/wiiu_exit.cpp): ProcUI (the HOME
// menu / "Close software" protocol), the in-game exit combo, clean shutdown.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Ask the emulator to stop. Safe from anywhere on the emulation thread, any
// number of times; the first reason is logged. The SH4 run loop returns at the
// end of its current timeslice, EmuMain() unwinds back to main(), and main()
// calls WiiU_Shutdown(). `why` must be a string literal.
void WiiU_RequestExit(const char *why);
bool WiiU_ExitRequested(void);

// Pump ProcUI once. Call regularly from the main core (the emulation thread:
// gx2Rend.cpp VBlank(); the menu loop). Blocks while the HOME menu is up.
// Returns false once the system asked us to quit (and requests the exit).
bool WiiU_ProcPump(void);

// Ordered teardown, from main() after EmuMain() returned: flash save, audio,
// renderer, log flush, then the ProcUI hand-off (SYSLaunchMenu + wait for the
// EXITING message when the exit was our own idea). Returns when main() may
// return.
void WiiU_Shutdown(void);

#ifdef __cplusplus
}
#endif
