#include "types.h"
#include "aica_if.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sb.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

#include <time.h>
#include <stdio.h>

extern "C" int get_dma_fix_preset();

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
VArray2 aica_ram;
u32 VREG    = 0;
u32 rtc_EN  = 0;

// ---------------------------------------------------------------------------
// Logging helper — redirect to your Wii logger by redefining AICA_LOG
// ---------------------------------------------------------------------------
#ifndef AICA_LOG
#  define AICA_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

// ---------------------------------------------------------------------------
// RTC
// ---------------------------------------------------------------------------

// Known Dreamcast epoch: 1998-11-27 00:00:00 local time
// DC RTC base value at that moment: 0x5BFC8900
static const time_t  DC_EPOCH_OFFSET = 0x5BFC8900UL;

u32 GetRTC_now()
{
    // Build the reference wall-clock time for the DC epoch
    struct tm ref = {};
    ref.tm_year = 1998 - 1900;
    ref.tm_mon  = 11   - 1;
    ref.tm_mday = 27;
    // tm_hour/min/sec left 0

    time_t dc_ref = mktime(&ref);
    if (dc_ref == (time_t)-1)
        return DC_EPOCH_OFFSET; // mktime failed, return base

    time_t now   = time(NULL);
    time_t delta = now - dc_ref;

    // Apply DST correction (DC RTC is plain linear seconds, no DST)
    struct tm local_now;
#if defined(_WIN32)
    localtime_s(&local_now, &now);
#else
    localtime_r(&now, &local_now);
#endif
    if (local_now.tm_isdst > 0)
        delta += 24 * 3600;

    return (u32)(DC_EPOCH_OFFSET + (u32)delta);
}

extern s32 rtc_cycles;

u32 ReadMem_aica_rtc(u32 addr, u32 sz)
{
    switch (addr & 0xFF)
    {
    case 0: return settings.dreamcast.RTC >> 16;
    case 4: return settings.dreamcast.RTC & 0xFFFF;
    case 8: return 0; // write-enable register reads as 0
    default:
        AICA_LOG("ReadMem_aica_rtc: invalid address 0x%08X\n", addr);
        return 0;
    }
}

