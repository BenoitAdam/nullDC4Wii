// drkMapleDevices.cpp : Wii Controller device mapping for Dreamcast emulator
//
// Improvements:
// - Better code organization and documentation
// - Configurable dead zones
// - Analog stick support with proper scaling
// - Trigger support from GameCube controller
// - More robust input handling
// - Cleaner button mapping logic
// - Removed Y-axis inversion (was breaking Zombie Revenge; most DC games expect positive Y = up)
// - Removed analog stick -> D-pad mapping (was triggering D-pad actions in Daytona USA)
// - Added GameCube D-Pad support
// - Added Wii Nunchuck analog stick support
// - Added Wii Nunchuck Z button as Dreamcast L trigger
// - Added Wii Classic Controller support (buttons, analog stick, shoulders)
// - Added Wii U GamePad (DRC) support via libwiidrc (vWii mode, Player 1)

#include "plugins/plugin_header.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiiuse/wpad.h>
#include "plugs/libwiidrc/wiidrc.h" // Wii U GamePad (vWii mode)
#include "dc/dc.h"

// Dreamcast controller button definitions
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
#define key_CONT_DPAD2_UP    (1 << 12)
#define key_CONT_DPAD2_DOWN  (1 << 13)
#define key_CONT_DPAD2_LEFT  (1 << 14)
#define key_CONT_DPAD2_RIGHT (1 << 15)

// Configuration constants
#define MAX_CONTROLLERS 4
#define ANALOG_DEADZONE 20
#define ANALOG_CENTER 128
#define TRIGGER_THRESHOLD 20

// Controller state arrays
u16 kcode[MAX_CONTROLLERS] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
u32 vks[MAX_CONTROLLERS] = {0};
s8 joyx[MAX_CONTROLLERS] = {0};
s8 joyy[MAX_CONTROLLERS] = {0};
u8 rt[MAX_CONTROLLERS] = {0};
u8 lt[MAX_CONTROLLERS] = {0};

/**
 * Clamps analog stick values to valid range and applies dead zone
 * @param value Raw analog stick value
 * @param deadzone Dead zone threshold
 * @return Clamped and scaled value (-128 to 127)
 */
static inline s8 ClampAnalogValue(s32 value, s32 deadzone)
{
    if (abs(value) < deadzone)
        return 0;
    
    // Clamp to valid range
    if (value > 127) value = 127;
    if (value < -128) value = -128;
    
    return (s8)value;
}

/**
 * Maps button state to Dreamcast controller format
 * @param port Controller port (0-3)
 * @param wiiButtons Wii Remote button state
 * @param gcButtons GameCube controller button state
 * @param nunchuckButtons Nunchuck button state
 * @param classicButtons Classic Controller button state (WPAD_CLASSIC_BUTTON_* bits, 0 if not attached)
 */
