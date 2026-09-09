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
// - Classic Controller: face buttons remapped by physical position, PLUS = Start
//   (HOME kept as Start too), left stick fixed (raw 6-bit range, was read as 0-255)
// - Added Sixaxis/DualShock3 (USB) support via libsicksaxis, one pad per port

#include "plugins/plugin_header.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include "plugs/libwiidrc/wiidrc.h" // Wii U GamePad (vWii mode)
#include "plugs/libsicksaxis/libsicksaxis/sicksaxis.h" // Sixaxis/DualShock3 (USB)
#include "dc/dc.h"

// Special controller layout preset, set from the options menu / game presets
// (see main.cpp g_special_layout_preset). Values must stay in sync with the
// SPECIAL_LAYOUT_* enum in main.cpp.
extern "C" int get_special_layout_preset(void);
#define SPECIAL_LAYOUT_OFF        0
#define SPECIAL_LAYOUT_CHUCHU     1
#define SPECIAL_LAYOUT_DDR_SELECT 2
#define SPECIAL_LAYOUT_USER_CFG   3

#include "plugs/drkMapleDevices/dc_pad_bits.h" // Dreamcast controller button definitions
#include "wii/user_controls.h" // USER CFG special layout (fully user-remappable)
#include "wii/wii_exit.h"     // ordered shutdown for the exit combo (EXIT FIX preset)

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
 * Scales a Classic Controller stick axis to the GameCube-style -128..127 range
 * using the calibration data wiiuse gathered during the expansion handshake.
 * The Classic Controller left stick reports raw 6-bit values (0-63, center ~32),
 * unlike the Nunchuck's 0-255 range, so it needs its own scaling.
 * @param pos Raw axis position
 * @param min Calibrated minimum for this axis
 * @param center Calibrated center for this axis
 * @param max Calibrated maximum for this axis
 * @return Scaled value (-128 to 127, 0 = centered)
 */
static s32 ScaleClassicStick(s32 pos, s32 min, s32 center, s32 max)
{
    // Some third-party controllers report garbage calibration; fall back to
    // the nominal 6-bit range in that case.
    if (min >= center || center >= max)
    {
        min = 0;
        center = 32;
        max = 63;
    }

    if (pos >= center)
        return ((pos - center) * 127) / (max - center);
    else
        return ((pos - center) * 128) / (center - min);
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

    int specialLayout = get_special_layout_preset();

    // Classic Controller face buttons are always mapped by physical position,
    // not label: Classic B (bottom) = DC A, Classic A (right) = DC B,
    // Classic Y (left) = DC X, Classic X (top) = DC Y. This already matches
    // the ChuChu Rocket layout below, so it needs no per-layout branch.
    if (classicButtons & WPAD_CLASSIC_BUTTON_B)
        kcode[port] &= ~key_CONT_A;

    if (classicButtons & WPAD_CLASSIC_BUTTON_A)
        kcode[port] &= ~key_CONT_B;

    if (classicButtons & WPAD_CLASSIC_BUTTON_X)
        kcode[port] &= ~key_CONT_Y;

    if (classicButtons & WPAD_CLASSIC_BUTTON_Y)
        kcode[port] &= ~key_CONT_X;

    if (specialLayout == SPECIAL_LAYOUT_CHUCHU)
    {
        // ChuChu Rocket layout: the Wiimote D-Pad drives DC A/B/X/Y directly
        // (the DC D-Pad itself is unassigned in this mode — see below), and
        // the GameCube face buttons are rotated one step.
        if (wiiButtons & WPAD_BUTTON_DOWN)
            kcode[port] &= ~key_CONT_A;

        if (wiiButtons & WPAD_BUTTON_RIGHT)
            kcode[port] &= ~key_CONT_B;

        if (wiiButtons & WPAD_BUTTON_LEFT)
            kcode[port] &= ~key_CONT_X;

        if (wiiButtons & WPAD_BUTTON_UP)
            kcode[port] &= ~key_CONT_Y;

        if (gcButtons & PAD_BUTTON_A)
            kcode[port] &= ~key_CONT_A;

        if (gcButtons & PAD_BUTTON_X)
            kcode[port] &= ~key_CONT_B;

        if (gcButtons & PAD_BUTTON_B)
            kcode[port] &= ~key_CONT_X;

        if (gcButtons & PAD_BUTTON_Y)
            kcode[port] &= ~key_CONT_Y;
    }
    else
    {
        // Face buttons - A/B/X/Y (legacy/default mapping)
        if (wiiButtons & WPAD_BUTTON_A || gcButtons & PAD_BUTTON_A)
            kcode[port] &= ~key_CONT_A;

        if (wiiButtons & WPAD_BUTTON_B || gcButtons & PAD_BUTTON_B)
            kcode[port] &= ~key_CONT_B;

        if (wiiButtons & WPAD_BUTTON_1 || gcButtons & PAD_BUTTON_Y)
            kcode[port] &= ~key_CONT_Y;

        if (wiiButtons & WPAD_BUTTON_2 || gcButtons & PAD_BUTTON_X)
            kcode[port] &= ~key_CONT_X;
    }

    // Start button - HOME on Wiimote, START on GameCube, PLUS or HOME on Classic Controller
    if (wiiButtons & WPAD_BUTTON_HOME || gcButtons & PAD_BUTTON_START
        || classicButtons & (WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_HOME))
        kcode[port] &= ~key_CONT_START;

    // Shoulder buttons - MINUS=L, PLUS=R on Wiimote; L/R on GameCube; Nunchuck Z=L;
    // ZL/L=L, ZR/R=R on Classic Controller
    if (wiiButtons & WPAD_BUTTON_MINUS || gcButtons & PAD_TRIGGER_L || nunchuckButtons & WPAD_NUNCHUK_BUTTON_Z
        || classicButtons & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_FULL_L))
        kcode[port] &= ~key_CONT_D;  // Left trigger

    if (wiiButtons & WPAD_BUTTON_PLUS || gcButtons & PAD_TRIGGER_R
        || classicButtons & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_FULL_R))
        kcode[port] &= ~key_CONT_C;  // Right trigger

    // D-Pad mapping - Wii Remote D-Pad. Unassigned under the ChuChu Rocket
    // layout, where these same physical presses drive the face buttons above
    // instead (see specialLayout branch).
    if (specialLayout != SPECIAL_LAYOUT_CHUCHU)
    {
        if (wiiButtons & WPAD_BUTTON_UP)
            kcode[port] &= ~key_CONT_DPAD_UP;

        if (wiiButtons & WPAD_BUTTON_DOWN)
            kcode[port] &= ~key_CONT_DPAD_DOWN;

        if (wiiButtons & WPAD_BUTTON_LEFT)
            kcode[port] &= ~key_CONT_DPAD_LEFT;

        if (wiiButtons & WPAD_BUTTON_RIGHT)
            kcode[port] &= ~key_CONT_DPAD_RIGHT;
    }

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
 * @param classicButtons Classic Controller button state (0 if not attached)
 */
