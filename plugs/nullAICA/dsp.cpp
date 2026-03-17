// dsp.cpp - DSP stub for Wii port
//
// The PC/Xenon versions used an x86 dynarec (dsp_recompile) which obviously
// cannot run on PowerPC. DSP emulation is disabled by default on Wii
// (aica_settings.DSPEnabled = 0). This stub satisfies the linker and keeps
// the door open for a future interpreted DSP implementation.

#include "dsp.h"
#include "aica_mem.h"

dsp_t dsp;

void dsp_init()
{
    memset(&dsp,   0, sizeof(dsp));
    memset(DSPData, 0, sizeof(*DSPData));

    dsp.dyndirty  = false;   // nothing to compile
    dsp.RBL       = 0x2000 - 1;
    dsp.RBP       = 0;
    dsp.regs.MDEC_CT = 1;
}

void dsp_term()
{
    // nothing to free
}

// Called once per sample when DSPEnabled == 1.
// Stub: zeroes EFREG so downstream VOLPAN reads produce silence
// rather than garbage. Replace with an interpreter if DSP is needed.
void dsp_step()
{
    memset(DSPData->EFREG, 0, sizeof(DSPData->EFREG));

    dsp.regs.MDEC_CT--;
    if (dsp.regs.MDEC_CT == 0)
        dsp.regs.MDEC_CT = dsp.RBL;
}

// Called by aica_mem whenever MPRO (DSP program) is written.
// Mark dirty so dsp_step knows the program changed (useful if you
// later add an interpreter).
void dsp_writenmem(u32 addr)
{
    addr -= 0x3000;
    if (addr >= 0x400 && addr < 0xC00)
        dsp.dyndirty = true;
}
