//
// emptyARM.cpp - Empty ARM7 CPU stub for nullDC4Wii
//
// Replaces vbaARM when ARM sound is not needed.
// Accepts all plugin calls silently so BIOS does not freeze
// waiting for ARM7 to respond.
//

#include "emptyARM.h"
#include <stdio.h>
#include <string.h>

static s32 FASTCALL emptyArm_Init(arm_init_params* p)
{
    printf("[EmptyARM] Init - ARM7 stubbed out\n");
    return rv_ok;
}

static void FASTCALL emptyArm_Reset(bool Manual)
{
    printf("[EmptyARM] Reset manual=%d\n", Manual);
}

static void FASTCALL emptyArm_Term()
{
    printf("[EmptyARM] Term\n");
}

// Called when game writes to ARM reset register (0x2C00)
// Real vbaARM calls arm_SetEnabled() here which starts ARM execution
// We do nothing - ARM stays "off", BIOS does not hang waiting for it
static void FASTCALL emptyArm_SetResetState(u32 state)
{
    // state==0 means "release reset" (start ARM)
    // state==1 means "hold in reset" (stop ARM)
    // We ignore both - ARM never runs
}

static void FASTCALL emptyArm_Update(u32 cycles)
{
    // No ARM execution
}

static void FASTCALL emptyArm_ArmInterruptChange(u32 bits, u32 L)
{
    // No interrupts from ARM
}

void emptyArmGetInterface(plugin_interface* info)
{
    memset(info, 0, sizeof(*info));

    info->common.InterfaceVersion = PLUGIN_I_F_VERSION;
    info->common.Type             = Plugin_ARM;
    info->common.InterfaceVersion = ARM_PLUGIN_I_F_VERSION;

    strncpy(info->common.Name,
            "EmptyARM stub [nullDC4Wii]",
            sizeof(info->common.Name) - 1);

    info->arm.Init              = emptyArm_Init;
    info->arm.Reset             = emptyArm_Reset;
    info->arm.Term              = emptyArm_Term;
    info->arm.Update            = emptyArm_Update;
    info->arm.ArmInterruptChange= emptyArm_ArmInterruptChange;
    info->arm.SetResetState     = emptyArm_SetResetState;
}
