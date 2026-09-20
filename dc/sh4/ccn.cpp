// CCN: SH4 Cache and TLB Controller emulation
// Reference: SH7091 Hardware Manual, section 7 (CCN)

/*
TODO:
Things worth doing
1. ccn_Reset is incomplete
2. The magic PC address is a ticking time bomb
3. The write handlers are globally linked but only used internally
4. printf is wrong for a Wii target
5. The CCR ORA (operand cache as RAM) bit is silently ignored
6. MMUCR's SQMD bit is silently ignored

COULD BE DONE :
- Replacing the CCN[idx].field = ... repetition with a loop/table — you'd need to expose the address list somehow, and the _addr tokens being struct members makes this genuinely awkward in this codebase. The readability gain isn't worth the complexity.
- Adding TLB emulation — the comment ONLY SQ remaps work is honest; full MMU emulation is a large project and most DC games run with AT=0 anyway.
- Changing the union bitfield layout — it works, it's tested, and bitfield portability issues are already handled by the endian guards. Don't touch it.

*/

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/mem/mmu.h"
#include "plugins/plugin_manager.h"
#include "ccn.h"
#include "sh4_registers.h"
#include "plugs/drkPvr/frame_prof.h" // FRAME_PROF: tag the ICI-driven cache clear

// JIT_ICI: what a guest CCR.ICI write does to the compiled-code cache.
// Defined in wii/main.cpp; reached through a getter so dc/ does not have to
// link a symbol out of wii/ (same shape as get_jit_sbp_preset()).
extern "C" int get_jit_ici_preset();

// ---------------------------------------------------------------------------
// Register storage
// ---------------------------------------------------------------------------
CCN_PTEH_type  CCN_PTEH;
CCN_PTEL_type  CCN_PTEL;
u32            CCN_TTB;
u32            CCN_TEA;
CCN_MMUCR_type CCN_MMUCR;
u8             CCN_BASRA;
u8             CCN_BASRB;
CCN_CCR_type   CCN_CCR;
u32            CCN_TRA;
u32            CCN_EXPEVT;
u32            CCN_INTEVT;
CCN_PTEA_type  CCN_PTEA;
CCN_QACR_type  CCN_QACR0;
CCN_QACR_type  CCN_QACR1;

// ---------------------------------------------------------------------------
// MMUCR write handler
// Logs MMU enable/disable transitions; clears self-clearing TI bit.
// ---------------------------------------------------------------------------
static void CCN_MMUCR_write(u32 value)
{
    CCN_MMUCR_type temp;
    temp.reg_data = value;

    if (temp.AT != CCN_MMUCR.AT)
    {
        printf("CCN: MMU address translation %s (pc=%08X)\n",
               temp.AT ? "ENABLED - only SQ remaps supported" : "disabled",
               curr_pc);
    }

    // TI is a self-clearing write-only bit (TLB invalidate).
    // This used to only LOG the request and clear the bit — the TLB itself was
    // never invalidated, so stale entries (and the sq_remap table derived from
    // them) survived a flush the guest believed had happened. Now actually
    // invalidates, matching the PSP port.
    if (temp.TI)
    {
        printf("CCN: TLB invalidate (pc=%08X)\n", curr_pc);
        mmu_InvalidateTLB();
        temp.TI = 0;
    }

    CCN_MMUCR = temp;
}

// ---------------------------------------------------------------------------
// CCR write handler
// Handles both ICI and OCI self-clearing invalidate bits.
// Logs cache enable/disable transitions for debugging.
// ---------------------------------------------------------------------------
static void CCN_CCR_write(u32 value)
{
    CCN_CCR_type temp;
    temp.reg_data = value;

    // Instruction cache invalidate (ICI is self-clearing)
    if (temp.ICI)
    {
        // Games invalidate the I-cache several times a second, so this line
        // floods the log for no benefit once the JIT is known to be resetting.
        // Uncomment (with the magic-address guard, which suppresses a noisy but
        // harmless BIOS invalidation) when chasing a stale-block bug.
        //if (curr_pc != 0xAC13DBF8)
        //    printf("CCN: I-cache invalidation requested (pc=%08X)\n", curr_pc);
        //
        // FRAME_PROF: tag it. This reaches recSh4_ClearCache() through the
        // sh4_cpu.ResetCache function pointer, so it matches no grep for the
        // function's name with a paren and — with the printf above commented
        // out — leaves nothing whatsoever in the log. A stage load that threw
        // away all ~3000 compiled blocks from here showed up as a bare clr=1
        // with no reason and no printed line anywhere near it.
        FP_CLR_TAG(FP_CLR_ICI);

        // JIT_ICI. The clear below is a SLEDGEHAMMER: the guest is saying "the
        // bytes behind my I-cache may have changed", and our answer is to throw
        // away all ~3000 compiled blocks -- the entire 6 MB code cache -- and
        // recompile the working set from scratch. FRAME_PROF measured that storm
        // at ~9-10 ms of jit in a single frame, twice, around a stage load.
        //
        // It is also far stronger than it needs to be. Almost none of those blocks
        // were affected; the guest typically rewrote one loader thunk. The clear
        // is here only because we have no per-block way to tell which ones went
        // stale -- except that we DO: that is exactly what JIT_SBP mode 2 is, a
        // source-byte compare at every RAM block entry.
        //
        //   0  CLEAR ALL   throw the cache away (legacy, and the default: it is
        //                  unconditionally correct and costs nothing per frame)
        //   1  IGNORE      do nothing, and rely on whatever JIT_SBP is set to.
        //                  With JIT_SBP < 2 this is a CORRECTNESS GAMBLE: a game
        //                  that really did rewrite code will execute the stale
        //                  translation. Offered because some games ICI defensively
        //                  without ever writing code, and for those it is free.
        //   2  IGNORE+SBP  ignore, and force JIT_SBP to 2 for the whole run so the
        //                  guard actually covers what the clear used to. This is
        //                  the coherent pair and the only one worth A/B-ing: it
        //                  trades a permanent per-block-entry tax against a ~10 ms
        //                  storm per invalidate. Mode 2 is self-interlocking on
        //                  purpose -- picking "ignore" and forgetting to raise
        //                  JIT_SBP is the one way to get a silent wrong answer.
        if (get_jit_ici_preset() == 0)
            sh4_cpu.ResetCache();

        temp.ICI = 0;
    }

    // Operand cache invalidate (OCI is self-clearing)
    // No OC emulation, but the bit must still be cleared per spec.
    if (temp.OCI)
    {
        temp.OCI = 0;
    }

    // Log cache enable state changes
    if (temp.ICE != CCN_CCR.ICE)
        printf("CCN: I-cache %s (pc=%08X)\n", temp.ICE ? "enabled" : "disabled", curr_pc);
    if (temp.OCE != CCN_CCR.OCE)
        printf("CCN: O-cache %s (pc=%08X)\n", temp.OCE ? "enabled" : "disabled", curr_pc);

    CCN_CCR = temp;
}

