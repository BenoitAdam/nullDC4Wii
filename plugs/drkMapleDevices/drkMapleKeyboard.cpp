// drkMapleKeyboard.cpp
// Dreamcast keyboard emulation for "The Typing of the Dead" (and other DC keyboard games).
//
// Supported games:
//   - The Typing of the Dead
//   - Sega Typing Racer (import)
//
// Hardware: A standard USB HID keyboard connected to the Wii.
//
// Dreamcast keyboard Maple device:
//   Reports up to 6 simultaneous key codes (like USB HID boot protocol),
//   plus modifier keys (Shift, Ctrl, Alt, etc.).
//   The DC keyboard is essentially a USB HID keyboard at the Maple level.
//
// Wii implementation notes:
//   libogc provides USB HID keyboard access through the usbkeyboard / USB HID API.
//   We read raw HID key codes and translate them directly to DC key codes.
//   DC key codes ARE USB HID key codes — same standard — so translation is 1:1
//   for the standard ASCII range.  Special keys (function keys, numpad, etc.)
//   are also identical.
//
// 2-player support:
//   "Typing of the Dead" does support 2-player co-op (each player needs a keyboard).
//   This requires two USB keyboards, connected via a USB hub.
//   The Wii has one USB port (or two on later models), so P2 keyboard support
//   depends on hardware.  We scan up to 2 HID keyboard devices.
//   If only one keyboard is found, P2 falls back to Wiimote D-pad for movement
//   and A/B for basic actions.
//
// Player count is read from get_player_count() defined in main.cpp.

#include "plugins/plugin_header.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiiuse/wpad.h>
#include <ogc/usb.h>            // USB HID base
#include "dc/dc.h"

// ============================================================================
// Dreamcast keyboard report structure
// Mirrors the Maple keyboard response packet.
// ============================================================================
typedef struct
{
    u8  modifiers;       // Shift/Ctrl/Alt/Win bitmask (same as USB HID)
    u8  leds;            // LED state (Caps Lock, Num Lock, Scroll Lock)
    u8  keys[6];         // Up to 6 simultaneous key codes (USB HID)
} DC_KeyboardReport;

// ============================================================================
// USB HID keyboard VID/PID scan
// ============================================================================
// libogc HID keyboard interface (usbkeyboard.h from devkitPro examples).
// We use the raw USB HID path so we can support 2 keyboards.
// Include the standard devkitPro keyboard helper if present, otherwise stub.
#ifdef HAVE_USBKEYBOARD_H
  #include <usbkeyboard.h>
  #define USB_KBD_AVAILABLE 1
#else
  #define USB_KBD_AVAILABLE 0
#endif

#define MAX_KB_PLAYERS 2

// Controller state arrays (defined in drkMapleDevices.cpp)
extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4];
extern s8  joyy[4];
extern u8  rt[4];
extern u8  lt[4];

// DC keyboard reports (one per player)
DC_KeyboardReport kb_report[MAX_KB_PLAYERS];

// Expose keyboard report to the Maple layer
extern "C"
{
    const DC_KeyboardReport* GetKeyboardReport(int port)
    {
        if (port < MAX_KB_PLAYERS)
            return &kb_report[port];
        return NULL;
    }
}

extern "C" int get_player_count(void);

// ============================================================================
// USB HID modifier byte bit definitions (same as USB spec / DC spec)
// ============================================================================
#define HID_MOD_LCTRL   (1 << 0)
#define HID_MOD_LSHIFT  (1 << 1)
#define HID_MOD_LALT    (1 << 2)
#define HID_MOD_LGUI    (1 << 3)
#define HID_MOD_RCTRL   (1 << 4)
#define HID_MOD_RSHIFT  (1 << 5)
#define HID_MOD_RALT    (1 << 6)
#define HID_MOD_RGUI    (1 << 7)

// ============================================================================
// Fallback: Wiimote D-pad -> DC keyboard arrow key codes
// Used for P2 if no second keyboard is attached.
// ============================================================================
// USB HID usage codes for arrow keys
#define HID_KEY_UP_ARROW    0x52
#define HID_KEY_DOWN_ARROW  0x51
#define HID_KEY_LEFT_ARROW  0x50
#define HID_KEY_RIGHT_ARROW 0x4F
#define HID_KEY_RETURN      0x28
#define HID_KEY_ESCAPE      0x29
#define HID_KEY_SPACE       0x2C

/**
 * Fills a DC_KeyboardReport with fallback Wiimote D-pad input.
 * Used when a USB keyboard is not available for a player.
 *
 * @param port    DC player port
 * @param report  Output keyboard report
 */
