#include "sgc_if.h"
#include "dsp.h"
#include "aica_mem.h"
#include <math.h>
#undef FAR

//#define CLIP_WARN
#define key_printf(x...)  printf(x)
#define aeg_printf(x...)  /*printf(x);*/
#define step_printf(x...) /*printf(x);*/

#ifdef CLIP_WARN
#define clip_verify(x) verify(x)
#else
#define clip_verify(x)
#endif

SampleType mixl;
SampleType mixr;

// x.15
s32  volume_lut[16];
u32  SendLevel[16] = {0xF000<<3,14<<3,13<<3,12<<3,11<<3,10<<3,9<<3,8<<3,
                      7<<3,6<<3,5<<3,4<<3,3<<3,2<<3,1<<3,0<<3};
s32  tl_lut[256]; // xx.15 format

// AEG timing tables (ms)
float AEG_Attack_Time[] =
{
    -1,-1,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
    1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
    76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
    2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0
};
float AEG_DSR_Time[] =
{
    -1,-1,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
    14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
    920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
    28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1
};

#define AEG_STEP_BITS 16
u32 AEG_ATT_SPS[64];
u32 AEG_DSR_SPS[64];

const char* stream_names[] =
{
    "0: 16-bit PCM",
    "1: 8-bit PCM",
    "2: 4-bit ADPCM",
    "3: 4-bit ADPCM stream"
};

// x.8 format
const s32 adpcm_qs[8] =
{
    0x0e6,0x0e6,0x0e6,0x0e6,0x133,0x199,0x200,0x266,
};
const s32 adpcm_scale[16] =
{
    1,3,5,7,9,11,13,15,
    -1,-3,-5,-7,-9,-11,-13,-15,
};

void AICA_Sample();

#define well(a,bits)   (((a)+((1<<(bits-1))))>>(bits))
#define FPChop(a,bits) ((a)>>(bits))
#define FPs            FPChop
#define FPMul(a,b,bits) (FPs((a)*(b),(bits)))

#define VOLPAN(value,vol,pan,outl,outr) {s32 temp;         \
    temp=FPMul((value),volume_lut[(vol)],15);              \
    u32 t_pan=(pan);                                       \
    SampleType Sc=FPMul(temp,volume_lut[0xF-(t_pan&0xF)],15); \
    if (t_pan & 0x10) { outl+=temp; outr+=Sc; }           \
    else              { outl+=Sc;   outr+=temp; }          \
}

s16 pl=0, pr=0;

struct DSP_OUT_VOL_REG
{
#ifdef WII
    u32 pad:16;
    u32 res_2:4;
    u32 EFSDL:4;
    u32 res_1:3;
    u32 EFPAN:5;
#else
    u32 EFPAN:5;
    u32 res_1:3;
    u32 EFSDL:4;
    u32 res_2:4;
    u32 pad:16;
#endif
};
DSP_OUT_VOL_REG* dsp_out_vol;

