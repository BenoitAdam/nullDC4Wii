#include "types.h"
#include <unistd.h>
#include "iso.h"
#include <fat.h>
#include <dirent.h>
#include <wiiuse/wpad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>	// gettime() / ticks_to_microsecs() for os_GetSeconds()
#include <asndlib.h>
#include <mp3player.h> // Was for testing playing an MP3 on menu. 
#include "wii/wii_audio.h"
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include "stdclass.h"   // for GetEmuPath() for bios check

// *** WII U GAMEPAD (DRC) SUPPORT (vWii mode) ***
#include "plugs/libwiidrc/wiidrc.h"

// *** GAME PRESETS ***
#include "wii/game_presets.h"

// *** ARM7DI CORE SELF-TEST ***
#include "plugs/vbaARM/arm7.h"

// ============================================================================
// GLOBAL EMULATOR PRESETS
// ============================================================================

int g_accuracy_preset = 2;     // 0=Fast, 1=Balanced, 2=Accurate (default)

extern "C" {
  int get_accuracy_preset() { return g_accuracy_preset; }
}

int g_graphism_preset = 1;     // 0=Low, 1=Normal, 2=High, 3=Extra

extern "C" {
  int get_graphism_preset() { return g_graphism_preset; }
}

int g_ratio_preset = 1;        // 0=Original (4/3), 1=Fullscreen (default)

extern "C" {
  int get_ratio_preset() { return g_ratio_preset; }
}

int g_advanced_alpha_preset = 1;

extern "C" {
  int get_advanced_alpha_preset() { return g_advanced_alpha_preset; }
}

int g_decal_alpha_preset = 0; // 0=legacy GX_MODULATE (faster, wrong transparency) 1=correct DecalAlpha shading (GX_DECAL)

extern "C" {
  int get_decal_alpha_preset() { return g_decal_alpha_preset; }
}

int g_frameskip_preset = 0;

extern "C" {
  int get_frameskip_preset() { return g_frameskip_preset; }
}

int g_4bpp_preset = 1;
// 0=I4 Stub, 1=4BPP Optimized, 2=CI4 fast, 3=CI4 normal, 4=RGB565

extern "C" {
  int get_4bpp_preset() { return g_4bpp_preset; }
}

int g_8bpp_preset = 1;
// 0=I8 Stub, 1=8BPP Optimized, 2=CI8 fast, 3=CI8 normal, 4=RGB565

extern "C" {
  int get_8bpp_preset() { return g_8bpp_preset; }
}

int g_isp_depth_preset = 1;
// 0=off (legacy: one global GEQUAL depth compare), 1=on (VQ-textured TR
//   polys lose equal-depth ties: GREATER + Z-write, others keep painter
//   order; Hokuto no Ken's VQ backdrop erased its fighters/HUD without
//   this — see gxRend.cpp ISP_DEPTH_FIX())

extern "C" {
  int get_isp_depth_preset() { return g_isp_depth_preset; }
}

int g_ignore_texa_preset = 1;
// 0=off (legacy: TEVSTAGE0 always modulates by texture alpha), 1=on (real
//   PVR forces texture alpha to 1.0 when TSP.IgnoreTexA is set; Hokuto no
//   Ken's VQ stage tiles with garbage alpha channels vanished without this
//   — see gxRend.cpp IGNORE_TEXA_FIX())

extern "C" {
  int get_ignore_texa_preset() { return g_ignore_texa_preset; }
}

int g_texture_cache_preset = 2;
// 0 = VERY FAST (skmp algorythm)
// 1 = FAST
// 2 = NORMAL
// 3 = QUALITY
// 4 = EXTRA (debug)
// 5 = EXTRA_DEBUG (debug)

extern "C" {
  int get_texture_cache_preset() { return g_texture_cache_preset; }
}

int g_ppz_write_preset = 1; // 1 for Test Drive / Demolition Racer

extern "C" {
  int get_ppz_write_preset() { return g_ppz_write_preset; }
}

int g_x_scaler_preset = 1; // 0=off (legacy), 1=PVR SCALER_CTL.hscale support (Omicron / Wacky Races render 1280 wide, scaler halves 2:1)

extern "C" {
  int get_x_scaler_preset() { return g_x_scaler_preset; }
}

int g_canvas_width_preset = 0; // 0=off (legacy 640 canvas); else forced canvas width in 240p modes (SF3 Double Impact: 384, the CPS3 arcade width)

extern "C" {
  int get_canvas_width_preset() { return g_canvas_width_preset; }
}

int g_framebuffer_2d = 0; // 1 to activate 2D Framebuffer

extern "C" {
  int get_framebuffer_2d() { return g_framebuffer_2d; }
}

int g_fmv_format_preset = 2; // 0=CMPR (DXT1), 1=RGBA8, 2=RGB565

extern "C" {
  int get_fmv_format_preset() { return g_fmv_format_preset; }
}

int g_jojo_fix_preset = 0; // 0=off (pre-fix behavior), 1 = On

extern "C" {
  int get_jojo_fix_preset() { return g_jojo_fix_preset; }
}

int g_vertex_color_fix_preset = 1; // 0 = Greyscale , 1 = On

extern "C" {
  int get_vertex_color_fix_preset() { return g_vertex_color_fix_preset; }
}

int g_blend_mode_preset = 1; // 0=off 1=on (per-polygon TSP blend, correct for RE3)

extern "C" {
  int get_blend_mode_preset() { return g_blend_mode_preset; }
}

int g_rgb565_opaque_alpha_preset = 0; // 1=force opaque for fmt0(ARGB1555)+fmt1(RGB565), 0=only fmt0 (Fixes POD 2)

extern "C" {
  int get_rgb565_opaque_alpha_preset() { return g_rgb565_opaque_alpha_preset; }
}

int g_blend_fps_boost_preset = 0; // 0=off (correct alpha), 1=on (few extra FPS, e.g. Castlevania, wrong alpha)

extern "C" {
  int get_blend_fps_boost_preset() { return g_blend_fps_boost_preset; }
}

int g_punch_through_preset = 0; // 0=legacy (PT polys drawn last in TR blend state), 1=OP->PT->TR order + PT_ALPHA_REF alpha test

extern "C" {
  int get_punch_through_preset() { return g_punch_through_preset; }
}

int g_offset_color_preset = 1; // 0=off (offset/specular color dropped, legacy), 1=on (PIX = base*tex + offset via 2nd TEV stage)

extern "C" {
  int get_offset_color_preset() { return g_offset_color_preset; }
}

int g_trans_sort_preset = 0; // 0=off (TR strips drawn in TA submission order, legacy), 1=on (per-strip back-to-front depth sort)

extern "C" {
  int get_trans_sort_preset() { return g_trans_sort_preset; }
}

int g_render_to_texture_preset = 0; // 0=off (RTT frames dropped, legacy), 1=on (EFB copied back into VRAM at FB_W_SOF1)

extern "C" {
  int get_render_to_texture_preset() { return g_render_to_texture_preset; }
}

int g_split_screen_preset = 0; // 0=off (every render pass presented fullscreen, legacy), 1=on (partial-clip passes scissored into the EFB, presented once per vblank — 2P splitscreen)

extern "C" {
  int get_split_screen_preset() { return g_split_screen_preset; }
}

int g_mipmap_preset = 0; // 0=off (legacy base-level-only, fastest), 1=fast (generated GX mip chain + nearest-mip bilinear), 2=trilinear (best quality, halves texture fill rate — e.g. -40% in Test Drive 6)

extern "C" {
  int get_mipmap_preset() { return g_mipmap_preset; }
}

int g_fixed_depth_preset = 0; // 0=off (per-vertex min/max W tracking, legacy), 1=wide fixed planes [0.0001..100000] (safe, coarse Z), 2=tight fixed planes [0.1..25000] (finer Z, extreme near/far geometry clips)

extern "C" {
  int get_fixed_depth_preset() { return g_fixed_depth_preset; }
}

int g_depth_clip_preset = 1; // 0=off (legacy: XF Z-clipping on, no near margin), 1=near margin (pad vtx_min_Z 0.1% so the nearest 2D layer can't land exactly on the near clip plane), 2=no clip (GX_SetClipMode(GX_CLIP_DISABLE), matches Dolphin which never Z-clips: out-of-range depth clamps instead of the poly vanishing)

extern "C" {
  int get_depth_clip_preset() { return g_depth_clip_preset; }
}

int g_async_render_preset = 0; // 0=off (CPU blocks in GX_DrawDone until the GPU finishes each frame, legacy), 1=on (frame queued, presented one vblank later; SH4 emulates while the GPU draws)

extern "C" {
  int get_async_render_preset() { return g_async_render_preset; }
}

int g_tmem_cache_preset = 0; // 0=off (full GPU texture cache invalidate every frame, legacy), 1=on (invalidate only on texture re-decode; unchanged textures stay cached in TMEM across frames)

extern "C" {
  int get_tmem_cache_preset() { return g_tmem_cache_preset; }
}

int g_speed_limiter_preset = 0; // 0=off (uncapped, may run >100%), 1=on (capped at real-hardware speed)

extern "C" {
  int get_speed_limiter_preset() { return g_speed_limiter_preset; }
}

int g_bg_poly_preset = 0; // 0=off (legacy: v0 color used for EFB clear only, no background quad drawn), 1=on (barycentric-extrapolated background quad drawn, e.g. Who Wants to Be a Millionaire)

extern "C" {
  int get_bg_poly_preset() { return g_bg_poly_preset; }
}

int g_player_count = 4;

extern "C" {
  int  get_player_count()      { return g_player_count; }
  void set_player_count(int n) { g_player_count = (n >= 1 && n <= 4) ? n : 1; }
}

// ============================================================================
// CONTROLLER TYPE
// ============================================================================
//   0 = STANDARD    Standard Dreamcast controller
//   1 = LIGHT_GUN   Light gun / Stunner
//   2 = MARACAS     Samba de Amigo maracas
//   3 = KEYBOARD    Typing of the Dead USB keyboard
//   4 = FISHING_ROD Sega Bass Fishing rod

