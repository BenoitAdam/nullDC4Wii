// wiiu/wiiu_game_presets.cpp
//
// The game_presets.cfg parser (wii/game_presets.cpp) is plain C, shared as-is.
// It is pulled in through this one-line wrapper instead of adding ../wii/ to
// wiiu/Makefile's SOURCES: that directory also holds wii/main.cpp, whose object
// name would collide with wiiu/main.cpp's, plus libogc-only files.

#include "wii/game_presets.cpp"
