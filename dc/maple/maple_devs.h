#pragma once
#include "types.h"

enum MapleDeviceType
{
	MDT_SegaController,
	MDT_SegaVMU,
	MDT_LightGun,       // Dreamcast light gun / Stunner (drkMapleLightGun.cpp)
	MDT_Maracas,        // Samba de Amigo maracas       (drkMapleMaracas.cpp)
	MDT_Keyboard,       // DC keyboard / Typing of Dead (drkMapleKeyboard.cpp)
	MDT_FishingRod,     // Sega Bass Fishing rod         (drkMapleFishingRod.cpp)

	MDT_Count
};

struct IMapleConfigMap;

struct maple_device
{
	u8 maple_port;	//raw maple port
	u8 bus_port;	//0 .. 5
	u8 bus_id;		//0 .. 3
	wchar logical_port[3];	//A0, etc
	IMapleConfigMap* config;

	//fill in the info
	void Setup(u32 prt);

	virtual void OnSetup(){};
	virtual ~maple_device();
	virtual u32 Dma(u32 Command,u32* buffer_in,u32 buffer_in_len,u32* buffer_out,u32& buffer_out_len)=0;
};

maple_device* maple_Create(MapleDeviceType type);