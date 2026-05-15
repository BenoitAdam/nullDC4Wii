// drkMapleFishingRod.cpp
// Dreamcast fishing rod controller emulation for "Sega Bass Fishing" (and "Get Bass").
//
// Supported games:
//   - Sega Bass Fishing / Get Bass
//   - Bass Fishing 2 (Japan)
//
// Dreamcast Fishing Controller ("Fishing Rod"):
//   The DC fishing controller is a two-part device:
//     - A "reel" unit: analog stick (lure direction), reel button (crank),
//       cast button (B), start, and a tension button.
//     - The rod tip position is encoded as an analog axis (tilt).
//   Maple reports:
//     kcode  : buttons (cast, reel-in, start, d-pad)
//     joyx   : rod tilt left/right   (-128 to 127)
//     joyy   : rod tilt up/down      (-128 to 127)
//     rt     : reel speed            (0 = slow, 255 = fast crank)
//     lt     : cast tension          (0-255, how far rod is pulled back)
//
// Wii mapping:
//   Wiimote held horizontally (NES-style) with IR disabled.
//   Motion is used for rod movements.
//
//   Cast (pull back + flick forward):
//     State machine: IDLE -> PULLBACK -> CAST
//     Detected by accelerometer: pull back (acc_z negative), then fast forward (acc_z positive spike).
//
//   Reel-in:
//     Rotate Wiimote around its long axis (roll) -> reel speed.
//     Detected from acc_x angular change.
//
//   Rod tip direction (lure aim):
//     Roll/pitch of Wiimote maps to joyx/joyy.
//
//   Buttons:
//     B      -> Cast button (also used to confirm cast in state machine)
//     A      -> Lock reel / action
//     PLUS   -> Start
//     MINUS  -> Pause
//     D-pad  -> Menu navigation
//     MINUS+PLUS -> Exit emulator
//
//   Nunchuck (optional):
//     If Nunchuck attached: analog stick controls lure direction more precisely.
//     Nunchuck Z = secondary action button.
//
// 2-player:
//   Port 0 = Wiimote 0  (Player 1)
//   Port 1 = Wiimote 1  (Player 2)
//
// Player count is read from get_player_count() defined in main.cpp.

#include "plugins/plugin_header.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <wiiuse/wpad.h>
#include "dc/dc.h"

// Dreamcast button definitions
#define key_CONT_C          (1 << 0)
#define key_CONT_B          (1 << 1)
#define key_CONT_A          (1 << 2)
#define key_CONT_START      (1 << 3)
#define key_CONT_DPAD_UP    (1 << 4)
#define key_CONT_DPAD_DOWN  (1 << 5)
#define key_CONT_DPAD_LEFT  (1 << 6)
#define key_CONT_DPAD_RIGHT (1 << 7)
#define key_CONT_Z          (1 << 8)
#define key_CONT_D          (1 << 11)

// Controller state arrays (defined in drkMapleDevices.cpp)
extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4];
extern s8  joyy[4];
extern u8  rt[4];
extern u8  lt[4];

extern "C" int get_player_count(void);

// ============================================================================
// Cast state machine
// ============================================================================
typedef enum
{
    CAST_IDLE     = 0,  // Waiting for player to start cast
    CAST_PULLBACK = 1,  // Rod is being pulled back
    CAST_THROWING = 2,  // Forward cast in progress
    CAST_HOLD     = 3,  // Cast button reported, latch for a few frames
} CastState;

#define MAX_FISHING_PLAYERS 2
#define CAST_HOLD_FRAMES    8   // Number of frames to hold the cast signal

// Per-port state
static CastState cast_state[MAX_FISHING_PLAYERS]     = {CAST_IDLE, CAST_IDLE};
static int       cast_hold_timer[MAX_FISHING_PLAYERS] = {0, 0};

