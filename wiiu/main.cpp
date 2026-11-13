// wiiu/main.cpp
//
// Wii U (native Cafe OS) entry point: boot, game picker, launch, exit.
// Sibling of wii/main.cpp, much smaller on purpose:
//
//  * The preset globals/getters are NOT here: wiiu/wiiu_presets.cpp compiles
//    wii/main.cpp's preset block (NDC_PRESETS_ONLY), so defaults and cfg keys
//    are shared with the Wii build. game_presets.cfg is applied the same way.
//  * The menu is a plain OSScreen text list (GamePad + TV): pick a game from
//    fs:/vol/external01/discs/, press A. No options pages yet - presets come
//    from game_presets.cfg ([default] + per-game sections, <wii u> sections
//    included: g_is_wiiu is true here).
//  * Exit: HOME menu -> "Close software", or MINUS + PLUS in game
//    (wiiu/wiiu_exit.cpp has the ProcUI rules).
//
// Also provides the platform callbacks the shared core expects from the
// platform's main.cpp (os_GetFile, os_GetSeconds, os_msgbox, selectedFilePath,
// wii_boot_log, g_is_wiiu).

#include "types.h"   // first, like the Wii files (config.h's BIG_ENDIAN poison)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdarg.h>

#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <proc_ui/procui.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <whb/proc.h>

#include "wii/game_presets.h"
#include "wiiu/wiiu_config.h"
#include "wiiu/wiiu_presets.h"
#include "wiiu/wiiu_fs.h"
#include "wiiu/wiiu_mem.h"
#include "wiiu/wiiu_exit.h"
#include "wiiu/wiiu_input.h"
#include "wii/wii_audio.h"

// ============================================================================
// Platform state the shared core reads
// ============================================================================

// nullDC.cpp (ELF boot) and os_GetFile(): the game the user picked.
char selectedFilePath[512] = "";

// wii/game_presets.cpp: enables the <wii u> cfg sections. Always true here.
bool g_is_wiiu = true;

// Preset globals read by the boot dump below (wii/main.cpp's preset block).
extern int g_dynarec_preset;
extern int g_render_delay_preset;
extern int g_texture_cache_preset;
extern int g_cdda_preset;
extern int g_cdda_clock_preset;
extern int g_sh4_clock_preset;

// ============================================================================
// OSScreen text console (menu + boot errors)
// ============================================================================
// Same buffer handling as libwhb's console (console.c): the OSScreen buffers
// live in the MEM1 frame heap while we own the foreground, are dropped when
// the HOME menu takes it (ProcUI RELEASE) and re-made when it comes back
// (ACQUIRE). OSScreen and GX2 cannot both own the scan buffers, so
// screen_term() runs before the renderer starts; the ProcUI callbacks stay
// registered but do nothing once the screen is down.

#define MENU_FRAME_HEAP_TAG 0x4E444355u   // 'NDCU'

static void    *s_scr_buf[2]  = { nullptr, nullptr };
static uint32_t s_scr_size[2] = { 0, 0 };
static bool     s_scr_up      = false;   // OSScreen in use (menu phase)
static bool     s_scr_fg      = false;   // buffers allocated (we own the foreground)

#define SCR_ROWS      16    // usable text rows on both screens
#define SCR_LIST_TOP  3
#define SCR_LIST_ROWS (SCR_ROWS - SCR_LIST_TOP - 2)

static uint32_t screen_acquired(void *context)
{
  if (!s_scr_up || s_scr_fg)
    return 0;
  MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
  MEMRecordStateForFrmHeap(heap, MENU_FRAME_HEAP_TAG);
  for (int s = 0; s < 2; s++)
  {
    s_scr_buf[s] = MEMAllocFromFrmHeapEx(heap, s_scr_size[s], 0x100);
    OSScreenSetBufferEx((OSScreenID)s, s_scr_buf[s]);
  }
  s_scr_fg = (s_scr_buf[0] && s_scr_buf[1]);
  return 0;
}

static uint32_t screen_released(void *context)
{
  if (!s_scr_fg)
    return 0;
  MEMFreeByStateToFrmHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1), MENU_FRAME_HEAP_TAG);
  s_scr_buf[0] = s_scr_buf[1] = nullptr;
  s_scr_fg = false;
  return 0;
}

