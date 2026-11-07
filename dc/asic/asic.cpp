/*
    asic.cpp - Dreamcast Holly ASIC interrupt controller
    Bridges hardware interrupts from Holly peripherals to the SH4 CPU.
*/

#include <stdio.h>   // [IRQ] diagnostic printf
#include "types.h"
#include "asic.h"
#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/maple/maple_if.h"

// Re-evaluate and forward all three IRL lines to the SH4.
// Called after any change to IST or IML registers.
static inline void asic_UpdatePending()
{
    // RL6 -> IRL9
    InterruptPend(sh4_IRL_9,
        ((SB_ISTNRM & SB_IML6NRM) | (SB_ISTERR & SB_IML6ERR) | (SB_ISTEXT & SB_IML6EXT)) != 0);

    // RL4 -> IRL11
    InterruptPend(sh4_IRL_11,
        ((SB_ISTNRM & SB_IML4NRM) | (SB_ISTERR & SB_IML4ERR) | (SB_ISTEXT & SB_IML4EXT)) != 0);

    // RL2 -> IRL13
    InterruptPend(sh4_IRL_13,
        ((SB_ISTNRM & SB_IML2NRM) | (SB_ISTERR & SB_IML2ERR) | (SB_ISTEXT & SB_IML2EXT)) != 0);
}

static void RaiseAsicNormal(HollyInterruptID inter)
{
    if (inter == holly_SCANINT2)
        maple_vblank();

    SB_ISTNRM |= 1u << (u8)inter;
    asic_UpdatePending();
}

static void RaiseAsicExt(HollyInterruptID inter)
{
    SB_ISTEXT |= 1u << (u8)inter;
    asic_UpdatePending();
}

static void RaiseAsicErr(HollyInterruptID inter)
{
    SB_ISTERR |= 1u << (u8)inter;
    asic_UpdatePending();
}

// ── [IRQ] diagnostic: Holly IRQs raised per second ───────────────────────────
// TEMPORARY, for "high SPEED / low RPS" (Star Wars Arcade): which events does
// the guest actually get, and at what rate? One increment per raised IRQ
// (a few hundred/s), printed once a second from SPG.cpp's stats block via
// asic_irq_diag_dump(). IRQ_DIAG = 1 armed, 0 compiled-out.
// Star Wars Arcade: all IRQs fired at normal rates (ruled out missing events);
// gated off again, set to 1 to re-arm (prints "[IRQ] ARMED").
static const int IRQ_DIAG = 0;
static u32 s_irq_cnt[3][32];
static u32 s_irq_armed = 0;

void fastcall asic_RaiseInterrupt(HollyInterruptID inter)
{
    if (IRQ_DIAG && (u32)(inter >> 8) < 3)
        s_irq_cnt[(u32)(inter >> 8)][(u32)inter & 31]++;

    switch (inter >> 8)
    {
    case 0: RaiseAsicNormal(inter); break;
    case 1: RaiseAsicExt(inter);    break;
    case 2: RaiseAsicErr(inter);    break;
    }
}

// Once a second (SPG.cpp). Prints every IRQ that fired, with its rate.
void asic_irq_diag_dump(double tdiff)
{
    if (!IRQ_DIAG || tdiff <= 0.0) return;
    if (!s_irq_armed)
    {
        s_irq_armed = 1;
        printf("[IRQ] ARMED -- Holly IRQs raised per second\n");
    }

    static const char *const nrm[22] = {
        "RENDER_DONE_vd", "RENDER_DONE_isp", "RENDER_DONE_tsp", "VBLANK_IN", "VBLANK_OUT",
        "HBLANK", "YUV_DMA", "OPAQUE", "OPAQUE_MOD", "TRANS", "TRANS_MOD", "PVR_DMA",
        "MAPLE_DMA", "MAPLE_VBOI", "GDROM_DMA", "SPU_DMA", "EXT_DMA1", "EXT_DMA2",
        "DEV_DMA", "CH2_DMA", "SORT_DMA", "PUNCHTHRU" };
    static const char *const ext[4] = { "GDROM_CMD", "SPU_IRQ", "EXP_8BIT", "EXP_PCI" };

    printf("[IRQ]");
    for (u32 i = 0; i < 22; i++)
        if (s_irq_cnt[0][i]) printf(" %s:%.1f", nrm[i], s_irq_cnt[0][i] / tdiff);
    for (u32 i = 0; i < 32; i++)
    {
        if (!s_irq_cnt[1][i]) continue;
        if (i < 4) printf(" %s:%.1f", ext[i], s_irq_cnt[1][i] / tdiff);
        else       printf(" ext%u:%.1f", i, s_irq_cnt[1][i] / tdiff);
    }
    for (u32 i = 0; i < 32; i++)
        if (s_irq_cnt[2][i]) printf(" ERR%u:%.1f", i, s_irq_cnt[2][i] / tdiff);
    printf("\n");
    fflush(stdout);

    for (u32 g = 0; g < 3; g++)
        for (u32 i = 0; i < 32; i++) s_irq_cnt[g][i] = 0;
}

// BUG FIX: original always cleared SB_ISTEXT regardless of interrupt type.
void fastcall asic_CancelInterrupt(HollyInterruptID inter)
{
    u32 bit = ~(1u << (u8)inter);
    switch (inter >> 8)
    {
    case 0: SB_ISTNRM &= bit; break;
    case 1: SB_ISTEXT &= bit; break;  // EXT cancel may be ignored by hardware anyway
    case 2: SB_ISTERR &= bit; break;
    }
    asic_UpdatePending();
}

// --- Register read/write handlers ---

