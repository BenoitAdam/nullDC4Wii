#pragma once
#include "types.h"
#include "config.h"

// Avoid macro collisions with system headers
#define LoadSettings LSPVR
#define SaveSettings SSPVR
#define settings pvrsetts

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Forward declarations
int msgboxf(const char* text, unsigned int type, ...);

// Verification macros - compile-time configurable
#if DO_VERIFY == OP_ON
    #define verifyf(x) verify(x)
    #define verifyc(x) verify(FAILED(x))
#else
    #undef verify
    #define verify(x) ((void)0)
    #define verifyf(x) (x)
    #define verifyc(x) (x)
#endif

#define fverify verify

#include "ta_structs.h"

// External declarations
extern s32 render_end_pending_cycles;
// render_delay preset: staggered render-done IRQ countdowns (see SPG.cpp);
// armed by StartRender() instead of render_end_pending_cycles when preset on
extern s32 render_isp_pending_cycles;
extern s32 render_tsp_pending_cycles;
extern s32 render_vd_pending_cycles;
extern pvr_init_params PVRPARAMS;

// Function declarations
void LoadSettings();
void SaveSettings();

// Renderer identification based on build configuration
#if REND_API == REND_PSP
    #define REND_NAME "PSPgu DHA"
#elif REND_API == REND_GLES2
    #define REND_NAME "OpenGL ES 2.0 HAL"
#elif REND_API == REND_SOFT
    #define REND_NAME "Software"
#elif REND_API == REND_WII
    #define REND_NAME "WIIgx DHA"
#elif REND_API == REND_PS2
    #define REND_NAME "PS2gs DHA"
#else
    #error "Invalid REND_API configuration. Must be one of: REND_PSP/REND_GLES2/REND_SOFT/REND_WII/REND_PS2"
#endif

/**
 * Settings structure for the PVR renderer
 * All settings are persisted via the config system
 */
struct _settings_type
{
    // Fullscreen configuration
    struct
    {
        u32 Enabled;        // 0=windowed, 1=fullscreen
        u32 Res_X;          // Horizontal resolution (-1=auto-detect)
        u32 Res_Y;          // Vertical resolution (-1=auto-detect)
        u32 Refresh_Rate;   // Refresh rate in Hz (-1=auto-detect)
    } Fullscreen;

    // Visual enhancement settings
    struct
    {
        u32 MultiSampleCount;   // MSAA sample count (0=off, 2/4/8 samples)
        u32 MultiSampleQuality; // MSAA quality level (hardware-dependent)
        u32 AspectRatioMode;    // 0=stretch, 1=4:3, 2=16:9
    } Enhancements;

    // Emulation accuracy settings
    struct
    {
        u32 PaletteMode;    // Palette texture handling mode
        u32 AlphaSortMode;  // Alpha sorting algorithm (0=off, 1=per-strip, 2=per-triangle)
        u32 ModVolMode;     // Modifier volume rendering mode
        u32 ZBufferMode;    // Z-buffer algorithm selection
    } Emulation;

    // On-Screen Display options
    struct
    {
        u32 ShowFPS;        // Display frames per second counter
        u32 ShowStats;      // Display detailed rendering statistics
    } OSD;
};

// Global settings instance
extern _settings_type settings;