static void screen_init(void)
{
  static bool s_callbacks = false;
  if (s_scr_up)
    return;
  OSScreenInit();
  s_scr_size[0] = OSScreenGetBufferSizeEx(SCREEN_TV);
  s_scr_size[1] = OSScreenGetBufferSizeEx(SCREEN_DRC);
  s_scr_up = true;
  screen_acquired(nullptr);
  OSScreenEnableEx(SCREEN_TV, TRUE);
  OSScreenEnableEx(SCREEN_DRC, TRUE);
  if (!s_callbacks)
  {
    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, screen_acquired, nullptr, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, screen_released, nullptr, 100);
    s_callbacks = true;
  }
}

static void screen_term(void)
{
  if (!s_scr_up)
    return;
  if (s_scr_fg)
  {
    OSScreenShutdown();
    screen_released(nullptr);
  }
  s_scr_up = false;
}

static void screen_begin(void)
{
  if (!s_scr_fg)
    return;
  OSScreenClearBufferEx(SCREEN_TV, 0x10182000);    // dark blue
  OSScreenClearBufferEx(SCREEN_DRC, 0x10182000);
}

static void screen_line(int row, const char *fmt, ...)
{
  if (!s_scr_fg)
    return;
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OSScreenPutFontEx(SCREEN_TV, 0, row, buf);
  OSScreenPutFontEx(SCREEN_DRC, 0, row, buf);
}

static void screen_end(void)
{
  if (!s_scr_fg)
    return;
  for (int s = 0; s < 2; s++)
  {
    DCFlushRange(s_scr_buf[s], s_scr_size[s]);
    OSScreenFlipBuffersEx((OSScreenID)s);
  }
}

// ============================================================================
// Menu input: GamePad + any KPAD controller, edge-triggered
// ============================================================================

enum
{
  MB_UP = 1, MB_DOWN = 2, MB_LEFT = 4, MB_RIGHT = 8, MB_A = 16, MB_B = 32
};

static uint32_t menu_buttons_down(void)
{
  static uint32_t s_prev = 0;
  uint32_t held = 0;

  VPADStatus vs;
  VPADReadError verr = VPAD_READ_SUCCESS;
  if (VPADRead(VPAD_CHAN_0, &vs, 1, &verr) > 0 && verr == VPAD_READ_SUCCESS)
  {
    const uint32_t h = vs.hold;
    if (h & VPAD_BUTTON_UP)    held |= MB_UP;
    if (h & VPAD_BUTTON_DOWN)  held |= MB_DOWN;
    if (h & VPAD_BUTTON_LEFT)  held |= MB_LEFT;
    if (h & VPAD_BUTTON_RIGHT) held |= MB_RIGHT;
    if (h & VPAD_BUTTON_A)     held |= MB_A;
    if (h & VPAD_BUTTON_B)     held |= MB_B;
    if (vs.leftStick.y >  0.6f) held |= MB_UP;
    if (vs.leftStick.y < -0.6f) held |= MB_DOWN;
  }

  for (int ch = 0; ch < 4; ch++)
  {
    KPADStatus ks;
    if (KPADRead((KPADChan)ch, &ks, 1) <= 0 || ks.error != 0)
      continue;
    uint32_t h;
    if (ks.extensionType == WPAD_EXT_PRO_CONTROLLER)
    {
      h = ks.pro.hold;
      if (h & WPAD_PRO_BUTTON_UP)    held |= MB_UP;
      if (h & WPAD_PRO_BUTTON_DOWN)  held |= MB_DOWN;
      if (h & WPAD_PRO_BUTTON_LEFT)  held |= MB_LEFT;
      if (h & WPAD_PRO_BUTTON_RIGHT) held |= MB_RIGHT;
      if (h & WPAD_PRO_BUTTON_A)     held |= MB_A;
      if (h & WPAD_PRO_BUTTON_B)     held |= MB_B;
    }
    else if (ks.extensionType == WPAD_EXT_CLASSIC)
    {
      h = ks.classic.hold;
      if (h & WPAD_CLASSIC_BUTTON_UP)    held |= MB_UP;
      if (h & WPAD_CLASSIC_BUTTON_DOWN)  held |= MB_DOWN;
      if (h & WPAD_CLASSIC_BUTTON_LEFT)  held |= MB_LEFT;
      if (h & WPAD_CLASSIC_BUTTON_RIGHT) held |= MB_RIGHT;
      if (h & WPAD_CLASSIC_BUTTON_A)     held |= MB_A;
      if (h & WPAD_CLASSIC_BUTTON_B)     held |= MB_B;
    }
    else
    {
      h = ks.hold;
      if (h & WPAD_BUTTON_UP)    held |= MB_UP;
      if (h & WPAD_BUTTON_DOWN)  held |= MB_DOWN;
      if (h & WPAD_BUTTON_LEFT)  held |= MB_LEFT;
      if (h & WPAD_BUTTON_RIGHT) held |= MB_RIGHT;
      if (h & WPAD_BUTTON_A)     held |= MB_A;
      if (h & WPAD_BUTTON_B)     held |= MB_B;
    }
  }

  const uint32_t down = held & ~s_prev;
  s_prev = held;
  return down;
}

