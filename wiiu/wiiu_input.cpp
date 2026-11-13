// wiiu/wiiu_input.cpp
//
// Wii U input -> Dreamcast maple controller. Replaces, for the Wii U build,
// everything plugs/drkMapleDevices/ provides on the Wii (those files are
// libogc/wiiuse-only and are filtered out in wiiu/Makefile):
//
//   kcode[] vks[] joyx[] joyy[] lt[] rt[]   read by dc/maple/maple_cfg.cpp
//   InitControllers() / UpdateInputState()  standard controller
//   Init*/Update*State() for light gun, maracas, keyboard, fishing rod
//   GetLightGunX/Y(), GetKeyboardReport()   read by dc/maple/maple_devs.cpp
//   ExitCombo_HostPoll()                    called by plugs/drkPvr/SPG.cpp
//
// Ports: port 0 = the GamePad (VPAD), OR'ed with KPAD channel 0, so a Pro
// Controller or Classic Controller on the first Wii remote slot also drives
// player 1. Ports 1-3 = KPAD channels 1-3.
//
// Mapping (positional, same as the Wii build's Classic Controller mapping in
// drkMapleDevices.cpp): B -> DC A, A -> DC B, Y -> DC X, X -> DC Y,
// PLUS -> START, D-pad -> D-pad, left stick -> analog, ZL/L -> LT, ZR/R -> RT.
// MINUS + PLUS held = leave the emulator (the HOME button opens the system
// HOME menu as usual; "Close software" there exits too).
//
// The special devices (light gun, maracas, keyboard, fishing rod) are not
// emulated yet on Wii U: their update functions drive the standard pad and
// the gun always reports "off-screen".

#include "types.h"   // first, like the Wii files (config.h's BIG_ENDIAN poison)
#include <string.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

#include "plugs/drkMapleDevices/dc_pad_bits.h"
#include "wiiu/wiiu_exit.h"
#include "wiiu/wiiu_input.h"

// --- State read by dc/maple/maple_cfg.cpp -----------------------------------
u16 kcode[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };   // active-low
u32 vks[4]   = { 0, 0, 0, 0 };
s8  joyx[4]  = { 0, 0, 0, 0 };
s8  joyy[4]  = { 0, 0, 0, 0 };                       // DC: +Y = down
u8  rt[4]    = { 0, 0, 0, 0 };
u8  lt[4]    = { 0, 0, 0, 0 };

// Normalised host state for one port, filled by the readers below.
struct HostPad
{
  bool  a, b, x, y, start, up, down, left, right, l, r, minus;
  float sx, sy;   // left stick, -1..1, +y = up (Nintendo convention)
};

static bool s_inited = false;
static KPADStatus s_kpad_last[4];
static bool       s_kpad_valid[4];

extern "C" void wiiu_input_init(void)
{
  if (s_inited)
    return;
  s_inited = true;
  // No VPADInit(): VPAD is up at boot on every firmware wut supports, and wut
  // marks the call deprecated.
  KPADInit();
  WPADEnableURCC(TRUE);   // Wii U Pro Controller
  memset(s_kpad_last, 0, sizeof(s_kpad_last));
  memset(s_kpad_valid, 0, sizeof(s_kpad_valid));
}

static s8 axis_to_s8(float v)
{
  if (v > 1.0f)  v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  // 10% dead zone, like ANALOG_DEADZONE on the Wii side.
  if (v > -0.10f && v < 0.10f)
    return 0;
  int i = (int)(v * 127.0f);
  return (s8)i;
}

