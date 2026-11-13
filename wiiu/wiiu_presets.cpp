// wiiu/wiiu_presets.cpp
//
// Every preset global and getter the shared core reads (g_*_preset /
// get_*_preset(), ~150 of them) lives in wii/main.cpp's "GLOBAL EMULATOR
// PRESETS" block. Rather than copy them (and have the defaults drift), the Wii U
// build compiles THAT block: NDC_PRESETS_ONLY makes wii/main.cpp skip its libogc
// includes and everything from "FILE BROWSER STATE" on (menu, boot, platform
// callbacks). A preset added to wii/main.cpp the usual way is therefore on the
// Wii U too, with the same default and the same game_presets.cfg key
// (wii/game_presets.cpp is shared as-is).
//
// WiiU_ForcePlatformPresets() then pins the few presets the Wii U build cannot
// honour. It runs after game_presets_apply(), so a cfg [default] or per-game
// section asking for them cannot switch them back on.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NDC_PRESETS_ONLY 1
#include "wii/main.cpp"

#include "wiiu/wiiu_presets.h"

extern "C" void WiiU_ForcePlatformPresets(void)
{
  // SH4 recompiler, its fastmem and its cache tricks: not built on Wii U
  // (config.h _WIIU -> USE_INTERP). RunDC() would fall back to the interpreter
  // on its own (Get_Sh4Recompiler -> Get_Sh4Interpreter under HOST_NO_REC); this
  // makes the log and the cfg read the same as what actually runs.
  g_dynarec_preset = 0;
  g_fastmem_preset = 0;

  // ARM7 JIT: plugs/vbaARM/arm7_jit.cpp compiles to stubs off OS_WII, and
  // arm_jit_active() already says false. 0 = the cached interpreter, which is
  // what the ARM7 core does with the JIT absent.
  g_arm7_jit_preset = 0;

  // CDDA_CLOCK = REAL TIME plays CD music on its own audio voice
  // (wii_cd_* in wii/wii_audio.cpp). wiiu/wiiu_audio.cpp does not have that
  // voice yet, so route CD music through the AICA mix (the emulated-clock path)
  // instead of losing it.
  g_cdda_clock_preset = 0;

  // The renderer-side presets (gx/lod_bias/aniso/autosort/fmv_format/...) are
  // gxRend.cpp's and are simply never read by gx2Rend.cpp; they can stay as
  // the cfg set them.
}