void WriteMem_aica_rtc(u32 addr, u32 data, u32 sz)
{
    switch (addr & 0xFF)
    {
    case 0:
        // High 16 bits — only honoured when write-enable is set
        if (rtc_EN)
        {
            settings.dreamcast.RTC = (settings.dreamcast.RTC & 0x0000FFFF)
                                   | ((data & 0xFFFF) << 16);
            rtc_EN = 0;
            SaveSettings();
        }
        return;

    case 4:
        // Low 16 bits — latch and reset internal cycle counter
        if (rtc_EN)
        {
            settings.dreamcast.RTC = (settings.dreamcast.RTC & 0xFFFF0000)
                                   | (data & 0xFFFF);
            rtc_cycles = SH4_CLOCK;
        }
        return;

    case 8:
        rtc_EN = data & 1;
        return;

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// AICA register access
// VREG lives at AICA offset 0x2C00 (32-bit) / 0x2C01 (8-bit, high byte)
// ---------------------------------------------------------------------------

static const u32 VREG_OFFSET_32 = 0x2C00;
static const u32 VREG_OFFSET_8  = 0x2C01;

u32 FASTCALL ReadMem_aica_reg(u32 addr, u32 sz)
{
    u32 offset = addr & 0x7FFF;

    if (sz == 1)
    {
        if (offset == VREG_OFFSET_8)
            return VREG;
    }
    else
    {
        if (offset == VREG_OFFSET_32)
            return libAICA_ReadMem_aica_reg(addr, sz) | (VREG << 8);
    }

    return libAICA_ReadMem_aica_reg(addr, sz);
}

void FASTCALL WriteMem_aica_reg(u32 addr, u32 data, u32 sz)
{
    u32 offset = addr & 0x7FFF;

    if (sz == 1)
    {
        if (offset == VREG_OFFSET_8)
        {
            VREG = data & 0xFF;
            AICA_LOG("VREG = %02X\n", VREG);
            return;
        }
    }
    else
    {
        if (offset == VREG_OFFSET_32)
        {
            VREG = (data >> 8) & 0xFF;
            AICA_LOG("VREG = %02X\n", VREG);
            // Mask out the VREG byte before forwarding so the plugin
            // doesn't see a stale value in bits [15:8]
            data &= ~0xFF00u;
        }
    }

    libAICA_WriteMem_aica_reg(addr, data, sz);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void aica_Init()  {}
void aica_Term()  {}

void aica_Reset(bool Manual)
{
    if (!Manual)
        aica_ram.Zero();
}

// ---------------------------------------------------------------------------
// DMA helpers
// ---------------------------------------------------------------------------

// Clamp transfer length to a sane maximum to guard against corrupted regs.
// 8 MB is larger than any real AICA RAM window.
static const u32 DMA_MAX_LEN = 8 * 1024 * 1024;

static inline u32 clamp_dma_len(u32 len)
{
    return (len < DMA_MAX_LEN) ? len : DMA_MAX_LEN;
}

// ---------------------------------------------------------------------------
// SB_ADST — AICA G2-DMA (system RAM → AICA sound RAM)
// ---------------------------------------------------------------------------

// DMA_FIX: deferred completion. Real G2-DMA takes time proportional to
// length (PSP schedules it at SH4_CLOCK/2/25MHz per byte — a 16-bit bus at
// 25MHz). The legacy code did the data copy AND the completion (SB_ADST=0 +
// IRQ) in the same instant, the same "zero-latency DMA" bug already found
// and fixed on ch2-DMA and PVR-DMA this investigation. A driver that games
// its own completion polling around a real transfer duration can get
// confused by an instant one.
s32 aica_dma_delay_cycles = 0;

static void aica_dma_complete()
{
    // Bit 31 of ADLEN: keep ADEN asserted (continuous mode)
    SB_ADEN  = (SB_ADLEN & 0x80000000) ? 1 : 0;
    SB_ADST  = 0;
    SB_ADLEN = 0;
    asic_RaiseInterrupt(holly_SPU_DMA);
}

void UpdateAicaDma(u32 cycles)
{
    if (aica_dma_delay_cycles > 0)
    {
        aica_dma_delay_cycles -= (s32)cycles;
        if (aica_dma_delay_cycles <= 0)
        {
            aica_dma_delay_cycles = 0;
            aica_dma_complete();
        }
    }
}

// PSP's Read_SB_ADST, ported verbatim (comment is PSP's own):
//
//   Le Mans and Looney Tunes sometimes send the same dma transfer twice after
//   checking SB_ADST == 0. To avoid this, we pretend SB_ADST is still set
//   when there is a pending aica-dma interrupt. This is only done once.
//
// HONEST CAVEAT: in PSP's own aica_sb_Init(), SB_ADST is registered with
// REG_READ_DATA set alongside this readFunction. The dispatch order in
// sb.cpp (both ports, identical) checks REG_READ_DATA FIRST and returns the
// raw register directly when it's set — readFunction is only consulted when
// it is NOT set. So this function is DEAD CODE in PSP itself; the "fix" its
// comment describes never actually runs on real hardware there either. Kept
// here, with matching flags, to faithfully replicate PSP's real (not
// aspirational) behavior rather than silently diverge into untested
// territory. Not a source of the stall either way — it's inert in both.
u32 Read_SB_ADST()
{
    if ((SB_ISTNRM & (1u << (u8)holly_SPU_DMA)) && !(SB_ADST & 2))
    {
        SB_ADST |= 2;
        return 1;
    }
    else
    {
        SB_ADST &= ~2u;
        return SB_ADST;
    }
}

void Write_SB_ADST(u32 data)
{
    if (!(data & 1))
        return;

    if (!(SB_ADEN & 1))
        return;

    if (SB_ADDIR == 1)
    {
        // Direction: AICA→system RAM is not implemented; warn and bail.
        msgboxf("AICA DMA: SB_ADDIR==1 (AICA→RAM) not supported",
                MBX_OK | MBX_ICONERROR);
        return;
    }

    u32 src = SB_ADSTAR;
    u32 dst = SB_ADSTAG;
    u32 len = clamp_dma_len(SB_ADLEN & 0x7FFFFFFF);

    // Transfer must be 4-byte aligned
    len &= ~3u;

    for (u32 i = 0; i < len; i += 4)
    {
        u32 word = ReadMem32_nommu(src + i);
        WriteMem32_nommu(dst + i, word);
    }

    SB_ADSTAR += len;
    SB_ADSTAG += len;
    SB_ADST    = 1;

    if (get_dma_fix_preset())
    {
        // DMA_FIX: completion is deferred — see UpdateAicaDma() above.
        // PSP: cycles = len * (SH4_CLOCK/2/25MHz) — a 16-bit bus at 25MHz.
        // Below ~4096 cycles PSP completes inline immediately; we always
        // defer by at least a handful of cycles instead, since instant
        // completion is the exact bug class this whole investigation has
        // been chasing.
        int cycles = (int)(len * (SH4_CLOCK / 2 / 25000000));
        aica_dma_delay_cycles = (cycles < 64) ? 64 : cycles;
    }
    else
    {
        // Legacy (pre-fix) behavior, kept for A/B comparison: complete the
        // transfer instantly, in the same call that started it.
        aica_dma_complete();
    }
}

// ---------------------------------------------------------------------------
// SB_E1ST — G2-EXT1 DMA (BBA / expansion port)
// ---------------------------------------------------------------------------
void Write_SB_E1ST(u32 data)
{
    if (!(data & 1))
        return;

    if (!(SB_E1EN & 1))
        return;

    u32 src = SB_E1STAR;
    u32 dst = SB_E1STAG;
    u32 len = clamp_dma_len(SB_E1LEN & 0x7FFFFFFF);

    len &= ~3u;

    if (SB_E1DIR == 1)
    {
        // Read from device: swap src/dst
        u32 tmp = src; src = dst; dst = tmp;
        AICA_LOG("G2-EXT1 DMA: read  dst=0x%08X src=0x%08X len=%u\n", dst, src, len);
    }
    else
    {
        AICA_LOG("G2-EXT1 DMA: write dst=0x%08X src=0x%08X len=%u\n", dst, src, len);
    }

    for (u32 i = 0; i < len; i += 4)
    {
        u32 word = ReadMem32_nommu(src + i);
        WriteMem32_nommu(dst + i, word);
    }

    SB_E1EN  = (SB_E1LEN & 0x80000000) ? 1 : 0;

    SB_E1STAR += len;
    SB_E1STAG += len;
    SB_E1ST   = 0;
    SB_E1LEN  = 0;

    asic_RaiseInterrupt(holly_EXT_DMA1);
}

// ---------------------------------------------------------------------------
// SB_E2ST — G2-EXT2 DMA. PSP implements this; the legacy code had no handler
// at all, so writes to SB_E2ST were pure register storage with no side
// effects — any game/driver using the second expansion DMA channel would
// silently hang waiting for a completion that could never come. Gated by
// DMA_FIX so OFF reproduces that legacy no-op-write behavior exactly.
// ---------------------------------------------------------------------------
void Write_SB_E2ST(u32 data)
{
    if (!get_dma_fix_preset())
    {
        // Legacy behavior: raw register storage, no DMA side effects.
        SB_E2ST = data;
        return;
    }

    if (!(data & 1))
        return;

    if (!(SB_E2EN & 1))
        return;

    u32 src = SB_E2STAR;
    u32 dst = SB_E2STAG;
    u32 len = clamp_dma_len(SB_E2LEN & 0x7FFFFFFF);

    len &= ~3u;

    if (SB_E2DIR == 1)
    {
        u32 tmp = src; src = dst; dst = tmp;
        AICA_LOG("G2-EXT2 DMA: read  dst=0x%08X src=0x%08X len=%u\n", dst, src, len);
    }
    else
    {
        AICA_LOG("G2-EXT2 DMA: write dst=0x%08X src=0x%08X len=%u\n", dst, src, len);
    }

    for (u32 i = 0; i < len; i += 4)
    {
        u32 word = ReadMem32_nommu(src + i);
        WriteMem32_nommu(dst + i, word);
    }

    SB_E2EN  = (SB_E2LEN & 0x80000000) ? 1 : 0;

    SB_E2STAR += len;
    SB_E2STAG += len;
    SB_E2ST   = 0;
    SB_E2LEN  = 0;

    asic_RaiseInterrupt(holly_EXT_DMA2);
}

// ---------------------------------------------------------------------------
// SB register setup
// ---------------------------------------------------------------------------
void aica_sb_Init()
{
    sb_regs[((SB_ADST_addr - SB_BASE) >> 2)].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_ADST_addr - SB_BASE) >> 2)].readFunction  = Read_SB_ADST;
    sb_regs[((SB_ADST_addr - SB_BASE) >> 2)].writeFunction = Write_SB_ADST;

    // G2-EXT1 (BBA)
    sb_regs[((SB_E1ST_addr - SB_BASE) >> 2)].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_E1ST_addr - SB_BASE) >> 2)].writeFunction = Write_SB_E1ST;

    // G2-EXT2, previously unimplemented
    sb_regs[((SB_E2ST_addr - SB_BASE) >> 2)].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    sb_regs[((SB_E2ST_addr - SB_BASE) >> 2)].writeFunction = Write_SB_E2ST;
}

void aica_sb_Reset(bool Manual) {}
void aica_sb_Term()             {}