// Hold the screen with a message until A is pressed or the system closes us.
static void screen_message_wait(const char *title, const char *l1, const char *l2)
{
  while (WiiU_ProcPump())
  {
    screen_begin();
    screen_line(0, "NullDC4WiiU");
    screen_line(2, "%s", title);
    if (l1) screen_line(4, "%s", l1);
    if (l2) screen_line(5, "%s", l2);
    screen_line(SCR_ROWS - 1, "A: continue    HOME: quit");
    screen_end();
    if (menu_buttons_down() & MB_A)
      return;
    OSSleepTicks(OSMillisecondsToTicks(16));
  }
}

// ============================================================================
// Game picker
// ============================================================================

#define MAX_GAMES  512
#define PATH_STRIDE 512

// Returns true with selectedFilePath set, false if the system closed us.
static bool pick_game(void)
{
  static char s_games[MAX_GAMES][PATH_STRIDE];
  const char *dir = wiiu_fs_discs_dir();
  int count = wiiu_fs_list_games(dir, &s_games[0][0], PATH_STRIDE, MAX_GAMES);
  int sel = 0;

  while (WiiU_ProcPump())
  {
    const uint32_t d = menu_buttons_down();
    if (count > 0)
    {
      if (d & MB_UP)    sel = (sel + count - 1) % count;
      if (d & MB_DOWN)  sel = (sel + 1) % count;
      if (d & MB_LEFT)  sel = (sel - SCR_LIST_ROWS < 0) ? 0 : sel - SCR_LIST_ROWS;
      if (d & MB_RIGHT) sel = (sel + SCR_LIST_ROWS >= count) ? count - 1 : sel + SCR_LIST_ROWS;
      if (d & MB_A)
      {
        snprintf(selectedFilePath, sizeof(selectedFilePath), "%s", s_games[sel]);
        return true;
      }
    }
    if (d & MB_B)   // rescan (card swapped / files added)
    {
      count = wiiu_fs_list_games(dir, &s_games[0][0], PATH_STRIDE, MAX_GAMES);
      sel = 0;
    }

    screen_begin();
    screen_line(0, "NullDC4WiiU - native Wii U (interpreter, basic port)");
    screen_line(1, "%s", dir);
    if (count == 0)
    {
      screen_line(SCR_LIST_TOP, "No games found.");
      screen_line(SCR_LIST_TOP + 1, "Put .gdi/.cdi/.chd/... files in the discs folder.");
    }
    else
    {
      const int top = (sel / SCR_LIST_ROWS) * SCR_LIST_ROWS;
      for (int i = 0; i < SCR_LIST_ROWS && top + i < count; i++)
      {
        const char *full = s_games[top + i];
        const char *name = strrchr(full, '/');
        name = name ? name + 1 : full;
        screen_line(SCR_LIST_TOP + i, "%s %.70s", (top + i == sel) ? ">" : " ", name);
      }
      screen_line(SCR_ROWS - 2, "%d / %d", sel + 1, count);
    }
    screen_line(SCR_ROWS - 1, "A: start   B: rescan   L/R: page   HOME: quit");
    screen_end();
    OSSleepTicks(OSMillisecondsToTicks(16));
  }
  return false;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv)
{
  WHBProcInit();

  // stdout -> the same ndclog.txt the Wii build writes, on the SD root.
  // Fully buffered like on the Wii: every boot step below fflush()es.
  freopen(WIIU_LOG_PATH, "w", stdout);
  printf("[boot] NullDC4WiiU (native Wii U, interpreter-only basic port)\n");
  fflush(stdout);

  screen_init();
  wiiu_input_init();   // the menu reads VPAD/KPAD below

  if (!WiiU_ArenaInit())
  {
    screen_message_wait("Out of memory", "Could not reserve the Dreamcast memory arena.", nullptr);
    screen_term();
    WiiU_Shutdown();
    return 0;
  }

  // AX before anything can push samples; the sink opens on wii_audio_aica_ready().
  wii_audio_init();

  const bool bios_ok = wiiu_fs_init();
  printf("[boot] app folder: %s (BIOS %s)\n", wiiu_fs_app_dir(), bios_ok ? "found" : "MISSING");
  fflush(stdout);
  if (!bios_ok)
  {
    screen_message_wait("Dreamcast BIOS not found",
                        "Put dc_boot.bin and dc_flash.bin in one of:",
                        WIIU_APP_DIR_NATIVE "data/   or   " WIIU_APP_DIR_WII "data/");
    // Carry on to the picker anyway: main___() re-checks and logs the path.
  }

  const char *cfg = wiiu_fs_presets_path();
  if (cfg)
    game_presets_load(cfg);
  printf("[boot] game_presets.cfg: %s\n", cfg ? cfg : "(none)");
  fflush(stdout);

  if (!pick_game())
  {
    screen_term();
    WiiU_Shutdown();
    return 0;
  }

  printf("[boot] game: %s\n", selectedFilePath);
  game_presets_apply(selectedFilePath);
  WiiU_ForcePlatformPresets();
  printf("[boot] preset: %s%s%s | dynarec=%d (interp) render_delay=%d tex_cache=%d "
         "cdda=%d cdda_clock=%d sh4_clock=%d\n",
         g_matched_preset_is_wiiu ? "<" : "[",
         g_matched_preset_name[0] ? g_matched_preset_name : "default",
         g_matched_preset_is_wiiu ? ">" : "]",
         g_dynarec_preset, g_render_delay_preset, g_texture_cache_preset,
         g_cdda_preset, g_cdda_clock_preset, g_sh4_clock_preset);
  fflush(stdout);

  // The renderer (InitRenderer -> WHBGfxInit) takes over the scan buffers.
  screen_term();

  char *emu_argv[] = { argv && argc > 0 ? argv[0] : (char *)"nulldc4wiiu", nullptr };
  const int rv = EmuMain(1, emu_argv);
  printf("[boot] EmuMain returned %d\n", rv);
  fflush(stdout);

  if (rv != 0 && !WiiU_ExitRequested())
  {
    // Start-up failure (no BIOS, no memory, plugins...). The renderer may or
    // may not be up; the log has the details. Leave through the menu.
    WiiU_RequestExit("start-up failed, see ndclog.txt");
  }

  WiiU_Shutdown();
  return 0;
}

