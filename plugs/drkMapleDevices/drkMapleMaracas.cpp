// drkMapleMaracas.cpp
// Samba de Amigo maracas emulation using dual Wiimotes + accelerometer
// 
// Supported game:
//   - Samba de Amigo (1 or 2 players)
//
// Dreamcast Maracas device:
//   The DC maracas controller reports:
//     - A shake flag (shaken / not shaken) per maraca
//     - A position flag: UP / MIDDLE / DOWN per maraca
//   This is derived from the accelerometer on each Wiimote.
//
// Control mapping:
//   1 PLAYER MODE:
//     Wiimote 0  ->  LEFT  maraca (port 0)
//     Wiimote 1  ->  RIGHT maraca (port 0, second maraca channel)
//
//   2 PLAYER MODE:
//     Player 1: Wiimote 0 (left maraca) + Wiimote 1 (right maraca)  -> port 0
//     Player 2: Wiimote 2 (left maraca) + Wiimote 3 (right maraca)  -> port 1
//     NOTE: 2-player Samba requires 4 Wiimotes total.
//
// Shake detection:
//   We treat the Wiimote as a maraca held vertically.
//   A "shake" is detected when the magnitude of acceleration change
//   exceeds SHAKE_THRESHOLD between two consecutive frames.
//
// Height detection:
//   The Y-axis (gravity component) tells us orientation:
//     acc_y >  HEIGHT_HIGH_THRESH  ->  UP
//     acc_y < -HEIGHT_LOW_THRESH   ->  DOWN
//     else                         ->  MIDDLE
//   (Wiimote held up = +Y gravity; held down = -Y gravity)
//
// Additional buttons:
//   A         -> Confirm / OK  (DC button A)
//   B         -> Back          (DC button B)
//   PLUS      -> Start
//   MINUS+PLUS -> Exit emulator
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

// Maraca-specific Dreamcast analog channels.
// Samba de Amigo uses the analog axes in Maple to encode maraca height.
// Convention (matches NAOMI / DC homebrew findings):
//   joyx = left  maraca height  (0=down, 128=mid, 255=up)
//   joyy = right maraca height  (same scale)
//   rt   = left  maraca shake   (0 or 255)
//   lt   = right maraca shake   (0 or 255)
#define MARACA_HEIGHT_UP     200
#define MARACA_HEIGHT_MID    128
#define MARACA_HEIGHT_DOWN    55

// Shake detection thresholds (accelerometer units, ~0-3g scale in libogc)
#define SHAKE_THRESHOLD      350   // delta magnitude to count as a shake
#define HEIGHT_HIGH_THRESH   200   // acc_y above this  -> UP
#define HEIGHT_LOW_THRESH   -200   // acc_y below this  -> DOWN

// Maximum DC ports (Samba supports 2 players)
#define MAX_MARACA_PORTS 2

// Controller state arrays (defined in drkMapleDevices.cpp)
extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4];
extern s8  joyy[4];
extern u8  rt[4];
extern u8  lt[4];

extern "C" int get_player_count(void);

// Previous accelerometer readings for delta-based shake detection
static s16 prev_acc_x[4] = {0};
static s16 prev_acc_y[4] = {0};
static s16 prev_acc_z[4] = {0};

// Shake latch: stays "on" for a few frames so the game reliably reads it
#define SHAKE_LATCH_FRAMES 3
static int shake_latch[4] = {0};

/**
 * Reads one Wiimote's accelerometer and derives:
 *   - shake (bool) : true if the maraca was shaken this frame
 *   - height (s8)  : MARACA_HEIGHT_UP / MID / DOWN
 *
 * @param chan      Wiimote channel (0-3)
 * @param outShake  Set to true if a shake was detected
 * @param outHeight Set to the encoded height value
 */
