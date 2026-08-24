/*
	Emulation Interface for very high level stuff, like starting/pausing emulation
	Rewritten for improved safety, clarity, and Wii compatibility
*/

#include "mem/sh4_mem.h"
#include "mem/memutil.h"
#include "sh4/sh4_opcode_list.h"
#include "pvr/pvr_if.h"
#include "mem/sh4_internal_reg.h"
#include "aica/aica_if.h"
#include "maple/maple_if.h"
#include "plugins/plugin_manager.h"
#include "dc.h"
#include "config/config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ARM7 CPU plugin (vbaARM) — forward declarations only.
// We do NOT include vbaARM.h here because it redefines ReadMemArrRet /
// WriteMemArrRet macros already defined in sh4_mem.h.
// arm_Init() is NOT called here — it is called inside libARM_Init() in
// plugin_manager.cpp where arm_init_params and aica_ram are both in scope.
// dc.cpp only needs Reset and Term, which take no struct parameters.
extern void FASTCALL armTerm ();
extern void FASTCALL armReset(bool Manual);

// ELF boot hook – defined in nullDC.cpp, called after every CPU reset
// so the ELF entry point overrides the BIOS reset vector.
extern "C" void dc_elf_pre_run_hook();

static inline void arm_Term()             { armTerm(); }
static inline void arm_Reset(bool manual) { armReset(manual); }

// Global state flags
static bool dc_inited = false;
static bool dc_reseted = false;
static bool dc_running = false;

// Set by the in-game exit combination (see RequestExitToMenu below). Volatile
// because it is written from maple polling deep inside the SH4 run loop and
// read again once that loop has unwound.
static volatile bool dc_exit_to_menu = false;

// Emulator thread states
enum emu_thread_state_t
{
	EMU_IDLE,
	EMU_CPU_START,
	EMU_SOFTRESET,
	EMU_NOP,
	EMU_QUIT,
	EMU_INIT,
	EMU_TERM,
	EMU_RESET,
	EMU_RESET_MANUAL,
};

// Emulator thread return values
enum emu_thread_rv_t
{
	RV_OK = 1,
	RV_ERROR = 2,
	RV_EXCEPTION = -2,
	RV_WAIT = -1,
};

static volatile emu_thread_state_t emu_thread_state = EMU_IDLE;
static volatile emu_thread_rv_t emu_thread_rv = RV_WAIT;

// Forward declarations
static void ResetDC(bool manual);
static emu_thread_rv_t ExecuteEmulatorCommand(emu_thread_state_t cmd);

/**
 * Execute emulator command with state machine
 * @param cmd Command to execute
 * @return Result of command execution
 */
static emu_thread_rv_t ExecuteEmulatorCommand(emu_thread_state_t cmd)
{
	emu_thread_state = cmd;
	
	while (emu_thread_state != EMU_QUIT)
	{
		switch (emu_thread_state)
		{
		case EMU_IDLE:
			return emu_thread_rv;

		case EMU_NOP:
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_OK;
			break;

		case EMU_CPU_START:
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_OK;
			dc_running = true;
			sh4_cpu.Run();
			dc_running = false;
			break;

		case EMU_SOFTRESET:
			emu_thread_state = EMU_CPU_START;
			ResetDC(true);
			break;

		case EMU_INIT:
			emu_thread_state = EMU_IDLE;

			if (!plugins_Init())
			{
				printf("ERROR: Plugin initialization failed\n");
				plugins_Term();
				emu_thread_rv = RV_ERROR;
				break;
			}

			sh4_cpu.Init();
			mem_Init();
			pvr_Init();
			aica_Init();   // AICA RAM is ready after this call
			// NOTE: ARM7 CPU is initialised by libARM_Init() inside
			// plugins_Init() (plugin_manager.cpp), which runs before
			// this EMU_INIT block. No arm_Init() call needed here.
			mem_map_defualt();

			emu_thread_rv = RV_OK;
			break;

		case EMU_TERM:
			emu_thread_state = EMU_IDLE;

			// Terminate in reverse order of initialization.
			// ARM must be torn down before AICA because it holds a pointer
			// into aica_ram; freeing AICA RAM first would leave a dangling ref.
			arm_Term();
			aica_Term();
			pvr_Term();
			mem_Term();
			sh4_cpu.Term();
			plugins_Term();

			emu_thread_rv = RV_OK;
			break;

		case EMU_RESET:
			emu_thread_state = EMU_IDLE;
			ResetDC(false);
			emu_thread_rv = RV_OK;
			break;

		case EMU_RESET_MANUAL:
			emu_thread_state = EMU_IDLE;
			ResetDC(true);
			emu_thread_rv = RV_OK;
			break;

		case EMU_QUIT:
			emu_thread_rv = RV_OK;
			break;

		default:
			printf("ERROR: Unknown emulator state: %d\n", emu_thread_state);
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_ERROR;
			break;
		}
	}
	
	return emu_thread_rv;
}

