#include "aica.h"
#include "sgc_if.h"
#include "aica_mem.h"
#include "wii/wii_audio.h"
#include <math.h>

#define SH4_IRQ_BIT (1<<(u8)holly_SPU_IRQ)

CommonData_struct* CommonData;
DSPData_struct*    DSPData;
InterruptInfo*     MCIEB;
InterruptInfo*     MCIPD;
InterruptInfo*     MCIRE;
InterruptInfo*     SCIEB;
InterruptInfo*     SCIPD;
InterruptInfo*     SCIRE;

// ---- Interrupts ----

u32 GetL(u32 witch)
{
    if (witch > 7)
        witch = 7;

    u32 bit = 1 << witch;
    u32 rv  = 0;

    if (CommonData->SCILV0 & bit) rv  = 1;
    if (CommonData->SCILV1 & bit) rv |= 2;
    if (CommonData->SCILV2 & bit) rv |= 4;

    return rv;
}

// ArmInterruptChange is provided by the vbaARM plugin (arm_mem.cpp). It sets
// the AICA interrupt-pending state which raises the ARM7 FIQ. The wrestler
// test (and real games) program SCIEB + timers and then wait for this FIQ to
// drive their main loop, so this MUST be wired up or the ARM7 spins in init
// and never reaches KeyOn.
// NOTE: declared with C++ linkage (NOT extern "C") to match the definition in
// vbaARM/arm_mem.cpp; an extern "C" wrapper here breaks the link.
void FASTCALL ArmInterruptChange(u32 bits, u32 L);

void update_arm_interrupts()
{
    u32 p_ints = SCIEB->full & SCIPD->full;

    u32 Lval = 0;
    if (p_ints)
    {
        u32 bit_value = 1;
        for (u32 i = 0; i < 11; i++)
        {
            if (p_ints & bit_value)
            {
                Lval = GetL(i);
                break;
            }
            bit_value <<= 1;
        }
    }

    ArmInterruptChange(p_ints, Lval);
}

void UpdateSh4Ints()
{
    u32 p_ints = MCIEB->full & MCIPD->full;
    if (p_ints)
    {
        if ((*aica_params.SB_ISTEXT & SH4_IRQ_BIT) == 0)
            aica_params.RaiseInterrupt(holly_SPU_IRQ);
    }
    else
    {
        if (*aica_params.SB_ISTEXT & SH4_IRQ_BIT)
            aica_params.CancelInterrupt(holly_SPU_IRQ);
    }
}

// ---- Timers ----

struct AicaTimerData
{
    union
    {
        struct
        {
#ifdef WII
            u32 pad:16;
            u32 nil:5;
            u32 md:3;
            u32 count:8;
#else
            u32 count:8;
            u32 md:3;
            u32 nil:5;
            u32 pad:16;
#endif
        };
        u32 data;
    };
};

class AicaTimer
{
public:
    AicaTimerData* data;
    s32  c_step;
    u32  m_step;
    u32  id;

    void Init(u8* regbase, u32 timer)
    {
        data   = (AicaTimerData*)&regbase[0x2890 + timer * 4];
        id     = timer;
        m_step = 1 << (data->md);
        c_step = m_step;
    }

    void StepTimer()
    {
        c_step--;
        if (c_step == 0)
        {
            c_step = m_step;
            data->count++;
            if (data->count == 0)
            {
                if      (id == 0) { SCIPD->TimerA = 1; MCIPD->TimerA = 1; }
                else if (id == 1) { SCIPD->TimerB = 1; MCIPD->TimerB = 1; }
                else              { SCIPD->TimerC = 1; MCIPD->TimerC = 1; }
            }
        }
    }

    void RegisterWrite()
    {
        u32 n_step = 1 << (data->md);
        if (n_step != m_step)
        {
            m_step = n_step;
            c_step = m_step;
        }
    }
};

AicaTimer timers[3];

// ---- Main loop ----