static inline void CheckExitCombination(u32 wiiButtons, u32 gcButtons, u32 classicButtons)
{
    // Exit on: MINUS+PLUS (Wiimote or Classic Controller) or R+L+Z (GameCube)
    if ((wiiButtons & WPAD_BUTTON_MINUS && wiiButtons & WPAD_BUTTON_PLUS) ||
        ((classicButtons & WPAD_CLASSIC_BUTTON_MINUS) && (classicButtons & WPAD_CLASSIC_BUTTON_PLUS)) ||
        (gcButtons & PAD_TRIGGER_R && gcButtons & PAD_TRIGGER_L && gcButtons & PAD_TRIGGER_Z))
    {
        WiiExitToLoader();
    }
}

// ============================================================================
// HOST-SIDE EXIT COMBO POLL  (EXIT FIX = 3)
// ============================================================================
//
// CheckExitCombination() above only ever runs when the GUEST reads the pad:
// it hangs off the Maple DMA the Dreamcast game issues. A game that stops
// polling Maple - crashed, stuck in a boot loop, waiting on something that
// never arrives - therefore cannot be exited at all, even though the
// emulator itself is still running perfectly well.
//
// This is the same test driven from the HOST side instead: SPG.cpp calls it
// off the emulated scanline counter, which advances for as long as the SH4
// core executes any code at all, whatever that code happens to be doing.
// Deliberately hung off the scanline tick and not the vblank event, because
// a game that programs SPG_VBLANK.vbstart past the counter wrap never fires
// a vblank event either (see the [SPG] notes in SPG.cpp).
//
// Only the hardcoded combos are tested, never the USER CFG remap - exactly
// like CheckExitCombination(), so this can never be the thing that locks a
// player out. Held state only, no ButtonsDown(), so the extra ScanPads()
// cannot steal an edge from the Maple path.
extern "C" void ExitCombo_HostPoll(void)
{
    PAD_ScanPads();
    WPAD_ScanPads();

    u32 wiiButtons     = 0;
    u32 gcButtons      = 0;
    u32 classicButtons = 0;

    for (int port = 0; port < MAX_CONTROLLERS; port++)
    {
        u32 held = WPAD_ButtonsHeld(port);
        wiiButtons |= held;
        gcButtons  |= PAD_ButtonsHeld(port);

        // Same derivation UpdateInputState() uses: the Classic Controller bits
        // live in the upper half of the WPAD word and alias the Nunchuck's, so
        // they only count once exp.type confirms a Classic is attached.
        WPADData *wpadData = WPAD_Data(port);
        if (wpadData && wpadData->exp.type == WPAD_EXP_CLASSIC)
            classicButtons |= held & 0xFFFF0000;
    }

    // Wii U GamePad (vWii): MINUS+PLUS, or the GamePad power button.
    if (WiiDRC_Inited())
    {
        WiiDRC_ScanPads();
        u32 drc = WiiDRC_ButtonsHeld();
        if (((drc & WIIDRC_BUTTON_MINUS) && (drc & WIIDRC_BUTTON_PLUS)) ||
            WiiDRC_ShutdownRequested())
        {
            WiiExitToLoader();
        }
    }

    CheckExitCombination(wiiButtons, gcButtons, classicButtons);
}