/**
 * Initialize the Dreamcast emulator
 * @return true on success, false on failure
 */
bool Init_DC()
{
	if (dc_inited)
	{
		printf("WARNING: DC already initialized\n");
		return true;
	}

	printf("Initializing Dreamcast emulator...\n");

	if (!plugins_Load())
	{
		printf("ERROR: Failed to load plugins\n");
		return false;
	}

	if (ExecuteEmulatorCommand(EMU_INIT) != RV_OK)
	{
		printf("ERROR: Emulator initialization failed\n");
		return false;
	}

	dc_inited = true;
	printf("Dreamcast emulator initialized successfully\n");
	return true;
}

/**
 * Internal reset function
 * @param manual true for manual reset, false for automatic
 */
static void ResetDC(bool manual)
{
	printf("Resetting DC (%s)...\n", manual ? "manual" : "auto");
	
	plugins_Reset(manual);
	sh4_cpu.Reset(manual);
	mem_Reset(manual);
	pvr_Reset(manual);
	aica_Reset(manual);

	// Reset ARM7 CPU — must come after aica_Reset() so that AICA RAM is
	// in its post-reset state before the ARM7 re-reads it.
	// On a non-manual (auto) reset aica_Reset clears aica_ram, so the ARM
	// must be reset afterwards to avoid running stale code.
	arm_Reset(manual);

	// ELF boot hook: if an ELF was loaded, override the BIOS reset vector
	// with the ELF entry point.  Must be called AFTER sh4_cpu.Reset() so
	// our PC value wins over the reset vector (0xA0000000).
	dc_elf_pre_run_hook();
}

/**
 * Perform a soft reset (can be called while CPU is running)
 * @return true if reset was initiated, false otherwise
 */
bool SoftReset_DC()
{
	if (!dc_inited)
	{
		printf("ERROR: Cannot soft reset - DC not initialized\n");
		return false;
	}

	if (sh4_cpu.IsCpuRunning())
	{
		printf("Performing soft reset...\n");
		sh4_cpu.Stop();
		ExecuteEmulatorCommand(EMU_SOFTRESET);
		return true;
	}
	
	printf("WARNING: Soft reset requested but CPU not running\n");
	return false;
}

/**
 * Holly-level system reset (SB_SFRES = 0x7611)
 *
 * Real hardware: writing this resets the Holly-controlled peripherals
 * (video, sound, etc.) but NOT the SH4 -- the CPU is the one issuing the
 * write and keeps running right through it. Deliberately mirrors only the
 * peripheral half of ResetDC(): no sh4_cpu.Reset() (would corrupt the live
 * CPU/GPR state we're executing under right now) and no mem_Reset() (would
 * wipe the RAM the current code is running from). aica_Reset(true) never
 * zeroes RAM (only the non-manual path does), so unlike ResetDC() there is
 * no AICA-RAM-before-ARM7-reset ordering constraint here.
 */
void HollySoftReset()
{
	printf("Performing Holly soft reset (peripherals only, CPU keeps running)...\n");

	plugins_Reset(true);
	pvr_Reset(true);
	aica_Reset(true);
	arm_Reset(true);
}