// ============================================================================
// PLATFORM CALLBACKS (os_base.h, nullDC.cpp, dc/mem/_vmem.cpp)
// ============================================================================

int os_GetFile(char *szFileName, char *szParse, u32 flags)
{
  // Same rule as the Wii build: an .elf is not a disc, nullDC.cpp loads it
  // itself, so the disc plugin must not get it.
  const size_t len = strlen(selectedFilePath);
  if (len == 0)
    return false;
  if (len > 4 && (strcasecmp(selectedFilePath + len - 4, ".elf") == 0))
    return false;
  strcpy(szFileName, selectedFilePath);
  return true;
}

double os_GetSeconds()
{
  // Delta from the first call, like the Wii version: the absolute time base
  // is too large for a double to keep frame-level precision.
  static OSTime t0 = 0;
  const OSTime now = OSGetSystemTime();
  if (t0 == 0)
    t0 = now;
  return (double)OSTicksToMicroseconds(now - t0) / 1000000.0;
}

int os_msgbox(const char *text, unsigned int type)
{
  printf("OS_MSGBOX: %s\n", text);
  fflush(stdout);
  return 0;
}

// _vmem.cpp and friends log their boot lines through this. On the Wii it also
// echoes to the on-screen console; here stdout already is the log file.
extern "C" void wii_boot_log(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}
