#include "types.h"
#include "maple_if.h"
#include "maple_helper.h"
#include "maple_devs.h"
#include "maple_cfg.h"

// ============================================================================
// Per-device update function declarations.
// Each lives in its own .cpp file in plugs/drkMapleDevices/.
// ============================================================================
void UpdateInputState(u32 port);        // Standard controller  (drkMapleDevices.cpp)
void UpdateLightGunState(u32 port);     // Light gun            (drkMapleLightGun.cpp)
void UpdateMaracasState(u32 port);      // Maracas              (drkMapleMaracas.cpp)
void UpdateKeyboardState(u32 port);     // Keyboard             (drkMapleKeyboard.cpp)
void UpdateFishingRodState(u32 port);   // Fishing rod          (drkMapleFishingRod.cpp)

// Init functions — called once before the emulator starts
void InitControllers(void);
void InitLightGun(void);
void InitMaracas(void);
void InitKeyboard(void);
void InitFishingRod(void);

// Controller type selection set by main.cpp options menu
extern "C" int get_controller_type();  // 0=standard 1=lightgun 2=maracas 3=keyboard 4=fishing

extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4], joyy[4];
extern u8  rt[4], lt[4];

// ----------------------------------------------------------------------------
// Helper: convert signed axis value [-128..127] to unsigned byte [0..255]
// Center (0) maps to 0x80.
// ----------------------------------------------------------------------------
static inline u8 SignedToByte(s8 val)
{
	return static_cast<u8>(static_cast<s32>(val) + 128);
}

// ----------------------------------------------------------------------------
// DispatchUpdateInputState
// Calls the correct platform update function for the active controller type.
// The keyboard device calls GetInput(nullptr) and reads its own report
// separately, so we handle that gracefully too.
// ----------------------------------------------------------------------------
static void DispatchUpdateInputState(u32 port)
{
	switch (get_controller_type())
	{
	case 1:  UpdateLightGunState(port);   break;
	case 2:  UpdateMaracasState(port);    break;
	case 3:  UpdateKeyboardState(port);   break;
	case 4:  UpdateFishingRodState(port); break;
	default: UpdateInputState(port);      break;
	}
}

// ----------------------------------------------------------------------------
// MapleConfigMap
// Bridges the platform's raw input globals into the PlainJoystickState format
// expected by the Maple device layer.
//
// For the keyboard device, GetInput() is called with pjs=nullptr just to
// trigger the platform scan; the actual key report is read via
// GetKeyboardReport() directly in maple_keyboard::dma().
// ----------------------------------------------------------------------------
struct MapleConfigMap : IMapleConfigMap
{
	maple_device* dev;

	explicit MapleConfigMap(maple_device* dev) : dev(dev) {}

	void GetInput(PlainJoystickState* pjs) override
	{
		if (!dev)
			return;

		DispatchUpdateInputState(dev->bus_id);

		// Keyboard: caller passes nullptr — nothing else to fill in here.
		if (!pjs)
			return;

		// kcode is active-low; bits 0 and 8 and 11-15 are reserved by the
		// Maple protocol. OR them in so they always read as "not pressed".
		// Bit layout: F901 = 1111 1001 0000 0001
		pjs->kcode = kcode[dev->bus_id] | 0xF901;

		pjs->joy[PJAI_X1] = SignedToByte(joyx[dev->bus_id]);
		pjs->joy[PJAI_Y1] = SignedToByte(joyy[dev->bus_id]);
		// X2/Y2 not driven by this source; leave them centered (set in ctor).

		pjs->trigger[PJTI_R] = rt[dev->bus_id];
		pjs->trigger[PJTI_L] = lt[dev->bus_id];
	}

	void SetImage(void* /*img*/) override
	{
		// VMU screen image upload — not implemented for Wii input backend
	}
};

// ----------------------------------------------------------------------------
// Device factory helpers
// ----------------------------------------------------------------------------

