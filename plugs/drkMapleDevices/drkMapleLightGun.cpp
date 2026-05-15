// drkMapleLightGun.cpp
// Light gun device emulation for Dreamcast on Wii
// Uses Wiimote IR pointer for aiming, Wiimote buttons for shooting.
//
// Supported games (Dreamcast Maple light gun / "Stunner"):
//   - House of the Dead 2
//   - Confidential Mission
//   - Death Crimson OX
//   - Virtua Cop 2
//   - Starship Troopers
//
// Wii controls:
//   IR pointer      -> Gun aim (screen X/Y coordinates)
//   B (trigger)     -> Shoot
//   A               -> Reload / offscreen shot trick
//   MINUS           -> Start
//   PLUS            -> Coin / insert
//   UP/DOWN/LEFT/RIGHT -> D-Pad (menus)
//   MINUS + PLUS    -> Exit emulator
//
// 2-player: Player 1 = Wiimote 0, Player 2 = Wiimote 1
// Both Wiimotes must point at the screen independently.
//
// Notes on Dreamcast light gun protocol:
//   The Dreamcast Maple light gun reports a screen X/Y coordinate
//   (0-639 horizontal, 0-479 vertical for NTSC) and a button mask.
//   The Wii IR camera gives dot positions in 0-1023 (X) and 0-767 (Y).
//   We scale those to Dreamcast coordinates here.
//   When the gun is pointed off-screen, we report (0xFFFF, 0xFFFF) which
//   is the conventional "missed" signal used by all DC gun games.
//
// Player count is read from get_player_count() defined in main.cpp.

#include "plugins/plugin_header.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiiuse/wpad.h>
#include "dc/dc.h"

// Dreamcast controller button definitions (same as drkMapleDevices.cpp)
#define key_CONT_C          (1 << 0)
#define key_CONT_B          (1 << 1)
#define key_CONT_A          (1 << 2)
#define key_CONT_START      (1 << 3)
#define key_CONT_DPAD_UP    (1 << 4)
#define key_CONT_DPAD_DOWN  (1 << 5)
#define key_CONT_DPAD_LEFT  (1 << 6)
#define key_CONT_DPAD_RIGHT (1 << 7)
#define key_CONT_Z          (1 << 8)
#define key_CONT_Y          (1 << 9)
#define key_CONT_X          (1 << 10)
#define key_CONT_D          (1 << 11)

// Dreamcast screen resolution (NTSC)
#define DC_SCREEN_W  640
#define DC_SCREEN_H  480

// Wii IR camera resolution
#define WII_IR_W  1024
#define WII_IR_H   768

// Sentinel value for off-screen / IR not visible
#define DC_OFFSCREEN 0xFFFF

// Maximum players
#define MAX_LG_PLAYERS 2

// Controller state arrays — shared with maple_cfg / maple_devs via plugin_header externs
extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4];
extern s8  joyy[4];
extern u8  rt[4];
extern u8  lt[4];

// Gun position reported to the Dreamcast (per port)
// These need to be read by maple_devs.cpp / maple_cfg.cpp when the
// Maple device type is set to MAPLE_DEVICE_LIGHTGUN.
u16 gun_x[MAX_LG_PLAYERS] = {DC_OFFSCREEN, DC_OFFSCREEN};
u16 gun_y[MAX_LG_PLAYERS] = {DC_OFFSCREEN, DC_OFFSCREEN};

// Expose gun position to rest of emulator
extern "C"
{
    u16 GetLightGunX(int port) { return (port < MAX_LG_PLAYERS) ? gun_x[port] : DC_OFFSCREEN; }
    u16 GetLightGunY(int port) { return (port < MAX_LG_PLAYERS) ? gun_y[port] : DC_OFFSCREEN; }
}

extern "C" int get_player_count(void);

/**
 * Scales a Wii IR coordinate to a Dreamcast screen coordinate.
 * Returns DC_OFFSCREEN if the IR dot is not visible.
 *
 * @param wiiVal   Raw IR value (0 to wiiMax-1), or negative if not visible
 * @param wiiMax   Range of Wii IR axis (1024 for X, 768 for Y)
 * @param dcMax    Range of DC screen axis (640 for X, 480 for Y)
 */