/**
 * Reset the Dreamcast emulator
 * @param manual true for manual reset, false for automatic
 * @return true on success, false on failure
 */
bool Reset_DC(bool manual)
{
	if (!dc_inited)
	{
		printf("ERROR: Cannot reset - DC not initialized\n");
		return false;
	}

	if (sh4_cpu.IsCpuRunning())
	{
		printf("ERROR: Cannot reset while CPU is running\n");
		return false;
	}

	if (manual)
		ExecuteEmulatorCommand(EMU_RESET_MANUAL);
	else
		ExecuteEmulatorCommand(EMU_RESET);

	dc_reseted = true;
	return true;
}

/**
 * Terminate the Dreamcast emulator
 */
void Term_DC()
{
	if (!dc_inited)
		return;

	printf("Terminating Dreamcast emulator...\n");
	
	Stop_DC();
	ExecuteEmulatorCommand(EMU_TERM);
	ExecuteEmulatorCommand(EMU_QUIT);
	
	dc_inited = false;
	dc_reseted = false;
	dc_running = false;
	
	printf("Dreamcast emulator terminated\n");
}

// ---------------------------------------------------------------------------
// Return-to-file-browser support
//
// The in-game exit combination used to call exit(0), which dropped straight
// back to the Homebrew Channel. It now unwinds the emulator instead, so
// wii/main.cpp can show the file browser again and launch another game
// without a reboot.
//
// The whole point is that RequestExitToMenu() runs on the emulation thread,
// from inside maple polling, with the SH4 mid-block. It therefore does the
// absolute minimum: raise a flag and clear sh4_int_bCpuRun. Both the
// interpreter loop and the dynarec mainloop test that flag once per
// timeslice and return normally, so the unwind happens through
// Run() -> Start_DC() -> RunDC() -> main___() -> EmuMain() -> main().
// ---------------------------------------------------------------------------

void RequestExitToMenu(void)
{
	if (dc_exit_to_menu)
		return;   // already asked for; don't spam the log while the combo is held

	printf("Exit combination pressed - returning to the file browser\n");
	dc_exit_to_menu = true;

	if (sh4_cpu.IsCpuRunning())
		sh4_cpu.Stop();
}

bool ExitToMenuRequested(void)
{
	return dc_exit_to_menu;
}

void ClearExitToMenu(void)
{
	dc_exit_to_menu = false;
}

/**
 * Swap the disc for a newly picked game, without a Term/Init round trip.
 *
 * Term_DC() would tear down the renderer plugin too, and InitRenderer() is
 * not re-entrant (GX_Init, SYS_AllocateFramebuffer, the MEM2 depth-peel
 * buffers -- all one-shot allocations that would leak on every relaunch).
 * So the core stays initialised and only the two things that are actually
 * per-game are redone here:
 *
 *   - the GD-ROM image: libGDR_Term()/libGDR_Init() re-runs InitDrive(),
 *     which re-reads the path through os_GetFile() -> selectedFilePath,
 *     already updated by the file browser.
 *   - dc_reseted is cleared so the next Start_DC() performs a full hard
 *     reset (RAM wiped, BIOS reloaded, JIT cache dropped, every peripheral
 *     reset) before the new game runs.
 */
void ReloadDisc_DC(void)
{
	if (!dc_inited)
		return;   // nothing running yet -- the normal boot path handles it

	printf("Loading the newly selected disc...\n");

	libGDR_Term();

	gdr_init_params gdr_info;
	if (libGDR_Init(&gdr_info) != rv_ok)
		printf("WARNING: failed to open the selected image\n");

	// Force Start_DC() to hard-reset the machine before running the new game.
	dc_reseted = false;
}

/**
 * Helper function to safely build file paths
 * @param base_path Base directory path
 * @param filename File to append
 * @return Allocated string with full path (caller must free)
 */