#pragma pack(1)
struct ChannelCommonData
{
#ifdef WII
    //+00
    u32 pad_2:16;
    u32 KYONEX:1;
    u32 KYONB:1;
    u32 res_1:3;
    u32 SSCTL:1;
    u32 LPCTL:1;
    u32 PCMS:2;
    u32 SA_hi:7;
    //+04
    u32 pad_3:16;
    u32 SA_low:16;
    //+08
    u32 pad_4:16;
    u32 LSA:16;
    //+0C
    u32 pad_5:16;
    u32 LEA:16;
    //+10
    u32 pad_7:16;
    u32 D2R:5;
    u32 D1R:5;
    u32 res_2:1;
    u32 AR:5;
    //+14
    u32 pad_8:16;
    u32 res_3:1;
    u32 LPSLNK:1;
    u32 KRS:4;
    u32 DL:5;
    u32 RR:5;
    //+18
    u32 pad_9:16;
    u32 rez_8_2:1;
    u32 OCT:4;
    u32 rez_8_1:1;
    u32 FNS:10;
    //+1C
    u32 pad_10:16;
    u32 LFORE:1;
    u32 LFOF:5;
    u32 PLFOWS:2;
    u32 PLFOS:3;
    u32 ALFOWS:2;
    u32 ALFOS:3;
    //+20
    u32 pad_11:16;
    u32 rez_20_0:8;
    u32 IMXL:4;
    u32 ISEL:4;
    //+24
    u32 pad_12:16;
    u32 rez_24_1:4;
    u32 DISDL:4;
    u32 rez_24_0:3;
    u32 DIPAN:5;
    //+28
    u32 pad_13:16;
    u32 TL:8;
    u32 rez_28_0:3;
    u32 Q:5;
    //+2C
    u32 pad_14:16;
    u32 rez_2C_0:3;
    u32 FLV0:13;
    //+30
    u32 pad_15:16;
    u32 rez_30_0:3;
    u32 FLV1:13;
    //+34
    u32 pad_16:16;
    u32 rez_34_0:3;
    u32 FLV2:13;
    //+38
    u32 pad_17:16;
    u32 rez_38_0:3;
    u32 FLV3:13;
    //+3C
    u32 pad_18:16;
    u32 rez_3C_0:3;
    u32 FLV4:13;
    //+40
    u32 pad_19:16;
    u32 rez_40_1:3;
    u32 FAR:5;
    u32 rez_40_0:3;
    u32 FD1R:5;
    //+44
    u32 pad_20:16;
    u32 rez_44_1:3;
    u32 FD2R:5;
    u32 rez_44_0:3;
    u32 FRR:5;
#else
    //+00
    u32 SA_hi:7;
    u32 PCMS:2;
    u32 LPCTL:1;
    u32 SSCTL:1;
    u32 res_1:3;
    u32 KYONB:1;
    u32 KYONEX:1;
    u32 pad_2:16;
    //+04
    u32 SA_low:16;
    u32 pad_3:16;
    //+08
    u32 LSA:16;
    u32 pad_4:16;
    //+0C
    u32 LEA:16;
    u32 pad_5:16;
    //+10
    u32 AR:5;
    u32 res_2:1;
    u32 D1R:5;
    u32 D2R:5;
    u32 pad_7:16;
    //+14
    u32 RR:5;
    u32 DL:5;
    u32 KRS:4;
    u32 LPSLNK:1;
    u32 res_3:1;
    u32 pad_8:16;
    //+18
    u32 FNS:10;
    u32 rez_8_1:1;
    u32 OCT:4;
    u32 rez_8_2:1;
    u32 pad_9:16;
    //+1C
    u32 ALFOS:3;
    u32 ALFOWS:2;
    u32 PLFOS:3;
    u32 PLFOWS:2;
    u32 LFOF:5;
    u32 LFORE:1;
    u32 pad_10:16;
    //+20
    u32 ISEL:4;
    u32 IMXL:4;
    u32 rez_20_0:8;
    u32 pad_11:16;
    //+24
    u32 DIPAN:5;
    u32 rez_24_0:3;
    u32 DISDL:4;
    u32 rez_24_1:4;
    u32 pad_12:16;
    //+28
    u32 Q:5;
    u32 rez_28_0:3;
    u32 TL:8;
    u32 pad_13:16;
    //+2C
    u32 FLV0:13;
    u32 rez_2C_0:3;
    u32 pad_14:16;
    //+30
    u32 FLV1:13;
    u32 rez_30_0:3;
    u32 pad_15:16;
    //+34
    u32 FLV2:13;
    u32 rez_34_0:3;
    u32 pad_16:16;
    //+38
    u32 FLV3:13;
    u32 rez_38_0:3;
    u32 pad_17:16;
    //+3C
    u32 FLV4:13;
    u32 rez_3C_0:3;
    u32 pad_18:16;
    //+40
    u32 FD1R:5;
    u32 rez_40_0:3;
    u32 FAR:5;
    u32 rez_40_1:3;
    u32 pad_19:16;
    //+44
    u32 FRR:5;
    u32 rez_44_0:3;
    u32 FD2R:5;
    u32 rez_44_1:3;
    u32 pad_20:16;
#endif
};

enum _EG_state
{
    EG_Attack  = 0,
    EG_Decay1  = 1,
    EG_Decay2  = 2,
    EG_Release = 3
};

struct ChannelEx;

void (FASTCALL* STREAM_STEP_LUT[5][2][2])(ChannelEx* ch);
void (FASTCALL* STREAM_INITAL_STEP_LUT[5])(ChannelEx* ch);
void (FASTCALL* AEG_STEP_LUT[4])(ChannelEx* ch);
void (FASTCALL* FEG_STEP_LUT[4])(ChannelEx* ch);
void (FASTCALL* ALFOWS_CALC[4])(ChannelEx* ch);
void (FASTCALL* PLFOWS_CALC[4])(ChannelEx* ch);

struct ChannelEx
{
    static ChannelEx Chans[64];

    ChannelCommonData* ccd;

