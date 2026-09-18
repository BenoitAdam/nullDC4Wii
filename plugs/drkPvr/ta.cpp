#include "ta.h"
#include "Renderer_if.h"	// TaTicks / TaCalls / PERF_TICKS — see the split doc there

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
void libPvr_TaSQ(u32* data)
{
    verify(TaCmd != nullptr);
    const u32 _t0 = PERF_TICKS32();
    Ta_Dma* t = (Ta_Dma*)data;
    TaCmd(t, t);
    TaPerf.ticks += (u32)(PERF_TICKS32() - _t0);
    TaPerf.calls++;
}

// DMA path: process a contiguous block of 32-byte TA entries
// 'size' is in units of Ta_Dma (32 bytes), not bytes
void libPvr_TaDMA(u32* data, u32 size)
{
    verify(TaCmd != nullptr);
    verify(size > 0);

    const u32 _t0 = PERF_TICKS32();

    Ta_Dma* ta_data     = (Ta_Dma*)data;
    Ta_Dma* ta_data_end = ta_data + size - 1;

    do
    {
        ta_data = TaCmd(ta_data, ta_data_end);
    }
    while (ta_data <= ta_data_end);

    TaPerf.ticks += (u32)(PERF_TICKS32() - _t0);
    TaPerf.calls++;
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
