#include "types.h"
#include "maple_if.h"
#include "maple_helper.h"
#include "maple_devs.h"
#include "maple_cfg.h"

void UpdateInputState(u32 port);

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
// MapleConfigMap
// Bridges the platform's raw input globals into the PlainJoystickState format
// expected by the Maple device layer.
// ----------------------------------------------------------------------------
struct MapleConfigMap : IMapleConfigMap
{
	maple_device* dev;

	explicit MapleConfigMap(maple_device* dev) : dev(dev) {}

	void GetInput(PlainJoystickState* pjs) override
	{
		if (!pjs || !dev)
			return;

		UpdateInputState(dev->bus_id);

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
// Returns false if creation fails.
static bool mcfg_Create(MapleDeviceType type, u32 bus, u32 port)
{
	maple_device* dev = maple_Create(type);
	if (!dev)
	{
		// Failed to allocate device — log or assert here if your platform supports it
		return false;
	}

	dev->Setup(maple_GetAddress(bus, port));
	dev->config = new MapleConfigMap(dev);
	dev->OnSetup();
	MapleDevices[bus][port] = dev;
	return true;
}

// g_player_count is set by main.cpp options menu before EmuMain() is called.
extern "C" int get_player_count();

void mcfg_CreateDevices()
{
	// Bus 0 = Player 1
	mcfg_Create(MDT_SegaController, 0, 5);
	mcfg_Create(MDT_SegaVMU,        0, 0);
	mcfg_Create(MDT_SegaVMU,        0, 1);

	// Bus 1 = Player 2 (only when 2-player mode is active)
	if (get_player_count() >= 2)
	{
		mcfg_Create(MDT_SegaController, 1, 5);
		mcfg_Create(MDT_SegaVMU,        1, 0);
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