// Thresholds (in raw libogc accelerometer units, roughly)
#define PULLBACK_THRESH   -150   // acc_z below this -> pulling back
#define CAST_THROW_THRESH  250   // acc_z above this -> forward throw
#define REEL_DELTA_THRESH   80   // acc_x delta to count as a reel crank

// Previous accelerometer values for delta computation
static s16 prev_acc_x[MAX_FISHING_PLAYERS] = {0};
static s16 prev_acc_z[MAX_FISHING_PLAYERS] = {0};

// Reel speed accumulator: decays each frame, spikes on crank detection
static u8 reel_speed[MAX_FISHING_PLAYERS] = {0};
#define REEL_DECAY 20   // How much reel speed decays per frame (towards 0)
#define REEL_SPIKE 80   // Reel speed added on each detected crank motion

/**
 * Clamps a value to the s8 range.
 */
static inline s8 ClampS8(s32 v)
{
    if (v >  127) return  127;
    if (v < -128) return -128;
    return (s8)v;
}

/**
 * Maps the Wiimote orientation to rod tip direction.
 * Held horizontally: acc_y gives left/right tilt, acc_x gives up/down tilt.
 *
 * @param ax, ay, az  Raw accelerometer values
 * @param outX        Rod horizontal position (-128 to 127)
 * @param outY        Rod vertical position   (-128 to 127)
 */
static void ComputeRodDirection(s16 ax, s16 ay, s16 az, s8 *outX, s8 *outY)
{
    // Scale from ~[-512,512] range to [-128,127]
    s32 scaledX =  (s32)ay / 4;  // left/right tilt
    s32 scaledY = -(s32)ax / 4;  // up/down tilt (invert so up = positive)

    *outX = ClampS8(scaledX);
    *outY = ClampS8(scaledY);
}

/**
 * Updates fishing rod input state for one player port.
 *
 * @param port  DC controller port (0 or 1)
 */