// Creates a single Maple device, wires up its config map, and registers it.
static bool mcfg_Create(MapleDeviceType type, u32 bus, u32 port)
{
	maple_device* dev = maple_Create(type);
	if (!dev)
		return false;

	dev->Setup(maple_GetAddress(bus, port));
	dev->config = new MapleConfigMap(dev);
	dev->OnSetup();
	MapleDevices[bus][port] = dev;
	return true;
}

// ----------------------------------------------------------------------------
// mcfg_GetControllerDeviceType
// Maps the user-facing controller type integer to the correct MapleDeviceType.
// ----------------------------------------------------------------------------
static MapleDeviceType mcfg_GetControllerDeviceType()
{
	switch (get_controller_type())
	{
	case 1:  return MDT_LightGun;
	case 2:  return MDT_Maracas;
	case 3:  return MDT_Keyboard;
	case 4:  return MDT_FishingRod;
	default: return MDT_SegaController;
	}
}

// ----------------------------------------------------------------------------
// mcfg_InitSpecialDevice
// Calls the one-time hardware init for whichever special device is selected.
// Safe to call for the standard controller too (InitControllers already
// existed before this system was added).
// ----------------------------------------------------------------------------
static void mcfg_InitSpecialDevice()
{
	switch (get_controller_type())
	{
	case 1:  InitLightGun();    break;
	case 2:  InitMaracas();     break;
	case 3:  InitKeyboard();    break;
	case 4:  InitFishingRod();  break;
	default: InitControllers(); break;
	}
}

// g_player_count is set by main.cpp options menu before EmuMain() is called.
extern "C" int get_player_count();

// ----------------------------------------------------------------------------
// mcfg_CreateDevices
// Instantiates the correct Maple devices for all active player slots.
//
// Device layout per player:
//   port 5 = main controller (sub-device slot for the bus root)
//   port 0 = VMU slot A1
//   port 1 = VMU slot A2  (only for standard controller)
//
// Special devices (light gun, maracas, keyboard, fishing rod) do not use
// VMUs — the real DC peripherals didn't expose VMU slots either.
//
// Maracas note:
//   Each DC port represents one "player" who uses 2 Wiimotes physically.
//   The maracas device for bus 1 (P2) needs Wiimotes 2 & 3, so 4 Wiimotes
//   total are required in 2-player maracas mode.
// ----------------------------------------------------------------------------
void mcfg_CreateDevices()
{
	MapleDeviceType ctrlType = mcfg_GetControllerDeviceType();
	bool isStandard = (ctrlType == MDT_SegaController);

	// One-time hardware / data-format initialisation
	mcfg_InitSpecialDevice();

	// --- Bus 0 : Player 1 ---
	mcfg_Create(ctrlType,       0, 5);  // Main controller

  // Claude Comment this : (probably wrong, because official lightgun has VMU)
	// VMUs only for standard controller — special devices (lightgun, maracas,
	// keyboard, fishing rod) have no VMU slots on real hardware.
	// maple_GetAttachedDevices() reports sub-port occupancy to the DC SH4; if a
	// VMU is present here for a lightgun port, the DC sees lightgun+VMU which is
	// an invalid configuration and games (e.g. Confidential Mission) reject it.
	if (isStandard)
	{
		mcfg_Create(MDT_SegaVMU, 0, 0);  // VMU slot A1
		mcfg_Create(MDT_SegaVMU, 0, 1);  // VMU slot A2
	}

	// --- Bus 1 : Player 2 (when 2-player mode is active) ---
	if (get_player_count() >= 2)
	{
		mcfg_Create(ctrlType,     1, 5);
		if (isStandard)
		{
			mcfg_Create(MDT_SegaVMU, 1, 0);
			mcfg_Create(MDT_SegaVMU, 1, 1);
		}
	}
}

void mcfg_DestroyDevices()
{
	for (int bus = 0; bus < 4; bus++)
	{
		for (int port = 0; port < 6; port++)
		{
			if (MapleDevices[bus][port])
			{
				delete MapleDevices[bus][port];
				MapleDevices[bus][port] = nullptr;
			}
		}
	}
}