static void read_vpad(HostPad &p)
{
  VPADStatus st;
  VPADReadError err = VPAD_READ_SUCCESS;
  static uint32_t s_hold = 0;
  static VPADVec2D s_stick = { 0.0f, 0.0f };

  if (VPADRead(VPAD_CHAN_0, &st, 1, &err) > 0 && err == VPAD_READ_SUCCESS)
  {
    s_hold  = st.hold;
    s_stick = st.leftStick;
  }
  else if (err == VPAD_READ_INVALID_CONTROLLER)
  {
    s_hold  = 0;
    s_stick.x = s_stick.y = 0.0f;
  }
  // VPAD_READ_NO_SAMPLES: keep the last sample (we may poll faster than 60 Hz)

  const uint32_t h = s_hold;
  p.b     |= (h & VPAD_BUTTON_A) != 0;
  p.a     |= (h & VPAD_BUTTON_B) != 0;
  p.y     |= (h & VPAD_BUTTON_X) != 0;
  p.x     |= (h & VPAD_BUTTON_Y) != 0;
  p.start |= (h & VPAD_BUTTON_PLUS) != 0;
  p.minus |= (h & VPAD_BUTTON_MINUS) != 0;
  p.up    |= (h & VPAD_BUTTON_UP) != 0;
  p.down  |= (h & VPAD_BUTTON_DOWN) != 0;
  p.left  |= (h & VPAD_BUTTON_LEFT) != 0;
  p.right |= (h & VPAD_BUTTON_RIGHT) != 0;
  p.l     |= (h & (VPAD_BUTTON_ZL | VPAD_BUTTON_L)) != 0;
  p.r     |= (h & (VPAD_BUTTON_ZR | VPAD_BUTTON_R)) != 0;
  if (s_stick.x * s_stick.x + s_stick.y * s_stick.y > p.sx * p.sx + p.sy * p.sy)
  {
    p.sx = s_stick.x;
    p.sy = s_stick.y;
  }
}

static void read_kpad(int chan, HostPad &p)
{
  KPADStatus st;
  if (KPADRead((KPADChan)chan, &st, 1) > 0)
  {
    s_kpad_last[chan]  = st;
    s_kpad_valid[chan] = (st.error == 0);
  }
  if (!s_kpad_valid[chan])
    return;

  const KPADStatus &k = s_kpad_last[chan];
  uint32_t h = 0;
  float sx = 0.0f, sy = 0.0f;

  if (k.extensionType == WPAD_EXT_PRO_CONTROLLER)
  {
    h  = k.pro.hold;
    sx = k.pro.leftStick.x;
    sy = k.pro.leftStick.y;
    p.b     |= (h & WPAD_PRO_BUTTON_A) != 0;
    p.a     |= (h & WPAD_PRO_BUTTON_B) != 0;
    p.y     |= (h & WPAD_PRO_BUTTON_X) != 0;
    p.x     |= (h & WPAD_PRO_BUTTON_Y) != 0;
    p.start |= (h & WPAD_PRO_BUTTON_PLUS) != 0;
    p.minus |= (h & WPAD_PRO_BUTTON_MINUS) != 0;
    p.up    |= (h & WPAD_PRO_BUTTON_UP) != 0;
    p.down  |= (h & WPAD_PRO_BUTTON_DOWN) != 0;
    p.left  |= (h & WPAD_PRO_BUTTON_LEFT) != 0;
    p.right |= (h & WPAD_PRO_BUTTON_RIGHT) != 0;
    p.l     |= (h & (WPAD_PRO_TRIGGER_ZL | WPAD_PRO_TRIGGER_L)) != 0;
    p.r     |= (h & (WPAD_PRO_TRIGGER_ZR | WPAD_PRO_TRIGGER_R)) != 0;
  }
  else if (k.extensionType == WPAD_EXT_CLASSIC)
  {
    h  = k.classic.hold;
    sx = k.classic.leftStick.x;
    sy = k.classic.leftStick.y;
    p.b     |= (h & WPAD_CLASSIC_BUTTON_A) != 0;
    p.a     |= (h & WPAD_CLASSIC_BUTTON_B) != 0;
    p.y     |= (h & WPAD_CLASSIC_BUTTON_X) != 0;
    p.x     |= (h & WPAD_CLASSIC_BUTTON_Y) != 0;
    p.start |= (h & WPAD_CLASSIC_BUTTON_PLUS) != 0;
    p.minus |= (h & WPAD_CLASSIC_BUTTON_MINUS) != 0;
    p.up    |= (h & WPAD_CLASSIC_BUTTON_UP) != 0;
    p.down  |= (h & WPAD_CLASSIC_BUTTON_DOWN) != 0;
    p.left  |= (h & WPAD_CLASSIC_BUTTON_LEFT) != 0;
    p.right |= (h & WPAD_CLASSIC_BUTTON_RIGHT) != 0;
    p.l     |= (h & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_L)) != 0;
    p.r     |= (h & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_R)) != 0;
  }
  else
  {
    // Bare Wii remote held sideways: D-pad rotated, 2 = A, 1 = B, + = START.
    h = k.hold;
    p.a     |= (h & WPAD_BUTTON_2) != 0;
    p.b     |= (h & WPAD_BUTTON_1) != 0;
    p.x     |= (h & WPAD_BUTTON_B) != 0;
    p.y     |= (h & WPAD_BUTTON_A) != 0;
    p.start |= (h & WPAD_BUTTON_PLUS) != 0;
    p.minus |= (h & WPAD_BUTTON_MINUS) != 0;
    p.up    |= (h & WPAD_BUTTON_RIGHT) != 0;
    p.down  |= (h & WPAD_BUTTON_LEFT) != 0;
    p.left  |= (h & WPAD_BUTTON_UP) != 0;
    p.right |= (h & WPAD_BUTTON_DOWN) != 0;
    if (k.extensionType == WPAD_EXT_NUNCHUK)
    {
      sx = k.nunchuk.stick.x;
      sy = k.nunchuk.stick.y;
      p.l |= (k.nunchuk.hold & WPAD_BUTTON_Z) != 0;
      p.r |= (k.nunchuk.hold & WPAD_BUTTON_C) != 0;
    }
  }

  if (sx * sx + sy * sy > p.sx * p.sx + p.sy * p.sy)
  {
    p.sx = sx;
    p.sy = sy;
  }
}