    u8*        SA;
    u32        CA;
    fp_22_10   step;
    u32        update_rate;

    SampleType s0, s1;

    struct
    {
        u32 LSA;
        u32 LEA;
        u8  looped;
    } loop;

    struct
    {
        s32 last_quant;
        void Reset(ChannelEx* ch)
        {
            last_quant = 127;
            ch->s0 = 0;
        }
    } adpcm;

    u32 noise_state;

    struct
    {
        u32        DLAtt;
        u32        DRAtt;
        u32        DSPAtt;
        SampleType* DSPOut;
    } VolMix;

    void (FASTCALL* StepAEG)(ChannelEx* ch);
    void (FASTCALL* StepFEG)(ChannelEx* ch);
    void (FASTCALL* StepStream)(ChannelEx* ch);
    void (FASTCALL* StepStreamInitial)(ChannelEx* ch);

    struct
    {
        s32 val;
        INLINE s32  GetValue()      { return val >> AEG_STEP_BITS; }
        INLINE void SetValue(u32 v) { val = v << AEG_STEP_BITS; }

        _EG_state state;
        u32 AttackRate;
        u32 Decay1Rate;
        u32 Decay2Value;
        u32 Decay2Rate;
        u32 ReleaseRate;
    } AEG;

    struct
    {
        s32        value;
        _EG_state  state;
    } FEG;

    struct
    {
        u32  counter;
        u32  start_value;
        u8   state;
        u8   alfo;
        u8   alfo_shft;
        u8   plfo;
        u8   plfo_shft;
        void (FASTCALL* alfo_calc)(ChannelEx* ch);
        void (FASTCALL* plfo_calc)(ChannelEx* ch);
        INLINE void Step(ChannelEx* ch)
        {
            counter--;
            if (counter == 0)
            {
                state++;
                counter = start_value;
                alfo_calc(ch);
                plfo_calc(ch);
            }
        }
        void Reset(ChannelEx* ch)
        {
            state = 0;
            counter = start_value;
            alfo_calc(ch);
            plfo_calc(ch);
        }
        void SetStartValue(u32 nv) { start_value = nv; counter = start_value; }
    } lfo;

    bool enabled;
    int  ChanelNumber;

    void Init(int cn, u8* ccd_raw)
    {
        ccd = (ChannelCommonData*)&ccd_raw[cn * 0x80];
        ChanelNumber = cn;
        for (u32 i = 0; i < 0x80; i++)
            RegWrite(i);
        disable();
    }
    void disable()
    {
        enabled = false;
        SetAegState(EG_Release);
        AEG.SetValue(0x3FF);
    }
    void enable() { enabled = true; }

    INLINE SampleType InterpolateSample()
    {
        u32 fp = step.fp;
        SampleType rv = FPMul(s0, (s32)(1024 - fp), 10);
        rv += FPMul(s1, (s32)(fp), 10);
        return rv;
    }

    INLINE void Step()
    {
        if (!enabled) return;

        SampleType sample = InterpolateSample();

        u32 ofsatt = lfo.alfo + (AEG.GetValue() >> 2);
        ofsatt = min(ofsatt, 255u);

        u32 const max_att = ((16 << 4) - 1) - ofsatt;
        s32* logtable = ofsatt + tl_lut;

        u32 dl = min(VolMix.DLAtt,  max_att);
        u32 dr = min(VolMix.DRAtt,  max_att);
        u32 ds = min(VolMix.DSPAtt, max_att);

        SampleType oLeft  = FPMul(sample, logtable[dl], 15);
        SampleType oRight = FPMul(sample, logtable[dr], 15);
        SampleType oDsp   = FPMul(sample, logtable[ds], 15);

        if ((AEG.state != EG_Attack) && (AEG.state != EG_Release))
        {
            if ((this->AEG.Decay2Value == 0) && ((s64)(oLeft + oRight + oDsp) == 0))
                this->SetAegState(EG_Attack);
        }

        *VolMix.DSPOut += oDsp;
        mixl += oLeft;
        mixr += oRight;

        StepAEG(this);
        StepFEG(this);
        StepStream(this);
        lfo.Step(this);
    }

    INLINE void Generate()    { Step(); }
    INLINE static void GenerateAll()
    {
        for (int i = 0; i < 64; i++)
            Chans[i].Generate();
    }

    void SetAegState(_EG_state newstate)
    {
        StepAEG    = AEG_STEP_LUT[newstate];
        AEG.state  = newstate;
        if (newstate == EG_Release)
            ccd->KYONB = 0;
    }
    void SetFegState(_EG_state newstate)
    {
        StepFEG   = FEG_STEP_LUT[newstate];
        FEG.state = newstate;
    }

