#pragma once
#include "nullAICA.h"
#include "wii_audiostream.h"
#include "assert.h"

#define SCIEB_addr  0x289C
#define SCIPD_addr  (0x289C+4)
#define SCIRE_addr  (0x289C+8)

#define MCIEB_addr  0x28B4
#define MCIPD_addr  (0x28B4+4)
#define MCIRE_addr  (0x28B4+8)

#define TIMER_A     0x2890
#define TIMER_B     (0x2890+4)
#define TIMER_C     (0x2890+8)
#define REG_L       (0x2D00)
#define REG_M       (0x2D04)

#define entry(name,sz) u32 name:sz;

#pragma pack(1)
struct CommonData_struct
{
#ifdef WII
    //+0
    u32 :16;
    entry(Mono,1);
    entry(pad0_0,5);
    entry(MEM8MB,1);
    entry(DAC18B,1);
    entry(VER,4);
    entry(MVOL,4);
    //+4
    u32 :16;
    entry(TESTB0,1);
    entry(RBL,2);
    entry(pad1_0,1);
    entry(RBP,12);
    //+8
    u32 :16;
    entry(pad3_0,3);
    entry(MOFUL,1);
    entry(MOEMP,1);
    entry(MIOVF,1);
    entry(MIFUL,1);
    entry(MIEMP,1);
    entry(MIBUF,8);
    //+C
    u32 :16;
    entry(padC_0,1);
    entry(AFSET,1);
    entry(MSLC,6);
    entry(MOBUF,8);
    //+10
    u32 :16;
    entry(LP,1);
    entry(SGC,2);
    entry(EG,13);
    //+14
    u32 :16;
    entry(CA,16);

    u8 pad_med_0[0x6C-4];

    //+80
    u32 :16;
    entry(DMEA_hi,7);
    entry(pad80_0,1);
    entry($TSCD,3);
    entry($T,1);
    entry(MRWINH,4);
    //+84
    u32 :16;
    entry(DMEA_lo,14);
    entry(pad84_0,2);
    //+88
    u32 :16;
    entry(DGATE,1);
    entry(DRGA,13);
    entry(pad88_0,2);
    //+8C
    u32 :16;
    entry(DDIR,1);
    entry(DLG,13);
    entry(pad8C_0,1);
    entry(DEXE,1);
    //+90
    u32 :16;
    entry(pad90_0,5);
    entry(TACTL,3);
    entry(TIMA,8);
    //+94
    u32 :16;
    entry(pad94_0,5);
    entry(TBCTL,3);
    entry(TIMB,8);
    //+98
    u32 :16;
    entry(pad98_0,5);
    entry(TCCTL,3);
    entry(TIMC,8);
    //+9C
    u32 :16;
    entry(pad9C_0,5);
    entry(SCIEB,11);
    //+A0
    u32 :16;
    entry(padA0_0,5);
    entry(SCIPD,11);
    //+A4
    u32 :16;
    entry(padA4_0,5);
    entry(SCIRE,11);
    //+A8
    u32 :16;
    entry(padA8_0,8);
    entry(SCILV0,8);
    //+AC
    u32 :16;
    entry(padAC_0,8);
    entry(SCILV1,8);
    //+B0
    u32 :16;
    entry(padB0_0,8);
    entry(SCILV2,8);
    //+B4
    u32 :16;
    entry(padB4_0,5)
    entry(MCIEB,11);
    //+B8
    u32 :16;
    entry(padB8_0,5)
    entry(MCIPD,11);
    //+BC
    u32 :16;
    entry(padBC_0,5)
    entry(MCIRE,11);

    u8 pad_lot_0[0x344-4];

    //+400
    u32 :16;
    entry(pad400_1,6);
    entry(VREG,2);
    entry(pad400_0,7);
    entry(AR,1);

    u8 pad_lot_1[0x100-4];

