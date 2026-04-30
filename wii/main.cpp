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
#include <gccore.h> // needed, or VScode will show errors
#include <asndlib.h>
#include <mp3player.h> // MP3 File
#include "sample_mp3.h" // MP3 File (generated header)
#include "wii/wii_audio.h"
#include <sdcard/wiisd_io.h>  // SD card I/O ops
#include <ogc/usbstorage.h>   // USB mass storage support

// ============================================================================
// GLOBAL EMULATOR PRESET
// ============================================================================
// Global variable to store user's calculations (CPU, FPU, GPU...) accuracy choice

int g_accuracy_preset = 2; // 0=Fast, 1=Balanced, 2=Accurate (default)

// Used by sh4_fpu.cpp ... (put additional files here)
extern "C" {
  int get_accuracy_preset() {
    return g_accuracy_preset;
  }
}

int g_graphism_preset = 1; // 0=Low (default), 1=Normal, 2=High, 3=Extra

// Used by gxRend
extern "C" {
  int get_graphism_preset() {
    return g_graphism_preset;
  }
}

int g_ratio_preset = 1; // 0=Original (4/3), 1=Fullscreen (defaut)

// Used by gxRend
extern "C" {
  int get_ratio_preset() {
    return g_ratio_preset;
  }
}

int g_advanced_alpha_preset = 0; // 0=no advanced alpha (Default), 1=Advanced Alpha (defaut)

// Used by gxRend
extern "C" {
  int get_advanced_alpha_preset() {
    return g_advanced_alpha_preset;
  }
}

int g_frameskip_preset = 0; // 0=No frame skip (defaut for now), 1=1 frame skip, 2=2 frame skip, 3=Auto frame skip

// Used by gxRend
extern "C" {
  int get_frameskip_preset() {
    return g_frameskip_preset;
  }
}

int g_4bpp_preset = 2;
// 0 = I4 Stub
// 1 = I4 (grey fast)
// 2 = CI4 (CI4 fast)
// 3 = CI4 (CI4 normal)
// 4 = RGB565

// Used by gxRend
extern "C" {
  int get_4bpp_preset() {
    return g_4bpp_preset;
  }
}

int g_8bpp_preset = 2;
// 0 = I8 Stub
// 1 = I8 (grey)
// 2 = CI8 (CI8 fast)
// 3 = CI8 (CI8 normal)
// 4 = RGB565

// Used by gxRend
extern "C" {
  int get_8bpp_preset() {
    return g_8bpp_preset;
  }
}

int g_texture_cache_preset = 0;
// 0 = CACHE_VERY_FAST
// 1 = CACHE_FAST
// 2 = CACHE_NORMAL
// 3 = CACHE_QUALITY

// Used by gxRend
extern "C" {
  int get_texture_cache_preset() {
    return g_texture_cache_preset;
  }
}


// ============================================================================
// DEBUG MODE
// ============================================================================

// Debug for messages that are looped
int g_debug_loop = 0; // 0= no debug 1=Debug

extern "C" {
  int get_debug_loop() {
    return g_debug_loop;
  }
}


// Debug for messages that are print once
int g_debug_message = 0; // 0= no debug 1=Debug

extern "C" {
  int get_debug_message() {
    return g_debug_message;
  }
}

// Debug for specific GDROM messages that are print once
int g_debug_gdrom = 0; // 0= no debug 1=Debug

extern "C" {
  int get_debug_gdrom() {
    return g_debug_gdrom;
  }
}

// ============================================================================

// Global variables
struct FileEntry
{
  char name[256];
  char fullPath[512];
  bool isDirectory;
};

// ============================================================================
// STORAGE SOURCE (SD / USB)
// ============================================================================
typedef enum {
  STORAGE_SD  = 0,
  STORAGE_USB = 1
} StorageSource;

StorageSource g_storage_source = STORAGE_SD; // Boot on SD by default
bool g_usb_mounted = false;                  // Track whether USB was successfully mounted

