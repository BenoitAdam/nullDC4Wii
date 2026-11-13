// wiiu/wiiu_config.h
// Wii U (native Cafe OS) platform constants: paths, memory sizes, screen
// geometry. Shared by every wiiu/*.cpp file and by plugs/drkPvr/gx2Rend.cpp.
#pragma once

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
// wut's devoptab mounts the SD card as "fs:/vol/external01" and chdir()s there
// at start-up (wutdevoptab, __init_wut_devoptab). Paths are spelled out in full
// anyway: a bare "/x" resolves to the FSA root, not the SD card.
#define WIIU_SD_ROOT            "fs:/vol/external01/"

// App folder candidates, probed in this order by wiiu_fs_init(). The first one
// holding data/dc_boot.bin wins. The second is the Wii build's folder, so one
// SD card with the BIOS in the usual Wii place works for both builds.
#define WIIU_APP_DIR_NATIVE     WIIU_SD_ROOT "wiiu/apps/nulldc4wiiu/"
#define WIIU_APP_DIR_WII        WIIU_SD_ROOT "apps/nulldc4wii/"

// Games folder: same "discs" folder the Wii build browses (sd:/discs/).
#define WIIU_DISCS_DIR          WIIU_SD_ROOT "discs/"

// The Wii build's /ndclog.txt, same place on the card.
#define WIIU_LOG_PATH           WIIU_SD_ROOT "ndclog.txt"

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
// Size of the "MEM2 arena" stand-in carved from the default heap at boot
// (wiiu/wiiu_mem.cpp). _vmem_reserve() needs ~42 MB out of it (64 KB alignment
// + 2 MB ARAM + 8 MB VRAM + 16 MB RAM + 2x8 MB render buffer). 64 MB leaves
// headroom; the Wii U has far more than that to spare.
#define WIIU_ARENA_SIZE         (64u * 1024u * 1024u)

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
// The Dreamcast frame is drawn into this offscreen target, then scaled onto
// the TV and the GamePad (gx2Rend.cpp). 640x480 = the PVR's native canvas.
#define WIIU_DC_FB_WIDTH        640
#define WIIU_DC_FB_HEIGHT       480

// Vertices / strips / polygon headers per frame for the GX2 renderer. Same
// orders of magnitude as gxRend.cpp's arenas.
#define WIIU_TA_VTX_MAX         (64 * 1024)
#define WIIU_TA_STRIP_MAX       (16 * 1024)
#define WIIU_TA_PARAM_MAX       (8 * 1024)

// Decoded-texture cache (gx2Rend.cpp): slots, and the largest DC texture side.
#define WIIU_TEX_CACHE_SLOTS    512
#define WIIU_TEX_MAX_SIDE       1024

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// AX ring per channel, in samples (44.1 kHz s16). 8192 = ~186 ms: deep enough
// that the interpreter-speed producer does not starve the DSP every frame.
#define WIIU_AUDIO_RING_SAMPLES 8192