    void KEY_ON()
    {
        if (AEG.state == EG_Release)
        {
            enable();
            SetAegState(EG_Attack);
            AEG.SetValue(0x3FF);
            SetFegState(EG_Attack);

            CA = 0;
            step.full = 0;
            loop.looped = false;
            adpcm.Reset(this);
            StepStreamInitial(this);

            // Filter removed: the buggy SFX never showed up while SA=0x2A04E
            // was excluded, meaning it likely reuses that same raw waveform
            // (a single wavetable sample pitched/retriggered for both BGM
            // notes and some SFX is common). Log everything again and
            // correlate by timing/channel instead of by address.
            key_printf("[AICA] KEY_ON  ch=%2d fmt=%s %s SA=0x%05X LSA=%u LEA=%u loopLen=%d TL=%u OCT=%d FNS=%u "
                       "KRS=%u DL=%u D1R=%u D2R=%u RR=%u effRR=%u\n",
                       ChanelNumber,
                       ccd->SSCTL ? "noise" : stream_names[ccd->PCMS],
                       ccd->LPCTL ? "loop" : "one-shot",
                       (ccd->SA_hi << 16) | ccd->SA_low,
                       ccd->LSA, ccd->LEA, (int)ccd->LEA - (int)ccd->LSA,
                       ccd->TL, ccd->OCT, ccd->FNS,
                       ccd->KRS, ccd->DL, ccd->D1R, ccd->D2R, ccd->RR, AEG_EffRate(ccd->RR));
        }
    }
    void KEY_OFF()
    {
        if (AEG.state != EG_Release)
        {
            key_printf("[AICA] KEY_OFF ch=%2d\n", ChanelNumber);
            SetAegState(EG_Release);
        }
    }

    void UpdateStreamStep()
    {
        s32 fmt = ccd->PCMS;
        if (ccd->SSCTL) fmt = 4;
        StepStream        = STREAM_STEP_LUT[fmt][ccd->LPCTL][ccd->LPSLNK];
        StepStreamInitial = STREAM_INITAL_STEP_LUT[fmt];
    }
    void UpdateSA()
    {
        u32 addr = (ccd->SA_hi << 16) | ccd->SA_low;
        if (ccd->PCMS == 0) addr &= ~1;
        SA = &aica_ram_l[addr];
    }
    void UpdateLoop()
    {
        loop.LSA = ccd->LSA;
        loop.LEA = ccd->LEA;
    }
    u32 AEG_EffRate(u32 re)
    {
        // Key-rate scaling per verified SCSP/AICA reference: when KRS==0xF,
        // octave/FNS contribution is disabled entirely (rate = 2*re only).
        // The previous formula applied the octave term even at KRS==0xF,
        // which for a KRS=0xF (no key-tracking) instrument at a negative
        // octave (OCT=15 => oct=-1, exactly what ChuChu Rocket's chime/BGM
        // instrument uses) skews the computed rate lower than real hardware
        // — i.e. decays/releases slower than intended. With several
        // overlapping chime notes fired in quick succession (a button-press
        // arpeggio), a too-slow release means each note keeps ringing well
        // after the next starts, smearing into what sounds like an
        // echo/slow-motion wash instead of a crisp jingle.
        s32 rate_offset = 0;
        if (ccd->KRS != 0xF)
        {
            s32 oct = (s32)(ccd->OCT ^ 8) - 8;   // sign-extend 4-bit OCT
            rate_offset = oct + 2 * (s32)ccd->KRS + ((ccd->FNS >> 9) & 1);
        }
        s32 rv = (re << 1) + rate_offset;
        if (rv < 0)    rv = 0;
        if (rv > 0x3f) rv = 0x3f;
        return rv;
    }
    void UpdateAEG()
    {
        AEG.AttackRate  = AEG_ATT_SPS[AEG_EffRate(ccd->AR)];
        AEG.Decay1Rate  = AEG_DSR_SPS[AEG_EffRate(ccd->D1R)];
        AEG.Decay2Value = ccd->DL << 5;
        AEG.Decay2Rate  = AEG_DSR_SPS[AEG_EffRate(ccd->D2R)];
        AEG.ReleaseRate = AEG_DSR_SPS[AEG_EffRate(ccd->RR)];
    }
    void UpdatePitch()
    {
        u32 oct = ccd->OCT;
        update_rate = 1024 | ccd->FNS;
        if (oct & 8) update_rate >>= (16 - oct);
        else         update_rate <<= oct;
    }
    void UpdateLFO()
    {
        int N = ccd->LFOF;
        int S = N >> 2;
        int M = (~N) & 3;
        int G = 128 >> S;
        int L = (G - 1) << 2;
        int O = L + G * (M + 1);
        lfo.SetStartValue(O);

        lfo.plfo_shft   = 8 - ccd->PLFOS;
        lfo.alfo_shft   = 8 - ccd->ALFOS;
        lfo.alfo_calc   = ALFOWS_CALC[ccd->ALFOWS];
        lfo.plfo_calc   = PLFOWS_CALC[ccd->PLFOWS];

        if (ccd->LFORE)
            lfo.Reset(this);
        else
        {
            lfo.alfo_calc(this);
            lfo.plfo_calc(this);
        }
        ccd->LFORE = 0;
    }
    void UpdateDSPMIX()
    {
        VolMix.DSPOut = &dsp.MIXS[ccd->ISEL];
    }
    void UpdateAtts()
    {
        u32 attFull = ccd->TL + SendLevel[ccd->DISDL];
        u32 attPan  = attFull + SendLevel[(~ccd->DIPAN) & 0xF];

        if (ccd->DIPAN & 0x10)
        {
            VolMix.DLAtt = attFull;
            VolMix.DRAtt = attPan;
        }
        else
        {
            VolMix.DLAtt = attPan;
            VolMix.DRAtt = attFull;
        }
        VolMix.DSPAtt = ccd->TL + SendLevel[ccd->IMXL];
    }
    void UpdateFEG() { /* TODO */ }

