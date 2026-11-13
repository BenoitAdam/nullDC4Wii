// wiiu/wiiu_fs.cpp
//
// SD card paths for the Wii U build. wut's devoptab already mounts the SD card
// as "fs:/vol/external01" (newlib fopen/opendir work on it directly), so there
// is no mount step here, only the folder layout:
//
//   fs:/vol/external01/wiiu/apps/nulldc4wiiu/data/dc_boot.bin   (native)
//   fs:/vol/external01/apps/nulldc4wii/data/dc_boot.bin         (Wii build's)
//   fs:/vol/external01/discs/                                   (games, shared)
//
// The first app folder that holds a BIOS wins, so one SD card set up for the
// Wii build also works for the Wii U build without copying anything.
//
// USB storage is not handled yet: under Aroma it needs a FAT/NTFS mount
// library (libfat equivalent) that this basic port does not link.

#include "types.h"   // first, like the Wii files (config.h's BIG_ENDIAN poison)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    // strcasecmp
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#include "stdclass.h"   // SetApplicationPath()
#include "wiiu/wiiu_config.h"
#include "wiiu/wiiu_fs.h"

static char s_app_dir[256]     = WIIU_APP_DIR_NATIVE;
static char s_presets_path[300] = "";

extern "C" bool wiiu_fs_exists(const char *path)
{
  struct stat st;
  return path && stat(path, &st) == 0;
}

extern "C" bool wiiu_fs_init(void)
{
  static const char *const kCandidates[] = { WIIU_APP_DIR_NATIVE, WIIU_APP_DIR_WII };
  bool found = false;

  for (unsigned i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++)
  {
    char bios[300];
    snprintf(bios, sizeof(bios), "%sdata/dc_boot.bin", kCandidates[i]);
    if (wiiu_fs_exists(bios))
    {
      snprintf(s_app_dir, sizeof(s_app_dir), "%s", kCandidates[i]);
      found = true;
      break;
    }
  }

  // GetEmuPath() concatenates onto this, so it keeps its trailing '/'.
  SetApplicationPath(s_app_dir);
  return found;
}

extern "C" const char *wiiu_fs_app_dir(void)   { return s_app_dir; }
extern "C" const char *wiiu_fs_discs_dir(void) { return WIIU_DISCS_DIR; }

extern "C" const char *wiiu_fs_presets_path(void)
{
  snprintf(s_presets_path, sizeof(s_presets_path), "%sgame_presets.cfg", s_app_dir);
  if (wiiu_fs_exists(s_presets_path))
    return s_presets_path;
  snprintf(s_presets_path, sizeof(s_presets_path), "%sgame_presets.cfg", WIIU_DISCS_DIR);
  if (wiiu_fs_exists(s_presets_path))
    return s_presets_path;
  return NULL;
}

// Same list as wii/main.cpp hasValidExtension().
static bool has_game_ext(const char *name)
{
  static const char *const kExt[] = { ".gdi", ".cdi", ".iso", ".bin", ".cue",
                                      ".nrg", ".mds", ".elf", ".chd" };
  size_t n = strlen(name);
  if (n < 4)
    return false;
  char ext[5];
  for (int i = 0; i < 4; i++)
    ext[i] = (char)tolower((unsigned char)name[n - 4 + i]);
  ext[4] = 0;
  for (unsigned i = 0; i < sizeof(kExt) / sizeof(kExt[0]); i++)
    if (strcmp(ext, kExt[i]) == 0)
      return true;
  return false;
}

// Entries are fixed-stride strings, so qsort can compare them in place.
static int cmp_entries(const void *a, const void *b)
{
  return strcasecmp((const char *)a, (const char *)b);
}

extern "C" int wiiu_fs_list_games(const char *dir, char *out, int stride, int max)
{
  DIR *d = opendir(dir);
  if (!d)
    return 0;

  int count = 0;
  struct dirent *e;
  while (count < max && (e = readdir(d)) != NULL)
  {
    if (e->d_name[0] == '.')
      continue;
    if (!has_game_ext(e->d_name))
      continue;
    snprintf(out + count * stride, (size_t)stride, "%s%s", dir, e->d_name);
    count++;
  }
  closedir(d);

  qsort(out, (size_t)count, (size_t)stride, cmp_entries);
  return count;
}
