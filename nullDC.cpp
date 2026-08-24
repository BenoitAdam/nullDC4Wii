// nullDC.cpp : Defines the entry point for the console application.
//

//initialse Emu
#include "types.h"
#include "dc/mem/_vmem.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "stdclass.h"
#include "dc/dc.h"
#include "config/config.h"
#include "plugins/plugin_manager.h"
#include "cl/cl.h"
#include "plugs/ImgReader/elf_loader.h"   // ELF boot support

// Frameskipping (preparation : working on it)
int g_current_frameskip = 0; // 0 = no skip, 1 = skip 1 frame, 2 = skip 2 frame
int g_frame_counter = 0;


__settings settings;

// Defined in wii/main.cpp — per-game preset (wii/game_presets.cpp) and
// options-menu override for settings.emulator.AudioBuffers.
extern "C" int get_audio_buffers_preset();

// Defined in wii/main.cpp — options-menu preset selecting the SH4 core
// back-end (0=Interpreter, 1=Dynarec, default).
extern "C" int get_dynarec_preset();

// ---------------------------------------------------------------------------
// ELF boot state
// Set by main___() if the selected file is a .elf.
// Consumed by RunDC() after Reset() so the entry point wins over the BIOS vector.
// ---------------------------------------------------------------------------
static bool  g_elf_pending = false;
static u32   g_elf_entry   = 0;

// ---------------------------------------------------------------------------
// Helper: case-insensitive check for a file extension
// ---------------------------------------------------------------------------
static bool HasExtension(const char* path, const char* ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    const char* tail = path + plen - elen;
    // Compare ignoring case
    for (size_t i = 0; i < elen; i++)
    {
        char a = tail[i] >= 'A' && tail[i] <= 'Z' ? tail[i] + 32 : tail[i];
        char b = ext[i]  >= 'A' && ext[i]  <= 'Z' ? ext[i]  + 32 : ext[i];
        if (a != b) return false;
    }
    return true;
}

//mainloop
int RunDC()
{ 

	if(settings.dynarec.Enable)
	{
		Get_Sh4Recompiler(&sh4_cpu);
		printf("[nullDC.cpp] Using Recompiler (Dynarec Enable)\n");
	}
	else
	{
		Get_Sh4Interpreter(&sh4_cpu);
		printf("[nullDC.cpp] Using Interpreter\n");
	}

	// Second and later runs: the user came back to the file browser with the
	// exit combination and launched another game. Start_DC() skips Init_DC()
	// because the core is still initialised, so the back-end selected just
	// above would never get its Init() -- and the user is free to switch core
	// between games. Both Sh4_int_Init() and recSh4_Init() are idempotent, and
	// recSh4_Init() is also what drops the stale JIT mainloop. IsDCInited() is
	// false on the first run, so nothing changes there.
	if (IsDCInited())
	{
		printf("[nullDC.cpp] Re-entry: re-initialising the SH4 back-end\n");
		sh4_cpu.Init();
	}

	// -----------------------------------------------------------------------
	// ELF boot: patch SH4 context AFTER the CPU back-end is chosen but
	// BEFORE Start_DC() begins executing instructions.
	//
	// Start_DC() calls sh4_cpu.Reset() internally which sets nextpc to the
	// BIOS reset vector (0xA0000000).  We need to override that after Reset
	// but before the first instruction fetch.
	//
	// The hook we use: elf_apply_entry() is called from inside dc_run_hook()
	// which is a weak/callback called by dc.cpp right after Reset() and
	// before the main execution loop starts.  If your dc.cpp does not have
	// that hook yet, see the comment block below for the alternative.
	// -----------------------------------------------------------------------
	if (g_elf_pending)
	{
		// dc.cpp must call dc_elf_pre_run_hook() between Reset() and Run().
		// We register our intent here; the actual register write happens in
		// the hook (see dc_elf_pre_run_hook() below).
		printf("[nullDC.cpp] [ELF] Boot mode active – entry will be applied after CPU reset\n");
	}

	Start_DC();	//this call is blocking ...
	
	return 0;
}