int g_controller_type = 0;

extern "C" {
  int get_controller_type() { return g_controller_type; }
}

static const char* kControllerTypeNames[] = {
  "STANDARD",
  "LIGHT GUN",
  "MARACAS (Samba de Amigo)",
  "KEYBOARD (Typing of the Dead)",
  "FISHING ROD (Bass Fishing)",
};
static const int kControllerTypeCount = 5;

// ============================================================================
// DEBUG MODE
// ============================================================================

int g_debug_loop = 0;
extern "C" { int get_debug_loop()    { return g_debug_loop;    } }

int g_debug_message = 0;
extern "C" { int get_debug_message() { return g_debug_message; } }

int g_debug_gdrom = 0;
extern "C" { int get_debug_gdrom()   { return g_debug_gdrom;   } }

// ============================================================================
// FILE BROWSER STATE
// ============================================================================

struct FileEntry
{
  char name[256];
  char fullPath[512];
  bool isDirectory;
};

typedef enum {
  STORAGE_SD  = 0,
  STORAGE_USB = 1
} StorageSource;

StorageSource g_storage_source = STORAGE_SD;
bool g_usb_mounted = false;

// Carved from the MEM2 arena on first listFilesInDirectory() call instead of
// sitting in MEM1 BSS (~197 KB) — MEM1 is nearly exhausted, MEM2 has headroom.
#define MAX_FILE_LIST 256
FileEntry* fileList = NULL;
int fileCount = 0;
char selectedFilePath[512] = "";
char currentPath[512] = "sd:/discs/";
const int ITEMS_PER_PAGE = 10;
int currentPage = 0;

// Double-buffer globals
static void *xfb[2];
static GXRModeObj *rmode = NULL;
static int fb = 0;

// ============================================================================
// FILE HELPERS
// ============================================================================

bool hasValidExtension(const char *filename)
{
  size_t len = strlen(filename);
  if (len < 4) return false;

  const char *ext = &filename[len - 4];
  char extLower[5];
  for (int i = 0; i < 4; i++)
    extLower[i] = tolower(ext[i]);
  extLower[4] = '\0';

  return (strcmp(extLower, ".gdi") == 0 ||
          strcmp(extLower, ".cdi") == 0 ||
          strcmp(extLower, ".iso") == 0 ||
          strcmp(extLower, ".bin") == 0 ||
          strcmp(extLower, ".cue") == 0 ||
          strcmp(extLower, ".nrg") == 0 ||
          strcmp(extLower, ".mds") == 0 ||
          strcmp(extLower, ".elf") == 0 ||
          strcmp(extLower, ".chd") == 0);
}

// ============================================================================
// STORAGE SWITCHING
// ============================================================================

bool switchToUSB()
{
  if (!g_usb_mounted)
  {
    if (USBStorage_Initialize() == 0)
    {
      for (int retry = 0; retry < 30; retry++)
      {
        if (fatMountSimple("usb", &__io_usbstorage))
        {
          g_usb_mounted = true;
          break;
        }
        usleep(100000);
      }
    }
  }
  if (g_usb_mounted)
  {
    g_storage_source = STORAGE_USB;
    strcpy(currentPath, "usb:/dreamcast/");
    return true;
  }
  return false;
}

void switchToSD()
{
  g_storage_source = STORAGE_SD;
  strcpy(currentPath, "sd:/discs/");
}

// ============================================================================
// DIRECTORY LISTING
// ============================================================================

void listFilesInDirectory(const char *dirPath)
{
  DIR *dir;
  struct dirent *entry;
  struct stat statbuf;

  // Allocate the browser list from MEM2 once (kept for the whole session).
  // fileCount stays 0 on failure, so the menu never dereferences NULL.
  if (!fileList)
  {
    u32 need = (u32)sizeof(FileEntry) * MAX_FILE_LIST;
    u8* lo   = (u8*)(((u32)SYS_GetArena2Lo() + 31) & ~31); // 32-byte align
    if ((u8*)SYS_GetArena2Hi() - lo < (s32)need)
    {
      printf("Not enough MEM2 for file browser list (%u KB)\n", need / 1024);
      fileCount = 0;
      return;
    }
    SYS_SetArena2Lo(lo + need);
    fileList = (FileEntry*)lo;
    memset(fileList, 0, need);
  }

  if ((dir = opendir(dirPath)) != NULL)
  {
    fileCount = 0;

    while ((entry = readdir(dir)) != NULL && fileCount < MAX_FILE_LIST)
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

      char fullPath[512];
      int pathLen = snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

      if ((size_t)pathLen >= sizeof(fullPath))
      {
        printf("Warning: Path too long, skipping: %s/%s\n", dirPath, entry->d_name);
        continue;
      }

      if (stat(fullPath, &statbuf) == 0)
      {
        if (S_ISDIR(statbuf.st_mode))
        {
          size_t maxName = sizeof(fileList[fileCount].name) - 3;
          snprintf(fileList[fileCount].name, sizeof(fileList[fileCount].name),
                   "[%.*s]", (int)maxName, entry->d_name);
          strcpy(fileList[fileCount].fullPath, fullPath);
          fileList[fileCount].isDirectory = true;
          fileCount++;
        }
        else if (hasValidExtension(entry->d_name))
        {
          strcpy(fileList[fileCount].name, entry->d_name);
          strcpy(fileList[fileCount].fullPath, fullPath);
          fileList[fileCount].isDirectory = false;
          fileCount++;
        }
      }
    }

    closedir(dir);

    // Sort: folders first, then alphabetically within each group
    for (int i = 0; i < fileCount - 1; i++)
    {
      for (int j = i + 1; j < fileCount; j++)
      {
        bool shouldSwap = false;
        if (!fileList[i].isDirectory && fileList[j].isDirectory)
          shouldSwap = true;
        else if (fileList[i].isDirectory == fileList[j].isDirectory)
          if (strcmp(fileList[i].name, fileList[j].name) > 0)
            shouldSwap = true;

        if (shouldSwap)
        {
          FileEntry temp = fileList[i];
          fileList[i] = fileList[j];
          fileList[j] = temp;
        }
      }
    }

    if (fileCount == 0)
    {
      strncpy(fileList[0].name, "<<NO COMPATIBLE FILE FOUND>>",
              sizeof(fileList[0].name) - 1);
      fileList[0].name[sizeof(fileList[0].name) - 1] = '\0';
      strcpy(fileList[0].fullPath, "");
      fileList[0].isDirectory = false;
      fileCount = 1;
    }
  }
  else
  {
    printf("Could not open directory: %s\n", dirPath);
    strncpy(fileList[0].name, "<<NO COMPATIBLE FILE FOUND>>",
            sizeof(fileList[0].name) - 1);
    fileList[0].name[sizeof(fileList[0].name) - 1] = '\0';
    strcpy(fileList[0].fullPath, "");
    fileList[0].isDirectory = false;
    fileCount = 1;
  }
}

// ============================================================================
// WII U GAMEPAD (DRC) MENU INPUT
// ============================================================================
//
// When running on a Wii U in vWii mode (via a forwarder), the GamePad is
// readable through libwiidrc. These helpers translate its buttons to the
// WPAD_BUTTON_* convention already used by every menu loop, following the
// same mapping as the GameCube pad merge (Y=button1, X=button2).
// On a real Wii, WiiDRC_Init() fails and both helpers return 0.
// ============================================================================

static u32 DRC_ToWPAD(u32 drc)
{
  u32 w = 0;
  if (drc & WIIDRC_BUTTON_UP)    w |= WPAD_BUTTON_UP;
  if (drc & WIIDRC_BUTTON_DOWN)  w |= WPAD_BUTTON_DOWN;
  if (drc & WIIDRC_BUTTON_LEFT)  w |= WPAD_BUTTON_LEFT;
  if (drc & WIIDRC_BUTTON_RIGHT) w |= WPAD_BUTTON_RIGHT;
  if (drc & WIIDRC_BUTTON_A)     w |= WPAD_BUTTON_A;
  if (drc & WIIDRC_BUTTON_B)     w |= WPAD_BUTTON_B;
  if (drc & WIIDRC_BUTTON_Y)     w |= WPAD_BUTTON_1;
  if (drc & WIIDRC_BUTTON_X)     w |= WPAD_BUTTON_2;
  if (drc & WIIDRC_BUTTON_MINUS) w |= WPAD_BUTTON_MINUS;
  if (drc & WIIDRC_BUTTON_PLUS)  w |= WPAD_BUTTON_PLUS;
  if (drc & WIIDRC_BUTTON_HOME)  w |= WPAD_BUTTON_HOME;
  return w;
}

// Scans the GamePad and returns freshly pressed buttons as WPAD bits.
// Call exactly once per menu loop iteration (alongside WPAD_ScanPads).
static u32 DRC_ButtonsDownWPAD()
{
  if (!WiiDRC_Inited())
    return 0;
  WiiDRC_ScanPads();
  if (WiiDRC_ShutdownRequested())
    exit(0);
  return DRC_ToWPAD(WiiDRC_ButtonsDown());
}

// Held buttons from the most recent scan (does not scan again).
static u32 DRC_ButtonsHeldWPAD()
{
  if (!WiiDRC_Inited())
    return 0;
  return DRC_ToWPAD(WiiDRC_ButtonsHeld());
}

// ============================================================================
// WII CLASSIC CONTROLLER MENU INPUT
// ============================================================================
//
// WPAD_ButtonsDown/Held already carry the Classic Controller buttons in the
// upper bits (WPAD_CLASSIC_BUTTON_*) of the same word, but the menu loops
// only test the Wiimote core WPAD_BUTTON_* bits. This translates the classic
// bits to that convention, following the same mapping as the GameCube pad
// and DRC merges (Y=button1, X=button2). Pass it the raw WPAD word and OR
// the result back in.
// ============================================================================

