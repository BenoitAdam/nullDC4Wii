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
#include "sample_mp3.h" // Was for testing playing an MP3 on menu. 
#include "wii/wii_audio.h"
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include "stdclass.h"   // for GetEmuPath() for bios check

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

int g_frameskip_preset = 0;

extern "C" {
  int get_frameskip_preset() { return g_frameskip_preset; }
}

int g_4bpp_preset = 2;
// 0=I4 Stub, 1=I4 grey fast, 2=CI4 fast, 3=CI4 normal, 4=RGB565

extern "C" {
  int get_4bpp_preset() { return g_4bpp_preset; }
}

int g_8bpp_preset = 2;
// 0=I8 Stub, 1=I8 grey, 2=CI8 fast, 3=CI8 normal, 4=RGB565

extern "C" {
  int get_8bpp_preset() { return g_8bpp_preset; }
}

int g_texture_cache_preset = 0;

extern "C" {
  int get_texture_cache_preset() { return g_texture_cache_preset; }
}

int g_ppz_write_preset = 1; // 1 for Test Drive / Demolition Racer

extern "C" {
  int get_ppz_write_preset() { return g_ppz_write_preset; }
}

int g_framebuffer_2d = 0; // 1 to activate 2D Framebuffer

extern "C" {
  int get_framebuffer_2d() { return g_framebuffer_2d; }
}

int g_gx_accurate = 0; // 1 to activate GX_ACCURATE

extern "C" {
  int get_gx_accurate() { return g_gx_accurate; }
}

int g_fmv_format_preset = 2; // 0=CMPR (DXT1), 1=RGBA8, 2=RGB565

extern "C" {
  int get_fmv_format_preset() { return g_fmv_format_preset; }
}

int g_player_count = 2;