    //+500
    u32 :16;
    entry(pad500_0,8);
    entry(L7_r,1);
    entry(L6_r,1);
    entry(L5_r,1);
    entry(L4_r,1);
    entry(L3_r,1);
    entry(L2_r,1);
    entry(L1_r,1);
    entry(L0_r,1);
    //+504
    u32 :16;
    entry(pad504_0,7);
    entry(RP,1);
    entry(M7_r,1);
    entry(M6_r,1);
    entry(M5_r,1);
    entry(M4_r,1);
    entry(M3_r,1);
    entry(M2_r,1);
    entry(M1_r,1);
    entry(M0_r,1);
#else
    // Little-endian layout (PC)
    entry(MVOL,4);
    entry(VER,4);
    entry(DAC18B,1);
    entry(MEM8MB,1);
    entry(pad0_0,5);
    entry(Mono,1);
    u32 :16;
    entry(RBP,12);
    entry(pad1_0,1);
    entry(RBL,2);
    entry(TESTB0,1);
    u32 :16;
    entry(MIBUF,8);
    entry(MIEMP,1);
    entry(MIFUL,1);
    entry(MIOVF,1);
    entry(MOEMP,1);
    entry(MOFUL,1);
    entry(pad3_0,3);
    u32 :16;
    entry(MOBUF,8);
    entry(MSLC,6);
    entry(AFSET,1);
    entry(padC_0,1);
    u32 :16;
    entry(EG,13);
    entry(SGC,2);
    entry(LP,1);
    u32 :16;
    entry(CA,16);
    u32 :16;
    u8 pad_med_0[0x6C-4];
    entry(MRWINH,4);
    entry($T,1);
    entry($TSCD,3);
    entry(pad80_0,1);
    entry(DMEA_hi,7);
    u32 :16;
    entry(pad84_0,2);
    entry(DMEA_lo,14);
    u32 :16;
    entry(pad88_0,2);
    entry(DRGA,13);
    entry(DGATE,1);
    u32 :16;
    entry(DEXE,1);
    entry(pad8C_0,1);
    entry(DLG,13);
    entry(DDIR,1);
    u32 :16;
    entry(TIMA,8);
    entry(TACTL,3);
    entry(pad90_0,5);
    u32 :16;
    entry(TIMB,8);
    entry(TBCTL,3);
    entry(pad94_0,5);
    u32 :16;
    entry(TIMC,8);
    entry(TCCTL,3);
    entry(pad98_0,5);
    u32 :16;
    entry(SCIEB,11);
    entry(pad9C_0,5);
    u32 :16;
    entry(SCIPD,11);
    entry(padA0_0,5);
    u32 :16;
    entry(SCIRE,11);
    entry(padA4_0,5);
    u32 :16;
    entry(SCILV0,8);
    entry(padA8_0,8);
    u32 :16;
    entry(SCILV1,8);
    entry(padAC_0,8);
    u32 :16;
    entry(SCILV2,8);
    entry(padB0_0,8);
    u32 :16;
    entry(MCIEB,11);
    entry(padB4_0,5)
    u32 :16;
    entry(MCIPD,11);
    entry(padB8_0,5)
    u32 :16;
    entry(MCIRE,11);
    entry(padBC_0,5)
    u32 :16;
    u8 pad_lot_0[0x344-4];
    entry(AR,1);
    entry(pad400_0,7);
    entry(VREG,2);
    entry(pad400_1,6);
    u32 :16;
    u8 pad_lot_1[0x100-4];
    entry(L0_r,1);
    entry(L1_r,1);
    entry(L2_r,1);
    entry(L3_r,1);
    entry(L4_r,1);
    entry(L5_r,1);
    entry(L6_r,1);
    entry(L7_r,1);
    entry(pad500_0,8);
    u32 :16;
    entry(M0_r,1);
    entry(M1_r,1);
    entry(M2_r,1);
    entry(M3_r,1);
    entry(M4_r,1);
    entry(M5_r,1);
    entry(M6_r,1);
    entry(M7_r,1);
    entry(RP,1);
    entry(pad504_0,7);
    u32 :16;
#endif
};

// DSP data layout (endian-neutral, accessed via 32-bit words)
struct DSPData_struct
{
    u32 COEF[128];
    u32 MADRS[64];
    u8  PAD0[0x100];
    u32 MPRO[128*4];
    u8  PAD1[0x400];
    struct { u32 l; u32 h; } TEMP[128];
    struct { u32 l; u32 h; } MEMS[32];
    struct { u32 l; u32 h; } MIXS[16];
    u32 EFREG[16];
    u32 EXTS[2];
};

union InterruptInfo
{
    struct
    {
#ifdef WII
        u32 padding:21;
        entry(SAMPLE_DONE,1);
        entry(MIDI_OUT,1);
        entry(TimerC,1);
        entry(TimerB,1);
        entry(TimerA,1);
        entry(SCPU,1);
        entry(DMA_END,1);
        entry(MIDI_IN,1);
        entry(res_3,1);
        entry(res_1,1);
        entry(INTON,1);
#else
        entry(INTON,1);
        entry(res_1,1);
        entry(res_3,1);
        entry(MIDI_IN,1);
        entry(DMA_END,1);
        entry(SCPU,1);
        entry(TimerA,1);
        entry(TimerB,1);
        entry(TimerC,1);
        entry(MIDI_OUT,1);
        entry(SAMPLE_DONE,1);
#endif
    };
    u32 full;
};

extern InterruptInfo* MCIEB;
extern InterruptInfo* MCIPD;
extern InterruptInfo* MCIRE;
extern InterruptInfo* SCIEB;
extern InterruptInfo* SCIPD;
extern InterruptInfo* SCIRE;

#undef entry

extern CommonData_struct* CommonData;
extern DSPData_struct*    DSPData;

void FASTCALL UpdateAICA(u32 Cycles);

void AICA_Init();
void AICA_Term();
void libAICA_TimeStep();  // called by wii_audio.cpp once per sample

void WriteAicaReg8(u32 reg, u32 data);
extern bool e68k_out;

template<u32 sz>
void WriteAicaReg(u32 reg, u32 data);