static void ReadMaracaWiimote(int chan, bool *outShake, u8 *outHeight)
{
    *outShake  = false;
    *outHeight = MARACA_HEIGHT_MID;

    WPADData *data = WPAD_Data(chan);
    if (!data)
        return;

    // Raw accelerometer values from libogc are in ~milli-g units.
    // We work with the raw s16 values for simplicity.
    s16 ax = (s16)data->accel.x;
    s16 ay = (s16)data->accel.y;
    s16 az = (s16)data->accel.z;

    // --- Shake detection (delta magnitude) ---
    s32 dx = ax - prev_acc_x[chan];
    s32 dy = ay - prev_acc_y[chan];
    s32 dz = az - prev_acc_z[chan];
    s32 mag = (s32)sqrtf((float)(dx*dx + dy*dy + dz*dz));

    if (mag > SHAKE_THRESHOLD)
        shake_latch[chan] = SHAKE_LATCH_FRAMES;

    if (shake_latch[chan] > 0)
    {
        *outShake = true;
        shake_latch[chan]--;
    }

    // Save previous values
    prev_acc_x[chan] = ax;
    prev_acc_y[chan] = ay;
    prev_acc_z[chan] = az;

    // --- Height detection (gravity on Y axis) ---
    // Wiimote held vertically with buttons facing player:
    //   top of remote up -> ay positive (gravity component)
    //   top of remote down -> ay negative
    if (ay > HEIGHT_HIGH_THRESH)
        *outHeight = MARACA_HEIGHT_UP;
    else if (ay < HEIGHT_LOW_THRESH)
        *outHeight = MARACA_HEIGHT_DOWN;
    else
        *outHeight = MARACA_HEIGHT_MID;
}

/**
 * Updates the maracas state for one Dreamcast port.
 * Each DC port uses TWO Wiimotes (left + right maraca).
 *
 * @param port  DC controller port (0 = P1, 1 = P2)
 */
void UpdateMaracasState(u32 port)
{
    if (port >= MAX_MARACA_PORTS || port >= (u32)get_player_count())
        return;

    WPAD_ScanPads();

    // Each DC port maps to two Wiimotes
    int leftChan  = port * 2;       // 0 (P1 left) or 2 (P2 left)
    int rightChan = port * 2 + 1;   // 1 (P1 right) or 3 (P2 right)

    bool  leftShake,  rightShake;
    u8    leftHeight, rightHeight;

    ReadMaracaWiimote(leftChan,  &leftShake,  &leftHeight);
    ReadMaracaWiimote(rightChan, &rightShake, &rightHeight);

    // Map to DC controller state
    // Height on analog axes (joyx = left height, joyy = right height)
    joyx[port] = (s8)((s32)leftHeight  - 128); // center around 0
    joyy[port] = (s8)((s32)rightHeight - 128);

    // Shake on trigger bytes (255 = shaking, 0 = still)
    lt[port] = leftShake  ? 255 : 0;
    rt[port] = rightShake ? 255 : 0;

    // --- Standard buttons ---
    // Use either Wiimote for buttons (primary = left maraca Wiimote)
    u32 wiiL = WPAD_ButtonsHeld(leftChan);
    u32 wiiR = WPAD_ButtonsHeld(rightChan);
    u32 anyButtons = wiiL | wiiR;

    kcode[port] = 0xFFFF;

    if (anyButtons & WPAD_BUTTON_A)     kcode[port] &= ~key_CONT_A;
    if (anyButtons & WPAD_BUTTON_B)     kcode[port] &= ~key_CONT_B;
    if (anyButtons & WPAD_BUTTON_PLUS)  kcode[port] &= ~key_CONT_START;
    if (anyButtons & WPAD_BUTTON_UP)    kcode[port] &= ~key_CONT_DPAD_UP;
    if (anyButtons & WPAD_BUTTON_DOWN)  kcode[port] &= ~key_CONT_DPAD_DOWN;
    if (anyButtons & WPAD_BUTTON_LEFT)  kcode[port] &= ~key_CONT_DPAD_LEFT;
    if (anyButtons & WPAD_BUTTON_RIGHT) kcode[port] &= ~key_CONT_DPAD_RIGHT;

    // Exit: MINUS + PLUS on either maraca Wiimote
    if (((wiiL | wiiR) & WPAD_BUTTON_MINUS) && ((wiiL | wiiR) & WPAD_BUTTON_PLUS))
        exit(0);
}

/**
 * Initializes maracas state for all active ports.
 * Sets Wiimote data format to include accelerometer.
 */
void InitMaracas(void)
{
    int players = get_player_count();
    int wiimoteCount = players * 2; // 2 Wiimotes per player

    for (int i = 0; i < wiimoteCount && i < 4; i++)
    {
        // Enable buttons + accelerometer (no IR needed for maracas)
        WPAD_SetDataFormat(i, WPAD_FMT_BTNS_ACC);

        prev_acc_x[i] = 0;
        prev_acc_y[i] = 0;
        prev_acc_z[i] = 0;
        shake_latch[i] = 0;
    }

    for (int i = 0; i < MAX_MARACA_PORTS; i++)
    {
        kcode[i] = 0xFFFF;
        joyx[i]  = 0;
        joyy[i]  = 0;
        rt[i]    = 0;
        lt[i]    = 0;
    }
}