static u16 ScaleIRCoord(s32 wiiVal, s32 wiiMax, s32 dcMax)
{
    if (wiiVal < 0 || wiiVal >= wiiMax)
        return DC_OFFSCREEN;

    // Simple linear scale
    s32 scaled = (wiiVal * dcMax) / wiiMax;

    // Clamp just in case of rounding edge
    if (scaled < 0)      scaled = 0;
    if (scaled >= dcMax) scaled = dcMax - 1;

    return (u16)scaled;
}

/**
 * Updates light gun input state for one player port.
 * Call once per frame per active port.
 *
 * @param port  Controller port (0 = player 1, 1 = player 2)
 */
void UpdateLightGunState(u32 port)
{
    if (port >= (u32)get_player_count() || port >= MAX_LG_PLAYERS)
        return;

    WPAD_ScanPads();

    // --- IR aiming ---
    WPADData *data = WPAD_Data(port);

    if (data && data->ir.valid)
    {
        // data->ir.x is 0..1023, data->ir.y is 0..767
        // The Wii IR camera origin is top-left; DC expects same origin.
        gun_x[port] = ScaleIRCoord((s32)data->ir.x, WII_IR_W, DC_SCREEN_W);
        gun_y[port] = ScaleIRCoord((s32)data->ir.y, WII_IR_H, DC_SCREEN_H);
    }
    else
    {
        // IR not visible — report off-screen shot
        gun_x[port] = DC_OFFSCREEN;
        gun_y[port] = DC_OFFSCREEN;
    }

    // --- Buttons ---
    u32 wiiButtons = WPAD_ButtonsHeld(port);

    // Reset to all-released
    kcode[port] = 0xFFFF;

    // Shoot — B trigger (most natural for a gun)
    if (wiiButtons & WPAD_BUTTON_B)
        kcode[port] &= ~key_CONT_A;

    // Off-screen reload trick — A button fires with gun_x/y set to off-screen
    // Some games (HotD2) use an off-screen shot to reload.
    if (wiiButtons & WPAD_BUTTON_A)
    {
        kcode[port] &= ~key_CONT_B;
        // Force off-screen so the game treats it as a reload
        gun_x[port] = DC_OFFSCREEN;
        gun_y[port] = DC_OFFSCREEN;
    }

    // Start / coin
    if (wiiButtons & WPAD_BUTTON_MINUS)
        kcode[port] &= ~key_CONT_START;

    if (wiiButtons & WPAD_BUTTON_PLUS)
        kcode[port] &= ~key_CONT_C;  // Coin / Insert

    // D-Pad (menu navigation in attract mode / config screens)
    if (wiiButtons & WPAD_BUTTON_UP)    kcode[port] &= ~key_CONT_DPAD_UP;
    if (wiiButtons & WPAD_BUTTON_DOWN)  kcode[port] &= ~key_CONT_DPAD_DOWN;
    if (wiiButtons & WPAD_BUTTON_LEFT)  kcode[port] &= ~key_CONT_DPAD_LEFT;
    if (wiiButtons & WPAD_BUTTON_RIGHT) kcode[port] &= ~key_CONT_DPAD_RIGHT;

    // Exit combination: MINUS + PLUS simultaneously
    if ((wiiButtons & WPAD_BUTTON_MINUS) && (wiiButtons & WPAD_BUTTON_PLUS))
        exit(0);
}

/**
 * Initializes light gun ports to safe default state.
 */
void InitLightGun(void)
{
    for (int i = 0; i < MAX_LG_PLAYERS; i++)
    {
        kcode[i]  = 0xFFFF;
        gun_x[i]  = DC_OFFSCREEN;
        gun_y[i]  = DC_OFFSCREEN;
        joyx[i]   = 0;
        joyy[i]   = 0;
        rt[i]     = 0;
        lt[i]     = 0;
    }

    // Enable IR on both Wiimotes (required for pointer tracking)
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    if (get_player_count() >= 2)
        WPAD_SetDataFormat(WPAD_CHAN_1, WPAD_FMT_BTNS_ACC_IR);
}