static void MapButtons(u32 port, u32 wiiButtons, u32 gcButtons, u32 nunchuckButtons, u32 classicButtons)
{
    // Initialize to all buttons released (bits set = released in Dreamcast format)
    kcode[port] = 0xFFFF;

    // Face buttons - A/B/X/Y
    if (wiiButtons & WPAD_BUTTON_A || gcButtons & PAD_BUTTON_A || classicButtons & WPAD_CLASSIC_BUTTON_A)
        kcode[port] &= ~key_CONT_A;

    if (wiiButtons & WPAD_BUTTON_B || gcButtons & PAD_BUTTON_B || classicButtons & WPAD_CLASSIC_BUTTON_B)
        kcode[port] &= ~key_CONT_B;

    if (wiiButtons & WPAD_BUTTON_1 || gcButtons & PAD_BUTTON_Y || classicButtons & WPAD_CLASSIC_BUTTON_Y)
        kcode[port] &= ~key_CONT_Y;

    if (wiiButtons & WPAD_BUTTON_2 || gcButtons & PAD_BUTTON_X || classicButtons & WPAD_CLASSIC_BUTTON_X)
        kcode[port] &= ~key_CONT_X;

    // Start button - HOME on Wiimote, START on GameCube/Classic Controller
    if (wiiButtons & WPAD_BUTTON_HOME || gcButtons & PAD_BUTTON_START || classicButtons & WPAD_CLASSIC_BUTTON_HOME)
        kcode[port] &= ~key_CONT_START;

    // Shoulder buttons - MINUS=L, PLUS=R on Wiimote; L/R on GameCube; Nunchuck Z=L;
    // ZL/L=L, ZR/R=R on Classic Controller
    if (wiiButtons & WPAD_BUTTON_MINUS || gcButtons & PAD_TRIGGER_L || nunchuckButtons & WPAD_NUNCHUK_BUTTON_Z
        || classicButtons & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_FULL_L))
        kcode[port] &= ~key_CONT_D;  // Left trigger

    if (wiiButtons & WPAD_BUTTON_PLUS || gcButtons & PAD_TRIGGER_R
        || classicButtons & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_FULL_R))
        kcode[port] &= ~key_CONT_C;  // Right trigger

    // D-Pad mapping - Wii Remote D-Pad
    if (wiiButtons & WPAD_BUTTON_UP)
        kcode[port] &= ~key_CONT_DPAD_UP;

    if (wiiButtons & WPAD_BUTTON_DOWN)
        kcode[port] &= ~key_CONT_DPAD_DOWN;

    if (wiiButtons & WPAD_BUTTON_LEFT)
        kcode[port] &= ~key_CONT_DPAD_LEFT;

    if (wiiButtons & WPAD_BUTTON_RIGHT)
        kcode[port] &= ~key_CONT_DPAD_RIGHT;

    // D-Pad mapping - GameCube Controller D-Pad
    if (gcButtons & PAD_BUTTON_UP)
        kcode[port] &= ~key_CONT_DPAD_UP;

    if (gcButtons & PAD_BUTTON_DOWN)
        kcode[port] &= ~key_CONT_DPAD_DOWN;

    if (gcButtons & PAD_BUTTON_LEFT)
        kcode[port] &= ~key_CONT_DPAD_LEFT;

    if (gcButtons & PAD_BUTTON_RIGHT)
        kcode[port] &= ~key_CONT_DPAD_RIGHT;

    // D-Pad mapping - Classic Controller D-Pad
    if (classicButtons & WPAD_CLASSIC_BUTTON_UP)
        kcode[port] &= ~key_CONT_DPAD_UP;

    if (classicButtons & WPAD_CLASSIC_BUTTON_DOWN)
        kcode[port] &= ~key_CONT_DPAD_DOWN;

    if (classicButtons & WPAD_CLASSIC_BUTTON_LEFT)
        kcode[port] &= ~key_CONT_DPAD_LEFT;

    if (classicButtons & WPAD_CLASSIC_BUTTON_RIGHT)
        kcode[port] &= ~key_CONT_DPAD_RIGHT;
}

/**
 * Maps analog stick input with dead zone handling
 * @param port Controller port (0-3)
 * @param stickX X-axis value
 * @param stickY Y-axis value (corrected for GameCube inversion)
 * @param expStickX Wiimote expansion (Nunchuck or Classic Controller) X-axis value
 * @param expStickY Wiimote expansion (Nunchuck or Classic Controller) Y-axis value
 */
static void MapAnalogStick(u32 port, s32 stickX, s32 stickY, s32 expStickX, s32 expStickY)
{
    // Prioritize GameCube analog stick, fall back to the Wiimote expansion stick
    s32 finalX = stickX;
    s32 finalY = stickY;

    // If GameCube stick is not being used (near center), use the expansion stick instead
    if (abs(stickX) < ANALOG_DEADZONE && abs(stickY) < ANALOG_DEADZONE)
    {
        finalX = expStickX;
        finalY = expStickY;
    }
    
    // Apply dead zone and clamp values
    joyx[port] = ClampAnalogValue(finalX, ANALOG_DEADZONE);
    // Y-axis is inverted to match Dreamcast controller orientation
    // (GameCube stick positive Y = up, Dreamcast expects positive Y = down)
    joyy[port] = ClampAnalogValue(-finalY, ANALOG_DEADZONE);
    
    // NOTE: Analog stick -> D-pad mapping intentionally removed.
    // It caused analog axes to trigger D-pad inputs in games like Daytona USA
    // (e.g. stick up/down changing camera view instead of accelerating/braking).
    // The physical D-pad remains fully functional via MapButtons().
}

