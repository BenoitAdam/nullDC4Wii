// wiiu/wiiu_fs.h
// SD card paths and directory listing for the Wii U build (wiiu/wiiu_fs.cpp).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Pick the app folder (data/dc_boot.bin lives there) and hand it to the core's
// SetApplicationPath(). Returns false when no candidate holds a BIOS; the
// folder is then still set to the native one so the error names a real path.
bool        wiiu_fs_init(void);

const char *wiiu_fs_app_dir(void);    // e.g. "fs:/vol/external01/wiiu/apps/nulldc4wiiu/"
const char *wiiu_fs_discs_dir(void);  // "fs:/vol/external01/discs/"

// game_presets.cfg: app folder first, then the discs folder (same order as the
// Wii build). Returns NULL when neither exists.
const char *wiiu_fs_presets_path(void);

bool wiiu_fs_exists(const char *path);

// Game files in `dir` with a supported extension, sorted by name. Fills up to
// `max` entries of `stride` bytes each (full path); returns the count.
int  wiiu_fs_list_games(const char *dir, char *out, int stride, int max);

#ifdef __cplusplus
}
#endif
