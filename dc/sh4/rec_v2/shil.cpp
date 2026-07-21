/*
    SH4 Intermediate Language (SHIL) — analysis helpers and canonical
    opcode instantiation.

    Changes vs original:
    - Fixed dead-write detection logic (was comparing write-ord >= read-ord
      which is backwards; a dead write happens when the *next* write comes
      before any read of the previously written register).
    - Fixed the liveness sentinel: the "never read" marker was 0xFFFFFFFF
      compared as u32, which reads as the *newest* possible read and hid the
      most common dead write of all (written twice, never read). Arrays are
      now s32 with a -1 sentinel.
    - Dead writes are only removed for ops on a purity whitelist
      (shil_op_is_pure). readm can hit MMIO where the read itself has*/
      //       hardware side effects; pref/writem/sync_*/ftrv/fsca all do work the
      /*
      register liveness cannot see.
    - AnalyseBlock is now ACTIVE by default behind the SHIL_DCE switch; set
      SHIL_DCE to 0 to restore the original disabled behaviour for A/B.
    - Per-block printf removed (it flooded the console and skewed the very
      speed comparison it was meant to support); optional periodic summary
      behind SHIL_DCE_STATS, off by default.
*/

#include "types.h"
#include "shil.h"
#include "decoder.h"

// ---------------------------------------------------------------------------
// Dead-code elimination master switch (A/B kill switch).
//   1 = pass active
//   0 = pass compiled out, identical to the original shipping behaviour
// Bisect a regression by setting this back to 0 and rebuilding.
// ---------------------------------------------------------------------------
#define SHIL_DCE 1

// Set to 1 to print a periodic "blocks / ops removed" summary. Off by default
// so a speed A/B runs with zero console output (printf on the compile path
// would itself skew the comparison).
#define SHIL_DCE_STATS 0

// ---------------------------------------------------------------------------
// Per-register liveness tracking — only compiled when DCE is active.
// ---------------------------------------------------------------------------
#if SHIL_DCE

// SIGNED, with -1 meaning "never read"/"never written". The original used u32
// with a 0xFFFFFFFF sentinel, which made "never read" compare as the *newest*
// possible read (0xFFFFFFFF < prevW is always false) — so a register that was
// written twice and never read, the single most common dead write, was never
// detected. That sentinel bug is why the pass looked useless enough to leave
// switched off.
static s32 RegisterWrite[sh4_reg_count];
static s32 RegisterRead [sh4_reg_count];

static u32 fallback_blocks = 0;
static u32 total_blocks    = 0;
static u32 REMOVED_OPS     = 0;

// Only ops that are pure register->register computation may be deleted.
// Everything else can carry effects this register-liveness analysis cannot
// see, and deleting one would be silent corruption rather than a speedup:
//   readm              - may target MMIO, where the *read* has hardware effects
//   writem / pref      - store to guest memory / trigger a store-queue burst
//   sync_sr/sync_fpscr - call UpdateSR/UpdateFPSCR, can switch register banks
//   ifb                - interpreter fallback, arbitrary effects
//   jdyn / jcond       - control flow
//   ftrv / fsca        - write their destination through a pointer, so the
//                        rd liveness above does not describe what they touch
//   div32u/s/p2        - multi-register division helpers
// Whitelist rather than blacklist on purpose: a future shil op defaults to
// "not deletable" instead of silently becoming a correctness bug.
static bool shil_op_is_pure(shilop op)
{
    switch (op)
    {
    case shop_mov32:     case shop_mov64:
    case shop_and:       case shop_or:        case shop_xor:
    case shop_not:       case shop_add:       case shop_sub:
    case shop_neg:       case shop_shl:       case shop_shr:
    case shop_sar:       case shop_ror:       case shop_shld:
    case shop_shad:      case shop_ext_s8:    case shop_ext_s16:
    case shop_mul_u16:   case shop_mul_s16:   case shop_mul_i32:
    case shop_mul_u64:   case shop_mul_s64:
    case shop_cvt_f2i_t: case shop_cvt_i2f_n: case shop_cvt_i2f_z:
    case shop_test:      case shop_seteq:     case shop_setge:
    case shop_setgt:     case shop_setae:     case shop_setab:
    case shop_fadd:      case shop_fsub:      case shop_fmul:
    case shop_fdiv:      case shop_fabs:      case shop_fneg:
    case shop_fsqrt:     case shop_fipr:      case shop_fmac:
    case shop_fsrra:     case shop_fseteq:    case shop_fsetgt:
        return true;
    default:
        return false;
    }
}

// Record the ordinal of the instruction that *reads* each sub-register of p.
static void RegReadInfo(const shil_param& p, s32 ord)
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
static void RegWriteInfo(shil_opcode* ops, const shil_param& p, s32 ord)
{
    if (!p.is_reg())
        return;
    const int n = p.count();
    for (int i = 0; i < n; ++i)
    {
        const u32 r     = p._reg + i;
        const s32 prevW = RegisterWrite[r];
        const s32 prevR = RegisterRead[r];

        // prevW < 0        -> no earlier write, nothing can be dead
        // prevR < prevW    -> no read since that write (prevR == -1, i.e.
        //                     never read at all, is correctly covered here)
        if (prevW >= 0 && prevR < prevW && shil_op_is_pure(ops[prevW].op))
        {
            // Per-opcode logging removed: fires on every dead write found,
            // which floods console output across thousands of blocks and
            // would itself skew any speed comparison.
            ops[prevW].Flow = 1;
        }
        RegisterWrite[r] = ord;
    }
}

#endif // SHIL_DCE

// ---------------------------------------------------------------------------
// Dead-code elimination pass (Write-after-Write without intervening Read).
//
// Scope and assumptions (both inherited from the original design):
//   * Block-local only. The last write to a register within a block is never
//     removed, so state handed to the next block stays correct.
//   * Assumes no precise mid-block exceptions — if an op faults partway
//     through a block, a removed earlier write would leave stale state
//     visible to the handler. Consistent with how the rest of rec_v2 works.
// Controlled by SHIL_DCE above.
// ---------------------------------------------------------------------------
void AnalyseBlock(DecodedBlock* blk)
{
#if !SHIL_DCE
    return;
#else
    memset(RegisterWrite, 0xFF, sizeof(RegisterWrite));  // -1 as s32
    memset(RegisterRead,  0xFF, sizeof(RegisterRead));   // -1 as s32

    ++total_blocks;

    const s32 nOps = static_cast<s32>(blk->oplist.size());

    for (s32 i = 0; i < nOps; ++i)
    {
        shil_opcode* op = &blk->oplist[i];
        op->Flow = 0;

        // An interpreter fallback can read and write any register, including
        // ones this analysis believes are dead. Bail on the whole block —
        // the purity whitelist only stops us deleting the ifb itself, not
        // the writes feeding it.
        if (op->op == shop_ifb)
        {
            ++fallback_blocks;
            for (s32 j = 0; j < i; ++j)
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

#if SHIL_DCE_STATS
    // Periodic, not per-block: a printf on every compiled block floods the
    // console and skews the very measurement it is meant to support.
    if ((total_blocks & 8191) == 0)
    {
        printf("[DCE] blocks=%u ifb-skipped=%u ops-removed=%u\n",
               total_blocks, fallback_blocks, REMOVED_OPS);
    }
#endif
#endif // SHIL_DCE
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