FileEntry fileList[256];
int fileCount = 0;
char selectedFilePath[512] = "";
char currentPath[512] = "sd:/discs/";
const int ITEMS_PER_PAGE = 10; // 10 for now is ok
int currentPage = 0;
int mp3mainmenu = 0; // 1 for playing MP3 in main menu

// Double-buffer globals (set up in main, used by menu loop)
static void *xfb[2];
static GXRModeObj *rmode = NULL;
static int fb = 0;

// Function to check if file is a GDI / CDI / ISO / BIN / CUE / NRG / MDS / ELF / CHD
// Currently supported: GDI (fully), maybe CDI/ISO/NRG/MDS/BIN/CUE/ELF (experimental)
bool hasValidExtension(const char *filename)
{
  size_t len = strlen(filename);
  if (len < 4)
    return false;

  const char *ext = &filename[len - 4];

  // Lowercase to make comparison not case sensible
  char extLower[5];
  for (int i = 0; i < 4; i++)
  {
    extLower[i] = tolower(ext[i]);
  }
  extLower[4] = '\0';

  // Check for valid extensions
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
// STORAGE SWITCHING (SD <-> USB)
// ============================================================================
// Attempt to mount USB and switch currentPath to usb:/discs/.
// Returns true on success, false if USB could not be mounted.
bool switchToUSB()
{
  if (!g_usb_mounted)
  {
    // Initialize the USB mass storage subsystem
    if (USBStorage_Initialize() == 0)
    {
      // Give the USB device up to 3 seconds to enumerate
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

// Switch back to SD card
void switchToSD()
{
  g_storage_source = STORAGE_SD;
  strcpy(currentPath, "sd:/discs/");
}

// Function to list files and directories in a folder
void listFilesInDirectory(const char *dirPath)
{
  DIR *dir;
  struct dirent *entry;
  struct stat statbuf;

  if ((dir = opendir(dirPath)) != NULL)
  {
    fileCount = 0;

    // Read all entries in directory
    while ((entry = readdir(dir)) != NULL && fileCount < 256)
    {
      // Ignore "." and ".." directories
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      {
        continue;
      }

      // Build full path
      char fullPath[512];
      int pathLen = snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

      // Check if path isn't trunkacted
      if ((size_t)pathLen >= sizeof(fullPath))
      {
        printf("Warning: Path too long, skipping: %s/%s\n", dirPath, entry->d_name);
        continue;
      }

      // Get file stats
      if (stat(fullPath, &statbuf) == 0)
      {
        if (S_ISDIR(statbuf.st_mode))
        {
          // It's a directory, add it
          // Claude AI want to add this 2 lines but this has nothing to do with AICA changes i think
          // Reserve 3 bytes for '[', ']', and '\0' so truncation never occurs
          size_t maxName = sizeof(fileList[fileCount].name) - 3;
          snprintf(fileList[fileCount].name, sizeof(fileList[fileCount].name), "[%.*s]", (int)maxName, entry->d_name);
          strcpy(fileList[fileCount].fullPath, fullPath);
          fileList[fileCount].isDirectory = true;
          fileCount++;
        }
        else if (hasValidExtension(entry->d_name))
        {
          // It's a valid file, add it
          strcpy(fileList[fileCount].name, entry->d_name);
          strcpy(fileList[fileCount].fullPath, fullPath);
          fileList[fileCount].isDirectory = false;
          fileCount++;
        }
      }
    }
    
    closedir(dir); // Close directory
    
    // Sorting : Folder first, then file (alphabeticaly)
    for (int i = 0; i < fileCount - 1; i++)
    {
      for (int j = i + 1; j < fileCount; j++)
      {
        bool shouldSwap = false;

        // If i is a file and j a folder : invert
        if (!fileList[i].isDirectory && fileList[j].isDirectory)
        {
          shouldSwap = true;
        }
        // If same type, sorting alphabeticaly
        else if (fileList[i].isDirectory == fileList[j].isDirectory)
        {
          if (strcmp(fileList[i].name, fileList[j].name) > 0)
          {
            shouldSwap = true;
          }
        }

        if (shouldSwap)
        {
          FileEntry temp = fileList[i];
          fileList[i] = fileList[j];
          fileList[j] = temp;
        }
      }
    }

    // If directory opened but no valid files found, add a placeholder entry
    if (fileCount == 0)
    {
      strncpy(fileList[0].name, "<<NO COMPATIBLE FILE FOUND>>", sizeof(fileList[0].name) - 1);
      fileList[0].name[sizeof(fileList[0].name) - 1] = '\0';
      strcpy(fileList[0].fullPath, "");
      fileList[0].isDirectory = false;
      fileCount = 1;
    }
  }
  else
  {
    // Directory could not be opened (e.g. no SD/USB, or folder not created yet)
    printf("Could not open directory: %s\n", dirPath);
    strncpy(fileList[0].name, "<<NO COMPATIBLE FILE FOUND>>", sizeof(fileList[0].name) - 1);
    fileList[0].name[sizeof(fileList[0].name) - 1] = '\0';
    strcpy(fileList[0].fullPath, "");
    fileList[0].isDirectory = false;
    fileCount = 1;
  }
}

// ============================================================================
// ACCURACY SELECTION MENU
// ============================================================================
void displayAccuracyMenu()
{
  int selectedOption = g_accuracy_preset; // Start with current setting
  
  while (true)
  {
    printf("\033[2J\033[H"); // Clear Screen
    printf("                  INFO - NullDC4Wii               \n");
    printf(" \n");
    printf("Information about preset :\n\n");

    printf("Calculations Accuracy (can lead to bugs if not AVERAGE):\n");  
    printf("> FAST - Maximum Speed (Use if you need more FPS (Framerate))\n");
    printf("> BALANCED - Good Balance \n");
    printf("> ACCURATE - Maximum Accuracy (closest to real hardware) \n\n");

    printf("Graphical settings \n");
    printf("> LOW    = GX_NEAR -   lod0 - GX_DISABLE (Wii) \n");
    printf("> NORMAL = GX_LINEAR - lod0 - GX_DISABLE (Wii) \n");
    printf("> HIGH   = GX_LINEAR - lodH - GX_ENABLE - Anisotropic x2 (WiiU) \n");
    printf("> EXTRA  = GX_LINEAR - lodE - GX_ENABLE - Anisotropic x4 (WiiU) \n\n");

    printf("Original Ratio (4/3) / FULLSCREEN (not implemented for now)\n");
    printf("> ORIGINAL - 4/3 ratio\n");
    printf("> FULLSCREEN \n");

    printf(" \n");
    printf("Current setting: \n");
    switch(g_accuracy_preset) {
      case 0: printf("FAST - "); break;
      case 1: printf("BALANCED - "); break;
      case 2: printf("ACCURATE - "); break;
    }
    switch(g_graphism_preset) {
      case 0: printf("LOW - "); break;
      case 1: printf("NORMAL -"); break;
      case 2: printf("HIGH - "); break;
      case 3: printf("EXTRA - "); break;
    }
    switch(g_ratio_preset) {
      case 0: printf("ORIGINAL - "); break;
      case 1: printf("FULLSCREEN\n"); break;
    }

    printf(" \n");
    
    printf("UP/DOWN: Select option | A: Confirm | B: Back\n");
    printf("\nNote: Change settings if you experience issues or need more speed.\n");
    
    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);
    
    if (pressed & WPAD_BUTTON_UP)
    {
      if (selectedOption > 0)
        selectedOption--;
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      if (selectedOption < 2)
        selectedOption++;
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      // Save selection and exit
      g_accuracy_preset = selectedOption;
      return;
    }
    else if (pressed & WPAD_BUTTON_B)
    {
      // Cancel and exit without saving
      return;
    }
    
    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }
}

// ============================================================================
// OPTIONS MENU (shown after file selection, before launch)
// ============================================================================
// Row indices in the options menu
#define OPT_LAUNCH      0
// row 1 is blank (skip)
#define OPT_GRAPHICS    2
#define OPT_ACCURACY    3
#define OPT_RATIO       4
#define OPT_ADV_ALPHA   5
#define OPT_FRAMESKIP   6
#define OPT_TEX_CACHE   7
#define OPT_4BPP        8
#define OPT_8BPP        9
#define OPT_MORE_INFO   10  // "More Info" screen (was button 2 in file browser)
#define OPT_ROW_COUNT   11  // total rows including blank

// Returns true if the user chose "Launch game", false if they pressed B to go back.
bool displayOptionsMenu()
{
  int selectedRow = OPT_LAUNCH; // start on "Launch game"

  while (true)
  {
    printf("\033[2J\033[H"); // Clear screen

    printf("NullDC4Wii - Alpha 0.21   OPTIONS\n");
    printf("===================================\n\n");

    // --- Launch game ---
    printf("%s LAUNCH GAME\n", (selectedRow == OPT_LAUNCH) ? ">" : " ");

    // --- Game name (max 60 characters) ---
    {
      // Extract filename from full path
      const char *gameName = strrchr(selectedFilePath, '/');
      gameName = (gameName != NULL) ? gameName + 1 : selectedFilePath;
      printf("    %.60s\n", gameName);
    }

    // --- Blank separator ---
    printf("\n");

    // --- Graphics ---
    printf("%s GRAPHICS      : ", (selectedRow == OPT_GRAPHICS) ? ">" : " ");
    switch (g_graphism_preset) {
      case 0: printf("[< LOW        >]"); break;
      case 1: printf("[< NORMAL     >]"); break;
      case 2: printf("[< HIGH       >]"); break;
      case 3: printf("[< EXTRA      >]"); break;
    }
    printf(" (Tip : 2D Games should use LOW)");
    printf("\n");

    // --- Accuracy ---
    printf("%s ACCURACY      : ", (selectedRow == OPT_ACCURACY) ? ">" : " ");
    switch (g_accuracy_preset) {
      case 0: printf("[< FAST       >]"); break;
      case 1: printf("[< BALANCED   >]"); break;
      case 2: printf("[< ACCURATE   >]"); break;
    }
    printf("\n");

    // --- Ratio ---
    printf("%s RATIO         : ", (selectedRow == OPT_RATIO) ? ">" : " ");
    switch (g_ratio_preset) {
      case 0: printf("[< ORIGINAL   >]"); break;
      case 1: printf("[< FULLSCREEN >]"); break;
    }
    printf("\n\n");

    // --- Advanced Alpha ---
    printf("%s ADVANCED ALPHA: ", (selectedRow == OPT_ADV_ALPHA) ? ">" : " ");
    switch (g_advanced_alpha_preset) {
      case 0: printf("[< NO  (DEFAULT) >]"); break;
      case 1: printf("[< YES (DEBUG)   >]"); break;
    }
    printf("\n");

    // --- Frameskipping ---
    printf("%s FRAMESKIPPING : ", (selectedRow == OPT_FRAMESKIP) ? ">" : " ");
    switch (g_frameskip_preset) {
      case 0: printf("[< 0 (DEFAULT)   >]"); break;
      case 1: printf("[< 1             >]"); break;
      case 2: printf("[< 2             >]"); break;
      case 3: printf("[< AUTO          >]"); break;
    }
    printf("\n");

    // --- Texture Cache ---
    printf("%s TEXTURE CACHE : ", (selectedRow == OPT_TEX_CACHE) ? ">" : " ");
    switch (g_texture_cache_preset) {
      case 0: printf("[< VERY FAST     >]"); break;
      case 1: printf("[< FAST          >]"); break;
      case 2: printf("[< NORMAL        >]"); break;
      case 3: printf("[< QUALITY       >]"); break;
    }
    printf("\n");

    // --- 4BPP ---
    printf("%s 4BPP MODE     : ", (selectedRow == OPT_4BPP) ? ">" : " ");
    switch (g_4bpp_preset) {
      case 0: printf("[< I4 STUB              >]"); break;
      case 1: printf("[< I4 GREY (FAST)       >]"); break;
      case 2: printf("[< CI4 (FAST)           >]"); break;
      case 3: printf("[< CI4 (NORMAL)         >]"); break;
      case 4: printf("[< RGB565 (ACCURATE)    >]"); break;
    }
    printf("\n");

    // --- 8BPP ---
    printf("%s 8BPP MODE     : ", (selectedRow == OPT_8BPP) ? ">" : " ");
    switch (g_8bpp_preset) {
      case 0: printf("[< I8 STUB              >]"); break;
      case 1: printf("[< I8 GREY (FAST)       >]"); break;
      case 2: printf("[< CI8 (FAST)           >]"); break;
      case 3: printf("[< CI8 (NORMAL)         >]"); break;
      case 4: printf("[< RGB565 (ACCURATE)    >]"); break;
    }
    printf("\n");

    printf("\n");

    // --- More Info ---
    printf("%s MORE INFO      (press A to open)\n", (selectedRow == OPT_MORE_INFO) ? ">" : " ");

    printf("\n");
    printf("UP/DOWN: Navigate | LEFT/RIGHT: Change value\n");
    printf("A: Launch / Open | B: Back to file list\n");

    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);

    // --- Navigation: UP ---
    if (pressed & WPAD_BUTTON_UP)
    {
      do {
        selectedRow = (selectedRow > 0) ? selectedRow - 1 : OPT_ROW_COUNT - 1;
      } while (selectedRow == 1); // skip blank row
    }
    // --- Navigation: DOWN ---
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      do {
        selectedRow = (selectedRow < OPT_ROW_COUNT - 1) ? selectedRow + 1 : 0;
      } while (selectedRow == 1); // skip blank row
    }
    // --- Value change: LEFT (cycle backwards) ---
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      switch (selectedRow) {
        case OPT_GRAPHICS:   g_graphism_preset       = (g_graphism_preset       + 3) % 4; break;
        case OPT_ACCURACY:   g_accuracy_preset        = (g_accuracy_preset        + 2) % 3; break;
        case OPT_RATIO:      g_ratio_preset           = (g_ratio_preset           + 1) % 2; break;
        case OPT_ADV_ALPHA:  g_advanced_alpha_preset  = (g_advanced_alpha_preset  + 1) % 2; break;
        case OPT_FRAMESKIP:  g_frameskip_preset       = (g_frameskip_preset       + 3) % 4; break;
        case OPT_TEX_CACHE:  g_texture_cache_preset   = (g_texture_cache_preset   + 3) % 4; break;
        case OPT_4BPP:       g_4bpp_preset            = (g_4bpp_preset            + 4) % 5; break;
        case OPT_8BPP:       g_8bpp_preset            = (g_8bpp_preset            + 4) % 5; break;
        default: break;
      }
    }
    // --- Value change: RIGHT (cycle forwards) ---
    else if (pressed & WPAD_BUTTON_RIGHT)
    {
      switch (selectedRow) {
        case OPT_GRAPHICS:   g_graphism_preset       = (g_graphism_preset       + 1) % 4; break;
        case OPT_ACCURACY:   g_accuracy_preset        = (g_accuracy_preset        + 1) % 3; break;
        case OPT_RATIO:      g_ratio_preset           = (g_ratio_preset           + 1) % 2; break;
        case OPT_ADV_ALPHA:  g_advanced_alpha_preset  = (g_advanced_alpha_preset  + 1) % 2; break;
        case OPT_FRAMESKIP:  g_frameskip_preset       = (g_frameskip_preset       + 1) % 4; break;
        case OPT_TEX_CACHE:  g_texture_cache_preset   = (g_texture_cache_preset   + 1) % 4; break;
        case OPT_4BPP:       g_4bpp_preset            = (g_4bpp_preset            + 1) % 5; break;
        case OPT_8BPP:       g_8bpp_preset            = (g_8bpp_preset            + 1) % 5; break;
        default: break;
      }
    }
    // --- A: launch or open sub-screen ---
    else if (pressed & WPAD_BUTTON_A)
    {
      if (selectedRow == OPT_LAUNCH)
        return true; // proceed to launch
      else if (selectedRow == OPT_MORE_INFO)
      {
        displayAccuracyMenu(); // reuse the existing info/accuracy screen
        // After returning, stay in the options menu
      }
    }
    // --- B: go back to file list ---
    else if (pressed & WPAD_BUTTON_B)
    {
      return false;
    }

    // Double-buffer swap
    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1;
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }
}