// ---------------------------------------------------------------------------
// ccn_Init -- wire all CCN registers into the SH4 register dispatch table
// Uses the original direct-indexing pattern (macros caused build errors
// because _addr tokens are struct members, not plain integers).
// ---------------------------------------------------------------------------
void ccn_Init()
{
    // CCN PTEH 0xFF000000
    CCN[(CCN_PTEH_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEH_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEH_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEH_addr&0xFF)>>2].data32        = &CCN_PTEH.reg_data;

    // CCN PTEL 0xFF000004
    CCN[(CCN_PTEL_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEL_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEL_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEL_addr&0xFF)>>2].data32        = &CCN_PTEL.reg_data;

    // CCN TTB 0xFF000008
    CCN[(CCN_TTB_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TTB_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TTB_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TTB_addr&0xFF)>>2].data32        = &CCN_TTB;

    // CCN TEA 0xFF00000C
    CCN[(CCN_TEA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TEA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TEA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TEA_addr&0xFF)>>2].data32        = &CCN_TEA;

    // CCN MMUCR 0xFF000010 -- read via data32, writes go through handler
    CCN[(CCN_MMUCR_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].writeFunction = CCN_MMUCR_write;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].data32        = &CCN_MMUCR.reg_data;

    // CCN BASRA 0xFF000014 (8-bit)
    CCN[(CCN_BASRA_addr&0xFF)>>2].flags         = REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_BASRA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_BASRA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_BASRA_addr&0xFF)>>2].data8         = &CCN_BASRA;

    // CCN BASRB 0xFF000018 (8-bit)
    CCN[(CCN_BASRB_addr&0xFF)>>2].flags         = REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_BASRB_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_BASRB_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_BASRB_addr&0xFF)>>2].data8         = &CCN_BASRB;

    // CCN CCR 0xFF00001C -- read via data32, writes go through handler
    CCN[(CCN_CCR_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    CCN[(CCN_CCR_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_CCR_addr&0xFF)>>2].writeFunction = CCN_CCR_write;
    CCN[(CCN_CCR_addr&0xFF)>>2].data32        = &CCN_CCR.reg_data;

    // CCN TRA 0xFF000020
    CCN[(CCN_TRA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TRA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TRA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TRA_addr&0xFF)>>2].data32        = &CCN_TRA;

    // CCN EXPEVT 0xFF000024
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].data32        = &CCN_EXPEVT;

    // CCN INTEVT 0xFF000028
    CCN[(CCN_INTEVT_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].data32        = &CCN_INTEVT;

    // CCN PTEA 0xFF000034
    CCN[(CCN_PTEA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEA_addr&0xFF)>>2].data32        = &CCN_PTEA.reg_data;

    // CCN QACR0 0xFF000038
    CCN[(CCN_QACR0_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_QACR0_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_QACR0_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_QACR0_addr&0xFF)>>2].data32        = &CCN_QACR0.reg_data;

    // CCN QACR1 0xFF00003C
    CCN[(CCN_QACR1_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_QACR1_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_QACR1_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_QACR1_addr&0xFF)>>2].data32        = &CCN_QACR1.reg_data;
}

// ---------------------------------------------------------------------------
// ccn_Reset -- restore power-on / reset values per SH7091 hardware manual
// EXPEVT = 0x000 on power-on, 0x020 on manual reset (original always wrote 0).
// ---------------------------------------------------------------------------
void ccn_Reset(bool Manual)
{
    CCN_TRA            = 0x00000000;
    CCN_EXPEVT         = Manual ? 0x00000020 : 0x00000000;
    CCN_MMUCR.reg_data = 0x00000000;
    CCN_CCR.reg_data   = 0x00000000;
}

void ccn_Term()
{
    // Nothing to release -- all storage is statically allocated
}