extern "C" {
  int  get_player_count()      { return g_player_count; }
  void set_player_count(int n) { g_player_count = (n >= 1 && n <= 2) ? n : 1; }
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

FileEntry fileList[256];
int fileCount = 0;
char selectedFilePath[512] = "";
char currentPath[512] = "sd:/discs/";
const int ITEMS_PER_PAGE = 10;
int currentPage = 0;
int mp3mainmenu = 0;

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

  if ((dir = opendir(dirPath)) != NULL)
  {
    fileCount = 0;

    while ((entry = readdir(dir)) != NULL && fileCount < 256)
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
      if (WPAD_ButtonsDown(0) != 0)
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
  int selectedOption = g_accuracy_preset;

  while (true)
  {
    printf("\033[2J\033[H");
    printf("                  INFO - NullDC4Wii               \n");
    printf(" \n");
    printf("Information about preset :\n\n");

    printf("Calculations Accuracy (can lead to bugs if not AVERAGE):\n");
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

    printf("Current setting:\n");
    switch(g_accuracy_preset) {
      case 0: printf("FAST - ");     break;
      case 1: printf("BALANCED - "); break;
      case 2: printf("ACCURATE - "); break;
    }
    switch(g_graphism_preset) {
      case 0: printf("LOW - ");    break;
      case 1: printf("NORMAL - "); break;
      case 2: printf("HIGH - ");   break;
      case 3: printf("EXTRA - ");  break;
    }
    switch(g_ratio_preset) {
      case 0: printf("ORIGINAL\n");   break;
      case 1: printf("FULLSCREEN\n"); break;
    }

    printf("\nUP/DOWN: Select option | A: Confirm | B: Back\n");
    printf("Note: Change settings if you experience issues or need more speed.\n");

    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);

    if (pressed & WPAD_BUTTON_UP)
    {
      if (selectedOption > 0) selectedOption--;
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      if (selectedOption < 2) selectedOption++;
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      g_accuracy_preset = selectedOption;
      return;
    }
    else if (pressed & WPAD_BUTTON_B)
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
#define OPT_GRAPHICS    4
#define OPT_ACCURACY    5
#define OPT_RATIO       6
#define OPT_PPZ_WRITE   7
#define OPT_ADV_ALPHA   8
#define OPT_FRAMEBUFFER_2D 9
#define OPT_GX_ACCURATE 10
#define OPT_FMV_FORMAT  11
#define OPT_FRAMESKIP   12
#define OPT_TEX_CACHE   13
#define OPT_4BPP        14
#define OPT_8BPP        15
#define OPT_PLAYERS     16
#define OPT_CTRL_TYPE   17
#define OPT_MORE_INFO   18
#define OPT_ROW_COUNT   19

// Rows that are display-only (not selectable by cursor)
static bool opt_row_is_display(int row)
{
  return (row == 1 || row == 2 || row == 3);
}

bool displayOptionsMenu()
{
  int selectedRow = OPT_LAUNCH;

  while (true)
  {
    printf("\033[2J\033[H");

    printf("NullDC4Wii - Alpha 0.32   OPTIONS\n");
    printf("===================================\n\n");

    // --- Row 0: Launch ---
    printf("%s LAUNCH GAME\n", (selectedRow == OPT_LAUNCH) ? ">" : " ");

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

    // --- Row 3: Blank separator ---
    printf("\n");

    // --- Row 4: Graphics ---
    printf("%s GRAPHICS      : ", (selectedRow == OPT_GRAPHICS) ? ">" : " ");
    switch (g_graphism_preset) {
      case 0: printf("[< LOW        >]"); break;
      case 1: printf("[< NORMAL     >]"); break;
      case 2: printf("[< HIGH       >]"); break;
      case 3: printf("[< EXTRA      >]"); break;
    }
    printf(" (Tip: 2D Games should use LOW)");
    printf("\n");

    // --- Row 5: Accuracy ---
    printf("%s ACCURACY      : ", (selectedRow == OPT_ACCURACY) ? ">" : " ");
    switch (g_accuracy_preset) {
      case 0: printf("[< FAST       >]"); break;
      case 1: printf("[< BALANCED   >]"); break;
      case 2: printf("[< ACCURATE   >]"); break;
    }
    printf(" (Tip: not much difference)");
    printf("\n");

    // --- Row 6: Ratio ---
    printf("%s RATIO         : ", (selectedRow == OPT_RATIO) ? ">" : " ");
    switch (g_ratio_preset) {
      case 0: printf("[< ORIGINAL   >]"); break;
      case 1: printf("[< FULLSCREEN >]"); break;
    }
    printf("\n\n");

    // --- Row 7: PPZ Write ---
    printf("%s PPZ_WRITE     : ", (selectedRow == OPT_PPZ_WRITE) ? ">" : " ");
    switch (g_ppz_write_preset) {
      case 0: printf("[< NO  >]"); break;
      case 1: printf("[< YES >]"); break;
    }
    printf("\n");

    // --- Row 8: Advanced Alpha ---
    printf("%s ADVANCED ALPHA: ", (selectedRow == OPT_ADV_ALPHA) ? ">" : " ");
    switch (g_advanced_alpha_preset) {
      case 0: printf("[< NO            >]"); break;
      case 1: printf("[< YES (DEFAULT) >]"); break;
    }
    printf("\n");

    // --- Row 9: 2D Framebuffer ---
    printf("%s 2D FRAMEBUFFER: ", (selectedRow == OPT_FRAMEBUFFER_2D) ? ">" : " ");
    switch (g_framebuffer_2d) {
      case 0: printf("[< NO  >]"); break;
      case 1: printf("[< YES >]"); break;
    }
    printf(" (Tip: try for 2D games)");
    printf("\n");

    // --- Row 10: GX Accurate ---
    printf("%s GX_ACCURATE   : ", (selectedRow == OPT_GX_ACCURATE) ? ">" : " ");
    switch (g_gx_accurate) {
      case 0: printf("[< NO  >]"); break;
      case 1: printf("[< YES >]"); break;
    }
    printf("\n");

    // --- Row 11: FMV Format ---
    printf("%s FMV FORMAT    : ", (selectedRow == OPT_FMV_FORMAT) ? ">" : " ");
    switch (g_fmv_format_preset) {
      case 0: printf("[< CMPR (DXT1)   >]"); break;
      case 1: printf("[< RGBA8         >]"); break;
      case 2: printf("[< RGB565        >]"); break;
    }
    printf("\n");

    // --- Row 8: Frameskipping ---
    printf("%s FRAMESKIPPING : ", (selectedRow == OPT_FRAMESKIP) ? ">" : " ");
    switch (g_frameskip_preset) {
      case 0: printf("[< 0 (DEFAULT)   >]"); break;
      case 1: printf("[< 1             >]"); break;
      case 2: printf("[< 2             >]"); break;
      case 3: printf("[< AUTO          >]"); break;
    }
    printf("\n");

    // --- Row 9: Texture Cache ---
    printf("%s TEXTURE CACHE : ", (selectedRow == OPT_TEX_CACHE) ? ">" : " ");
    switch (g_texture_cache_preset) {
      case 0: printf("[< VERY FAST     >]"); break;
      case 1: printf("[< FAST          >]"); break;
      case 2: printf("[< NORMAL        >]"); break;
      case 3: printf("[< QUALITY       >]"); break;
    }
    printf("\n");

    // --- Row 10: 4BPP ---
    printf("%s 4BPP MODE     : ", (selectedRow == OPT_4BPP) ? ">" : " ");
    switch (g_4bpp_preset) {
      case 0: printf("[< I4 STUB           >]"); break;
      case 1: printf("[< I4 GREY (FAST)    >]"); break;
      case 2: printf("[< CI4 (FAST)        >]"); break;
      case 3: printf("[< CI4 (NORMAL)      >]"); break;
      case 4: printf("[< RGB565 (ACCURATE) >]"); break;
    }
    printf("\n");

    // --- Row 11: 8BPP ---
    printf("%s 8BPP MODE     : ", (selectedRow == OPT_8BPP) ? ">" : " ");
    switch (g_8bpp_preset) {
      case 0: printf("[< I8 STUB           >]"); break;
      case 1: printf("[< I8 GREY (FAST)    >]"); break;
      case 2: printf("[< CI8 (FAST)        >]"); break;
      case 3: printf("[< CI8 (NORMAL)      >]"); break;
      case 4: printf("[< RGB565 (ACCURATE) >]"); break;
    }
    printf("\n\n");

    // --- Row 12: Players ---
    printf("%s PLAYERS       : ", (selectedRow == OPT_PLAYERS) ? ">" : " ");
    printf(g_player_count == 1 ? "[< 1 PLAYER  >]" : "[< 2 PLAYERS >]");
    printf("\n");

    // --- Row 13: Controller ---
    printf("%s CONTROLLER    : ", (selectedRow == OPT_CTRL_TYPE) ? ">" : " ");
    switch (g_controller_type) {
      case 0: printf("[< STANDARD              >]"); break;
      case 1: printf("[< LIGHT GUN             >]"); break;
      case 2: printf("[< MARACAS               >]"); break;
      case 3: printf("[< KEYBOARD              >]"); break;
      case 4: printf("[< FISHING ROD           >]"); break;
    }
    switch (g_controller_type) {
      case 1: printf(" (Needs Sensor Bar + IR)"); break;
      case 2: printf(" (2 Wiimotes per player)"); break;
      case 3: printf(" (USB keyboard or D-pad)"); break;
      case 4: printf(" (Motion controls)");        break;
      default: break;
    }
    printf("\n");

    // --- Row 14: More Info ---
    printf("%s MORE INFO      (press A to open)\n", (selectedRow == OPT_MORE_INFO) ? ">" : " ");

    printf("\n");
    printf("UP/DOWN: Navigate | LEFT/RIGHT: Change value\n");
    printf("A: Launch / Open | B: Back to file list\n");

    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);

    if (pressed & WPAD_BUTTON_UP)
    {
      do {
        selectedRow = (selectedRow > 0) ? selectedRow - 1 : OPT_ROW_COUNT - 1;
      } while (opt_row_is_display(selectedRow));
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      do {
        selectedRow = (selectedRow < OPT_ROW_COUNT - 1) ? selectedRow + 1 : 0;
      } while (opt_row_is_display(selectedRow));
    }
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      switch (selectedRow) {
        case OPT_GRAPHICS:  g_graphism_preset      = (g_graphism_preset      + 3) % 4; break;
        case OPT_ACCURACY:  g_accuracy_preset       = (g_accuracy_preset       + 2) % 3; break;
        case OPT_RATIO:     g_ratio_preset          = (g_ratio_preset          + 1) % 2; break;
        case OPT_PPZ_WRITE: g_ppz_write_preset      = (g_ppz_write_preset      + 1) % 2; break;
        case OPT_ADV_ALPHA: g_advanced_alpha_preset = (g_advanced_alpha_preset + 1) % 2; break;
        case OPT_FRAMEBUFFER_2D: g_framebuffer_2d   = (g_framebuffer_2d        + 1) % 2; break;
        case OPT_GX_ACCURATE: g_gx_accurate         = (g_gx_accurate           + 1) % 2; break;
        case OPT_FMV_FORMAT: g_fmv_format_preset    = (g_fmv_format_preset     + 2) % 3; break;
        case OPT_FRAMESKIP: g_frameskip_preset      = (g_frameskip_preset      + 3) % 4; break;
        case OPT_TEX_CACHE: g_texture_cache_preset  = (g_texture_cache_preset  + 3) % 4; break;
        case OPT_4BPP:      g_4bpp_preset           = (g_4bpp_preset           + 4) % 5; break;
        case OPT_8BPP:      g_8bpp_preset           = (g_8bpp_preset           + 4) % 5; break;
        case OPT_PLAYERS:   g_player_count          = (g_player_count == 1) ? 2 : 1; break;
        case OPT_CTRL_TYPE: g_controller_type       = (g_controller_type + kControllerTypeCount - 1) % kControllerTypeCount; break;
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
        case OPT_FRAMEBUFFER_2D: g_framebuffer_2d   = (g_framebuffer_2d        + 1) % 2; break;
        case OPT_GX_ACCURATE: g_gx_accurate         = (g_gx_accurate           + 1) % 2; break;
        case OPT_FMV_FORMAT: g_fmv_format_preset    = (g_fmv_format_preset     + 1) % 3; break;
        case OPT_FRAMESKIP: g_frameskip_preset      = (g_frameskip_preset      + 1) % 4; break;
        case OPT_TEX_CACHE: g_texture_cache_preset  = (g_texture_cache_preset  + 1) % 4; break;
        case OPT_4BPP:      g_4bpp_preset           = (g_4bpp_preset           + 1) % 5; break;
        case OPT_8BPP:      g_8bpp_preset           = (g_8bpp_preset           + 1) % 5; break;
        case OPT_PLAYERS:   g_player_count          = (g_player_count == 1) ? 2 : 1; break;
        case OPT_CTRL_TYPE: g_controller_type       = (g_controller_type + 1) % kControllerTypeCount; break;
        default: break;
      }
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      if (selectedRow == OPT_LAUNCH)
        return true;
      else if (selectedRow == OPT_MORE_INFO)
        displayAccuracyMenu();
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
    printf("\nNullDC4Wii - Alpha 0.32   ");
    printf("Current directory: %s\n", currentPath);

    printf("(-) GRAPHICS: ");
    switch(g_graphism_preset) {
      case 0: printf("LOW   "); break;
      case 1: printf("NORMAL"); break;
      case 2: printf("HIGH  "); break;
      case 3: printf("EXTRA "); break;
    }
    printf("  (HOME) ACCURACY: ");
    switch(g_accuracy_preset) {
      case 0: printf("FAST    "); break;
      case 1: printf("BALANCED"); break;
      case 2: printf("ACCURATE"); break;
    }
    printf("  (+) RATIO: ");
    switch(g_ratio_preset) {
      case 0: printf("ORIGINAL  "); break;
      case 1: printf("FULLSCREEN"); break;
    }
    printf("\nSelect a game file: (GDI/CDI/BIN/CUE works)\n\n");

    int totalPages = (fileCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex = startIndex + ITEMS_PER_PAGE;
    if (endIndex > fileCount) endIndex = fileCount;

    for (int i = startIndex; i < endIndex; i++)
      printf(i == selectedIndex ? "> %s\n" : "  %s\n", fileList[i].name);

    printf("\n--- Page %02d/%02d ---\n\n", currentPage + 1, totalPages);
    printf("HELP ME BUILD THIS PROJECT !! ANY HELP IS WELCOME !!\n");
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
    u32 pressed = WPAD_ButtonsDown(0);

    if (pressed & WPAD_BUTTON_MINUS)
      g_graphism_preset = (g_graphism_preset + 1) % 4;

    if (pressed & WPAD_BUTTON_PLUS)
      g_ratio_preset = (g_ratio_preset + 1) % 2;

    if (pressed & WPAD_BUTTON_HOME)
      g_accuracy_preset = (g_accuracy_preset + 1) % 3;

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
    else if ((WPAD_ButtonsHeld(0) & WPAD_BUTTON_PLUS) &&
             (WPAD_ButtonsHeld(0) & WPAD_BUTTON_MINUS))
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
  if (mp3mainmenu)
    MP3Player_Init();

  PAD_Init();
  WPAD_Init();

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

  if (mp3mainmenu)
    MP3Player_PlayBuffer(sample_mp3, sample_mp3_size, NULL);

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
  game_presets_load("sd:/data/game_presets.cfg");

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

        launchGame = displayOptionsMenu();
        if (!launchGame)
          continue; // B pressed — back to file list
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
    printf("2D Framebuffer : %s\n", g_framebuffer_2d ? "YES" : "NO");
    printf("GX_ACCURATE    : %s\n", g_gx_accurate ? "YES" : "NO");
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
      case 2: printf("NORMAL\n");    break;
      case 3: printf("QUALITY\n");   break;
    }
    printf("4BPP Mode      : ");
    switch(g_4bpp_preset) {
      case 0: printf("I4 STUB\n");        break;
      case 1: printf("I4 GREY (FAST)\n"); break;
      case 2: printf("CI4 (FAST)\n");     break;
      case 3: printf("CI4 (NORMAL)\n");   break;
      case 4: printf("RGB565\n");         break;
    }
    printf("8BPP Mode      : ");
    switch(g_8bpp_preset) {
      case 0: printf("I8 STUB\n");        break;
      case 1: printf("I8 GREY (FAST)\n"); break;
      case 2: printf("CI8 (FAST)\n");     break;
      case 3: printf("CI8 (NORMAL)\n");   break;
      case 4: printf("RGB565\n");         break;
    }
    printf("Players        : %d\n", g_player_count);
    printf("Controller     : %s\n",
      (g_controller_type >= 0 && g_controller_type < kControllerTypeCount)
        ? kControllerTypeNames[g_controller_type] : "UNKNOWN");

    if (g_controller_type == 1)
      printf("               (Light gun: make sure Sensor Bar is on!)\n");
    if (g_controller_type == 2 && g_player_count == 2)
      printf("               (Maracas 2P: needs 4 Wiimotes!)\n");
    if (g_controller_type == 3)
      printf("               (Keyboard: connect USB keyboard for best experience)\n");
  }

  if (mp3mainmenu)
    MP3Player_Stop();

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