    void RegWrite(u32 offset)
    {
        switch (offset)
        {
        case 0x00:
            UpdateStreamStep(); UpdateSA(); break;
        case 0x01:
            UpdateStreamStep(); UpdateSA();
            if (ccd->KYONEX)
            {
                ccd->KYONEX = 0;
                for (int i = 0; i < 64; i++)
                {
                    if (Chans[i].ccd->KYONB) Chans[i].KEY_ON();
                    else                      Chans[i].KEY_OFF();
                }
            }
            break;
        case 0x04: case 0x05:
            UpdateSA(); break;
        case 0x08: case 0x09: case 0x0C: case 0x0D:
            UpdateLoop(); break;
        case 0x10: case 0x11:
            UpdateAEG(); break;
        case 0x14: case 0x15:
            UpdateStreamStep(); UpdateAEG(); break;
        case 0x18: case 0x19:
            UpdatePitch(); break;
        case 0x1C: case 0x1D:
            UpdateLFO(); break;
        case 0x20:
            UpdateDSPMIX(); UpdateAtts(); break;
        case 0x24: case 0x25:
            UpdateAtts(); break;
        case 0x28:
            UpdateFEG(); break;
        case 0x29:
            UpdateAtts(); break;
        case 0x2C: case 0x2D: case 0x30: case 0x31:
        case 0x34: case 0x35: case 0x38: case 0x39:
        case 0x3C: case 0x3D: case 0x40: case 0x41:
        case 0x44: case 0x45:
            UpdateFEG(); break;
        }
    }
};

INLINE SampleType DecodeADPCM(u32 sample, s32 prev, s32& quant)
{
    s32 sign = 1 - ((sample >> 3) << 1);
    u32 data = sample & 7;
    SampleType rv = prev + (sign * ((quant * adpcm_scale[data]) >> 3));
    quant = (quant * adpcm_qs[data]) >> 8;
    clip(quant, 127, 24576);
    clip16(rv);
    return rv;
}

template<s32 PCMS, bool last>
INLINE void FASTCALL StepDecodeSample(ChannelEx* ch, u32 CA)
{
    if (!last && PCMS < 2) return;

    s16* sptr16 = (s16*)ch->SA;
    s8*  sptr8  = (s8*) ch->SA;
    u8*  uptr8  = (u8*) ch->SA;

    SampleType s0, s1;
    switch (PCMS)
    {
    case -1:
        ch->noise_state = ch->noise_state * 16807 + 0xbeef;
        s0 = ch->noise_state; s0 >>= 16;
        s1 = ch->noise_state * 16807 + 0xbeef; s1 >>= 16;
        break;
    case 0:
        s0 = sptr16[(CA + 0) ^ 1];
        s1 = sptr16[(CA + 1) ^ 1];
        break;
    case 1:
        s0 = sptr8[(CA + 0) ^ 3] << 8;
        s1 = sptr8[(CA + 1) ^ 3] << 8;
        break;
    case 2: case 3:
        {
            u8 ad1 = uptr8[(CA >> 1) ^ 3];
            u8 ad2 = uptr8[((CA + 1) >> 1) ^ 3];
            u8 sf  = (CA & 1) << 2;
            ad1 >>= sf;
            ad2 >>= 4 - sf;
            ad1 &= 0xF;
            ad2 &= 0xF;
            s32 q = ch->adpcm.last_quant;
            s0 = DecodeADPCM(ad1, ch->s0, q);
            ch->adpcm.last_quant = q;
            s1 = last ? DecodeADPCM(ad2, s0, q) : 0;
        }
        break;
    }
    ch->s0 = s0;
    ch->s1 = s1;
}