u32 Read_SB_ISTNRM()
{
    // Bits 29:0 = normal interrupts; bit 30 = EXT summary; bit 31 = ERR summary
    u32 tmp = SB_ISTNRM & 0x3FFFFFFFu;
    if (SB_ISTEXT) tmp |= 0x40000000u;
    if (SB_ISTERR) tmp |= 0x80000000u;
    return tmp;
}

void Write_SB_ISTNRM(u32 data)
{
    SB_ISTNRM &= ~data;  // Writing a 1 clears the bit (acknowledge)
    asic_UpdatePending();
}

void Write_SB_ISTEXT(u32 data)
{
    // EXT interrupts must be cancelled at the source — writes are intentionally ignored
    (void)data;
}

void Write_SB_ISTERR(u32 data)
{
    SB_ISTERR &= ~data;
    asic_UpdatePending();
}

// IML mask register writes — update the relevant priority line only
void Write_SB_SB_IML6NRM(u32 data) { SB_IML6NRM = data; InterruptPend(sh4_IRL_9,  ((SB_ISTNRM & SB_IML6NRM) | (SB_ISTERR & SB_IML6ERR) | (SB_ISTEXT & SB_IML6EXT)) != 0); }
void Write_SB_SB_IML4NRM(u32 data) { SB_IML4NRM = data; InterruptPend(sh4_IRL_11, ((SB_ISTNRM & SB_IML4NRM) | (SB_ISTERR & SB_IML4ERR) | (SB_ISTEXT & SB_IML4EXT)) != 0); }
void Write_SB_SB_IML2NRM(u32 data) { SB_IML2NRM = data; InterruptPend(sh4_IRL_13, ((SB_ISTNRM & SB_IML2NRM) | (SB_ISTERR & SB_IML2ERR) | (SB_ISTEXT & SB_IML2EXT)) != 0); }

void Write_SB_SB_IML6EXT(u32 data) { SB_IML6EXT = data; InterruptPend(sh4_IRL_9,  ((SB_ISTNRM & SB_IML6NRM) | (SB_ISTERR & SB_IML6ERR) | (SB_ISTEXT & SB_IML6EXT)) != 0); }
void Write_SB_SB_IML4EXT(u32 data) { SB_IML4EXT = data; InterruptPend(sh4_IRL_11, ((SB_ISTNRM & SB_IML4NRM) | (SB_ISTERR & SB_IML4ERR) | (SB_ISTEXT & SB_IML4EXT)) != 0); }
void Write_SB_SB_IML2EXT(u32 data) { SB_IML2EXT = data; InterruptPend(sh4_IRL_13, ((SB_ISTNRM & SB_IML2NRM) | (SB_ISTERR & SB_IML2ERR) | (SB_ISTEXT & SB_IML2EXT)) != 0); }

void Write_SB_SB_IML6ERR(u32 data) { SB_IML6ERR = data; InterruptPend(sh4_IRL_9,  ((SB_ISTNRM & SB_IML6NRM) | (SB_ISTERR & SB_IML6ERR) | (SB_ISTEXT & SB_IML6EXT)) != 0); }
void Write_SB_SB_IML4ERR(u32 data) { SB_IML4ERR = data; InterruptPend(sh4_IRL_11, ((SB_ISTNRM & SB_IML4NRM) | (SB_ISTERR & SB_IML4ERR) | (SB_ISTEXT & SB_IML4EXT)) != 0); }
void Write_SB_SB_IML2ERR(u32 data) { SB_IML2ERR = data; InterruptPend(sh4_IRL_13, ((SB_ISTNRM & SB_IML2NRM) | (SB_ISTERR & SB_IML2ERR) | (SB_ISTEXT & SB_IML2EXT)) != 0); }

// --- Lifecycle ---

void asic_reg_Init()
{
    sb_regs[((SB_ISTNRM_addr-SB_BASE)>>2)].flags         = REG_32BIT_READWRITE;
    sb_regs[((SB_ISTNRM_addr-SB_BASE)>>2)].readFunction  = Read_SB_ISTNRM;
    sb_regs[((SB_ISTNRM_addr-SB_BASE)>>2)].writeFunction = Write_SB_ISTNRM;

    sb_regs[((SB_ISTEXT_addr-SB_BASE)>>2)].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_ISTEXT_addr-SB_BASE)>>2)].writeFunction = Write_SB_ISTEXT;

    sb_regs[((SB_ISTERR_addr-SB_BASE)>>2)].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_ISTERR_addr-SB_BASE)>>2)].writeFunction = Write_SB_ISTERR;

    // NRM masks
    sb_regs[((SB_IML6NRM_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML6NRM_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML6NRM;
    sb_regs[((SB_IML4NRM_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML4NRM_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML4NRM;
    sb_regs[((SB_IML2NRM_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML2NRM_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML2NRM;

    // EXT masks
    sb_regs[((SB_IML6EXT_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML6EXT_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML6EXT;
    sb_regs[((SB_IML4EXT_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML4EXT_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML4EXT;
    sb_regs[((SB_IML2EXT_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML2EXT_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML2EXT;

    // ERR masks
    sb_regs[((SB_IML6ERR_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML6ERR_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML6ERR;
    sb_regs[((SB_IML4ERR_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML4ERR_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML4ERR;
    sb_regs[((SB_IML2ERR_addr-SB_BASE)>>2)].flags = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_IML2ERR_addr-SB_BASE)>>2)].writeFunction = Write_SB_SB_IML2ERR;
}

void asic_reg_Term()  {}
void asic_reg_Reset(bool Manual) {}