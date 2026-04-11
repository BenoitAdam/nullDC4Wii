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

// ============================================================================
// GLOBAL EMULATOR PRESET
// ============================================================================
// Global variable to store user's calculations (CPU, FPU, GPU...) accuracy choice
int g_accuracy_preset = 2; // 0=Fast, 1=Balanced, 2=Accurate (default)

// These will be used by sh4_fpu.cpp ... (put additional files here)
extern "C" {
  int get_accuracy_preset() {
    return g_accuracy_preset;
  }
}

int g_graphism_preset = 1; // 0=Low (default), 1=Normal, 2=High, 3=Extra

// These will be used by gxRend.cpp ... (put additional files here)
extern "C" {
  int get_graphism_preset() {
    return g_graphism_preset;
  }
}

int g_ratio_preset = 1; // 0=Original (4/3), 1=Fullscreen (defaut)

// These will be used by gxRend (and maybe other files)
extern "C" {
  int get_ratio_preset() {
    return g_ratio_preset;
  }
}

int g_advanced_alpha_preset = 0; // 0=no advanced alpha (Default), 1=Advanced Alpha (defaut)

// These will be used by gxRend
extern "C" {
  int get_advanced_alpha_preset() {
    return g_advanced_alpha_preset;
  }
}

int g_frameskip_preset = 0; // 0=No frame skip (defaut for now), 1=1 frame skip, 2=2 frame skip, 3=Auto frame skip

// These will be used by gxRend
extern "C" {
  int get_frameskip_preset() {
    return g_frameskip_preset;
  }
}

int g_4bpp_preset = 2; // 0 = I4 Stub, 1 = CI4, 2 = RGB565

// These will be used by gxRend
extern "C" {
  int get_4bpp_preset() {
    return g_4bpp_preset;
  }
}

int g_8bpp_preset = 2; // 0 = I8 Stub, 1 = CI8, 2 = RGB565

// These will be used by gxRend
extern "C" {
  int get_8bpp_preset() {
    return g_8bpp_preset;
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
// FPS BOOST : FASTER FPS Improvements
// ============================================================================

/* 
From version alpha 0.14 I'm introducing this global variable
as Dreamcast Intro and some menu already hit 57FPS, it could be difficult to see regression

From version alpha 0.14, every FPS Boost should be in a "if" or "if/else" statement

FPS Boost should be to 1 when compiling for release

*/

int g_fps_boost = 0; // 0= debug 1=Normal

extern "C" {
  int get_fps_boost() {
    return g_fps_boost;
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
  }
  else
  {
    // Error
    perror("Could not open directory");
    printf("Could not open directory: %s\n", dirPath);
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
#define OPT_FPS_BOOST   7
#define OPT_4BPP        8
#define OPT_8BPP        9
#define OPT_ROW_COUNT   10  // total rows including blank

// Returns true if the user chose "Launch game", false if they pressed B to go back.
bool displayOptionsMenu()
{
  int selectedRow = OPT_LAUNCH; // start on "Launch game"

  while (true)
  {
    printf("\033[2J\033[H"); // Clear screen

    printf("NullDC4Wii - Alpha 0.18   OPTIONS\n");
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

    // --- FPS Boost ---
    printf("%s FPS BOOST     : ", (selectedRow == OPT_FPS_BOOST) ? ">" : " ");
    switch (g_fps_boost) {
      case 0: printf("[< NO (DEBUG)    >]"); break;
      case 1: printf("[< YES (DEFAULT) >]"); break;
    }
    printf("\n");

    // --- 4BPP ---
    printf("%s 4BPP MODE     : ", (selectedRow == OPT_4BPP) ? ">" : " ");
    switch (g_4bpp_preset) {
      case 0: printf("[< I4 STUB (FAST)     >]"); break;
      case 1: printf("[< CI4 (OK BUT FUZZY) >]"); break;
      case 2: printf("[< RGB565 (ACCURATE)  >]"); break;
    }
    printf("\n");

    // --- 8BPP ---
    printf("%s 8BPP MODE     : ", (selectedRow == OPT_8BPP) ? ">" : " ");
    switch (g_8bpp_preset) {
      case 0: printf("[< I8 STUB (FAST)     >]"); break;
      case 1: printf("[< CI8 (OK BUT FUZZY) >]"); break;
      case 2: printf("[< RGB565 (ACCURATE)  >]"); break;
    }
    printf("\n");

    printf("\n");
    printf("UP/DOWN: Navigate | LEFT/RIGHT: Change value\n");
    printf("A: Launch | LEFT/RIGHT: Change value | B: Back to file list\n");

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
        case OPT_FPS_BOOST:  g_fps_boost              = (g_fps_boost              + 1) % 2; break;
        case OPT_4BPP:       g_4bpp_preset            = (g_4bpp_preset            + 2) % 3; break;
        case OPT_8BPP:       g_8bpp_preset            = (g_8bpp_preset            + 2) % 3; break;
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
        case OPT_FPS_BOOST:  g_fps_boost              = (g_fps_boost              + 1) % 2; break;
        case OPT_4BPP:       g_4bpp_preset            = (g_4bpp_preset            + 1) % 3; break;
        case OPT_8BPP:       g_8bpp_preset            = (g_8bpp_preset            + 1) % 3; break;
        default: break;
      }
    }
    // --- A: launch only (use LEFT/RIGHT to change values) ---
    else if (pressed & WPAD_BUTTON_A)
    {
      if (selectedRow == OPT_LAUNCH)
        return true; // proceed to launch
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
    printf("\nNullDC4Wii - Alpha 0.18   ");
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
    printf("A: Select | B: Back | 1: BIOS | 2: More Info | (-) + (+): Exit\n");
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
      // Open FPU accuracy menu
      displayAccuracyMenu();
      // After returning, continue showing file menu
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
      // Check if it's a directory
      if (fileList[selectedIndex].isDirectory)
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
      if (strcmp(currentPath, "sd:/discs/") != 0 && strcmp(currentPath, "sd:/discs") != 0)
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


  // Initialise SD Card
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
    // If no SD card :
    printf("ERROR: Failed to mount SD card!\n");
    printf("Press HOME to exit.\n");
    while (1)
    {
      WPAD_ScanPads();
      if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
        exit(0);
      usleep(100000); // Wait time to save GPU
      VIDEO_WaitVSync();
    }
  }

  void SetApplicationPath(const wchar *path);

  // List files in initial directory
  listFilesInDirectory(currentPath);

  // If there is file (there always will be with the option "No Disc boot to BIOS")
  if (fileCount > 0)
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
    printf("FPS Boost      : %s\n", g_fps_boost ? "YES" : "NO (DEBUG)");
    printf("4BPP Mode      : ");
    switch(g_4bpp_preset) {
      case 0: printf("I4 STUB\n"); break;
      case 1: printf("CI4\n");     break;
      case 2: printf("RGB565\n");  break;
    }
    printf("8BPP Mode      : ");
    switch(g_8bpp_preset) {
      case 0: printf("I8 STUB\n"); break;
      case 1: printf("CI8\n");     break;
      case 2: printf("RGB565\n");  break;
    }
  }
  else
  {
    // If no valid disc file found: boot to BIOS directly
    printf("No valid disc files found in sd:/discs/. Booting to BIOS...\n");
    usleep(2000000);
    handleBIOSBoot();
  }

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