// Function to display menu and allow selection with Wiimote
int displayMenuAndSelectFile()
{
  int selectedIndex = 0;
  currentPage = 0;

  while (true)
  {
    printf("\033[2J\033[H"); // Clear Screen
    printf("\nNullDC4Wii - Alpha 0.21   ");
    printf("Current directory: %s\n", currentPath);
    // Display current GRAPHISM preset (cycled with Minus)
    printf("(-) GRAPHICS: ");
    switch(g_graphism_preset) {
      case 0: printf("LOW   "); break;
      case 1: printf("NORMAL"); break;
      case 2: printf("HIGH  "); break;
      case 3: printf("EXTRA "); break;
    }
    // Display current ACCURACY preset (cycled with Plus)
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
    printf("\nSelect a game file: (GDI Works, see Github README for other format)\n\n");
    
    

    // Calculate pagination
    int totalPages = (fileCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1)
      totalPages = 1;
    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex = startIndex + ITEMS_PER_PAGE;
    if (endIndex > fileCount)
      endIndex = fileCount;

    // Display files for current page
    for (int i = startIndex; i < endIndex; i++)
    {
      if (i == selectedIndex)
      {
        printf("> %s\n", fileList[i].name);
      }
      else
      {
        printf("  %s\n", fileList[i].name);
      }
    }

    // Display page info
    printf("\n--- Page %02d/%02d ---\n\n", currentPage + 1, totalPages);
    printf("HELP ME BUILD THIS PROJECT !! ANY HELP IS WELCOME !!\n");
    printf("https://github.com/BenoitAdam94/nullDC4Wii\n");
    printf("Contact : xalegamingchannel@gmail.com\n");
    printf("HELP ME ON THE COMPATIBILITY LIST !!\n");
    printf("Compatibility WIKI : https://wiibrew.org/wiki/NullDC4Wii/Compatibility\n\n");
    // Show which storage is active
    printf("Storage: %s", (g_storage_source == STORAGE_SD) ? "[SD CARD]  (2: Switch to USB)"
                                                            : "[USB]      (2: Switch to SD)");
    printf("\n");
    printf("A: Select | B: Back | 1: BIOS | (-) + (+): Exit\n");
    printf("INGAME : Press (-) and (+) simultaneously to Exit \n");

    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);

    if (pressed & WPAD_BUTTON_MINUS)
    {
      // Cycle GRAPHISM preset: LOW -> NORMAL -> HIGH -> EXTRA -> LOW 
      g_graphism_preset = (g_graphism_preset + 1) % 4;
    }
    if (pressed & WPAD_BUTTON_PLUS)
    {
      // Cycle SCREEN ratio: ORIGINAL <-> FULLSCREEN
      g_ratio_preset = (g_ratio_preset + 1) % 2;
    }
    if (pressed & WPAD_BUTTON_HOME)
    {
      // Cycle ACCURACY preset: FAST -> BALANCED -> ACCURATE -> FAST
      g_accuracy_preset = (g_accuracy_preset + 1) % 3;
    }
    if (pressed & WPAD_BUTTON_1)
    {
      return -2; // Boot to BIOS
    }
    if (pressed & WPAD_BUTTON_2)
    {
      // Toggle between SD and USB storage
      if (g_storage_source == STORAGE_SD)
      {
        // Try to mount and switch to USB
        printf("\033[2J\033[H");
        printf("Switching to USB...\n");
        VIDEO_SetNextFramebuffer(xfb[fb]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        fb ^= 1;
        console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);

        if (switchToUSB())
        {
          printf("USB mounted! Loading usb:/dreamcast/ ...\n");
        }
        else
        {
          printf("ERROR: Could not mount USB device.\n");
          printf("Make sure a USB drive is connected.\n");
          usleep(2000000);
          // Stay on SD
        }
      }
      else
      {
        // Switch back to SD
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
        // Change page if necessary
        if (selectedIndex < startIndex)
        {
          currentPage--;
        }
      }
    }
    else if (pressed & WPAD_BUTTON_DOWN)
    {
      if (selectedIndex < fileCount - 1)
      {
        selectedIndex++;
        // Change page if necessary
        if (selectedIndex >= endIndex)
        {
          currentPage++;
        }
      }
    }
    else if (pressed & WPAD_BUTTON_LEFT)
    {
      // Previous page
      if (currentPage > 0)
      {
        currentPage--;
        selectedIndex = currentPage * ITEMS_PER_PAGE;
      }
    }
    else if (pressed & WPAD_BUTTON_RIGHT)
    {
      // Next page
      if (currentPage < totalPages - 1)
      {
        currentPage++;
        selectedIndex = currentPage * ITEMS_PER_PAGE;
      }
    }
    else if (pressed & WPAD_BUTTON_A)
    {
      // Ignore the placeholder "no files" entry
      if (strlen(fileList[selectedIndex].fullPath) == 0 && !fileList[selectedIndex].isDirectory)
      {
        // Nothing to do — placeholder row
      }
      // Check if it's a directory
      else if (fileList[selectedIndex].isDirectory)
      {
        // Enter directory
        strcpy(currentPath, fileList[selectedIndex].fullPath);
        listFilesInDirectory(currentPath);
        selectedIndex = 0;
        currentPage = 0;
      }
      else
      {
        // It's a file or "No disc" option
        return selectedIndex;
      }
    }
    else if (pressed & WPAD_BUTTON_B)
    {
      // Go back to parent directory
      const char *rootPath = (g_storage_source == STORAGE_SD) ? "sd:/discs/" : "usb:/dreamcast/";
      if (strcmp(currentPath, rootPath) != 0)
      {
        char *lastSlash = strrchr(currentPath, '/');
        if (lastSlash != NULL && lastSlash != currentPath)
        {
          *lastSlash = '\0';
        }
        listFilesInDirectory(currentPath);
        selectedIndex = 0;
        currentPage = 0;
      }
    }
    else if ((WPAD_ButtonsHeld(0) & WPAD_BUTTON_PLUS) && (WPAD_ButtonsHeld(0) & WPAD_BUTTON_MINUS))
    {
      return -1; // Exit to main menu
    }

    // Double-buffer swap: point hardware at the buffer we just wrote, then flip
    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb ^= 1; // toggle between 0 and 1
    // Re-init console to the new back buffer so next printf goes there
    console_init(xfb[fb], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  }

  return selectedIndex;
}