void InitControllers(void)
{
  wiiu_input_init();
  for (int i = 0; i < 4; i++)
  {
    kcode[i] = 0xFFFF;
    vks[i]   = 0;
    joyx[i]  = joyy[i] = 0;
    lt[i]    = rt[i]   = 0;
  }
}

void UpdateInputState(u32 port)
{
  if (port > 3)
    return;
  wiiu_input_init();

  HostPad p;
  memset(&p, 0, sizeof(p));
  if (port == 0)
    read_vpad(p);
  read_kpad((int)port, p);

  u16 k = 0xFFFF;
  if (p.a)     k &= ~key_CONT_A;
  if (p.b)     k &= ~key_CONT_B;
  if (p.x)     k &= ~key_CONT_X;
  if (p.y)     k &= ~key_CONT_Y;
  if (p.start) k &= ~key_CONT_START;
  if (p.up)    k &= ~key_CONT_DPAD_UP;
  if (p.down)  k &= ~key_CONT_DPAD_DOWN;
  if (p.left)  k &= ~key_CONT_DPAD_LEFT;
  if (p.right) k &= ~key_CONT_DPAD_RIGHT;
  kcode[port] = k;

  joyx[port] = axis_to_s8(p.sx);
  joyy[port] = axis_to_s8(-p.sy);   // DC +Y = down
  lt[port]   = p.l ? 255 : 0;
  rt[port]   = p.r ? 255 : 0;

  if (p.minus && p.start)
    WiiU_RequestExit("MINUS + PLUS");
}

// SPG.cpp polls this off the scanline counter with EXIT FIX = 3, so the exit
// combo still works while a game has stopped reading the maple bus.
extern "C" void ExitCombo_HostPoll(void)
{
  HostPad p;
  memset(&p, 0, sizeof(p));
  read_vpad(p);
  for (int ch = 0; ch < 4; ch++)
    read_kpad(ch, p);
  if (p.minus && p.start)
    WiiU_RequestExit("MINUS + PLUS (host poll)");
}

// --- Special devices: standard pad for now (see the header) ------------------
void InitLightGun(void)   { InitControllers(); }
void InitMaracas(void)    { InitControllers(); }
void InitKeyboard(void)   { InitControllers(); }
void InitFishingRod(void) { InitControllers(); }

void UpdateLightGunState(u32 port)   { UpdateInputState(port); }
void UpdateMaracasState(u32 port)    { UpdateInputState(port); }
void UpdateKeyboardState(u32 port)   { UpdateInputState(port); }
void UpdateFishingRodState(u32 port) { UpdateInputState(port); }

extern "C" u16 GetLightGunX(int port) { return 0xFFFF; }   // off-screen
extern "C" u16 GetLightGunY(int port) { return 0xFFFF; }

// Same layout as the struct dc/maple/maple_devs.cpp declares.
struct DC_KeyboardReport
{
  u8 modifiers;
  u8 leds;
  u8 keys[6];
};
static DC_KeyboardReport s_kb_report = { 0, 0, { 0, 0, 0, 0, 0, 0 } };
extern "C" const DC_KeyboardReport *GetKeyboardReport(int port) { return &s_kb_report; }
