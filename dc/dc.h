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
