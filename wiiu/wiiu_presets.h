// wiiu/wiiu_presets.h
// Wii U side of the preset system (wiiu/wiiu_presets.cpp). The presets
// themselves are wii/main.cpp's, compiled with NDC_PRESETS_ONLY.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Pin the presets the Wii U build cannot honour (dynarec, fastmem, ARM7 JIT).
// Call after game_presets_apply(), before EmuMain().
void WiiU_ForcePlatformPresets(void);

#ifdef __cplusplus
}
#endif
