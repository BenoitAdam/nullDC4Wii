#pragma once
#include "types.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
extern u32     VREG;
extern VArray2 aica_ram;

// ---------------------------------------------------------------------------
// RTC
// ---------------------------------------------------------------------------
u32  GetRTC_now();
u32  ReadMem_aica_rtc (u32 addr,             u32 sz);
void WriteMem_aica_rtc(u32 addr, u32 data,   u32 sz);

// ---------------------------------------------------------------------------
// AICA register access
// ---------------------------------------------------------------------------
u32  FASTCALL ReadMem_aica_reg (u32 addr,           u32 sz);
void FASTCALL WriteMem_aica_reg(u32 addr, u32 data, u32 sz);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void aica_Init ();
void aica_Reset(bool Manual);
void aica_Term ();

void aica_sb_Init ();
void aica_sb_Reset(bool Manual);
void aica_sb_Term ();

// DMA_FIX: deferred AICA G2-DMA (SB_ADST) completion, counted down every
// timeslice from UpdateSystem().
void UpdateAicaDma(u32 cycles);

// ---------------------------------------------------------------------------
// Update macro — delegates to the plugin
// ---------------------------------------------------------------------------
#define UpdateAica(clc) libAICA_Update(clc)
#define UpdateArm(clc)  libARM_Update(clc)