// ============================================================================
// SIXAXIS / DUALSHOCK3 (USB) SUPPORT
// ============================================================================
//
// A PS3 Sixaxis/DualShock3 connected via a Wii USB port is read through
// libsicksaxis (raw USB HID control transfers). This needs IOS58 (Nintendo's
// own "USB2" IOS, present on every retail Wii) reloaded once at boot, before
// WPAD_Init() — see SS_Init(), called from wii/main.cpp. Buttons/stick are
// translated to the WPAD_CLASSIC_BUTTON_* convention (same trick used for
// the Wii U GamePad above), so the existing mapping logic handles them for
// free. Unlike the GamePad (Player 1 only), each connected pad drives its
// own matching port, up to MAX_CONTROLLERS.
// ============================================================================

static struct ss_device s_ssDev[MAX_CONTROLLERS];
static bool s_ssInited = false;

// GameCube button that must be held at boot to switch Sixaxis/DS3 support on.
// Any of the four ports counts.
//
// It is deliberately a GameCube button and not a Wiimote one: the GameCube
// ports hang off the PowerPC's Serial Interface, which is real hardware on
// our side of the machine and owned by nobody else, so it can be read before
// IOS is up and is completely unaffected by the reload below. A Wiimote
// cannot work here -- reading one needs the very Bluetooth stack we are
// deciding whether to destroy, and at this point in boot no Wiimote has
// connected yet, so there would be nothing to read.
#define SS_BOOT_GATE_BUTTON PAD_BUTTON_B

static bool SS_BootGateHeld(void)
{
    // PAD_Init() has already run (wii/main.cpp), but the Serial Interface
    // needs a few poll cycles before PAD_ButtonsHeld() reports anything, and
    // there is no video retrace to wait on this early. ~200 ms of short
    // sleeps covers that and doubles as the window the player is holding the
    // button through.
    u32 held = 0;

    for (int i = 0; i < 10; i++)
    {
        usleep(20 * 1000);
        PAD_ScanPads();
        for (int port = 0; port < MAX_CONTROLLERS; port++)
            held |= PAD_ButtonsHeld(port);
    }

    return (held & SS_BOOT_GATE_BUTTON) != 0;
}

/**
 * One-time boot init, opt-in: with the gate button held it makes sure we are
 * on IOS58, brings up the USB stack and readies one ss_device slot per
 * controller port; with nothing held it does nothing at all and leaves IOS
 * (and therefore Bluetooth) untouched. Called from wii/main.cpp after
 * PAD_Init() and before WPAD_Init().
 */
void SS_Init()
{
    // Sixaxis/DS3 support is opt-in, because switching it on costs everyone
    // else something. Raw USB HID access to the pad needs IOS58, and
    // IOS_ReloadIOS() restarts the whole I/O OS - Bluetooth stack included -
    // so every connected Wiimote drops its link and has to re-establish it
    // while WPAD_Init() is already running a few milliseconds later. That
    // race is what made the Wiimote intermittently refuse to connect, in the
    // menus and in game. Holding the gate button is the player saying "I want
    // the DS3 and I accept the reload"; with nothing held, IOS is left exactly
    // as the loader handed it over and Bluetooth is never touched.
    if (!SS_BootGateHeld())
    {
        printf("[sixaxis] gate button not held at boot - DS3 support off, IOS left alone\n");
        return; // s_ssInited stays false: every SS_* entry point below no-ops
    }

    // The Homebrew Channel normally launches us under IOS58 already, in which
    // case the reload would drop Bluetooth for nothing. Only pay for it when
    // we are genuinely on another IOS, and then give the rebuilt stack real
    // time to come back up before WPAD_Init() touches it.
    if (IOS_GetVersion() != 58)
    {
        IOS_ReloadIOS(58);
        usleep(500 * 1000);
    }

    USB_Initialize();
    ss_init();
    for (int i = 0; i < MAX_CONTROLLERS; i++)
        ss_initialize(&s_ssDev[i]);
    s_ssInited = true;
    printf("[sixaxis] enabled, running on IOS%d\n", (int)IOS_GetVersion());
}