void SetApplicationPath(wchar *path);

// ============================================================================
// BIOS BOOT HELPER
// ============================================================================
// Called when no disc is selected (button 1 in file browser, or no disc files found).
// Sets selectedFilePath to empty and shows the options menu.
// Returns true if the user confirmed launch, false if they pressed B to go back.
void handleBIOSBoot()
{
  strcpy(selectedFilePath, ""); // No disc — launch straight to BIOS
}

int main(int argc, wchar *argv[])
{
  // Initialize the video system
  VIDEO_Init();

  /* Audio */
  ASND_Init();
  wii_audio_init();
  if(mp3mainmenu) {
    MP3Player_Init(); // MP3 init
  }
  // And at shutdown / exit, add:
  //   wii_audio_term();
  //   ASND_End();


  // This function initialises the attached controllers (devkit wii-example)
  PAD_Init();
  WPAD_Init();

  // Obtain the preferred video mode from the system
  rmode = VIDEO_GetPreferredMode(NULL);

  // Allocate TWO framebuffers for double-buffering (prevents flicker)
  xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  fb = 0; // current back buffer index

  // Initialise the console on the first buffer
  console_init(xfb[0], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);

  // Set up the video registers with the chosen mode
  VIDEO_Configure(rmode);

  // Tell the video hardware where our display memory is
  VIDEO_SetNextFramebuffer(xfb[fb]);

  // Make the display visible
  VIDEO_SetBlack(false);

  // Flush the video register changes to the hardware
  VIDEO_Flush();

  // Wait for Video setup to complete
  VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // Playing MP3 file
  if(mp3mainmenu) {
    MP3Player_PlayBuffer(sample_mp3, sample_mp3_size, NULL);
  }


  // Initialise SD Card (primary storage, USB is secondary)
  if (fatInitDefault())
  {
    printf("SD card mounted!\n");
    // This part export every printf to a txt file and disable every printf afterwards
    /*
    if (!fopen("/dolphin", "r"))
        freopen("/ndclog.txt", "w", stdout);
        */
  }
  else
  {
    // SD not available: warn user but don't block — they may want to use USB
    printf("WARNING: Could not mount SD card.\n");
    printf("You can press 2 in the file browser to switch to USB.\n");
    usleep(2000000);
  }

  void SetApplicationPath(const wchar *path);

  // List files in initial directory (always returns at least a placeholder row)
  listFilesInDirectory(currentPath);

  // Main menu loop
  {
    bool launchGame = false;

    while (!launchGame)
    {
      int selectedIndex = displayMenuAndSelectFile();

      if (selectedIndex == -1)
      {
        // HOME pressed: exit
        printf("Exiting...\n");
        return 0;
      }
      else if (selectedIndex == -2)
      {
        // Boot to BIOS (button 1 pressed): launch immediately, no options screen
        handleBIOSBoot();
        launchGame = true;
      }
      else if (selectedIndex >= 0)
      {
        // A file has been selected: show options before launching
        strcpy(selectedFilePath, fileList[selectedIndex].fullPath);
        launchGame = displayOptionsMenu();
        if (!launchGame)
          continue; // B pressed in options: return to file list
      }
    }

    // Print launch summary
    printf("\x1b[2J\x1b[H"); // Clear Screen
    if (strlen(selectedFilePath) > 0)
      printf("Selected file  : %s\n", selectedFilePath);
    else
      printf("Booting to BIOS (no disc)...\n");

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
    printf("Frameskipping  : ");
    switch(g_frameskip_preset) {
      case 0: printf("0\n");    break;
      case 1: printf("1\n");    break;
      case 2: printf("2\n");    break;
      case 3: printf("AUTO\n"); break;
    }
    printf("Texture Cache      : ");
    switch(g_texture_cache_preset) {
      case 0: printf("VERY FAST\n"); break;
      case 1: printf("FAST\n");      break;
      case 2: printf("NORMAL\n");    break;
      case 3: printf("QUALITY\n");   break;
    }
    printf("4BPP Mode      : ");
    switch(g_4bpp_preset) {
      case 0: printf("I4 STUB\n");          break;
      case 1: printf("I4 GREY (FAST)\n");   break;
      case 2: printf("CI4 (FAST)\n");       break;
      case 3: printf("CI4 (NORMAL)\n");     break;
      case 4: printf("RGB565\n");           break;
    }
    printf("8BPP Mode      : ");
    switch(g_8bpp_preset) {
      case 0: printf("I8 STUB\n");           break;
      case 1: printf("I8 GREY (FAST)\n");    break;
      case 2: printf("CI8 (FAST)\n");        break;
      case 3: printf("CI8 (NORMAL)\n");      break;
      case 4: printf("RGB565\n");            break;
    }
  } // end main menu loop

  // Stop menu music before handing audio to the emulator
  if(mp3mainmenu) {
    MP3Player_Stop();
  }

  int rv = EmuMain(argc, argv); // Launching the Emulator (nullDC.cpp)

  return rv;
}

// Select file
int os_GetFile(char *szFileName, char *szParse, u32 flags)
{
  if (strlen(selectedFilePath) > 0)
  {
    // ELF files are loaded directly into RAM — don't pass to disc plugin
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
  return clock() / (double)CLOCKS_PER_SEC;
}

int os_msgbox(const wchar *text, unsigned int type)
{
  printf("OS_MSGBOX: %s\n", text);
  return 0;
}
