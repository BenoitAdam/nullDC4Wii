// wiiu/wiiu_exit.cpp
//
// Exit handling for the Wii U build. Counterpart of wii/wii_exit.cpp, but the
// problem is different: on the Wii the danger was libogc's SYS_ResetSystem
// spinning on live hardware; on Cafe OS the rule is the ProcUI protocol.
//
//  * ProcUI must be pumped on the main core, regularly. WiiU_ProcPump() does it
//    (from gx2Rend.cpp VBlank() during emulation, from the menu loop before).
//    While the HOME menu is open the pump BLOCKS inside ProcUIProcessMessages,
//    which conveniently pauses the emulation; WHBProcIsRunning() handles the
//    foreground release/acquire (and libwhb's gfx callbacks re-create the TV
//    and GamePad buffers on the way back).
//  * "Close software" in the HOME menu arrives as PROCUI_STATUS_EXITING: the
//    pump returns false and ProcUI is already shut down; we stop the SH4 and
//    unwind back to main(), which returns.
//  * Leaving on our own (MINUS + PLUS) is NOT "return from main": the app must
//    call SYSLaunchMenu() and keep pumping until the system sends EXITING.
//    WiiU_Shutdown() does that hand-off.
//
// The emulator itself is stopped with sh4_cpu.Stop(): the interpreter returns
// from sh4_cpu.Run() at the end of its timeslice, Start_DC()/RunDC() return,
// main___() saves settings and EmuMain() returns to wiiu/main.cpp.

#include "types.h"   // first, like the Wii files (config.h's BIG_ENDIAN poison)
#include <stdio.h>
#include <whb/proc.h>
#include <sysapp/launch.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include "plugins/plugin_manager.h"   // sh4_cpu
#include "wii/wii_audio.h"
#include "wiiu/wiiu_exit.h"
#include "wiiu/wiiu_mem.h"

extern "C" void dc_flash_save_at_exit(void);   // dc/dc.cpp (FLASH_SAVE preset)
extern "C" void gx2Rend_Shutdown(void);        // plugs/drkPvr/gx2Rend.cpp

static bool        s_exit_requested = false;
static bool        s_procui_exited  = false;   // system already sent EXITING
static const char *s_exit_reason    = "";

extern "C" void WiiU_RequestExit(const char *why)
{
  if (s_exit_requested)
    return;
  s_exit_requested = true;
  s_exit_reason    = why ? why : "?";
  printf("[EXIT] requested: %s\n", s_exit_reason);
  fflush(stdout);
  // Harmless when the CPU is not running yet (menu): the flag alone then
  // keeps main() from launching a game.
  if (sh4_cpu.IsCpuRunning && sh4_cpu.Stop && sh4_cpu.IsCpuRunning())
    sh4_cpu.Stop();
}

extern "C" bool WiiU_ExitRequested(void)
{
  return s_exit_requested;
}

extern "C" bool WiiU_ProcPump(void)
{
  if (s_procui_exited)
    return false;
  if (WHBProcIsRunning())
    return true;
  // WHBProcIsRunning() has run ProcUIShutdown() already: never pump again.
  s_procui_exited = true;
  WiiU_RequestExit("HOME menu: close software");
  return false;
}

extern "C" void WiiU_Shutdown(void)
{
  printf("[EXIT] shutting down (%s)\n", s_exit_reason[0] ? s_exit_reason : "emulator returned");
  fflush(stdout);

  // Same first step as the Wii exit path: the file system is certainly
  // healthy here and the flash write must not race anything.
  dc_flash_save_at_exit();

  wii_audio_term();
  fflush(NULL);

  if (!s_procui_exited)
  {
    // Our own exit: hand control back to the system menu and keep ProcUI
    // alive until it tells us to go. WHBProcIsRunning() returns false (after
    // ProcUIShutdown) once the EXITING message has been processed.
    SYSLaunchMenu();
    while (WHBProcIsRunning())
      OSSleepTicks(OSMillisecondsToTicks(10));
    s_procui_exited = true;
  }

  // GPU and memory go after the ProcUI loop, the order libwhb's samples use:
  // the foreground release during that loop runs libwhb's gfx callbacks.
  gx2Rend_Shutdown();
  WiiU_ArenaTerm();
  fflush(NULL);
  WHBProcShutdown();
}