/**
 * Maps trigger inputs from GameCube controller, Wiimote +/-, Nunchuck Z, and Classic Controller ZL/ZR
 * @param port Controller port (0-3)
 * @param wiiButtons Wii Remote button state
 * @param gcButtons GameCube button state
 * @param nunchuckButtons Nunchuck button state
 * @param classicButtons Classic Controller button state (0 if not attached)
 */
static void MapTriggers(u32 port, u32 wiiButtons, u32 gcButtons, u32 nunchuckButtons, u32 classicButtons)
{
    // Map L/R triggers (Dreamcast uses 0-255 range)
    // Note: PAD_TriggerL/R would give analog values, but using digital for simplicity
    // Wiimote PLUS / Nunchuck Z / Classic ZR,ZL mirror the digital C/D bits set in
    // MapButtons(), since some games (e.g. Daytona USA, Chu Chu Rocket) read the
    // analog trigger value instead of the digital button bit.
    rt[port] = (gcButtons & PAD_TRIGGER_R || wiiButtons & WPAD_BUTTON_PLUS
                || classicButtons & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_FULL_R)) ? 255 : 0;
    lt[port] = (gcButtons & PAD_TRIGGER_L || wiiButtons & WPAD_BUTTON_MINUS || nunchuckButtons & WPAD_NUNCHUK_BUTTON_Z
                || classicButtons & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_FULL_L)) ? 255 : 0;
}

/**
 * Checks for exit combination and exits if detected
 * @param wiiButtons Wii Remote button state
 * @param gcButtons GameCube controller button state
 */
static inline void CheckExitCombination(u32 wiiButtons, u32 gcButtons)
{
    // Exit on: MINUS+PLUS (Wiimote) or R+L+Z (GameCube)
    if ((wiiButtons & WPAD_BUTTON_MINUS && wiiButtons & WPAD_BUTTON_PLUS) ||
        (gcButtons & PAD_TRIGGER_R && gcButtons & PAD_TRIGGER_L && gcButtons & PAD_TRIGGER_Z))
    {
        exit(0);
    }
}

/**
 * Updates the input state for a specific controller port
 * Reads from Wii Remote (+ Nunchuck/Classic Controller expansion) and GameCube
 * controller, and maps everything to Dreamcast format.
 * @param port Controller port (0-3)
 */