/**
 * Closes every pad libsicksaxis still has open. Called from the exit path
 * (wii/wii_exit.cpp) so no USB HID device handle is left dangling in IOS when
 * libogc's __IOS_ShutdownSubsystems() runs. No-op when SS_Init() never armed.
 */
extern "C" void SS_Term(void)
{
    if (!s_ssInited)
        return;

    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (ss_is_connected(&s_ssDev[i]))
            ss_close(&s_ssDev[i]);
    }
    s_ssInited = false;
}

/**
 * True once SS_Init() actually brought the USB stack up, i.e. the boot gate
 * button was held. Lets the CONTROLS menu tell "no pad plugged in" apart from
 * "the feature was never switched on this boot".
 */
int SS_IsEnabled(void)
{
    return s_ssInited ? 1 : 0;
}

// How many calls apart two USB enumerations may be. Every caller runs at
// frame rate, so this works out to about one enumeration per second - still
// prompt enough that plugging a pad in gets noticed while a menu is up.
#define SS_POLL_INTERVAL 60

static unsigned s_ssPollTick = 0;

/**
 * Claims any newly-plugged Sixaxis/DS3 and starts its async report reading.
 * Idempotent and safe to call every frame/menu tick — it throttles its own
 * USB enumeration, see below.
 */
void SS_PollConnections(void)
{
    if (!s_ssInited)
        return;

    // ss_open() is not cheap: each call runs a full USB_GetDeviceList(), a
    // synchronous IOS IPC round-trip, whether or not a pad is there. This
    // function is called once per frame from every menu loop and once per
    // emulated frame in game, so scanning all four slots unthrottled meant
    // ~240 enumerations a second contending with the Bluetooth module for the
    // same IOS - which shows up as the Wiimote dropping or refusing to
    // connect. Enumerate about once a second instead.
    if (s_ssPollTick++ % SS_POLL_INTERVAL != 0)
        return;

    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (ss_is_connected(&s_ssDev[i]))
            continue;

        // Every slot scans the one shared device list, so a slot that finds
        // no unclaimed pad means the slots after it will not find one either.
        if (ss_open(&s_ssDev[i]) <= 0)
            break;

        ss_start_reading(&s_ssDev[i]);
    }
}

/**
 * Translates a connected pad's current report to WPAD_CLASSIC_BUTTON_* bits.
 * @param port Controller port (0 to MAX_CONTROLLERS-1)
 */
static u32 SS_ToClassicButtons(int port)
{
    if (!s_ssInited || !ss_is_connected(&s_ssDev[port]))
        return 0;

    const struct SS_BUTTONS *b = &s_ssDev[port].pad.buttons;
    u32 w = 0;

    if (b->up)       w |= WPAD_CLASSIC_BUTTON_UP;
    if (b->down)     w |= WPAD_CLASSIC_BUTTON_DOWN;
    if (b->left)     w |= WPAD_CLASSIC_BUTTON_LEFT;
    if (b->right)    w |= WPAD_CLASSIC_BUTTON_RIGHT;
    if (b->cross)    w |= WPAD_CLASSIC_BUTTON_B;
    if (b->circle)   w |= WPAD_CLASSIC_BUTTON_A;
    if (b->square)   w |= WPAD_CLASSIC_BUTTON_Y;
    if (b->triangle) w |= WPAD_CLASSIC_BUTTON_X;
    if (b->start)    w |= WPAD_CLASSIC_BUTTON_PLUS;
    if (b->select)   w |= WPAD_CLASSIC_BUTTON_MINUS;
    if (b->PS)       w |= WPAD_CLASSIC_BUTTON_HOME;
    if (b->L1 || b->L2) w |= WPAD_CLASSIC_BUTTON_FULL_L;
    if (b->R1 || b->R2) w |= WPAD_CLASSIC_BUTTON_FULL_R;

    return w;
}

/**
 * Buttons held across every connected pad, OR'd together. Used by
 * wii/main.cpp's menu-navigation helper (mirrors DRC_ButtonsHeldWPAD).
 */