static u32 CLASSIC_ToWPAD(u32 wpad)
{
  u32 w = 0;
  if (wpad & WPAD_CLASSIC_BUTTON_UP)    w |= WPAD_BUTTON_UP;
  if (wpad & WPAD_CLASSIC_BUTTON_DOWN)  w |= WPAD_BUTTON_DOWN;
  if (wpad & WPAD_CLASSIC_BUTTON_LEFT)  w |= WPAD_BUTTON_LEFT;
  if (wpad & WPAD_CLASSIC_BUTTON_RIGHT) w |= WPAD_BUTTON_RIGHT;
  if (wpad & WPAD_CLASSIC_BUTTON_A)     w |= WPAD_BUTTON_A;
  if (wpad & WPAD_CLASSIC_BUTTON_B)     w |= WPAD_BUTTON_B;
  if (wpad & WPAD_CLASSIC_BUTTON_Y)     w |= WPAD_BUTTON_1;
  if (wpad & WPAD_CLASSIC_BUTTON_X)     w |= WPAD_BUTTON_2;
  if (wpad & WPAD_CLASSIC_BUTTON_MINUS) w |= WPAD_BUTTON_MINUS;
  if (wpad & WPAD_CLASSIC_BUTTON_PLUS)  w |= WPAD_BUTTON_PLUS;
  if (wpad & WPAD_CLASSIC_BUTTON_HOME)  w |= WPAD_BUTTON_HOME;
  return w;
}

// ============================================================================
// BIOS PRESENCE CHECK
// ============================================================================
//
// Checks for the presence of dc_boot.bin and dc_flash.bin in the data/
// folder (same location used by GetEmuPath()/LoadBiosFiles() in dc.cpp).
// Prints "missing BIOS file <name>" for each missing file, then pauses
// and waits for a button press so the message is actually seen before
// the file browser clears the screen.
// ============================================================================

void checkBiosFiles()
{
  const char* kBiosFiles[] = { "dc_boot.bin", "dc_flash.bin" };
  const int kBiosFileCount = 2;
  bool anyMissing = false;

  printf("\033[2J\033[H");

  for (int i = 0; i < kBiosFileCount; i++)
  {
    char subpath[64];
    snprintf(subpath, sizeof(subpath), "data/%s", kBiosFiles[i]);

    char* fullPath = GetEmuPath(subpath);
    if (!fullPath)
      continue;

    FILE* f = fopen(fullPath, "rb");
    if (!f)
    {
      printf("missing BIOS file %s\n", kBiosFiles[i]);
      anyMissing = true;
    }
    else
    {
      fclose(f);
    }

    free(fullPath);
  }

  if (anyMissing)
  {
    printf("\nPlace the missing file(s) in the data/ folder.\n");
    printf("Press any button to continue...\n");

    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    while (true)
    {
      WPAD_ScanPads();
      if (WPAD_ButtonsDown(0) != 0 || DRC_ButtonsDownWPAD() != 0)
        break;
      VIDEO_WaitVSync();
    }
  }
}

// ============================================================================
// ACCURACY / INFO MENU
// ============================================================================

void displayAccuracyMenu()
{

  while (true)
  {
    printf("\033[2J\033[H");
    printf("                  INFO - NullDC4Wii               \n");
    printf(" \n");
    printf("Information about preset :\n\n");

    printf("Calculations Accuracy (can lead to bugs if not ACCURATE):\n");
    printf("> FAST     - Maximum Speed\n");
    printf("> BALANCED - Good Balance\n");
    printf("> ACCURATE - Maximum Accuracy (closest to real hardware)\n\n");

    printf("Graphical settings:\n");
    printf("> LOW    = GX_NEAR   - lod0 - GX_DISABLE (Wii)\n");
    printf("> NORMAL = GX_LINEAR - lod0 - GX_DISABLE (Wii)\n");
    printf("> HIGH   = GX_LINEAR - lodH - GX_ENABLE  - Anisotropic x2 (WiiU)\n");
    printf("> EXTRA  = GX_LINEAR - lodE - GX_ENABLE  - Anisotropic x4 (WiiU)\n\n");

    printf("Ratio:\n");
    printf("> ORIGINAL   - 4/3 ratio\n");
    printf("> FULLSCREEN\n\n");

    printf("\nB: Back\n");
    printf("Note: Change settings if you experience issues or need more speed.\n");

    WPAD_ScanPads();
    PAD_ScanPads();
    u32 wmPressed = WPAD_ButtonsDown(0);
    u32 pressed = wmPressed | CLASSIC_ToWPAD(wmPressed) | DRC_ButtonsDownWPAD()
                | ((PAD_ButtonsDown(0) & PAD_BUTTON_B) ? WPAD_BUTTON_B : 0);

    if (pressed & WPAD_BUTTON_B)
    {
      return;
    }

    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }
}

// ============================================================================
// OPTIONS MENU
// ============================================================================

#define OPT_LAUNCH      0
// row 1 = game name (display only, not selectable)
// row 2 = preset banner (display only, not selectable)
// row 3 = blank separator
// Numbering below follows the order rows are actually printed on screen.
#define OPT_GRAPHICS    4
#define OPT_ACCURACY    5
#define OPT_RATIO       6
#define OPT_PPZ_WRITE   7
#define OPT_DECAL_ALPHA 8
#define OPT_FRAMEBUFFER_2D 9
#define OPT_FMV_FORMAT  10
#define OPT_FRAMESKIP   11
#define OPT_TEX_CACHE   12
#define OPT_4BPP        13
#define OPT_8BPP        14
#define OPT_JOJO_FIX    15
#define OPT_SPEED_LIMIT 16
#define OPT_VERTEX_COLOR_FIX 17
#define OPT_ADV_ALPHA   18
#define OPT_BLEND_MODE  19
#define OPT_BLEND_FPS_BOOST 20
#define OPT_RGB565_OPAQUE_ALPHA 21
#define OPT_PUNCH_THROUGH 22
#define OPT_OFFSET_COLOR 23
#define OPT_TRANS_SORT  24
#define OPT_RENDER_TO_TEXTURE 25
#define OPT_SPLIT_SCREEN 26
#define OPT_MIPMAP      27
#define OPT_FIXED_DEPTH 28
#define OPT_DEPTH_CLIP  29
#define OPT_ASYNC_RENDER 30
#define OPT_TMEM_CACHE  31
#define OPT_BG_POLY     32
#define OPT_X_SCALER    33
#define OPT_CANVAS_WIDTH 34
#define OPT_ROW_COUNT   35

// Rows that are display-only (not selectable by cursor)
static bool opt_row_is_display(int row)
{
  return (row == 1 || row == 2 || row == 3);
}

// Second options page holds the less commonly tweaked presets, so the
// main page doesn't scroll off screen.
#define OPT_PAGE_COUNT 2

static int opt_row_page(int row)
{
  switch (row) {
    case OPT_ACCURACY:
    case OPT_PPZ_WRITE:
    case OPT_FMV_FORMAT:
    case OPT_VERTEX_COLOR_FIX:
    case OPT_MIPMAP:
    case OPT_FIXED_DEPTH:
    case OPT_DEPTH_CLIP:
    case OPT_ASYNC_RENDER:
    case OPT_TMEM_CACHE:
    case OPT_BG_POLY:
    case OPT_X_SCALER:
    case OPT_CANVAS_WIDTH:
      return 1;
    default:
      return 0;
  }
}

// OPT_LAUNCH is shared across both pages so A/B/1 keep working everywhere.
static bool opt_row_on_page(int row, int page)
{
  if (row == OPT_LAUNCH)
    return true;
  return opt_row_page(row) == page;
}

