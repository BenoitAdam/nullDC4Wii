#pragma once

/*
    game_presets.h - Per-game preset system for NullDC4Wii
    Place game_presets.cfg on sd:/discs/game_presets.cfg
*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Remember the preset config file path and log how many sections it holds.
 * Nothing is parsed or stored in RAM — game_presets_apply() streams the
 * file from SD each launch.
 * Safe to call even if the file doesn't exist (prints a warning, uses defaults).
 * @param cfg_path Full path e.g. "sd:/discs/game_presets.cfg"
 */
void game_presets_load(const char* cfg_path);

/**
 * Stream the config file and apply [default] plus the first matching
 * preset for the given file path.
 * Must be called AFTER game_presets_load() and BEFORE displayOptionsMenu()
 * so the options screen already shows the preset values.
 * Sets g_matched_preset_name to the matched keyword, or "" if no match.
 * @param filepath Full path to the selected disc/elf image
 */
void game_presets_apply(const char* filepath);

/**
 * Name of the last matched preset keyword (empty string if no match).
 * Displayed in the options menu under the game name.
 */
extern char g_matched_preset_name[];

#ifdef __cplusplus
}
#endif