// ---------------------------------------------------------------------------
// dc_elf_pre_run_hook  – called by dc.cpp after sh4_cpu.Reset() and before
//                        the execution loop starts.
//
// ADD ONE LINE to your dc.cpp Start_DC() function, right after sh4_cpu.Reset()
// is called:
//
//   extern void dc_elf_pre_run_hook();
//   dc_elf_pre_run_hook();
//
// That's the only change needed in dc.cpp.
// ---------------------------------------------------------------------------
extern "C" void dc_elf_pre_run_hook()
{
	if (g_elf_pending)
	{
		elf_apply_entry(g_elf_entry);
		g_elf_pending = false;

		// Flush the dynarec block cache AND the PPC code cache.
		// ngen_ResetBlocks() is empty in wii_driver.cpp, so bm_Reset() alone
		// only clears the hash table — the compiled PPC code stays in memory
		// and can still be jumped to via direct pointers held by BIOS blocks.
		// sh4_cpu.ResetCache() flushes the actual PPC emit buffer, making
		// those stale addresses unreachable.
		extern void bm_Reset();
		bm_Reset();
		sh4_cpu.ResetCache();
	}
}

//////////////////////////////////////

//command line parsing & init
int main___(int argc,char* argv[])
{
	if(ParseCommandLine(argc,argv))
	{
		printf("\n\n[nullDC.cpp] (Exiting due to command line, without starting nullDC)\n");
		return 69;
	}

	if(!cfgOpen())
	{
		msgboxf(_T("[nullDC.cpp] Unable to open config file"),MBX_ICONERROR);
		return -4;
	}
	printf("[nullDC] cfgOpen OK\n");
	LoadSettings();
	printf("[nullDC] LoadSettings done\n");

	int rv= 0;

	char* temp_path=GetEmuPath(_T("data/dc_boot.bin"));
	printf("[nullDC] checking BIOS: %s\n", temp_path);
	FILE* fr=fopen(temp_path,"r");
	if (!fr)
	{
		msgboxf(_T("Unable to find bios -- exiting\n%s"),MBX_ICONERROR,temp_path);
		rv=-3;
		goto cleanup;
	}
	fclose(fr);
	printf("[nullDC] BIOS found OK\n");
	free(temp_path);

	temp_path=GetEmuPath(_T("data/dc_flash.bin"));
	printf("[nullDC] checking flash: %s\n", temp_path);
	fr=fopen(temp_path,"r");
	if (!fr)
	{
		msgboxf(_T("Unable to find flash -- exiting\n%s"),MBX_ICONERROR,temp_path);
		rv=-6;
		goto cleanup;
	}
	fclose(fr);
	printf("[nullDC] flash found OK\n");
	free(temp_path);

	printf("[nullDC] Loading plugins!\n");
	while (!plugins_Load())
	{
		//if (!plugins_Select())
		{
			msgboxf("Unable to load plugins -- exiting\n",MBX_ICONERROR);
			rv = -2;
			goto cleanup;
		}
	}
	printf("[nullDC] plugins_Load OK\n");

	// -----------------------------------------------------------------------
	// ELF detection: if the selected file is a .elf, load its segments into
	// emulated RAM now (memory is reserved, plugins are loaded).
	// The actual PC override happens later in dc_elf_pre_run_hook(), after
	// the CPU back-end has been reset.
	// -----------------------------------------------------------------------
	{
		// Read the selected path directly – we cannot use os_GetFile() here because
		// we already patched it to return false for .elf files (to block the disc plugin).
		extern char selectedFilePath[512];
		const char* elfPath = selectedFilePath;

		if (HasExtension(elfPath, ".elf"))
		{
			printf("[ELF] Detected ELF file: %s\n", elfPath);
			u32 entry = 0;
			if (elf_load(elfPath, &entry))
			{
				g_elf_entry   = entry;
				g_elf_pending = true;
				printf("[ELF] Segments loaded, will boot to 0x%08X\n", entry);
			}
			else
			{
				printf("[ELF] Failed to load ELF – falling back to BIOS\n");
				// g_elf_pending stays false → normal BIOS boot
			}
		}
	}

	printf("[nullDC] calling RunDC()...\n");
	rv = RunDC();
	printf("[nullDC] RunDC() returned rv=%d\n", rv);

cleanup:

	printf("__Cleanup\n");

	SaveSettings();
	return rv;
}