bool displayOptionsMenu()
{
  int selectedRow = OPT_LAUNCH;
  int optionsPage = 0;

  // Debounce: the A press used to select the file in the browser can
  // otherwise bleed through as a fresh "A down" event on the very first
  // scan here (seen with the GameCube pad), instantly triggering LAUNCH
  // before the menu is even shown. Wait for A to be released first.
  while ((WPAD_ButtonsHeld(0) & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))
         || (PAD_ButtonsHeld(0) & PAD_BUTTON_A)
         || (DRC_ButtonsHeldWPAD() & WPAD_BUTTON_A))
  {
    WPAD_ScanPads();
    PAD_ScanPads();
    if (WiiDRC_Inited())
      WiiDRC_ScanPads();
    VIDEO_WaitVSync();
  }

  while (true)
  {
    printf("\033[2J\033[H");

    // --- Row 0: Launch ---
    printf("%s LAUNCH GAME\n",
           (selectedRow == OPT_LAUNCH) ? ">" : " ");

    // --- Row 1: Game name (display only) ---
    {
      const char *gameName = strrchr(selectedFilePath, '/');
      gameName = (gameName != NULL) ? gameName + 1 : selectedFilePath;
      printf("    %.60s\n", gameName);
    }

    // --- Row 2: Preset banner (display only) ---
    if (g_matched_preset_name[0] != '\0')
      printf("    * Preset [%s] applied\n", g_matched_preset_name);
    else
      printf("    (no game preset matched)\n");

    printf("\n");

    if (optionsPage == 1)
      printf("    -- PRESETS PAGE 2/2 (1: Back to Page 1) --\n\n");

    if (optionsPage == 0) {
    // --- Row 3: Graphics ---
    printf("%s GRAPHICS       : ", (selectedRow == OPT_GRAPHICS) ? ">" : " ");
    switch (g_graphism_preset) {
      case 0: printf("[< LOW               >]"); break;
      case 1: printf("[< NORMAL            >]"); break;
      case 2: printf("[< HIGH              >]"); break;
      case 3: printf("[< EXTRA             >]"); break;
    }
    printf(" (2D Games should use LOW)");
    printf("\n");

    // --- Row 5: Ratio ---
    printf("%s RATIO          : ", (selectedRow == OPT_RATIO) ? ">" : " ");
    switch (g_ratio_preset) {
      case 0: printf("[< ORIGINAL          >]"); break;
      case 1: printf("[< FULLSCREEN        >]"); break;
    }
    printf("\n");

    // --- Row 8: Decal Alpha Fix ---
    printf("%s DECAL ALPHA    : ", (selectedRow == OPT_DECAL_ALPHA) ? ">" : " ");
    switch (g_decal_alpha_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" Fix Crazy Taxi's cars");
    printf("\n");

    // --- Row 9: 2D Framebuffer ---
    printf("%s 2D FRAMEBUFFER : ", (selectedRow == OPT_FRAMEBUFFER_2D) ? ">" : " ");
    switch (g_framebuffer_2d) {
      case 0: printf("[< NO                >]"); break;
      case 1: printf("[< YES               >]"); break;
    }
    printf(" Try for 2D games");
    printf("\n");

    // --- Row 11: Frameskipping ---
    printf("%s FRAMESKIPPING  : ", (selectedRow == OPT_FRAMESKIP) ? ">" : " ");
    switch (g_frameskip_preset) {
      case 0: printf("[< 0 (DEFAULT)       >]"); break;
      case 1: printf("[< 1                 >]"); break;
      case 2: printf("[< 2                 >]"); break;
      case 3: printf("[< AUTO              >]"); break;
    }
    printf("\n");

    // --- Row 12: Texture Cache ---
    printf("%s TEXTURE CACHE  : ", (selectedRow == OPT_TEX_CACHE) ? ">" : " ");
    switch (g_texture_cache_preset) {
      case 0: printf("[< VERY FAST         >]"); break;
      case 1: printf("[< FAST              >]"); break;
      case 2: printf("[< NORMAL (DEFAULT)  >]"); break;
      case 3: printf("[< QUALITY (SLOW)    >]"); break;
    }
    printf(" Can have huge FPS impact");
    printf("\n");

    // --- Row 13: 4BPP ---
    printf("%s 4BPP MODE      : ", (selectedRow == OPT_4BPP) ? ">" : " ");
    switch (g_4bpp_preset) {
      case 0: printf("[< I4 STUB           >]"); break;
      case 1: printf("[< 4BPP OPTIMIZED    >]"); break;
      case 2: printf("[< CI4 (FAST)        >]"); break;
      case 3: printf("[< CI4 (NORMAL)      >]"); break;
      case 4: printf("[< RGB565 (ACCURATE) >]"); break;
    }
    printf("\n");

    // --- Row 14: 8BPP ---
    printf("%s 8BPP MODE      : ", (selectedRow == OPT_8BPP) ? ">" : " ");
    switch (g_8bpp_preset) {
      case 0: printf("[< I8 STUB           >]"); break;
      case 1: printf("[< 8BPP OPTIMIZED    >]"); break;
      case 2: printf("[< CI8 (FAST)        >]"); break;
      case 3: printf("[< CI8 (NORMAL)      >]"); break;
      case 4: printf("[< RGB565 (ACCURATE) >]"); break;
    }
    printf("\n");

    // --- Row 15: Jojo Fix ---
    printf("%s JOJO FIX       : ", (selectedRow == OPT_JOJO_FIX) ? ">" : " ");
    switch (g_jojo_fix_preset) {
      case 0: printf("[< OFF               >]"); break;
      case 1: printf("[< ON (DEFAULT)      >]"); break;
    }
    printf(" for JoJo's Bizarre Adventure");
    printf("\n");

    // --- Row 16: Speed Limiter ---
    printf("%s SPEED LIMITER  : ", (selectedRow == OPT_SPEED_LIMIT) ? ">" : " ");
    switch (g_speed_limiter_preset) {
      case 0: printf("[< OFF (UNCAPPED)    >]"); break;
      case 1: printf("[< ON (CAP 100%%)     >]"); break;
    }
    printf(" Stops speed exceeding 100%%");
    printf("\n");

    // --- Row 7: Advanced Alpha ---
    printf("%s ADVANCED ALPHA : ", (selectedRow == OPT_ADV_ALPHA) ? ">" : " ");
    switch (g_advanced_alpha_preset) {
      case 0: printf("[< NO                >]"); break;
      case 1: printf("[< YES (DEFAULT)     >]"); break;
    }
    printf("\n");

    // --- Row 23: Blend Mode ---
    printf("%s > BLEND MODE   : ", (selectedRow == OPT_BLEND_MODE) ? ">" : " ");
    switch (g_blend_mode_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" ON for Resident Evil 3");
    printf("\n");

    // --- Row 25: Blend FPS Boost ---
    printf("%s >> FPS BOOST   : ", (selectedRow == OPT_BLEND_FPS_BOOST) ? ">" : " ");
    switch (g_blend_fps_boost_preset) {
      case 0: printf("[< OFF (CORRECT)     >]"); break;
      case 1: printf("[< ON (FASTER)       >]"); break;
    }
    printf(" +2 FPS in BLEND MODE but bad alpha");
    printf("\n");

    // --- Row 24: RGB565 Opaque Alpha ---
    printf("%s RGB565 ALPHA   : ", (selectedRow == OPT_RGB565_OPAQUE_ALPHA) ? ">" : " ");
    switch (g_rgb565_opaque_alpha_preset) {
      case 0: printf("[< OFF (FMT0 ONLY)   >]"); break;
      case 1: printf("[< ON (FMT0+FMT1)    >]"); break;
    }
    printf(" OFF for POD2");
    printf("\n");

    // --- Row 26: Punch-Through ---
    printf("%s PUNCH THROUGH  : ", (selectedRow == OPT_PUNCH_THROUGH) ? ">" : " ");
    switch (g_punch_through_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" PT list alpha test");
    printf("\n");

    // --- Row 27: Offset (specular) color ---
    printf("%s OFFSET COLOR   : ", (selectedRow == OPT_OFFSET_COLOR) ? ">" : " ");
    switch (g_offset_color_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" specular highlights");
    printf("\n");

    // --- Row 28: Translucent depth sort ---
    printf("%s TRANS SORT     : ", (selectedRow == OPT_TRANS_SORT) ? ">" : " ");
    switch (g_trans_sort_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" translucent polys / No flickers");
    printf("\n");

    // --- Row 29: Render to texture ---
    printf("%s RENDER TO TEX  : ", (selectedRow == OPT_RENDER_TO_TEXTURE) ? ">" : " ");
    switch (g_render_to_texture_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" mirrors/TV screens");
    printf("\n");

    // --- Row 30: Split-screen multiplayer ---
    printf("%s SPLIT SCREEN   : ", (selectedRow == OPT_SPLIT_SCREEN) ? ">" : " ");
    switch (g_split_screen_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" 2P viewports, Daytona USA");
    printf("\n\n");

    printf("A: Launch | B: Back | 1: More Info | 2: Page 2 | alpha0.51\n");
    } // end page 0

    if (optionsPage == 1) {
    // --- Row 4: Accuracy ---
    printf("%s ACCURACY       : ", (selectedRow == OPT_ACCURACY) ? ">" : " ");
    switch (g_accuracy_preset) {
      case 0: printf("[< FAST (DEFAULT)    >]"); break;
      case 1: printf("[< BALANCED          >]"); break;
      case 2: printf("[< ACCURATE          >]"); break;
    }
    printf(" ACCURATE if strange AI behavior");
    printf("\n");

    // --- Row 6: PPZ Write ---
    printf("%s PPZ_WRITE      : ", (selectedRow == OPT_PPZ_WRITE) ? ">" : " ");
    switch (g_ppz_write_preset) {
      case 0: printf("[< NO                >]"); break;
      case 1: printf("[< YES               >]"); break;
    }
    printf("\n");

    // --- Row 10: FMV Format ---
    printf("%s FMV FORMAT     : ", (selectedRow == OPT_FMV_FORMAT) ? ">" : " ");
    switch (g_fmv_format_preset) {
      case 0: printf("[< CMPR (DXT1)       >]"); break;
      case 1: printf("[< RGBA8             >]"); break;
      case 2: printf("[< RGB565 (FASTER)   >]"); break;
    }
    printf(" CMPR if some movie display white");
    printf("\n");

    // --- Row 17b: Intensity Color Fix ---
    printf("%s VERTEX COLOR   : ", (selectedRow == OPT_VERTEX_COLOR_FIX) ? ">" : " ");
    switch (g_vertex_color_fix_preset) {
      case 0: printf("[< OFF (GRAY SCALE)  >]"); break;
      case 1: printf("[< ON                >]"); break;
    }
    printf(" Crazy Taxi's arrow or JSR logo");
    printf("\n");

    // --- Row: Mipmap generation ---
    printf("%s MIPMAPS        : ", (selectedRow == OPT_MIPMAP) ? ">" : " ");
    switch (g_mipmap_preset) {
      case 0: printf("[< OFF (FASTEST)     >]"); break;
      case 1: printf("[< FAST              >]"); break;
      case 2: printf("[< TRILINEAR (SLOW)  >]"); break;
    }
    printf(" less shimmer far away");
    printf("\n");

    // --- Row: Fixed depth projection ---
    printf("%s FIXED DEPTH    : ", (selectedRow == OPT_FIXED_DEPTH) ? ">" : " ");
    switch (g_fixed_depth_preset) {
      case 0: printf("[< OFF (DYNAMIC)     >]"); break;
      case 1: printf("[< WIDE (BUGGY)      >]"); break;
      case 2: printf("[< TIGHT (Fix Z-Fght)>]"); break;
    }
    printf(" fixed near/far planes. Z-Fighting");
    printf("\n");

    // --- Row: Depth clip behaviour ---
    printf("%s DEPTH CLIP     : ", (selectedRow == OPT_DEPTH_CLIP) ? ">" : " ");
    switch (g_depth_clip_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< NEAR MARGIN (WII) >]"); break;
      case 2: printf("[< NO CLIP (DOLPHIN) >]"); break;
    }
    printf(" 2D/menus invisible on real Wii");
    printf("\n");

    // --- Row: Async render (CPU/GPU overlap) ---
    printf("%s ASYNC RENDER   : ", (selectedRow == OPT_ASYNC_RENDER) ? ">" : " ");
    switch (g_async_render_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< ON (FASTER?)      >]"); break;
    }
    printf(" uses GPU, cost 1 frame input-lag");
    printf("\n");

    // --- Row: TMEM texture cache ---
    printf("%s TMEM CACHE     : ", (selectedRow == OPT_TMEM_CACHE) ? ">" : " ");
    switch (g_tmem_cache_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< ON (FASTER?)      >]"); break;
    }
    printf(" keep GPU texture cache warm");
    printf("\n");

    // --- Row: Background polygon rendering ---
    printf("%s BG POLYGON     : ", (selectedRow == OPT_BG_POLY) ? ">" : " ");
    switch (g_bg_poly_preset) {
      case 0: printf("[< OFF (FASTER)      >]"); break;
      case 1: printf("[< ON (CORRECT)      >]"); break;
    }
    printf(" (bg gradient/texture)");
    printf("\n");

    // --- Row: PVR horizontal X-Scaler ---
    printf("%s X SCALER       : ", (selectedRow == OPT_X_SCALER) ? ">" : " ");
    switch (g_x_scaler_preset) {
      case 0: printf("[< OFF (LEGACY)      >]"); break;
      case 1: printf("[< ON (DEFAULT)      >]"); break;
    }
    printf(" ON for Omicron/Wacky Races");
    printf("\n");

    // --- Row: Forced canvas width (240p scenes) ---
    printf("%s CANVAS WIDTH   : ", (selectedRow == OPT_CANVAS_WIDTH) ? ">" : " ");
    if (g_canvas_width_preset <= 0)
      printf("[< OFF (640, LEGACY) >]");
    else
      printf("[< %-4d              >]", g_canvas_width_preset);
    printf(" SF3 double impact=384");
    printf("\n");
    } // end page 1


    WPAD_ScanPads();
    PAD_ScanPads();
    u32 wmPressed = WPAD_ButtonsDown(0);
    u32 pressed = wmPressed | CLASSIC_ToWPAD(wmPressed) | DRC_ButtonsDownWPAD();

    // GameCube controller (Player 1) — same mapping convention as in-game
    // input (see drkMapleDevices.cpp): Y=button1, X=button2.
    u16 gcPressed = PAD_ButtonsDown(0);
    if (gcPressed & PAD_BUTTON_UP)    pressed |= WPAD_BUTTON_UP;
    if (gcPressed & PAD_BUTTON_DOWN)  pressed |= WPAD_BUTTON_DOWN;
    if (gcPressed & PAD_BUTTON_LEFT)  pressed |= WPAD_BUTTON_LEFT;
    if (gcPressed & PAD_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    if (gcPressed & PAD_BUTTON_A)     pressed |= WPAD_BUTTON_A;
    if (gcPressed & PAD_BUTTON_B)     pressed |= WPAD_BUTTON_B;
    if (gcPressed & PAD_BUTTON_Y)     pressed |= WPAD_BUTTON_1;
    if (gcPressed & PAD_BUTTON_X)     pressed |= WPAD_BUTTON_2;

    if (pressed & WPAD_BUTTON_UP)
    {
      do {
        selectedRow = (selectedRow > 0) ? selectedRow - 1 : OPT_ROW_COUNT - 1;
      } while (opt_row_is_display(selectedRow) || !opt_row_on_page(selectedRow, optionsPage));
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      do {
        selectedRow = (selectedRow < OPT_ROW_COUNT - 1) ? selectedRow + 1 : 0;
      } while (opt_row_is_display(selectedRow) || !opt_row_on_page(selectedRow, optionsPage));
    }
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      switch (selectedRow) {
        case OPT_GRAPHICS:  g_graphism_preset      = (g_graphism_preset      + 3) % 4; break;
        case OPT_ACCURACY:  g_accuracy_preset       = (g_accuracy_preset       + 2) % 3; break;
        case OPT_RATIO:     g_ratio_preset          = (g_ratio_preset          + 1) % 2; break;
        case OPT_PPZ_WRITE: g_ppz_write_preset      = (g_ppz_write_preset      + 1) % 2; break;
        case OPT_ADV_ALPHA: g_advanced_alpha_preset = (g_advanced_alpha_preset + 1) % 2; break;
        case OPT_DECAL_ALPHA: g_decal_alpha_preset  = (g_decal_alpha_preset    + 1) % 2; break;
        case OPT_FRAMEBUFFER_2D: g_framebuffer_2d   = (g_framebuffer_2d        + 1) % 2; break;
        case OPT_FMV_FORMAT: g_fmv_format_preset    = (g_fmv_format_preset     + 2) % 3; break;
        case OPT_FRAMESKIP: g_frameskip_preset      = (g_frameskip_preset      + 3) % 4; break;
        case OPT_TEX_CACHE: g_texture_cache_preset  = (g_texture_cache_preset  + 3) % 4; break;
        case OPT_4BPP:      g_4bpp_preset           = (g_4bpp_preset           + 4) % 5; break;
        case OPT_8BPP:      g_8bpp_preset           = (g_8bpp_preset           + 4) % 5; break;
        case OPT_JOJO_FIX:  g_jojo_fix_preset       = (g_jojo_fix_preset       + 1) % 2; break;
        case OPT_SPEED_LIMIT: g_speed_limiter_preset = (g_speed_limiter_preset + 1) % 2; break;
        case OPT_VERTEX_COLOR_FIX: g_vertex_color_fix_preset = (g_vertex_color_fix_preset + 1) % 2; break;
        case OPT_BLEND_MODE: g_blend_mode_preset    = (g_blend_mode_preset    + 1) % 2; break;
        case OPT_RGB565_OPAQUE_ALPHA: g_rgb565_opaque_alpha_preset = (g_rgb565_opaque_alpha_preset + 1) % 2; break;
        case OPT_BLEND_FPS_BOOST: g_blend_fps_boost_preset = (g_blend_fps_boost_preset + 1) % 2; break;
        case OPT_PUNCH_THROUGH: g_punch_through_preset = (g_punch_through_preset + 1) % 2; break;
        case OPT_OFFSET_COLOR: g_offset_color_preset = (g_offset_color_preset + 1) % 2; break;
        case OPT_TRANS_SORT: g_trans_sort_preset = (g_trans_sort_preset + 1) % 2; break;
        case OPT_RENDER_TO_TEXTURE: g_render_to_texture_preset = (g_render_to_texture_preset + 1) % 2; break;
        case OPT_SPLIT_SCREEN: g_split_screen_preset = (g_split_screen_preset + 1) % 2; break;
        case OPT_MIPMAP:    g_mipmap_preset          = (g_mipmap_preset          + 2) % 3; break;
        case OPT_FIXED_DEPTH: g_fixed_depth_preset   = (g_fixed_depth_preset     + 2) % 3; break;
        case OPT_DEPTH_CLIP: g_depth_clip_preset     = (g_depth_clip_preset      + 2) % 3; break;
        case OPT_ASYNC_RENDER: g_async_render_preset = (g_async_render_preset    + 1) % 2; break;
        case OPT_TMEM_CACHE: g_tmem_cache_preset     = (g_tmem_cache_preset      + 1) % 2; break;
        case OPT_BG_POLY:    g_bg_poly_preset        = (g_bg_poly_preset         + 1) % 2; break;
        case OPT_X_SCALER:   g_x_scaler_preset       = (g_x_scaler_preset        + 1) % 2; break;
        case OPT_CANVAS_WIDTH:
          if (g_canvas_width_preset <= 0)        g_canvas_width_preset = 1280;
          else if (g_canvas_width_preset <= 320) g_canvas_width_preset = 0;
          else                                   g_canvas_width_preset -= 16;
          break;
        default: break;
      }
    }
    else if (pressed & WPAD_BUTTON_RIGHT)
    {
      switch (selectedRow) {
        case OPT_GRAPHICS:  g_graphism_preset      = (g_graphism_preset      + 1) % 4; break;
        case OPT_ACCURACY:  g_accuracy_preset       = (g_accuracy_preset       + 1) % 3; break;
        case OPT_RATIO:     g_ratio_preset          = (g_ratio_preset          + 1) % 2; break;
        case OPT_PPZ_WRITE: g_ppz_write_preset      = (g_ppz_write_preset      + 1) % 2; break;
        case OPT_ADV_ALPHA: g_advanced_alpha_preset = (g_advanced_alpha_preset + 1) % 2; break;
        case OPT_DECAL_ALPHA: g_decal_alpha_preset  = (g_decal_alpha_preset    + 1) % 2; break;
        case OPT_FRAMEBUFFER_2D: g_framebuffer_2d   = (g_framebuffer_2d        + 1) % 2; break;
        case OPT_FMV_FORMAT: g_fmv_format_preset    = (g_fmv_format_preset     + 1) % 3; break;
        case OPT_FRAMESKIP: g_frameskip_preset      = (g_frameskip_preset      + 1) % 4; break;
        case OPT_TEX_CACHE: g_texture_cache_preset  = (g_texture_cache_preset  + 1) % 4; break;
        case OPT_4BPP:      g_4bpp_preset           = (g_4bpp_preset           + 1) % 5; break;
        case OPT_8BPP:      g_8bpp_preset           = (g_8bpp_preset           + 1) % 5; break;
        case OPT_JOJO_FIX:  g_jojo_fix_preset       = (g_jojo_fix_preset       + 1) % 2; break;
        case OPT_SPEED_LIMIT: g_speed_limiter_preset = (g_speed_limiter_preset + 1) % 2; break;
        case OPT_VERTEX_COLOR_FIX: g_vertex_color_fix_preset = (g_vertex_color_fix_preset + 1) % 2; break;
        case OPT_BLEND_MODE: g_blend_mode_preset    = (g_blend_mode_preset    + 1) % 2; break;
        case OPT_RGB565_OPAQUE_ALPHA: g_rgb565_opaque_alpha_preset = (g_rgb565_opaque_alpha_preset + 1) % 2; break;
        case OPT_BLEND_FPS_BOOST: g_blend_fps_boost_preset = (g_blend_fps_boost_preset + 1) % 2; break;
        case OPT_PUNCH_THROUGH: g_punch_through_preset = (g_punch_through_preset + 1) % 2; break;
        case OPT_OFFSET_COLOR: g_offset_color_preset = (g_offset_color_preset + 1) % 2; break;
        case OPT_TRANS_SORT: g_trans_sort_preset = (g_trans_sort_preset + 1) % 2; break;
        case OPT_RENDER_TO_TEXTURE: g_render_to_texture_preset = (g_render_to_texture_preset + 1) % 2; break;
        case OPT_SPLIT_SCREEN: g_split_screen_preset = (g_split_screen_preset + 1) % 2; break;
        case OPT_MIPMAP:    g_mipmap_preset          = (g_mipmap_preset          + 1) % 3; break;
        case OPT_FIXED_DEPTH: g_fixed_depth_preset   = (g_fixed_depth_preset     + 1) % 3; break;
        case OPT_DEPTH_CLIP: g_depth_clip_preset     = (g_depth_clip_preset      + 1) % 3; break;
        case OPT_ASYNC_RENDER: g_async_render_preset = (g_async_render_preset    + 1) % 2; break;
        case OPT_TMEM_CACHE: g_tmem_cache_preset     = (g_tmem_cache_preset      + 1) % 2; break;
        case OPT_BG_POLY:    g_bg_poly_preset        = (g_bg_poly_preset         + 1) % 2; break;
        case OPT_X_SCALER:   g_x_scaler_preset       = (g_x_scaler_preset        + 1) % 2; break;
        case OPT_CANVAS_WIDTH:
          if (g_canvas_width_preset <= 0)         g_canvas_width_preset = 320;
          else if (g_canvas_width_preset >= 1280) g_canvas_width_preset = 0;
          else                                    g_canvas_width_preset += 16;
          break;
        default: break;
      }
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      if (selectedRow == OPT_LAUNCH)
        return true;
    }
    else if (pressed & WPAD_BUTTON_1)
    {
      if (optionsPage == 1)
      {
        optionsPage = 0;
        selectedRow = OPT_LAUNCH;
      }
      else
      {
        displayAccuracyMenu();
      }
    }
    else if (pressed & WPAD_BUTTON_2)
    {
      optionsPage = (optionsPage + 1) % OPT_PAGE_COUNT;
      selectedRow = OPT_LAUNCH;
    }
    else if (pressed & WPAD_BUTTON_B)
    {
      return false;
    }

    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }
}