void UpdateFishingRodState(u32 port)
{
    if (port >= MAX_FISHING_PLAYERS || port >= (u32)get_player_count())
        return;

    WPAD_ScanPads();

    WPADData *data    = WPAD_Data(port);
    u32 wiiButtons    = WPAD_ButtonsHeld(port);

    // Reset buttons
    kcode[port] = 0xFFFF;

    // --- Accelerometer ---
    s16 ax = 0, ay = 0, az = 0;
    if (data)
    {
        ax = (s16)data->accel.x;
        ay = (s16)data->accel.y;
        az = (s16)data->accel.z;
    }

    // --- Cast state machine ---
    switch (cast_state[port])
    {
        case CAST_IDLE:
            // Detect start of pullback: acc_z goes very negative
            // OR player presses B to manually trigger a cast
            if (az < PULLBACK_THRESH)
            {
                cast_state[port] = CAST_PULLBACK;
                // lt = how far back we are (tension: higher = further back)
                s32 tension = (-(s32)az - (-PULLBACK_THRESH));
                if (tension < 0)   tension = 0;
                if (tension > 255) tension = 255;
                lt[port] = (u8)tension;
            }
            else
            {
                lt[port] = 0;
            }
            break;

        case CAST_PULLBACK:
            // Update tension
            {
                s32 tension = (-(s32)az);
                if (tension < 0)   tension = 0;
                if (tension > 255) tension = 255;
                lt[port] = (u8)tension;
            }
            // Detect forward throw
            if (az > CAST_THROW_THRESH)
            {
                cast_state[port]     = CAST_HOLD;
                cast_hold_timer[port] = CAST_HOLD_FRAMES;
                lt[port]             = 0;
            }
            // If acc_z returns to idle, abort (player put rod down)
            if (az > -50)
            {
                cast_state[port] = CAST_IDLE;
                lt[port]         = 0;
            }
            break;

        case CAST_HOLD:
            // Hold the "cast" signal for a few frames so Maple layer reads it
            kcode[port] &= ~key_CONT_B;  // B = Cast button on DC fishing rod
            cast_hold_timer[port]--;
            if (cast_hold_timer[port] <= 0)
                cast_state[port] = CAST_IDLE;
            break;

        default:
            cast_state[port] = CAST_IDLE;
            break;
    }

    // Manual B press also casts (fallback for players who prefer button)
    if (wiiButtons & WPAD_BUTTON_B)
        kcode[port] &= ~key_CONT_B;

    // --- Reel speed detection ---
    // Detect rotational movement on the Wiimote's long axis (roll)
    // by looking at acc_x delta between frames.
    s32 deltaX = (s32)ax - (s32)prev_acc_x[port];
    if (abs(deltaX) > REEL_DELTA_THRESH)
    {
        reel_speed[port] = (u8)((reel_speed[port] + REEL_SPIKE > 255) ? 255 : reel_speed[port] + REEL_SPIKE);
    }
    else
    {
        // Decay towards 0
        reel_speed[port] = (u8)((reel_speed[port] > REEL_DECAY) ? reel_speed[port] - REEL_DECAY : 0);
    }
    rt[port] = reel_speed[port];

    // A button = lock reel / action
    if (wiiButtons & WPAD_BUTTON_A)
        kcode[port] &= ~key_CONT_A;

    // --- Nunchuck support ---
    if (data && data->exp.type == WPAD_EXP_NUNCHUK)
    {
        // Nunchuck stick overrides accelerometer-based direction for finer control
        s32 nx = (s32)data->exp.nunchuk.js.pos.x - 128;
        s32 ny = (s32)data->exp.nunchuk.js.pos.y - 128;

        joyx[port] = ClampS8(nx);
        joyy[port] = ClampS8(-ny);  // Invert Y: up = positive on DC

        // Nunchuck Z = secondary action (map to DC Z button)
        if (data->exp.nunchuk.btns & WPAD_NUNCHUK_BUTTON_Z)
            kcode[port] &= ~key_CONT_Z;
    }
    else
    {
        // No nunchuck: derive direction from Wiimote accelerometer
        s8 rodX, rodY;
        ComputeRodDirection(ax, ay, az, &rodX, &rodY);
        joyx[port] = rodX;
        joyy[port] = rodY;
    }

    // --- Standard navigation buttons ---
    if (wiiButtons & WPAD_BUTTON_UP)    kcode[port] &= ~key_CONT_DPAD_UP;
    if (wiiButtons & WPAD_BUTTON_DOWN)  kcode[port] &= ~key_CONT_DPAD_DOWN;
    if (wiiButtons & WPAD_BUTTON_LEFT)  kcode[port] &= ~key_CONT_DPAD_LEFT;
    if (wiiButtons & WPAD_BUTTON_RIGHT) kcode[port] &= ~key_CONT_DPAD_RIGHT;

    if (wiiButtons & WPAD_BUTTON_PLUS)  kcode[port] &= ~key_CONT_START;

    // Exit combination
    if ((wiiButtons & WPAD_BUTTON_MINUS) && (wiiButtons & WPAD_BUTTON_PLUS))
        exit(0);

    // Save previous accelerometer values for next frame
    prev_acc_x[port] = ax;
    prev_acc_z[port] = az;
}

/**
 * Initializes fishing rod state for all active ports.
 */
void InitFishingRod(void)
{
    for (int i = 0; i < MAX_FISHING_PLAYERS; i++)
    {
        kcode[i]              = 0xFFFF;
        joyx[i]               = 0;
        joyy[i]               = 0;
        rt[i]                 = 0;
        lt[i]                 = 0;
        cast_state[i]         = CAST_IDLE;
        cast_hold_timer[i]    = 0;
        reel_speed[i]         = 0;
        prev_acc_x[i]         = 0;
        prev_acc_z[i]         = 0;
    }

    // Enable accelerometer data (no IR needed)
    int players = get_player_count();
    for (int i = 0; i < players && i < MAX_FISHING_PLAYERS; i++)
    {
        WPAD_SetDataFormat(i, WPAD_FMT_BTNS_ACC);
    }
}
