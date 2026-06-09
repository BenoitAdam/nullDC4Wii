#pragma once
#include "aica.h"

struct dsp_t
{
    // Buffered DSP state
    u32  TEMP[128];   // 24-bit wide
    u32  MEMS[32];    // 24-bit wide
    s32  MIXS[16];    // 20-bit wide

    // Ring buffer params (decoded from CommonData)
    u32  RBP;
    u32  RBL;

    struct
    {
        s32  MAD_OUT;
        s32  MEM_ADDR;
        s32  MEM_RD_DATA;
        s32  MEM_WT_DATA;
        s32  FRC_REG;
        s32  ADRS_REG;
        s32  Y_REG;

        u32  MDEC_CT;
        u32  MWT_1;
        u32  MRD_1;
        u32  MADRS;
        u32  NOFL_1;
        u32  NOFL_2;
    } regs;

    u32  DEC_;

    signed int ACC;
    signed int SHIFTED;
    signed int X;
    signed int Y;
    signed int B;
    signed int INPUTS;
    signed int MEMVAL;
    signed int FRC_REG;
    signed int Y_REG;
    unsigned int ADDR;
    unsigned int ADRS_REG;

    bool dyndirty;
};

extern dsp_t dsp;

void dsp_init();
void dsp_term();
void dsp_step();
void dsp_writenmem(u32 addr);