template<s32 PCMS>
void FASTCALL StepDecodeSampleInitial(ChannelEx* ch)
{
    StepDecodeSample<PCMS, true>(ch, 0);
}

template<s32 PCMS, u32 LPCTL, u32 LPSLNK>
void FASTCALL StreamStep(ChannelEx* ch)
{
    ch->step.full += ch->update_rate;
    fp_22_10 sp = ch->step;
    ch->step.ip = 0;

    while (sp.ip > 0)
    {
        sp.ip--;

        u32 CA   = ch->CA + 1;
        u32 ca_t = CA;
        if (PCMS == 3) ca_t &= ~3;

        if (LPSLNK)
        {
            if ((ch->AEG.state == EG_Attack) && (CA >= ch->loop.LSA))
            {
                step_printf("[%d]LPSLNK->Decay1\n", ch->ChanelNumber);
                ch->SetAegState(EG_Decay1);
            }
        }
        if (ca_t >= ch->loop.LEA)
        {
            ch->loop.looped = 1;
            CA = ch->loop.LSA;
            if (LPCTL)
            {
                if (PCMS == 2) ch->adpcm.Reset(ch);
            }
            else
            {
                ch->disable();
            }
        }
        ch->CA = CA;
        if (sp.ip == 0)
            StepDecodeSample<PCMS, true>(ch, CA);
        else
            StepDecodeSample<PCMS, false>(ch, CA);
    }
}

template<s32 ALFOWS>
void FASTCALL CalcAlfo(ChannelEx* ch)
{
    u32 rv;
    switch (ALFOWS)
    {
    case 0: rv = ch->lfo.state; break;
    case 1: rv = ch->lfo.state & 0x80 ? 255 : 0; break;
    case 2: rv = (ch->lfo.state & 0x7f) ^ (ch->lfo.state & 0x80 ? 0x7F : 0); rv <<= 1; break;
    case 3: rv = (ch->lfo.state >> 3) ^ (ch->lfo.state << 3) ^ (ch->lfo.state & 0xE3); break;
    default: rv = 0; break;
    }
    ch->lfo.alfo = rv >> ch->lfo.alfo_shft;
}

template<s32 PLFOWS>
void FASTCALL CalcPlfo(ChannelEx* ch)
{
    u32 rv;
    switch (PLFOWS)
    {
    case 0: rv = ch->lfo.state; break;
    case 1: rv = ch->lfo.state & 0x80 ? 0x80 : 0x7F; break;
    case 2:
        rv = (ch->lfo.state & 0x7f) ^ (ch->lfo.state & 0x80 ? 0x7F : 0);
        rv <<= 1;
        rv = (u8)(rv - 0x80);
        break;
    case 3: rv = (ch->lfo.state >> 3) ^ (ch->lfo.state << 3) ^ (ch->lfo.state & 0xE3); break;
    default: rv = 0; break;
    }
    ch->lfo.alfo = rv >> ch->lfo.plfo_shft;
}

template<u32 state>
void FASTCALL AegStep(ChannelEx* ch)
{
    switch (state)
    {
    case EG_Attack:
        ch->AEG.val -= ch->AEG.AttackRate;
        if (ch->AEG.GetValue() <= 0)
        {
            ch->AEG.SetValue(0);
            if (!ch->ccd->LPSLNK)
                ch->SetAegState(EG_Decay1);
        }
        break;
    case EG_Decay1:
        ch->AEG.val += ch->AEG.Decay1Rate;
        if (((u32)ch->AEG.GetValue()) >= ch->AEG.Decay2Value)
            ch->SetAegState(EG_Decay2);
        break;
    case EG_Decay2:
        ch->AEG.val += ch->AEG.Decay2Rate;
        if (ch->AEG.GetValue() >= 0x3FF)
        {
            ch->AEG.SetValue(0x3FF);
            ch->SetAegState(EG_Release);
        }
        break;
    case EG_Release:
        ch->AEG.val += ch->AEG.ReleaseRate;
        if (ch->AEG.GetValue() >= 0x3FF)
        {
            ch->AEG.SetValue(0x3FF);
            ch->disable();
        }
        break;
    }
}

