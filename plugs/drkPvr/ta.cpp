#include "ta.h"
#include "wii/wii_timeprof.h"   // TP_TA bucket (vertex conversion cost)

// Tile Accelerator (TA) state machine for PowerVR2 (Dreamcast) emulation
// Handles DMA and Store Queue writes, dispatches polygon/vertex/control params

namespace TASplitter
{
    // Current active command handler (state machine pointer)
    TaListFP* TaCmd = nullptr;
}

using namespace TASplitter;

// Store Queue path: single 32-byte write (e.g. via SQ registers)
void libPvr_TaSQ(u32* data)
{
    unsigned int _t0 = tprof_now();
    verify(TaCmd != nullptr);
    Ta_Dma* t = (Ta_Dma*)data;
    TaCmd(t, t);
    tprof_leave(TP_TA, _t0);
}

// DMA path: process a contiguous block of 32-byte TA entries
// 'size' is in units of Ta_Dma (32 bytes), not bytes
void libPvr_TaDMA(u32* data, u32 size)
{
    unsigned int _t0 = tprof_now();
    verify(TaCmd != nullptr);
    verify(size > 0);

    Ta_Dma* ta_data     = (Ta_Dma*)data;
    Ta_Dma* ta_data_end = ta_data + size - 1;

    do
    {
        ta_data = TaCmd(ta_data, ta_data_end);
    }
    while (ta_data <= ta_data_end);
    tprof_leave(TP_TA, _t0);
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