static void FillWiimoteFallback(u32 port, DC_KeyboardReport *report)
{
    memset(report, 0, sizeof(DC_KeyboardReport));

    WPAD_ScanPads();
    u32 held = WPAD_ButtonsHeld(port);

    int keyIdx = 0;

    if ((held & WPAD_BUTTON_UP)    && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_UP_ARROW;
    if ((held & WPAD_BUTTON_DOWN)  && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_DOWN_ARROW;
    if ((held & WPAD_BUTTON_LEFT)  && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_LEFT_ARROW;
    if ((held & WPAD_BUTTON_RIGHT) && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_RIGHT_ARROW;
    if ((held & WPAD_BUTTON_A)     && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_RETURN;
    if ((held & WPAD_BUTTON_B)     && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_ESCAPE;
    if ((held & WPAD_BUTTON_1)     && keyIdx < 6)
        report->keys[keyIdx++] = HID_KEY_SPACE;

    // Exit: MINUS + PLUS
    if ((held & WPAD_BUTTON_MINUS) && (held & WPAD_BUTTON_PLUS))
        exit(0);
}

/**
 * Updates keyboard state for one DC player port.
 *
 * If a USB keyboard is available for this player, reads it.
 * Otherwise falls back to Wiimote D-pad.
 *
 * @param port  DC controller port (0 = P1, 1 = P2)
 */
void UpdateKeyboardState(u32 port)
{
    if (port >= MAX_KB_PLAYERS || port >= (u32)get_player_count())
        return;

    memset(&kb_report[port], 0, sizeof(DC_KeyboardReport));

#if USB_KBD_AVAILABLE
    // Try to read from USB HID keyboard for this player slot
    // usbkeyboard provides USBKeyboard_Read() which fills a HID boot report.
    // If the device for this port is not connected, fall back to Wiimote.
    USBKeyboard_EventData kbEvent;
    if (USBKeyboard_Read(port, &kbEvent))
    {
        // Copy modifier + key codes directly (DC uses same format as USB HID)
        kb_report[port].modifiers = kbEvent.modifiers;
        for (int i = 0; i < 6; i++)
            kb_report[port].keys[i] = kbEvent.keys[i];
    }
    else
    {
        // No USB keyboard — use Wiimote D-pad fallback
        FillWiimoteFallback(port, &kb_report[port]);
    }
#else
    // USB keyboard library not available — always use Wiimote fallback
    FillWiimoteFallback(port, &kb_report[port]);
#endif

    // Also mirror minimal navigation into kcode for any maple_cfg code
    // that still reads the standard button state:
    //   Enter -> A, Escape -> B, arrow keys -> D-pad
    kcode[port] = 0xFFFF;

    bool hasEnter  = false;
    bool hasEscape = false;
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;

    for (int i = 0; i < 6; i++)
    {
        switch (kb_report[port].keys[i])
        {
            case HID_KEY_RETURN:      hasEnter  = true; break;
            case HID_KEY_ESCAPE:      hasEscape = true; break;
            case HID_KEY_UP_ARROW:    hasUp     = true; break;
            case HID_KEY_DOWN_ARROW:  hasDown   = true; break;
            case HID_KEY_LEFT_ARROW:  hasLeft   = true; break;
            case HID_KEY_RIGHT_ARROW: hasRight  = true; break;
            default: break;
        }
    }

    // Dreamcast button definitions
    #define key_CONT_A          (1 << 2)
    #define key_CONT_B          (1 << 1)
    #define key_CONT_START      (1 << 3)
    #define key_CONT_DPAD_UP    (1 << 4)
    #define key_CONT_DPAD_DOWN  (1 << 5)
    #define key_CONT_DPAD_LEFT  (1 << 6)
    #define key_CONT_DPAD_RIGHT (1 << 7)

    if (hasEnter)  kcode[port] &= ~key_CONT_A;
    if (hasEscape) kcode[port] &= ~key_CONT_B;
    if (hasUp)     kcode[port] &= ~key_CONT_DPAD_UP;
    if (hasDown)   kcode[port] &= ~key_CONT_DPAD_DOWN;
    if (hasLeft)   kcode[port] &= ~key_CONT_DPAD_LEFT;
    if (hasRight)  kcode[port] &= ~key_CONT_DPAD_RIGHT;
}

/**
 * Initializes keyboard state and USB HID subsystem.
 */
void InitKeyboard(void)
{
    for (int i = 0; i < MAX_KB_PLAYERS; i++)
    {
        memset(&kb_report[i], 0, sizeof(DC_KeyboardReport));
        kcode[i] = 0xFFFF;
        joyx[i]  = 0;
        joyy[i]  = 0;
        rt[i]    = 0;
        lt[i]    = 0;
    }

#if USB_KBD_AVAILABLE
    USBKeyboard_Initialize();
    printf("[Keyboard] USB HID keyboard initialized.\n");
#else
    printf("[Keyboard] No USB keyboard lib - using Wiimote D-pad fallback.\n");
    printf("[Keyboard] For full Typing of the Dead experience,\n");
    printf("[Keyboard] connect a USB keyboard to the Wii.\n");
#endif
}