void UpdateInputState(u32 port)
{
    // Validate port number
    if (port >= MAX_CONTROLLERS)
        return;

    // Scan for new controller input
    PAD_ScanPads();
    WPAD_ScanPads();

    // Read current button states
    u32 wiiButtons = WPAD_ButtonsHeld(port);
    u32 gcButtons = PAD_ButtonsHeld(port);

    // Read Wiimote expansion device state (Nunchuck or Classic Controller;
    // the two are mutually exclusive, only one can be plugged in at a time)
    WPADData *wpadData = WPAD_Data(port);
    u32 nunchuckButtons = 0;
    u32 classicButtons = 0;
    s32 expStickX = 0;
    s32 expStickY = 0;

    if (wpadData && wpadData->exp.type == WPAD_EXP_NUNCHUK)
    {
        nunchuckButtons = wpadData->exp.nunchuk.btns;
        // Scale Nunchuck joystick (0-255 range) to match GameCube (-128 to 127)
        expStickX = (s32)(wpadData->exp.nunchuk.js.pos.x) - 128;
        expStickY = (s32)(wpadData->exp.nunchuk.js.pos.y) - 128;
    }
    else if (wpadData && wpadData->exp.type == WPAD_EXP_CLASSIC)
    {
        // WPAD_CLASSIC_BUTTON_* bits alias WPAD_NUNCHUK_BUTTON_* bits within
        // wiiButtons (both expansions share the upper 16 bits of the word),
        // so only treat them as Classic Controller buttons once exp.type
        // confirms a Classic Controller is actually attached.
        classicButtons = wiiButtons & 0xFFFF0000;
        // Scale Classic Controller left stick (0-255 range) to match GameCube (-128 to 127)
        expStickX = (s32)(wpadData->exp.classic.ljs.pos.x) - 128;
        expStickY = (s32)(wpadData->exp.classic.ljs.pos.y) - 128;
    }

    // Wii U GamePad (vWii mode) — drives Player 1 only. Its layout matches
    // the Classic Controller, so translate WIIDRC_BUTTON_* bits to
    // WPAD_CLASSIC_BUTTON_* and let the existing mapping logic handle it.
    if (port == 0 && WiiDRC_Inited())
    {
        WiiDRC_ScanPads();
        u32 drc = WiiDRC_ButtonsHeld();

        if (drc & WIIDRC_BUTTON_A)     classicButtons |= WPAD_CLASSIC_BUTTON_A;
        if (drc & WIIDRC_BUTTON_B)     classicButtons |= WPAD_CLASSIC_BUTTON_B;
        if (drc & WIIDRC_BUTTON_X)     classicButtons |= WPAD_CLASSIC_BUTTON_X;
        if (drc & WIIDRC_BUTTON_Y)     classicButtons |= WPAD_CLASSIC_BUTTON_Y;
        if (drc & WIIDRC_BUTTON_PLUS)  classicButtons |= WPAD_CLASSIC_BUTTON_HOME; // Start
        if (drc & WIIDRC_BUTTON_UP)    classicButtons |= WPAD_CLASSIC_BUTTON_UP;
        if (drc & WIIDRC_BUTTON_DOWN)  classicButtons |= WPAD_CLASSIC_BUTTON_DOWN;
        if (drc & WIIDRC_BUTTON_LEFT)  classicButtons |= WPAD_CLASSIC_BUTTON_LEFT;
        if (drc & WIIDRC_BUTTON_RIGHT) classicButtons |= WPAD_CLASSIC_BUTTON_RIGHT;
        if (drc & (WIIDRC_BUTTON_L | WIIDRC_BUTTON_ZL))
            classicButtons |= WPAD_CLASSIC_BUTTON_FULL_L;
        if (drc & (WIIDRC_BUTTON_R | WIIDRC_BUTTON_ZR))
            classicButtons |= WPAD_CLASSIC_BUTTON_FULL_R;

        // Left stick: calibrated raw range is only about +/-72, so scale
        // towards +/-127 (ClampAnalogValue clamps any overshoot). Only used
        // when no Wiimote expansion stick is deflected.
        if (expStickX == 0 && expStickY == 0)
        {
            expStickX = ((s32)WiiDRC_lStickX() * 7) / 4;
            expStickY = ((s32)WiiDRC_lStickY() * 7) / 4;
        }

        // Exit combo: MINUS+PLUS (same as Wiimote); GamePad power button
        // (shutdown request from IOS) also exits.
        if (((drc & WIIDRC_BUTTON_MINUS) && (drc & WIIDRC_BUTTON_PLUS)) ||
            WiiDRC_ShutdownRequested())
        {
            exit(0);
        }
    }

    // Read GameCube analog stick position
    s32 stickX = PAD_StickX(port);
    s32 stickY = PAD_StickY(port);

    // Check for exit combination
    CheckExitCombination(wiiButtons, gcButtons);

    // Map all inputs to Dreamcast controller format
    MapButtons(port, wiiButtons, gcButtons, nunchuckButtons, classicButtons);
    MapAnalogStick(port, stickX, stickY, expStickX, expStickY);
    MapTriggers(port, wiiButtons, gcButtons, nunchuckButtons, classicButtons);
}

/**
 * Initializes all controller ports to default state
 */
void InitControllers(void)
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        kcode[i] = 0xFFFF;  // All buttons released
        vks[i] = 0;
        joyx[i] = 0;
        joyy[i] = 0;
        rt[i] = 0;
        lt[i] = 0;
    }
}