// ============================================================================
// CONTROLS MENU
// ============================================================================
// Shown after the options menu, right before launch. Holds the
// player-count / controller-type presets that used to live as two rows
// inside the options menu — split out onto their own screen so they're
// easy to find and don't scroll off the options page.

#define CTRL_LAUNCH     0
// row 1 = game name (display only, not selectable)
#define CTRL_PLAYER1    2
#define CTRL_PLAYER2    3
#define CTRL_PLAYER3    4
#define CTRL_PLAYER4    5
#define CTRL_TYPE       6
#define CTRL_ROW_COUNT  7

static bool ctrl_row_is_display(int row)
{
  return (row == 1);
}

// Per-player controller mode:
//   PLAYER_CTRL_OFF           OFF (bus not created; not available for Player 1)
//   PLAYER_CTRL_WIIMOTE_GC    WIIMOTE/GAMECUBE   (bare Wiimote + Nunchuk + GameCube pad)
//   PLAYER_CTRL_WIICLASSIC_GC WIICLASSIC/GAMECUBE (Wii Classic Controller + GameCube pad)
//
// Player 1 is always active. Enabling a player forces every lower-numbered
// player on too, so slots 1..idx stay contiguous from player 1. Disabling a
// player only affects that player's own slot — it does NOT cascade to
// higher-numbered players, so e.g. player 3 OFF with player 4 still ON is a
// valid "gap" configuration. g_player_count is derived as the highest
// non-OFF player index + 1 (see ctrl_cycle_player_mode), so such a gap still
// counts as N-player support as long as player N is on.
enum { PLAYER_CTRL_OFF = 0, PLAYER_CTRL_WIIMOTE_GC = 1, PLAYER_CTRL_WIICLASSIC_GC = 2 };
#define PLAYER_CTRL_MODE_COUNT 3