template<u32 state>
void FASTCALL FegStep(ChannelEx* ch) { /* stub */ }

void staticinitialise()
{
    STREAM_STEP_LUT[0][0][0]=&StreamStep<0,0,0>; STREAM_STEP_LUT[1][0][0]=&StreamStep<1,0,0>;
    STREAM_STEP_LUT[2][0][0]=&StreamStep<2,0,0>; STREAM_STEP_LUT[3][0][0]=&StreamStep<3,0,0>;
    STREAM_STEP_LUT[4][0][0]=&StreamStep<-1,0,0>;

    STREAM_STEP_LUT[0][0][1]=&StreamStep<0,0,1>; STREAM_STEP_LUT[1][0][1]=&StreamStep<1,0,1>;
    STREAM_STEP_LUT[2][0][1]=&StreamStep<2,0,1>; STREAM_STEP_LUT[3][0][1]=&StreamStep<3,0,1>;
    STREAM_STEP_LUT[4][0][1]=&StreamStep<-1,0,1>;

    STREAM_STEP_LUT[0][1][0]=&StreamStep<0,1,0>; STREAM_STEP_LUT[1][1][0]=&StreamStep<1,1,0>;
    STREAM_STEP_LUT[2][1][0]=&StreamStep<2,1,0>; STREAM_STEP_LUT[3][1][0]=&StreamStep<3,1,0>;
    STREAM_STEP_LUT[4][1][0]=&StreamStep<-1,1,0>;

    STREAM_STEP_LUT[0][1][1]=&StreamStep<0,1,1>; STREAM_STEP_LUT[1][1][1]=&StreamStep<1,1,1>;
    STREAM_STEP_LUT[2][1][1]=&StreamStep<2,1,1>; STREAM_STEP_LUT[3][1][1]=&StreamStep<3,1,1>;
    STREAM_STEP_LUT[4][1][1]=&StreamStep<-1,1,1>;

    STREAM_INITAL_STEP_LUT[0]=&StepDecodeSampleInitial<0>;
    STREAM_INITAL_STEP_LUT[1]=&StepDecodeSampleInitial<1>;
    STREAM_INITAL_STEP_LUT[2]=&StepDecodeSampleInitial<2>;
    STREAM_INITAL_STEP_LUT[3]=&StepDecodeSampleInitial<3>;
    STREAM_INITAL_STEP_LUT[4]=&StepDecodeSampleInitial<-1>;

    AEG_STEP_LUT[0]=&AegStep<0>; AEG_STEP_LUT[1]=&AegStep<1>;
    AEG_STEP_LUT[2]=&AegStep<2>; AEG_STEP_LUT[3]=&AegStep<3>;

    FEG_STEP_LUT[0]=&FegStep<0>; FEG_STEP_LUT[1]=&FegStep<1>;
    FEG_STEP_LUT[2]=&FegStep<2>; FEG_STEP_LUT[3]=&FegStep<3>;

    ALFOWS_CALC[0]=&CalcAlfo<0>; ALFOWS_CALC[1]=&CalcAlfo<1>;
    ALFOWS_CALC[2]=&CalcAlfo<2>; ALFOWS_CALC[3]=&CalcAlfo<3>;

    PLFOWS_CALC[0]=&CalcPlfo<0>; PLFOWS_CALC[1]=&CalcPlfo<1>;
    PLFOWS_CALC[2]=&CalcPlfo<2>; PLFOWS_CALC[3]=&CalcPlfo<3>;
}

#define AicaChannel ChannelEx
AicaChannel AicaChannel::Chans[64];
#define Chans AicaChannel::Chans

double dbToval(double db) { return pow(10, db / 20.0); }

u32 CalcAegSteps(float t)
{
    const double aeg_allsteps = 1024 * (1 << AEG_STEP_BITS) - 1;
    if (t < 0)  return 0;
    if (t == 0) return (u32)aeg_allsteps;
    double scnt  = 44.1 * t;
    double steps = aeg_allsteps / scnt;
    return (u32)(steps + 0.5);
}

