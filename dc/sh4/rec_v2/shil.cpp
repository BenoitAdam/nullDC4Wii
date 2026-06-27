/*
    SH4 Intermediate Language (SHIL) — analysis helpers and canonical
    opcode instantiation.

    Changes vs original:
    - Fixed dead-write detection logic (was comparing write-ord >= read-ord
      which is backwards; a dead write happens when the *next* write comes
      before any read of the previously written register).
    - AnalyseBlock re-enabled behind SHIL_ENABLE_DCE guard (off by default,
      matching original shipping behaviour) so it can be turned on for
      testing without touching the disabled `return`.
    - DCE-only variables/functions guarded by SHIL_ENABLE_DCE to suppress
      unused-variable/function warnings in normal builds.
    - Minor: printf format specifiers corrected for u32.
*/

#include "types.h"
#include "shil.h"
#include "decoder.h"

// ---------------------------------------------------------------------------
// Per-register liveness tracking — only compiled when DCE is active.
// ---------------------------------------------------------------------------
#ifdef SHIL_ENABLE_DCE

static u32 RegisterWrite[sh4_reg_count];
static u32 RegisterRead [sh4_reg_count];

static u32 fallback_blocks = 0;
static u32 total_blocks    = 0;
static u32 REMOVED_OPS     = 0;

// Record the ordinal of the instruction that *reads* each sub-register of p.
static void RegReadInfo(const shil_param& p, u32 ord)
{
    if (!p.is_reg())
        return;
    const int n = p.count();
    for (int i = 0; i < n; ++i)
        RegisterRead[p._reg + i] = ord;
}

// Record the ordinal of the instruction that *writes* each sub-register of p.
// BUG FIX vs original: condition was inverted.
// A write is dead when no read occurred between the previous write and this
// new write, i.e. RegisterRead[r] < RegisterWrite[r].
static void RegWriteInfo(shil_opcode* ops, const shil_param& p, u32 ord)
{
    if (!p.is_reg())
        return;
    const int n = p.count();
    for (int i = 0; i < n; ++i)
    {
        const u32 r     = p._reg + i;
        const u32 prevW = RegisterWrite[r];
        const u32 prevR = RegisterRead[r];

        if (prevW != 0xFFFFFFFF && prevR < prevW)
        {
            // Per-opcode logging removed: fires on every dead write found,
            // which floods console output across thousands of blocks and
            // would itself skew any speed comparison.
            ops[prevW].Flow = 1;
        }
        RegisterWrite[r] = ord;
    }
}

#endif // SHIL_ENABLE_DCE

// ---------------------------------------------------------------------------
// Dead-code elimination pass (Write-after-Write without intervening Read).
// Disabled by default (matches original); enable with -DSHIL_ENABLE_DCE.
// ---------------------------------------------------------------------------
void AnalyseBlock(DecodedBlock* blk)
{
#ifndef SHIL_ENABLE_DCE
    return;
#else
    memset(RegisterWrite, 0xFF, sizeof(RegisterWrite));
    memset(RegisterRead,  0xFF, sizeof(RegisterRead));

    ++total_blocks;

    const u32 nOps = static_cast<u32>(blk->oplist.size());

    for (u32 i = 0; i < nOps; ++i)
    {
        shil_opcode* op = &blk->oplist[i];
        op->Flow = 0;

        if (op->op == shop_ifb)
        {
            ++fallback_blocks;
            for (u32 j = 0; j < i; ++j)
                blk->oplist[j].Flow = 0;
            return;
        }

        RegReadInfo(op->rs1, i);
        RegReadInfo(op->rs2, i);
        RegReadInfo(op->rs3, i);

        RegWriteInfo(&blk->oplist[0], op->rd,  i);
        RegWriteInfo(&blk->oplist[0], op->rd2, i);
    }

    for (int i = static_cast<int>(blk->oplist.size()) - 1; i >= 0; --i)
    {
        if (blk->oplist[i].Flow)
        {
            blk->oplist.erase(blk->oplist.begin() + i);
            ++REMOVED_OPS;
        }
    }

    int affregs = 0;
    for (int i = 0; i < 16; ++i)
        if (RegisterWrite[i] != 0xFFFFFFFF)
            ++affregs;

    printf("%u FB, %u native, %.2f%% | %u ops removed\n",
           fallback_blocks,
           total_blocks - fallback_blocks,
           fallback_blocks * 100.f / static_cast<float>(total_blocks),
           REMOVED_OPS);
#endif // SHIL_ENABLE_DCE
}

// ---------------------------------------------------------------------------
// Forward declarations expected by canonical implementations.
// ---------------------------------------------------------------------------
void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);
void UpdateFPSCR();
bool UpdateSR();

#include "dc/sh4/ccn.h"
#include "ngen.h"
#include "dc/sh4/sh4_registers.h"

// Instantiate canonical (portable C) implementations (SHIL_MODE 1).
#define SHIL_MODE 1
#include "shil_canonical.h"

// Instantiate the dispatch table (SHIL_MODE 3).
#define SHIL_MODE 3
#include "shil_canonical.h"