static int g_player_mode[4] = { PLAYER_CTRL_WIIMOTE_GC, PLAYER_CTRL_WIIMOTE_GC,
                                 PLAYER_CTRL_WIIMOTE_GC, PLAYER_CTRL_WIIMOTE_GC };

static void ctrl_sync_player_mode_from_count()
{
  for (int i = 0; i < 4; i++)
  {
    bool shouldBeOn = (i < g_player_count);
    bool isOn = (g_player_mode[i] != PLAYER_CTRL_OFF);
    if (shouldBeOn && !isOn)
      g_player_mode[i] = PLAYER_CTRL_WIIMOTE_GC;
    else if (!shouldBeOn)
      g_player_mode[i] = PLAYER_CTRL_OFF;
  }
}

// dir: -1 to cycle left/back, +1 to cycle right/forward.
static void ctrl_cycle_player_mode(int idx, int dir)
{
  if (idx == 0)
  {
    // Player 1 can't be turned off; just flip between the two device types.
    g_player_mode[0] = (g_player_mode[0] == PLAYER_CTRL_WIIMOTE_GC)
                          ? PLAYER_CTRL_WIICLASSIC_GC : PLAYER_CTRL_WIIMOTE_GC;
    return;
  }

  bool wasOff = (g_player_mode[idx] == PLAYER_CTRL_OFF);
  g_player_mode[idx] = (g_player_mode[idx] + dir + PLAYER_CTRL_MODE_COUNT) % PLAYER_CTRL_MODE_COUNT;
  bool nowOff = (g_player_mode[idx] == PLAYER_CTRL_OFF);

  if (wasOff && !nowOff)
  {
    // Enabling: force every lower-numbered player on too, so buses stay
    // contiguous from player 1. Turning a player OFF only affects that
    // player's own slot — it does NOT cascade to higher-numbered players,
    // so e.g. player 3 OFF with player 4 still ON is a valid gap.
    for (int i = 0; i < idx; i++)
      if (g_player_mode[i] == PLAYER_CTRL_OFF)
        g_player_mode[i] = PLAYER_CTRL_WIIMOTE_GC;
  }

  int count = 1;
  for (int i = 1; i < 4; i++)
    if (g_player_mode[i] != PLAYER_CTRL_OFF) count = i + 1;
  g_player_count = count;
}

static const char* ctrl_player_mode_label(int mode)
{
  switch (mode) {
    case PLAYER_CTRL_WIICLASSIC_GC: return "WIICLASSIC/GAMECUBE";
    case PLAYER_CTRL_OFF:           return "OFF";
    default:                        return "WIIMOTE/GAMECUBE";
  }
}

bool displayControlsMenu()
{
  int selectedRow = CTRL_LAUNCH;
  ctrl_sync_player_mode_from_count();

  // Debounce: don't let the A press that confirmed the options menu
  // bleed through as an instant LAUNCH here.
  while ((WPAD_ButtonsHeld(0) & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))
         || (PAD_ButtonsHeld(0) & PAD_BUTTON_A)
         || (DRC_ButtonsHeldWPAD() & WPAD_BUTTON_A))
  {
    WPAD_ScanPads();
    PAD_ScanPads();
    if (WiiDRC_Inited())
      WiiDRC_ScanPads();
    VIDEO_WaitVSync();
  }

  while (true)
  {
    printf("\033[2J\033[H");

    // --- Row 0: Launch ---
    printf("%s LAUNCH GAME      (A: Launch | B: Back)\n",
           (selectedRow == CTRL_LAUNCH) ? ">" : " ");

    // --- Row 1: Game name (display only) ---
    {
      const char *gameName = strrchr(selectedFilePath, '/');
      gameName = (gameName != NULL) ? gameName + 1 : selectedFilePath;
      printf("    %.60s\n", gameName);
    }

    printf("\n    -- CONTROLS --\n\n");

    // --- Rows 2-5: Players ---
    for (int p = 0; p < 4; p++)
    {
      int row = CTRL_PLAYER1 + p;
      const char *label = ctrl_player_mode_label(g_player_mode[p]);
      printf("%s PLAYER %d        : [< %-20s >]\n",
             (selectedRow == row) ? ">" : " ", p + 1, label);
    }

    // --- Row 6: Controller ---
    printf("%s CONTROLLER      : ", (selectedRow == CTRL_TYPE) ? ">" : " ");
    switch (g_controller_type) {
      case 0: printf("[< STANDARD          >]"); break;
      case 1: printf("[< LIGHT GUN         >]"); break;
      case 2: printf("[< MARACAS           >]"); break;
      case 3: printf("[< KEYBOARD          >]"); break;
      case 4: printf("[< FISHING ROD       >]"); break;
    }
    switch (g_controller_type) {
      case 1: printf(" (Needs Sensor Bar + IR)"); break;
      case 2: printf(" (2 Wiimotes per player)"); break;
      case 3: printf(" (USB keyboard or D-pad)"); break;
      case 4: printf(" (Motion controls)");        break;
      default: break;
    }
    printf("\n");

    // --- Row 7: Wii U GamePad status (display only) ---
    printf("    WII U GAMEPAD    : %s\n",
           WiiDRC_Inited() ? "[DETECTED]     (drives Player 1)" : "[NOT DETECTED]");

    WPAD_ScanPads();
    PAD_ScanPads();
    u32 wmPressed = WPAD_ButtonsDown(0);
    u32 pressed = wmPressed | CLASSIC_ToWPAD(wmPressed) | DRC_ButtonsDownWPAD();

    // GameCube controller (Player 1) — same mapping convention as the
    // other menus (see displayOptionsMenu).
    u16 gcPressed = PAD_ButtonsDown(0);
    if (gcPressed & PAD_BUTTON_UP)    pressed |= WPAD_BUTTON_UP;
    if (gcPressed & PAD_BUTTON_DOWN)  pressed |= WPAD_BUTTON_DOWN;
    if (gcPressed & PAD_BUTTON_LEFT)  pressed |= WPAD_BUTTON_LEFT;
    if (gcPressed & PAD_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    if (gcPressed & PAD_BUTTON_A)     pressed |= WPAD_BUTTON_A;
    if (gcPressed & PAD_BUTTON_B)     pressed |= WPAD_BUTTON_B;

    if (pressed & WPAD_BUTTON_UP)
    {
      do {
        selectedRow = (selectedRow > 0) ? selectedRow - 1 : CTRL_ROW_COUNT - 1;
      } while (ctrl_row_is_display(selectedRow));
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      do {
        selectedRow = (selectedRow < CTRL_ROW_COUNT - 1) ? selectedRow + 1 : 0;
      } while (ctrl_row_is_display(selectedRow));
    }
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      switch (selectedRow) {
        case CTRL_PLAYER1: ctrl_cycle_player_mode(0, -1); break;
        case CTRL_PLAYER2: ctrl_cycle_player_mode(1, -1); break;
        case CTRL_PLAYER3: ctrl_cycle_player_mode(2, -1); break;
        case CTRL_PLAYER4: ctrl_cycle_player_mode(3, -1); break;
        case CTRL_TYPE:    g_controller_type = (g_controller_type + kControllerTypeCount - 1) % kControllerTypeCount; break;
        default: break;
      }
    }
    else if (pressed & WPAD_BUTTON_RIGHT)
    {
      switch (selectedRow) {
        case CTRL_PLAYER1: ctrl_cycle_player_mode(0, 1); break;
        case CTRL_PLAYER2: ctrl_cycle_player_mode(1, 1); break;
        case CTRL_PLAYER3: ctrl_cycle_player_mode(2, 1); break;
        case CTRL_PLAYER4: ctrl_cycle_player_mode(3, 1); break;
        case CTRL_TYPE:    g_controller_type = (g_controller_type + 1) % kControllerTypeCount; break;
        default: break;
      }
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      if (selectedRow == CTRL_LAUNCH)
        return true;
    }
    else if (pressed & WPAD_BUTTON_B)
    {
      return false;
    }

    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }
}

