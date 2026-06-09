// nullAICA plugin glue.
//
// Provides the libAICA_* entry points that dc/aica/aica_if.cpp,
// dc/mem/sh4_area0.cpp and plugins/plugin_manager.cpp dispatch to, forwarding
// them to the real nullAICA sample engine. This replaces the old EmptyAICA
// HLE-stub plugin (removed). With EmptyAICA gone:
//   * AICA register reads/writes hit the real SGC/DSP engine (so channels
//     actually get programmed and produce sound),
//   * AICA wave RAM is the shared DC AICA RAM (the same buffer the ARM7 and
//     SH4 use), accessed with the big-endian swap convention used everywhere
//     else on the Wii (byte ^3, halfword ^2, word direct),
//   * writes to the ARM reset register (0x2C00) enable/disable the ARM7 sound
//     CPU via the ARM plugin's SetResetState (this is what kicks the ARM7
//     sound driver into running).

#include "nullAICA.h"
#include "aica.h"
#include "aica_mem.h"

// Shared AICA wave RAM (set in init_mem() from aica_params.aica_ram).
extern u8* aica_ram_l;

// ARM7 enable hook (plugin_manager forwards to the ARM plugin's SetResetState).
extern void libARM_SetResetState(u32 state);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
s32 FASTCALL libAICA_Load()   { return rv_ok; }
void FASTCALL libAICA_Unload() {}

s32 FASTCALL libAICA_Init(aica_init_params* param)
{
    return InitAica(param);
}

void FASTCALL libAICA_Term()
{
    TermAica();
}

void FASTCALL libAICA_Reset(bool Manual)
{
    ResetAica(Manual);
}

// ---------------------------------------------------------------------------
// Register access (forward to the real engine)
// ---------------------------------------------------------------------------
u32 FASTCALL libAICA_ReadMem_aica_reg(u32 addr, u32 size)
{
    return aica_ReadMem_reg(addr, size);
}

void FASTCALL libAICA_WriteMem_aica_reg(u32 addr, u32 data, u32 size)
{
    // 0x2C00 is the ARM7 reset/run control (ARMRST). bit0 == 0 -> ARM7 runs.
    // EmptyAICA used to watch this; with it removed, do the same here so the
    // ARM7 sound CPU is actually enabled when the game programs AICA.
    if ((addr & 0x7FFF) == 0x2C00)
    {
        printf("[NullAICA] ARMRST write: 0x%X (ARM7 %s)\n",
               data, (data & 1) ? "halted" : "running");
        libARM_SetResetState(data & 1);
    }

    aica_WriteMem_reg(addr, data, size);
}

// ---------------------------------------------------------------------------
// Wave RAM access (shared DC AICA RAM, big-endian swap convention)
// ---------------------------------------------------------------------------
u32 libAICA_ReadMem_aica_ram(u32 addr, u32 size)
{
    addr &= AICA_RAM_MASK;
    if (size == 1)
        return aica_ram_l[addr ^ 3];
    else if (size == 2)
        return *(u16*)&aica_ram_l[addr ^ 2];
    else
        return *(u32*)&aica_ram_l[addr];
}

void libAICA_WriteMem_aica_ram(u32 addr, u32 data, u32 size)
{
    addr &= AICA_RAM_MASK;
    if (size == 1)
        aica_ram_l[addr ^ 3] = (u8)data;
    else if (size == 2)
        *(u16*)&aica_ram_l[addr ^ 2] = (u16)data;
    else
        *(u32*)&aica_ram_l[addr] = data;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
// AICA sample generation is driven from the SH4 timeslice via armUpdateARM
// (see plugs/vbaARM/arm_aica.cpp), so this bulk update is a no-op now. Kept so
// the UpdateAica() dispatch in MediumUpdate still links.
void FASTCALL libAICA_Update(u32 Cycles)
{
    (void)Cycles;
}
