#pragma once

/*
    game_presets.h - Per-game preset system for NullDC4Wii
    game_presets.cfg lives next to boot.dol (sd:/apps/nulldc4wii/ or
    usb:/apps/nulldc4wii/) or in the games folder - loadGamePresets() in
    wii/main.cpp probes both devices and hands the winner to
    game_presets_load().
*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Remember the preset config file path and log how many sections it holds.
 * Nothing is parsed or stored in RAM — game_presets_apply() streams the
 * file back off the card/drive each launch.
 * Safe to call even if the file doesn't exist (prints a warning, uses defaults).
 * @param cfg_path Full path e.g. "sd:/discs/game_presets.cfg" or
 *                 "usb:/apps/nulldc4wii/game_presets.cfg"
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

/**
 * True when the matched section used <angle> brackets in the .cfg, i.e. a
 * Wii-U-only section. Display the name as <name> instead of [name].
 */
extern bool g_matched_preset_is_wiiu;

#ifdef __cplusplus
}
#endif