static char* BuildFilePath(const char* base_path, const char* filename)
{
	if (!base_path || !filename)
		return NULL;

	size_t base_len = strlen(base_path);
	size_t file_len = strlen(filename);
	
	// Allocate space for base + filename + null terminator
	char* full_path = (char*)malloc(base_len + file_len + 1);
	if (!full_path)
	{
		printf("ERROR: Failed to allocate memory for file path\n");
		return NULL;
	}

	// Copy base path and append filename
	strcpy(full_path, base_path);
	strcat(full_path, filename);

	return full_path;
}

/**
 * Load BIOS and system files
 */
void LoadBiosFiles()
{
	printf("Loading BIOS files...\n");

	char* base_path = GetEmuPath("data/");
	if (!base_path)
	{
		printf("ERROR: Failed to get emulator data path\n");
		return;
	}

	bool any_loaded = false;

	// Load boot ROM
	char* boot_path = BuildFilePath(base_path, "dc_boot.bin");
	if (boot_path)
	{
		if (LoadFileToSh4Bootrom(boot_path))
		{
			printf("Loaded dc_boot.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load dc_boot.bin\n");
		}
		free(boot_path);
	}

	// Try to load saved flash first, fallback to default
	char* flash_path = BuildFilePath(base_path, "dc_flash_wb.bin");
	if (flash_path)
	{
		if (LoadFileToSh4Flashrom(flash_path))
		{
			printf("Loaded dc_flash_wb.bin (writeback)\n");
			any_loaded = true;
		}
		else
		{
			printf("No writeback flash found, trying default...\n");
			free(flash_path);
			
			flash_path = BuildFilePath(base_path, "dc_flash.bin");
			if (flash_path)
			{
				if (LoadFileToSh4Flashrom(flash_path))
				{
					printf("Loaded dc_flash.bin\n");
					any_loaded = true;
				}
				else
				{
					printf("WARNING: Failed to load dc_flash.bin\n");
				}
			}
		}
		if (flash_path)
			free(flash_path);
	}

	// Load syscalls
	char* syscalls_path = BuildFilePath(base_path, "syscalls.bin");
	if (syscalls_path)
	{
		if (LoadFileToSh4Mem(0x00000, syscalls_path))
		{
			printf("Loaded syscalls.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load syscalls.bin\n");
		}
		free(syscalls_path);
	}

	// Load IP.BIN
	char* ip_path = BuildFilePath(base_path, "IP.bin");
	if (ip_path)
	{
		if (LoadFileToSh4Mem(0x08000, ip_path))
		{
			printf("Loaded IP.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load IP.bin\n");
		}
		free(ip_path);
	}

	free(base_path);

	if (any_loaded)
		printf("BIOS files loaded\n");
	else
		printf("WARNING: No BIOS files were loaded successfully\n");
}

/**
 * Start the Dreamcast emulator
 */
void Start_DC()
{
	printf("Starting Dreamcast emulator...\n");

	if (sh4_cpu.IsCpuRunning())
	{
		printf("WARNING: CPU already running\n");
		return;
	}

	if (!dc_inited)
	{
		if (!Init_DC())
		{
			printf("ERROR: Failed to initialize DC\n");
			return;
		}
	}

	if (!dc_reseted)
	{
		printf("Performing initial reset...\n");
		if (!Reset_DC(false))
		{
			printf("ERROR: Failed to reset DC\n");
			return;
		}
	}

	if (ExecuteEmulatorCommand(EMU_CPU_START) != RV_OK)
	{
		printf("ERROR: Failed to start CPU\n");
		return;
	}

	printf("Dreamcast emulator started\n");
}

/**
 * Stop the Dreamcast emulator
 */
void Stop_DC()
{
	if (!dc_inited)
		return;

	if (sh4_cpu.IsCpuRunning())
	{
		printf("Stopping Dreamcast emulator...\n");
		sh4_cpu.Stop();
		ExecuteEmulatorCommand(EMU_NOP);
		printf("Dreamcast emulator stopped\n");
	}
}

/**
 * Check if DC is initialized
 * @return true if initialized, false otherwise
 */
bool IsDCInited()
{
	return dc_inited;
}

/**
 * Check if DC is running
 * @return true if running, false otherwise
 */
bool IsDCRunning()
{
	return dc_running;
}
