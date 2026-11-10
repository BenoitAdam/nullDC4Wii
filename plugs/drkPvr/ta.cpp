#include "ta.h"
#include "Renderer_if.h"	// TaPerf / PERF_TICKS32 / g_ta_profile_preset

// Tile Accelerator (TA) state machine for PowerVR2 (Dreamcast) emulation
// Handles DMA and Store Queue writes, dispatches polygon/vertex/control params

namespace TASplitter
{
    // Current active command handler (state machine pointer)
    TaListFP* TaCmd = nullptr;
}

using namespace TASplitter;

// Store Queue path: single 32-byte write (e.g. via SQ registers)
//
// This is THE geometry hot path: one call per 32-byte TA block, i.e. very
// nearly one call per Dreamcast vertex, ~500 K/s in a heavy scene. Keep the
// wrapper itself minimal -- it used to be 41 PowerPC instructions around an
// indirect call whose real work is ~33, because the two gettime() reads are
// 64-bit time-base consistency loops (6 mftbu/mftb between them) and the
// 64-bit subtract needed two non-volatiles saved across the call. PERF_TICKS32
// (Renderer_if.h) is one mftb and a wrap-exact 32-bit delta.
// The measured variant, deliberately out of line (TA_PROFILE, default off).
// Keeping it in its own noinline function is what lets libPvr_TaSQ below stay
// frameless: the two time-base reads need a value live across the call to
// TaCmd, which costs a non-volatile and therefore a prologue and epilogue, and
// the geometry path should not pay for that when nobody is profiling.
static void __attribute__((noinline)) ta_sq_measured(Ta_Dma* t)
{
    const u32 _t0 = PERF_TICKS32();
    TaCmd(t, t);
    TaPerf.ticks += (u32)(PERF_TICKS32() - _t0);
    TaPerf.calls++;
}

// [RAW] scene_log raw TA dump (2026-11-10, World Series Baseball 2K2 black menu).
// The [SCN] census showed vertices whose x/y/z/colour words held the wrong
// values; this prints the 32-byte blocks exactly as the game sent them, BEFORE
// any parsing, so the parse can be checked against the hardware layout.
// gxRend.cpp's scene_dump_pass() arms it (g_scn_raw_left = N blocks) at the start
// of each burst, so it only ever prints while scene_log is on. Off, its whole
// cost is the extra test in libPvr_TaSQ / libPvr_TaDMA (one load, folded into
// the existing g_ta_profile_preset test).
u32 g_scn_raw_left = 0;

static void __attribute__((noinline)) ta_raw_dump(const Ta_Dma* t, u32 n, const char* path)
{
    const u32* w = (const u32*)t;
    for (u32 i = 0; i < n && g_scn_raw_left; i++, w += 8, g_scn_raw_left--)
    {
        // PCW: ParaType 31..29, EndOfStrip 28, ListType 26..24, Group_En 23.
        printf("[RAW] %s pt=%u eos=%u lt=%u  %08X %08X %08X %08X %08X %08X %08X %08X\n",
               path, (unsigned)(w[0] >> 29), (unsigned)((w[0] >> 28) & 1),
               (unsigned)((w[0] >> 24) & 7),
               (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
               (unsigned)w[4], (unsigned)w[5], (unsigned)w[6], (unsigned)w[7]);
    }
    if (!g_scn_raw_left)
    {
        printf("[RAW] (dump budget used up; re-armed at the next [SCN] burst)\n");
        fflush(stdout);
    }
}

// Slow side of the store-queue path: raw dump and/or TA_PROFILE measurement.
static void __attribute__((noinline)) ta_sq_slow(Ta_Dma* t)
{
    if (g_scn_raw_left)
        ta_raw_dump(t, 1, "sq ");
    if (g_ta_profile_preset)
        ta_sq_measured(t);
    else
        TaCmd(t, t);
}

void libPvr_TaSQ(u32* data)
{
    verify(TaCmd != nullptr);
    Ta_Dma* t = (Ta_Dma*)data;
    if (__builtin_expect(g_ta_profile_preset | g_scn_raw_left, 0))
    {
        ta_sq_slow(t);
        return;
    }
    TaCmd(t, t);
}

// DMA path: process a contiguous block of 32-byte TA entries
// 'size' is in units of Ta_Dma (32 bytes), not bytes
void libPvr_TaDMA(u32* data, u32 size)
{
    verify(TaCmd != nullptr);
    verify(size > 0);

    const u32 _t0 = g_ta_profile_preset ? PERF_TICKS32() : 0;

    Ta_Dma* ta_data     = (Ta_Dma*)data;
    Ta_Dma* ta_data_end = ta_data + size - 1;

    if (__builtin_expect(g_scn_raw_left, 0))
        ta_raw_dump(ta_data, size, "dma");

    do
    {
        ta_data = TaCmd(ta_data, ta_data_end);
    }
    while (ta_data <= ta_data_end);

    // A DMA batch carries many blocks per call, so the bracket is cheap here
    // relative to the work -- but it is gated too, so `ta:` never reports a
    // mixture of measured DMA and unmeasured store-queue traffic.
    if (g_ta_profile_preset)
    {
        TaPerf.ticks += (u32)(PERF_TICKS32() - _t0);
        TaPerf.calls++;
    }
}

namespace TASplitter
{
    // Called when the hardware signals list continuation (TA_LIST_CONT register)
    // Resets per-list finished flags so lists can be reopened
    void TA_ListCont()
    {
        // Decoder-side continuation is handled in FifoSplitter::ListCont()
    }

    // Called when the hardware initializes a new TA list (TA_LIST_INIT register)
    // Resets the full TA state and command pointer
    void TA_ListInit()
    {
        // TaCmd is reset to ta_main inside FifoSplitter::ListInit()
    }

    // Called on TA soft reset (SOFTRESET register write)
    // Clears all list state without a full hardware reset
    void TA_SoftReset()
    {
        // Decoder-side reset handled in FifoSplitter::SoftReset()
    }
}
