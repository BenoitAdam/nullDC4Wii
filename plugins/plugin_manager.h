#pragma once
#include "types.h"
#include "dc/sh4/sh4_if.h"
#include "plugins/plugin_header.h"  // must come before plugin_types.h
#include "plugins/plugin_types.h"   // arm_init_params, aica_init_params, etc.

extern sh4_if				  sh4_cpu;

bool plugins_Load();
void plugins_Unload();
//bool plugins_Select();
void plugins_Reset(bool Manual);

//sh4 thread
bool plugins_Init();
void plugins_Term();


//PVR
	s32 FASTCALL libPvr_Load();
	void FASTCALL libPvr_Unload();

	s32 FASTCALL libPvr_Init(pvr_init_params* stuff);
	void FASTCALL libPvr_Reset(bool Manual);
	void FASTCALL libPvr_Term();

	void FASTCALL libPvr_UpdatePvr(u32 cycles);			//called every ~ 1800 cycles , set to 0 if not used
	void libPvr_TaDMA(u32* data,u32 count);				//size is 32 byte transfer counts
	void libPvr_TaSQ(u32* data);				//size is 32 byte transfer counts
	u32 FASTCALL libPvr_ReadReg(u32 addr,u32 size);
	void FASTCALL libPvr_WriteReg(u32 addr,u32 data,u32 size);

	//Will be called only when pvr locking is enabled
	void FASTCALL libPvr_LockedBlockWrite(vram_block* block,u32 addr);	//set to 0 if not used
	void FASTCALL libPvr_Update(u32 cycles);				//called every ~1800 cycles, set to 0 if not used


//AICA
	s32 FASTCALL libAICA_Load();
	void FASTCALL libAICA_Unload();

	s32 FASTCALL libAICA_Init(aica_init_params* stuff);
	void FASTCALL libAICA_Reset(bool Manual);
	void FASTCALL libAICA_Term();


	u32  FASTCALL libAICA_ReadMem_aica_reg(u32 addr,u32 size);
	void FASTCALL libAICA_WriteMem_aica_reg(u32 addr,u32 data,u32 size);

	u32  libAICA_ReadMem_aica_ram(u32 addr,u32 size);
	void libAICA_WriteMem_aica_ram(u32 addr,u32 data,u32 size);
	void FASTCALL libAICA_Update(u32 cycles);				//called every ~1800 cycles, set to 0 if not used

//ARM CPU plugin (vbaARM)
	void        libARM_Load   ();
	void        libARM_Unload ();
	s32  FASTCALL libARM_Init   (arm_init_params* initp);
	void FASTCALL libARM_Term   ();
	void FASTCALL libARM_Reset  (bool Manual);
	void FASTCALL libARM_Update (u32 cycles);
	void          libARM_SetResetState(u32 state);


//GDR
	s32 FASTCALL libGDR_Load();
	void FASTCALL libGDR_Unload();

	s32 FASTCALL libGDR_Init(gdr_init_params* param);
	void FASTCALL libGDR_Reset(bool M);
	void FASTCALL libGDR_Term();

	//IO
	void FASTCALL libGDR_ReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz);
	void FASTCALL libGDR_ReadSubChannel(u8 * buff, u32 format, u32 len);
	void FASTCALL libGDR_GetToc(u32* toc,u32 area);
	u32 FASTCALL libGDR_GetDiscType();
	void FASTCALL libGDR_GetSessionInfo(u8* pout,u8 session);


//ExtDev
	s32 FASTCALL libExtDevice_Load();
	void FASTCALL libExtDevice_Unload();

	s32 FASTCALL libExtDevice_Init(ext_device_init_params* param);
	void FASTCALL libExtDevice_Reset(bool M);
	void FASTCALL libExtDevice_Term();

	u32  FASTCALL libExtDevice_ReadMem_A0_006(u32 addr,u32 size);
	void FASTCALL libExtDevice_WriteMem_A0_006(u32 addr,u32 data,u32 size);

	//Area 0 , 0x01000000- 0x01FFFFFF	[Ext. Device]
	u32 FASTCALL libExtDevice_ReadMem_A0_010(u32 addr,u32 size);
	void FASTCALL libExtDevice_WriteMem_A0_010(u32 addr,u32 data,u32 size);
	
	//Area 5
	u32 FASTCALL libExtDevice_ReadMem_A5(u32 addr,u32 size);
	void FASTCALL libExtDevice_WriteMem_A5(u32 addr,u32 data,u32 size);

//Maple