void sgc_Init()
{
    staticinitialise();

    for (s32 i = 0; i < 16; i++)
    {
        volume_lut[i] = (s32)((1 << 15) / pow(2.0, (15 - i) / 2.0));
        if (i == 0) volume_lut[i] = 0;
    }
    for (s32 i = 0; i < 256; i++)
        tl_lut[i] = (s32)((1 << 15) / pow(2.0, i / 16.0));

    for (int i = 0; i < 64; i++)
    {
        AEG_ATT_SPS[i] = CalcAegSteps(AEG_Attack_Time[i]);
        AEG_DSR_SPS[i] = CalcAegSteps(AEG_DSR_Time[i]);
    }
    for (int i = 0; i < 64; i++)
        Chans[i].Init(i, aica_reg);

    dsp_out_vol = (DSP_OUT_VOL_REG*)&aica_reg[0x2000];
    dsp_init();
}

void sgc_Term() {}

void WriteChannelReg8(u32 channel, u32 reg)
{
    Chans[channel].RegWrite(reg);
}

void ReadCommonReg(u32 reg, bool byte)
{
    switch (reg)
    {
    case 0x2808: case 0x2809:
        CommonData->MIEMP = 1;
        CommonData->MOEMP = 1;
        break;
    case 0x2810: case 0x2811:
        {
            u32 chan = CommonData->MSLC;
            CommonData->LP  = Chans[chan].loop.looped;
            CommonData->EG  = Chans[chan].AEG.GetValue();
            CommonData->SGC = Chans[chan].AEG.state;
            if (!(byte && reg == 0x2810))
                Chans[chan].loop.looped = 0;
        }
        break;
    case 0x2814: case 0x2815:
        {
            u32 chan = CommonData->MSLC;
            CommonData->CA = Chans[chan].CA;
        }
        break;
    }
}

void WriteCommonReg8(u32 reg, u32 data)
{
    WriteMemArr(aica_reg, reg, data, 1);
    if (reg == 0x2804 || reg == 0x2805)
    {
        dsp.RBL      = (8192 << CommonData->RBL) - 1;
        dsp.RBP      = (CommonData->RBP * 2048) & AICA_RAM_MASK;
        dsp.dyndirty = true;
    }
}

#define CDDA_SIZE  (2352/2)
s16 cdda_sector[CDDA_SIZE] = {0};
u32 cdda_index = CDDA_SIZE << 1;

void AICA_Sample()
{
    mixl = 0;
    mixr = 0;
    memset(dsp.MIXS, 0, sizeof(dsp.MIXS));

    ChannelEx::GenerateAll();

    // CDDA input — aica_init_params on Wii has no CDDA_Sector callback.
    // Fill with silence; CDDA audio via GD-ROM is handled elsewhere.
    if (cdda_index >= CDDA_SIZE)
    {
        cdda_index = 0;
        memset(cdda_sector, 0, sizeof(cdda_sector));
    }
    s32 EXTS0L = cdda_sector[cdda_index];
    s32 EXTS0R = cdda_sector[cdda_index + 1];
    cdda_index += 2;

    // CDDA mix
    if (aica_settings.CDDAMute == 0)
    {
        VOLPAN(EXTS0L, dsp_out_vol[16].EFSDL, dsp_out_vol[16].EFPAN, mixl, mixr);
        VOLPAN(EXTS0R, dsp_out_vol[17].EFSDL, dsp_out_vol[17].EFPAN, mixl, mixr);
    }

    // DSP effects
    if (aica_settings.DSPEnabled)
    {
        dsp_step();
        for (int i = 0; i < 16; i++)
        {
            VOLPAN((*(s16*)&DSPData->EFREG[i]), dsp_out_vol[i].EFSDL, dsp_out_vol[i].EFPAN, mixl, mixr);
        }
    }

    // Mono
    if (CommonData->Mono)
    {
        mixl += mixr;
        mixr = mixl;
    }

    // Master volume
    u32 mvol = CommonData->MVOL;
    s32 val  = (s32)(volume_lut[mvol] * aica_settings.Volume / 100.0f);

    mixl = (s32)FPMul((s64)mixl, val, 15);
    mixr = (s32)FPMul((s64)mixr, val, 15);

    if (CommonData->DAC18B)
    {
        mixl = FPs(mixl, 2);
        mixr = FPs(mixr, 2);
    }

    clip16(mixl);
    clip16(mixr);

    pl = mixl;
    pr = mixr;

    wii_WriteSample((s16)mixr, (s16)mixl);
}