// ============================================================================
// FILE BROWSER MENU
// ============================================================================

int displayMenuAndSelectFile()
{
  int selectedIndex = 0;
  currentPage = 0;

  while (true)
  {
    printf("\033[2J\033[H");
    printf("\nNullDC4Wii - Alpha 0.51   ");
    printf("Current directory: %s\n", currentPath);

    printf("Select a game file: (GDI/CDI/BIN/CUE works)\n\n");
    

    int totalPages = (fileCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex = startIndex + ITEMS_PER_PAGE;
    if (endIndex > fileCount) endIndex = fileCount;

    for (int i = startIndex; i < endIndex; i++)
      printf(i == selectedIndex ? "> %s\n" : "  %s\n", fileList[i].name);

    printf("\n--- Page %02d/%02d ---\n\n", currentPage + 1, totalPages);
    printf("If you are a develloper in C/C++, please check Github !!\n");
    printf("https://github.com/BenoitAdam94/nullDC4Wii\n");
    printf("Contact : xalegamingchannel@gmail.com\n");
    printf("HELP ME ON THE COMPATIBILITY LIST !!\n");
    printf("Compatibility WIKI : https://wiibrew.org/wiki/NullDC4Wii/Compatibility\n\n");
    printf("Storage: %s", (g_storage_source == STORAGE_SD)
      ? "[SD CARD]  (2: Switch to USB)"
      : "[USB]      (2: Switch to SD)");
    printf("\n");
    printf("A: Select | B: Back | 1: BIOS | (-) + (+): Exit\n");
    printf("INGAME: Press (-) and (+) simultaneously to Exit\n");

    WPAD_ScanPads();
    PAD_ScanPads();
    u32 wmPressed = WPAD_ButtonsDown(0);
    u32 pressed = wmPressed | CLASSIC_ToWPAD(wmPressed) | DRC_ButtonsDownWPAD();

    // GameCube controller (Player 1) — same mapping convention as in-game
    // input (see drkMapleDevices.cpp): Y=button1, X=button2.
    u16 gcPressed = PAD_ButtonsDown(0);
    if (gcPressed & PAD_BUTTON_UP)    pressed |= WPAD_BUTTON_UP;
    if (gcPressed & PAD_BUTTON_DOWN)  pressed |= WPAD_BUTTON_DOWN;
    if (gcPressed & PAD_BUTTON_LEFT)  pressed |= WPAD_BUTTON_LEFT;
    if (gcPressed & PAD_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    if (gcPressed & PAD_BUTTON_A)     pressed |= WPAD_BUTTON_A;
    if (gcPressed & PAD_BUTTON_B)     pressed |= WPAD_BUTTON_B;
    if (gcPressed & PAD_BUTTON_Y)     pressed |= WPAD_BUTTON_1;
    if (gcPressed & PAD_BUTTON_X)     pressed |= WPAD_BUTTON_2;

    if (pressed & WPAD_BUTTON_1)
      return -2; // Boot to BIOS

    if (pressed & WPAD_BUTTON_2)
    {
      printf("\033[2J\033[H");
      if (g_storage_source == STORAGE_SD)
      {
        printf("Switching to USB...\n");
        VIDEO_SetNextFramebuffer(xfb[fb]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        fb ^= 1;
        console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                     rmode->fbWidth * VI_DISPLAY_PIX_SZ);

        if (switchToUSB())
          printf("USB mounted! Loading usb:/dreamcast/ ...\n");
        else
        {
          printf("ERROR: Could not mount USB device.\n");
          usleep(2000000);
        }
      }
      else
      {
        switchToSD();
        printf("Switched back to SD card.\n");
        usleep(500000);
      }
      listFilesInDirectory(currentPath);
      selectedIndex = 0;
      currentPage = 0;
      continue;
    }

    if (pressed & WPAD_BUTTON_UP)
    {
      if (selectedIndex > 0)
      {
        selectedIndex--;
        if (selectedIndex < startIndex) currentPage--;
      }
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      if (selectedIndex < fileCount - 1)
      {
        selectedIndex++;
        if (selectedIndex >= endIndex) currentPage++;
      }
    }
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      if (currentPage > 0)
      {
        currentPage--;
        selectedIndex = currentPage * ITEMS_PER_PAGE;
      }
    }
    else if (pressed & WPAD_BUTTON_RIGHT)
    {
      if (currentPage < totalPages - 1)
      {
        currentPage++;
        selectedIndex = currentPage * ITEMS_PER_PAGE;
      }
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      if (strlen(fileList[selectedIndex].fullPath) == 0 &&
          !fileList[selectedIndex].isDirectory)
      {
        // Placeholder — nothing to do
      }
      else if (fileList[selectedIndex].isDirectory)
      {
        strcpy(currentPath, fileList[selectedIndex].fullPath);
        listFilesInDirectory(currentPath);
        selectedIndex = 0;
        currentPage = 0;
      }
      else
      {
        return selectedIndex;
      }
    }
    else if (pressed & WPAD_BUTTON_B)
    {
      const char *rootPath = (g_storage_source == STORAGE_SD)
        ? "sd:/discs/" : "usb:/dreamcast/";
      if (strcmp(currentPath, rootPath) != 0)
      {
        char *lastSlash = strrchr(currentPath, '/');
        if (lastSlash != NULL && lastSlash != currentPath)
          *lastSlash = '\0';
        listFilesInDirectory(currentPath);
        selectedIndex = 0;
        currentPage = 0;
      }
    }
    else if (((WPAD_ButtonsHeld(0) | CLASSIC_ToWPAD(WPAD_ButtonsHeld(0))
               | DRC_ButtonsHeldWPAD()) & WPAD_BUTTON_PLUS) &&
             ((WPAD_ButtonsHeld(0) | CLASSIC_ToWPAD(WPAD_ButtonsHeld(0))
               | DRC_ButtonsHeldWPAD()) & WPAD_BUTTON_MINUS))
    {
      return -1; // Exit
    }

    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }

  return selectedIndex;
}

void SetApplicationPath(wchar *path);

// ============================================================================
// BIOS BOOT HELPER
// ============================================================================

void handleBIOSBoot()
{
  strcpy(selectedFilePath, "");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, wchar *argv[])
{
  VIDEO_Init();

  ASND_Init();
  wii_audio_init();

  PAD_Init();
  WPAD_Init();

  // Wii U GamePad (only succeeds on Wii U in vWii mode; harmless on real Wii)
  WiiDRC_Init();

  rmode = VIDEO_GetPreferredMode(NULL);

  xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  fb = 0;

  console_init(xfb[0], 20, 20, rmode->fbWidth, rmode->xfbHeight,
               rmode->fbWidth * VI_DISPLAY_PIX_SZ);

  VIDEO_Configure(rmode);
  VIDEO_SetNextFramebuffer(xfb[fb]);
  VIDEO_SetBlack(false);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // ---------------------------------------------------------------------------
  // ARM7DI core self-test — runs before anything else so a broken AICA ARM
  // core is caught (and visible on screen) before we ever boot a game.
  // ---------------------------------------------------------------------------
  // {
  //   int arm7_failures = arm_RunSelfTests();
  //   if (arm7_failures != 0)
  //   {
  //     printf("\nARM7DI SELF-TEST FAILED (%d). Press HOME to exit.\n", arm7_failures);
  //     while (true)
  //     {
  //       WPAD_ScanPads();
  //       if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
  //         break;
  //       VIDEO_WaitVSync();
  //     }
  //     return 1;
  //   }
  // }

  // ---------------------------------------------------------------------------
  // Mount SD card
  // ---------------------------------------------------------------------------
  if (fatInitDefault())
  {
    printf("SD card mounted!\n");
  }
  else
  {
    printf("WARNING: Could not mount SD card.\n");
    printf("You can press 2 in the file browser to switch to USB.\n");
    usleep(2000000);
  }

  // ---------------------------------------------------------------------------
  // Load game presets  (optional — missing file is silently ignored)
  // ---------------------------------------------------------------------------
  game_presets_load("sd:/discs/game_presets.cfg");

  void SetApplicationPath(const wchar *path);

  // ---------------------------------------------------------------------------
  // Check for required BIOS files before showing the file browser.
  // ---------------------------------------------------------------------------
  checkBiosFiles();

  listFilesInDirectory(currentPath);

  // ---------------------------------------------------------------------------
  // Main menu loop
  // ---------------------------------------------------------------------------
  {
    bool launchGame = false;

    while (!launchGame)
    {
      int selectedIndex = displayMenuAndSelectFile();

      if (selectedIndex == -1)
      {
        printf("Exiting...\n");
        return 0;
      }
      else if (selectedIndex == -2)
      {
        // Boot to BIOS — clear preset name, no file selected
        handleBIOSBoot();
        g_matched_preset_name[0] = '\0';
        launchGame = true;
      }
      else if (selectedIndex >= 0)
      {
        strcpy(selectedFilePath, fileList[selectedIndex].fullPath);

        // Apply game preset BEFORE showing the options menu so the user
        // sees the recommended values and can still tweak them manually.
        game_presets_apply(selectedFilePath);

        if (!displayOptionsMenu())
          continue; // B pressed — back to file list

        launchGame = displayControlsMenu();
        if (!launchGame)
          continue; // B pressed — back to options menu
      }
    }

    // Print launch summary
    printf("\x1b[2J\x1b[H");
    if (strlen(selectedFilePath) > 0)
      printf("Selected file  : %s\n", selectedFilePath);
    else
      printf("Booting to BIOS (no disc)...\n");

    if (g_matched_preset_name[0] != '\0')
      printf("Game preset    : [%s] applied\n", g_matched_preset_name);
    else
      printf("Game preset    : none\n");

    printf("Graphics       : ");
    switch(g_graphism_preset) {
      case 0: printf("LOW\n");    break;
      case 1: printf("NORMAL\n"); break;
      case 2: printf("HIGH\n");   break;
      case 3: printf("EXTRA\n");  break;
    }
    printf("Accuracy       : ");
    switch(g_accuracy_preset) {
      case 0: printf("FAST\n");     break;
      case 1: printf("BALANCED\n"); break;
      case 2: printf("ACCURATE\n"); break;
    }
    printf("Ratio          : ");
    switch(g_ratio_preset) {
      case 0: printf("ORIGINAL\n");   break;
      case 1: printf("FULLSCREEN\n"); break;
    }
    printf("Advanced Alpha : %s\n", g_advanced_alpha_preset ? "YES (DEBUG)" : "NO");
    printf("Decal Alpha Fix: %s\n", g_decal_alpha_preset ? "YES (DEFAULT)" : "NO");
    printf("2D Framebuffer : %s\n", g_framebuffer_2d ? "YES" : "NO");
    printf("PPZ_WRITE      : %s\n", g_ppz_write_preset ? "YES" : "NO");
    printf("FMV Format     : ");
    switch(g_fmv_format_preset) {
      case 0: printf("CMPR (DXT1)\n"); break;
      case 1: printf("RGBA8\n");       break;
      case 2: printf("RGB565\n");      break;
    }
    printf("Frameskipping  : ");
    switch(g_frameskip_preset) {
      case 0: printf("0\n");    break;
      case 1: printf("1\n");    break;
      case 2: printf("2\n");    break;
      case 3: printf("AUTO\n"); break;
    }
    printf("Texture Cache  : ");
    switch(g_texture_cache_preset) {
      case 0: printf("VERY FAST\n"); break;
      case 1: printf("FAST\n");      break;
      case 2: printf("NORMAL (DEFAULT)\n");    break;
      case 3: printf("QUALITY\n");   break;
    }
    printf("4BPP Mode      : ");
    switch(g_4bpp_preset) {
      case 0: printf("I4 STUB\n");        break;
      case 1: printf("4BPP OPTIMIZED\n"); break;
      case 2: printf("CI4 (FAST)\n");     break;
      case 3: printf("CI4 (NORMAL)\n");   break;
      case 4: printf("RGB565\n");         break;
    }
    printf("8BPP Mode      : ");
    switch(g_8bpp_preset) {
      case 0: printf("I8 STUB\n");        break;
      case 1: printf("8BPP OPTIMIZED\n"); break;
      case 2: printf("CI8 (FAST)\n");     break;
      case 3: printf("CI8 (NORMAL)\n");   break;
      case 4: printf("RGB565\n");         break;
    }
    printf("Jojo Fix       : %s\n", g_jojo_fix_preset ? "YES" : "NO");
    printf("Speed Limiter  : %s\n", g_speed_limiter_preset ? "ON (cap 100%)" : "OFF (uncapped)");
    printf("Vertex Color Fix: %s\n", g_vertex_color_fix_preset ? "ON" : "OFF");
    printf("Blend Mode     : %s\n", g_blend_mode_preset ? "ON (CORRECT)" : "OFF (LEGACY)");
    printf("RGB565 Opq Alpha: %s\n", g_rgb565_opaque_alpha_preset ? "ON (FMT0+FMT1)" : "OFF (FMT0 ONLY)");
    printf("Blend FPS Boost: %s\n", g_blend_fps_boost_preset ? "ON (FASTER)" : "OFF (CORRECT)");
    printf("Punch Through  : %s\n", g_punch_through_preset ? "ON (CORRECT)" : "OFF (FASTER?)");
    printf("Offset Color   : %s\n", g_offset_color_preset ? "ON (SPECULAR)" : "OFF (LEGACY)");
    printf("Trans Sort     : %s\n", g_trans_sort_preset ? "ON (SORTED)" : "OFF (LEGACY)");
    printf("Render To Tex  : %s\n", g_render_to_texture_preset ? "ON (CORRECT)" : "OFF (LEGACY)");
    printf("Split Screen   : %s\n", g_split_screen_preset ? "ON (2P VIEWPORTS)" : "OFF (LEGACY)");
    printf("Mipmaps        : ");
    switch (g_mipmap_preset) {
      case 0: printf("OFF (FASTEST)\n");    break;
      case 1: printf("FAST\n");             break;
      case 2: printf("TRILINEAR (SLOW)\n"); break;
    }
    printf("Fixed Depth    : ");
    switch (g_fixed_depth_preset) {
      case 0: printf("OFF (DYNAMIC)\n"); break;
      case 1: printf("WIDE\n");          break;
      case 2: printf("TIGHT\n");         break;
    }
    printf("Depth Clip     : ");
    switch (g_depth_clip_preset) {
      case 0: printf("OFF (LEGACY)\n");      break;
      case 1: printf("NEAR MARGIN\n");       break;
      case 2: printf("NO CLIP (DOLPHIN)\n"); break;
    }
    printf("Async Render   : %s\n", g_async_render_preset ? "ON (FASTER?)" : "OFF (LEGACY)");
    printf("TMEM Cache     : %s\n", g_tmem_cache_preset ? "ON (FASTER?)" : "OFF (LEGACY)");
    printf("BG Polygon     : %s\n", g_bg_poly_preset ? "ON (CORRECT)" : "OFF (FASTER)");
    printf("X Scaler       : %s\n", g_x_scaler_preset ? "ON (DEFAULT)" : "OFF (LEGACY)");
    if (g_canvas_width_preset <= 0)
      printf("Canvas Width   : OFF (640, LEGACY)\n");
    else
      printf("Canvas Width   : %d\n", g_canvas_width_preset);
    printf("Players        : %d\n", g_player_count);
    printf("Controller     : %s\n",
      (g_controller_type >= 0 && g_controller_type < kControllerTypeCount)
        ? kControllerTypeNames[g_controller_type] : "UNKNOWN");

    if (g_controller_type == 1)
      printf("               (Light gun: make sure Sensor Bar is on!)\n");
    if (g_controller_type == 2 && g_player_count > 1)
      printf("               (Maracas %dP: needs %d Wiimotes!)\n", g_player_count, g_player_count * 2);
    if (g_controller_type == 3)
      printf("               (Keyboard: connect USB keyboard for best experience)\n");
  }

  int rv = EmuMain(argc, argv);
  return rv;
}

// ============================================================================
// PLATFORM CALLBACKS
// ============================================================================

int os_GetFile(char *szFileName, char *szParse, u32 flags)
{
  if (strlen(selectedFilePath) > 0)
  {
    size_t len = strlen(selectedFilePath);
    if (len > 4 && (strcmp(selectedFilePath + len - 4, ".elf") == 0 ||
                    strcmp(selectedFilePath + len - 4, ".ELF") == 0))
      return false;

    strcpy(szFileName, selectedFilePath);
    return true;
  }
  return false;
}

double os_GetSeconds()
{
  // NOTE: do NOT use clock()/CLOCKS_PER_SEC here — on the Wii's newlib/libogc
  // clock() has no real backing timer and returns a (near-)constant.
  //
  // We read the PowerPC time base via gettime() (64-bit ticks, advances at
  // PPC_TIMER_CLOCK = bus/4 = 60.75 MHz on Wii). Two pitfalls handled here:
  //   * The absolute TB is enormous (Dolphin starts it ~5e16, i.e. decades of
  //     ticks). Converting that whole value to a double loses the ~15 ms
  //     per-frame increment in the mantissa, so seconds looked frozen. We
  //     therefore anchor at the first call and only ever convert the DELTA.
  //   * ticks_to_microsecs()/1e6 on the raw value also inflated the magnitude;
  //     delta math keeps the numbers small and precise.
  static u64 t0 = 0;
  u64 now = gettime();
  if (t0 == 0)
    t0 = now;
  return (double)ticks_to_microsecs(now - t0) / 1000000.0;
}

int os_msgbox(const wchar *text, unsigned int type)
{
  printf("OS_MSGBOX: %s\n", text);
  return 0;
}