//entry point, platform specific main() calls this when done with init/stuff
int EmuMain(int argc, char* argv[])
{
	printf("[nullDC] EmuMain: start\n");
	printf(VER_FULLNAME " starting up ..\n");

	// _vmem_reserve() permanently bumps the Wii MEM2 arena (~26 MB for
	// ARAM+VRAM+RAM), so it must run exactly once per session. EmuMain() is
	// re-entered when the exit combination sends the user back to the file
	// browser and they pick another game -- the guest memory blocks are
	// still there and still mapped, so skip it.
	static bool vmem_reserved = false;
	if (!vmem_reserved)
	{
		printf("[nullDC] EmuMain: _vmem_reserve()...\n");
		if (!_vmem_reserve())
		{
			msgboxf("Unable to reserve nullDC memory ...",MBX_OK | MBX_ICONERROR);
			return -5;
		}
		vmem_reserved = true;
		printf("[nullDC] EmuMain: _vmem_reserve OK\n");
	}

	printf("[nullDC] EmuMain: calling main___()...\n");
	int rv=main___(argc,argv);
	printf("[nullDC] EmuMain: main___() returned rv=%d\n", rv);

#ifndef _ANDROID
	// Only on a real exit -- a return to the file browser keeps the guest
	// memory (and everything mapped into it) alive for the next game.
	if (!ExitToMenuRequested())
		_vmem_release();
#endif

	return rv;
}


void LoadSettings()
{
	printf("[nullDC.cpp] Loading settings\n");

	// SH4 core back-end: options-menu preset (wii/main.cpp), default Dynarec.
	// 0 = Interpreter, 1 = Dynarec.
	settings.dynarec.Enable = (get_dynarec_preset() != 0);
	settings.dynarec.CPpass=cfgLoadInt("nullDC","Dynarec.DoConstantPropagation",1)!=0;
	settings.dynarec.UnderclockFpu=cfgLoadInt("nullDC","Dynarec.UnderclockFpu",0)!=0;
	settings.dynarec.safemode=cfgLoadInt("nullDC","Dynarec.SafeMode",0)!=0;

	settings.dreamcast.cable=cfgLoadInt("nullDC","Dreamcast.Cable",3);
	settings.dreamcast.RTC=cfgLoadInt("nullDC","Dreamcast.RTC",GetRTC_now());

	settings.emulator.AutoStart=cfgLoadInt("nullDC","Emulator.AutoStart",0)!=0;
	settings.emulator.NoConsole=cfgLoadInt("nullDC","Emulator.NoConsole",0)!=0;
	settings.emulator.AudioBuffers=cfgLoadInt("nullDC","Emulator.AudioBuffers",0);
	if (settings.emulator.AudioBuffers>3)
		settings.emulator.AudioBuffers=3;

	// Per-game preset override (see wii/game_presets.cpp "audio_buffers=N")
	// and/or the options menu (wii/main.cpp), applied after the cfg value
	// so a matched preset or manual menu choice always wins.
	int audio_buffers_preset = get_audio_buffers_preset();
	if (audio_buffers_preset >= 0 && audio_buffers_preset <= 3)
		settings.emulator.AudioBuffers = audio_buffers_preset;

	printf("[nullDC.cpp] Loaded settings\n");
}
void SaveSettings()
{
	cfgSaveInt("nullDC","Dynarec.Enabled",settings.dynarec.Enable);
	cfgSaveInt("nullDC","Dynarec.DoConstantPropagation",settings.dynarec.CPpass);
	cfgSaveInt("nullDC","Dynarec.UnderclockFpu",settings.dynarec.UnderclockFpu);
	cfgSaveInt("nullDC","Dynarec.SafeMode",settings.dynarec.safemode);
	cfgSaveInt("nullDC","Dreamcast.Cable",settings.dreamcast.cable);
	cfgSaveInt("nullDC","Dreamcast.RTC",settings.dreamcast.RTC);
	cfgSaveInt("nullDC","Emulator.AutoStart",settings.emulator.AutoStart);
	cfgSaveInt("nullDC","Emulator.NoConsole",settings.emulator.NoConsole);
	cfgSaveInt("nullDC","Emulator.AudioBuffers",settings.emulator.AudioBuffers);
}