u32 SS_GetClassicButtonsHeld(void)
{
    u32 held = 0;
    for (int i = 0; i < MAX_CONTROLLERS; i++)
        held |= SS_ToClassicButtons(i);
    return held;
}

/**
 * Number of currently-connected Sixaxis/DS3 pads (for the CONTROLS menu
 * status line).
 */
int SS_ConnectedCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_CONTROLLERS; i++)
        if (ss_is_connected(&s_ssDev[i]))
            count++;
    return count;
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
        // Classic Controller left stick is raw 6-bit (0-63, center ~32), NOT
        // 0-255 like the Nunchuck. Scale it via its calibration data.
        joystick_t *ljs = &wpadData->exp.classic.ljs;
        expStickX = ScaleClassicStick(ljs->pos.x, ljs->min.x, ljs->center.x, ljs->max.x);
        expStickY = ScaleClassicStick(ljs->pos.y, ljs->min.y, ljs->center.y, ljs->max.y);
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
        if (drc & WIIDRC_BUTTON_PLUS)  classicButtons |= WPAD_CLASSIC_BUTTON_PLUS; // Start
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
            WiiExitToLoader();
        }
    }

    // Sixaxis/DualShock3 (USB) — unlike the GamePad above, each connected
    // pad drives its own matching port rather than only Player 1. The
    // actual USB device-list scan (picking up newly-plugged pads) only
    // needs to run once per frame, so it piggybacks on port 0's call.
    if (s_ssInited)
    {
        if (port == 0)
            SS_PollConnections();

        classicButtons |= SS_ToClassicButtons(port);

        // Left stick (0-255, center ~128). Note: USB HID gives Y raw with
        // 0 = up / 255 = down, the opposite of the Nunchuck/GameCube
        // convention (positive = up) that MapAnalogStick() expects, so Y
        // is negated here to match — otherwise up/down come out swapped.
        // Only used when no Wiimote expansion stick is deflected.
        if (ss_is_connected(&s_ssDev[port]) && expStickX == 0 && expStickY == 0)
        {
            expStickX = (s32)s_ssDev[port].pad.left_analog.x - 128;
            expStickY = -((s32)s_ssDev[port].pad.left_analog.y - 128);
        }
    }

    // Read GameCube analog stick position (main stick + C-stick, the latter
    // only consumed by the USER CFG layout below as extra digital sources)
    s32 stickX = PAD_StickX(port);
    s32 stickY = PAD_StickY(port);
    s32 subStickX = PAD_SubStickX(port);
    s32 subStickY = PAD_SubStickY(port);

    // Check for exit combination (always live, regardless of layout, so a
    // typo in user_controls.cfg can never lock a player out of the menu)
    CheckExitCombination(wiiButtons, gcButtons, classicButtons);

    int specialLayout = get_special_layout_preset();

    // USER CFG special layout: the whole button/stick/trigger mapping comes
    // from user_controls.cfg instead of the hardcoded logic below (see
    // wii/user_controls.cpp). Falls through to the legacy mapping if the
    // file was never found/loaded, so picking this layout is never a dead end.
    if (specialLayout == SPECIAL_LAYOUT_USER_CFG && user_controls_loaded())
    {
        if (UserControls_CheckExitCombo(wiiButtons, gcButtons, nunchuckButtons, classicButtons))
            WiiExitToLoader();

        UserControls_Update(wiiButtons, gcButtons, nunchuckButtons, classicButtons,
                             stickX, stickY, subStickX, subStickY, expStickX, expStickY,
                             &kcode[port], &joyx[port], &joyy[port], &lt[port], &rt[port]);
        return;
    }

    // Map all inputs to Dreamcast controller format
    MapButtons(port, wiiButtons, gcButtons, nunchuckButtons, classicButtons);
    MapAnalogStick(port, stickX, stickY, expStickX, expStickY);
    MapTriggers(port, wiiButtons, gcButtons, nunchuckButtons, classicButtons);

    // DDR Club Mix / 2nd Mix special layout: these games read the analog
    // stick pushed fully down as a "Select" input (used to change dancer/
    // arrow skin/sequence type and to enter the Left, Right and Shuffle
    // arrow option codes). That's awkward to hit reliably on a GameCube
    // stick, so holding the GameCube Z button (PAD_TRIGGER_Z -- libogc's
    // only macro for it, despite the name; otherwise unused by this
    // emulator's normal mapping) forces the same "down" reading.
    if (specialLayout == SPECIAL_LAYOUT_DDR_SELECT && (gcButtons & PAD_TRIGGER_Z))
    {
        joyx[port] = 0;
        joyy[port] = 127; // DC positive Y = down, see MapAnalogStick()
    }
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