void FASTCALL UpdateAICA(u32 Samples)
{
    while (Samples > 0)
    {
        Samples--;

        AICA_Sample();
        SCIPD->SAMPLE_DONE = 1;

        for (int i = 0; i < 3; i++)
            timers[i].StepTimer();
    }

    update_arm_interrupts();
    UpdateSh4Ints();
}

// ---- Register writes ----

template<u32 sz>
void WriteAicaReg(u32 reg, u32 data)
{
    switch (reg)
    {
    case SCIPD_addr:
        verify(sz != 1);
        if (data & (1 << 5))
        {
            SCIPD->SCPU = 1;
            update_arm_interrupts();
        }
        return; // read-only

    case SCIRE_addr:
        verify(sz != 1);
        SCIPD->full &= ~data;
        data = 0;
        update_arm_interrupts();
        break;

    case MCIPD_addr:
        if (data & (1 << 5))
        {
            verify(sz != 1);
            MCIPD->SCPU = 1;
            UpdateSh4Ints();
        }
        return; // read-only

    case MCIRE_addr:
        verify(sz != 1);
        MCIPD->full &= ~data;
        UpdateSh4Ints();
        break;

    case TIMER_A:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[0].RegisterWrite();
        break;

    case TIMER_B:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[1].RegisterWrite();
        break;

    case TIMER_C:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[2].RegisterWrite();
        break;

    default:
        WriteMemArr(aica_reg, reg, data, sz);
        break;
    }
}

template void WriteAicaReg<1>(u32 reg, u32 data);
template void WriteAicaReg<2>(u32 reg, u32 data);

// ---- Init / Term ----

void AICA_Init()
{
    printf("[AICA] AICA_Init: start\n");
    printf("[AICA] AICA_Init: sizeof(CommonData)=%d (need 0x508=%d)\n", (int)sizeof(*CommonData), 0x508);
    printf("[AICA] AICA_Init: sizeof(DSPData)=%d (need 0x15C8=%d)\n",   (int)sizeof(*DSPData),    0x15C8);

    verify(sizeof(*CommonData) == 0x508);
    verify(sizeof(*DSPData)    == 0x15C8);
    printf("[AICA] AICA_Init: struct sizes OK\n");

    printf("[AICA] AICA_Init: aica_reg ptr = %p\n", (void*)aica_reg);
    CommonData = (CommonData_struct*)&aica_reg[0x2800];
    DSPData    = (DSPData_struct*)   &aica_reg[0x3000];
    SCIEB = (InterruptInfo*)&aica_reg[0x289C];
    SCIPD = (InterruptInfo*)&aica_reg[0x289C + 4];
    SCIRE = (InterruptInfo*)&aica_reg[0x289C + 8];
    MCIEB = (InterruptInfo*)&aica_reg[0x28B4];
    MCIPD = (InterruptInfo*)&aica_reg[0x28B4 + 4];
    MCIRE = (InterruptInfo*)&aica_reg[0x28B4 + 8];
    printf("[AICA] AICA_Init: pointers set\n");

    printf("[AICA] AICA_Init: sgc_Init()...\n");
    sgc_Init();
    printf("[AICA] AICA_Init: sgc_Init() done\n");

    printf("[AICA] AICA_Init: timers init...\n");
    for (int i = 0; i < 3; i++)
        timers[i].Init(aica_reg, i);
    printf("[AICA] AICA_Init: timers done\n");

    printf("[AICA] AICA_Init: wii_audio_aica_ready()...\n");
    wii_audio_aica_ready();
    printf("[AICA] AICA_Init: done\n");
}

void AICA_Term()
{
    sgc_Term();
}

// Steps one AICA sample, ticks timers, sets SAMPLE_DONE, and updates the
// ARM7 / SH4 interrupt lines. Driven once per sample from the SH4 timeslice
// (armUpdateARM) — see [[aica-arm-timing]].
void libAICA_TimeStep()
{
    AICA_Sample();
    SCIPD->SAMPLE_DONE = 1;

    for (int i = 0; i < 3; i++)
        timers[i].StepTimer();

    update_arm_interrupts();
    UpdateSh4Ints();
}
