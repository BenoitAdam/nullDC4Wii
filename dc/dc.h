/*
	dc.h - Dreamcast Emulator Core Interface
	High-level emulation control for Wii platform
*/

#ifndef DC_H
#define DC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Dreamcast emulator core
 * This allocates all necessary resources and must be called before
 * any other emulation functions.
 * 
 * @return true on success, false on failure
 */
bool Init_DC();

/**
 * Perform a soft reset of the emulator
 * Can be called while the CPU is running. The CPU will be stopped,
 * reset, and restarted automatically.
 *
 * @return true if reset was initiated, false if CPU wasn't running
 */
bool SoftReset_DC();

/**
 * Perform a Holly-level system reset (SB_SFRES = 0x7611), as issued by the
 * real BIOS/IP.BIN mid-boot to reconfigure video timing before jumping to
 * the game. Unlike SoftReset_DC(), this must be safe to call synchronously
 * from inside the SH4's own execution (the CPU is the one issuing the
 * write) — it resets only the Holly-controlled peripherals (PVR/SPG,
 * renderer, AICA, ARM7), never the SH4 itself or main RAM.
 */
void HollySoftReset();

/**
 * Perform a hard reset of the emulator
 * Cannot be called while the CPU is running.
 * 
 * @param Manual true for manual reset (preserves some state),
 *               false for complete reset
 * @return true on success, false if DC not initialized or CPU running
 */
bool Reset_DC(bool Manual);

/**
 * Terminate the Dreamcast emulator
 * Stops emulation and frees all allocated resources.
 * After calling this, Init_DC() must be called again to use the emulator.
 */
void Term_DC();

/**
 * Start the Dreamcast emulator
 * Initializes and resets the emulator if necessary, then starts CPU execution.
 */
void Start_DC();

/**
 * Stop the Dreamcast emulator
 * Halts CPU execution but maintains emulator state.
 */
void Stop_DC();

/**
 * Load BIOS and system files from the data directory
 * Loads: dc_boot.bin, dc_flash.bin (or dc_flash_wb.bin),
 *        syscalls.bin, and IP.bin
 */
void LoadBiosFiles();

/**
 * Request that emulation stop and control return to the Wii file browser.
 *
 * This is what the in-game exit combination (Wiimote MINUS+PLUS,
 * GameCube L+R+Z) now does instead of exit(0). It is called from inside
 * maple polling -- i.e. from the SH4's own execution -- so it must not
 * tear anything down itself. It only raises a flag and asks the CPU to
 * stop; the run loop then unwinds normally back to wii/main.cpp, which
 * shows the file browser again.
 */
void RequestExitToMenu(void);

/**
 * True if RequestExitToMenu() was called and has not been consumed yet.
 * wii/main.cpp checks this after EmuMain() returns to tell "the user asked
 * for the menu" apart from "emulation ended by itself".
 */
bool ExitToMenuRequested(void);

/**
 * Clear the exit-to-menu flag. Call before (re)entering emulation.
 */
void ClearExitToMenu(void);

/**
 * Re-open the GD-ROM image for a newly selected game and arm a full hard
 * reset for the next Start_DC().
 *
 * Used when the file browser was re-entered mid-session: the emulator core
 * stays initialised (no Term_DC()/Init_DC() round trip, which would re-run
 * one-time GX / arena allocations), but the disc image and the whole machine
 * state must be swapped out for the newly picked game.
 */
void ReloadDisc_DC(void);

/**
 * Check if the Dreamcast emulator is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool IsDCInited();

/**
 * Check if the Dreamcast emulator is currently running
 * 
 * @return true if CPU is executing, false otherwise
 */
bool IsDCRunning();

#ifdef __cplusplus
}
#endif

#endif // DC_H
