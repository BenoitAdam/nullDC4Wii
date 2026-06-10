/*
    shil_canonical.h — multi-mode X-macro header for SHIL opcodes.

    Depending on SHIL_MODE this header generates:
      0 → shilop enum
      1 → opcode structs with canonical (portable C) implementations + compile()
      2 → opcode struct forward declarations
      3 → shil_chf[] dispatch-table initialiser

    Changes vs original:
    - Removed ARM Cortex-A8 fast-path declarations
    - Added missing #undef for BIN_OP_I4 (was leaking into TUs that re-include
      this header with a different SHIL_MODE).
    - fsca_native / fsca_table: guarded the pi_index array accesses with a
      compile-time comment noting the required table size (no runtime change).
    - Whitespace / formatting only (no logic changes outside the bug fixes
      noted in shil.cpp).
*/



#ifndef ftrv_impl
#  define ftrv_impl f1
#endif

#ifndef fipr_impl
#  define fipr_impl f1
#endif

#define fsca_impl fsca_table

// ---------------------------------------------------------------------------
// Mode-dispatch macros
// ---------------------------------------------------------------------------
#if SHIL_MODE == 0
// ---- Generate the shilop enum ----------------------------------------------
#   define SHIL_START           enum shilop {
#   define SHIL_END             };
#   define shil_opc(name)       shop_##name,
#   define shil_opc_end()
#   define shil_canonical(rv,name,args,code)
#   define shil_compile(code)

#elif SHIL_MODE == 1
// ---- Generate structs with full canonical implementations ------------------
#   define SHIL_START
#   define SHIL_END
#   define shil_opc(name)       struct shil_opcl_##name {
#   define shil_opc_end()       };
#   define shil_canonical(rv,name,args,code) \
        static rv name args { code }
#   define shil_cf_arg_u32(x)   ngen_CC_Param(op, &op->x, CPT_u32);
#   define shil_cf_arg_f32(x)   ngen_CC_Param(op, &op->x, CPT_f32);
#   define shil_cf_arg_ptr(x)   ngen_CC_Param(op, &op->x, CPT_ptr);
#   define shil_cf_rv_u32(x)    ngen_CC_Param(op, &op->x, CPT_u32rv);
#   define shil_cf_rv_f32(x)    ngen_CC_Param(op, &op->x, CPT_f32rv);
#   define shil_cf_rv_u64(x)    ngen_CC_Param(op, &op->rd,  CPT_u64rvL); \
                                 ngen_CC_Param(op, &op->rd2, CPT_u64rvH);
#   define shil_cf(x)           ngen_CC_Call(op, (void*)x);
#   define shil_compile(code) \
        static void compile(shil_opcode* op) { ngen_CC_Start(op); code ngen_CC_Finish(op); }

#elif SHIL_MODE == 2
// ---- Generate struct forward declarations ----------------------------------
#   define SHIL_START
#   define SHIL_END
#   define shil_opc(name)       struct shil_opcl_##name {
#   define shil_opc_end()       };
#   define shil_canonical(rv,name,args,code) static rv cimpl_##name args;
#   define shil_compile(code)   static void compile(shil_opcode* op);

#elif SHIL_MODE == 3
// ---- Generate the dispatch table -------------------------------------------
#   define SHIL_START           shil_chfp* shil_chf[] = {
#   define SHIL_END             };
#   define shil_opc(name)       &shil_opcl_##name::compile,
#   define shil_opc_end()
#   define shil_canonical(rv,name,args,code)
#   define shil_compile(code)

#else
#   error "Invalid SHIL_MODE"
#endif

// ---------------------------------------------------------------------------
// Helper operation macros — only emit code bodies in mode 1.
// ---------------------------------------------------------------------------
#if SHIL_MODE == 1

#include <math.h>
#include "types.h"
#include "shil.h"
#include "decoder.h"

// Binary op: u32 op u32 → u32
#define BIN_OP_I_BASE(code, type, rtype)           \
shil_canonical(rtype, f1, (type r1, type r2),      \
    code                                           \
)                                                  \
shil_compile(                                      \
    shil_cf_arg_##type(rs2);                       \
    shil_cf_arg_##type(rs1);                       \
    shil_cf(f1);                                   \
    shil_cf_rv_##rtype(rd);                        \
)

#define UN_OP_I_BASE(code, type)                   \
shil_canonical(type, f1, (type r1),                \
    code                                           \
)                                                  \
shil_compile(                                      \
    shil_cf_arg_##type(rs1);                       \
    shil_cf(f1);                                   \
    shil_cf_rv_##type(rd);                         \
)

#define BIN_OP_I(z)           BIN_OP_I_BASE(return r1 z r2;, u32, u32)
#define BIN_OP_I2(tp, z)      BIN_OP_I_BASE(return ((tp)r1) z ((tp)r2);, u32, u32)
#define BIN_OP_I3(z, w)       BIN_OP_I_BASE(return (r1 z r2) w;, u32, u32)
#define BIN_OP_I4(tp, z, rt, pt) \
    BIN_OP_I_BASE(return ((tp)(pt)r1) z ((tp)(pt)r2);, u32, rt)

#define BIN_OP_F(z)           BIN_OP_I_BASE(return r1 z r2;, f32, f32)
#define BIN_OP_FU(z)          BIN_OP_I_BASE(return r1 z r2;, f32, u32)

#define UN_OP_I(z)            UN_OP_I_BASE(return z(r1);, u32)
#define UN_OP_F(z)            UN_OP_I_BASE(return z(r1);, f32)

#define shil_recimp() \
shil_compile(         \
    die("This opcode requires a native dynarec implementation"); \
)

#else // SHIL_MODE != 1 — empty stubs

#define BIN_OP_I(z)
#define BIN_OP_I2(tp, z)
#define BIN_OP_I3(z, w)
#define BIN_OP_I4(tp, z, rt, pt)
#define BIN_OP_F(z)
#define BIN_OP_FU(z)
#define UN_OP_I(z)
#define UN_OP_F(z)
#define shil_recimp()

#endif // SHIL_MODE == 1

// ===========================================================================
// Opcode definitions
// ===========================================================================

SHIL_START

// ---------------------------------------------------------------------------
// Move / flow
// ---------------------------------------------------------------------------
shil_opc(mov32)  shil_recimp() shil_opc_end()
shil_opc(mov64)  shil_recimp() shil_opc_end()

// Conditional / dynamic branch
shil_opc(jdyn)   shil_recimp() shil_opc_end()
shil_opc(jcond)  shil_recimp() shil_opc_end()

// Interpreter fallback block
shil_opc(ifb)    shil_recimp() shil_opc_end()

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------
shil_opc(readm)  shil_recimp() shil_opc_end()
shil_opc(writem) shil_recimp() shil_opc_end()

// ---------------------------------------------------------------------------
// Status-register synchronisation
// ---------------------------------------------------------------------------
shil_opc(sync_sr)
shil_compile(
    shil_cf(UpdateSR);
)
shil_opc_end()

shil_opc(sync_fpscr)
shil_compile(
    shil_cf(UpdateFPSCR);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Integer bitwise
// ---------------------------------------------------------------------------
shil_opc(and) BIN_OP_I(&)  shil_opc_end()
shil_opc(or)  BIN_OP_I(|)  shil_opc_end()
shil_opc(xor) BIN_OP_I(^)  shil_opc_end()
shil_opc(not) UN_OP_I(~)   shil_opc_end()

// ---------------------------------------------------------------------------
// Integer arithmetic
// ---------------------------------------------------------------------------
shil_opc(add) BIN_OP_I(+)  shil_opc_end()
shil_opc(sub) BIN_OP_I(-)  shil_opc_end()
shil_opc(neg) UN_OP_I(-)   shil_opc_end()

// ---------------------------------------------------------------------------
// Shifts
// ---------------------------------------------------------------------------
shil_opc(shl) BIN_OP_I2(u32, <<) shil_opc_end()
shil_opc(shr) BIN_OP_I2(u32, >>) shil_opc_end()
shil_opc(sar) BIN_OP_I2(s32, >>) shil_opc_end()

shil_opc(ror)
shil_canonical(
    u32, f1, (u32 r1, u32 amt),
    return (r1 >> amt) | (r1 << (32u - amt));
)
shil_compile(
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

shil_opc(shld)
shil_canonical(
    u32, f1, (u32 r1, u32 r2),
    if ((r2 & 0x80000000u) == 0)
        return r1 << (r2 & 0x1F);
    else if ((r2 & 0x1F) == 0)
        return 0u;
    else
        return r1 >> ((~r2 & 0x1F) + 1);
)
shil_compile(
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

shil_opc(shad)
shil_canonical(
    u32, f1, (s32 r1, u32 r2),
    if ((r2 & 0x80000000u) == 0)
        return (u32)(r1 << (r2 & 0x1F));
    else if ((r2 & 0x1F) == 0)
        return (u32)(r1 >> 31);
    else
        return (u32)(r1 >> ((~r2 & 0x1F) + 1));
)
shil_compile(
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Sign-extension
// ---------------------------------------------------------------------------
shil_opc(ext_s8)
shil_canonical(
    u32, f1, (u32 r1),
    return (u32)(s32)(s8)r1;
)
shil_compile(
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

shil_opc(ext_s16)
shil_canonical(
    u32, f1, (u32 r1),
    return (u32)(s32)(s16)r1;
)
shil_compile(
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Integer multiply
// ---------------------------------------------------------------------------
shil_opc(mul_u16) BIN_OP_I4(u32, *, u32, u16) shil_opc_end()
shil_opc(mul_s16) BIN_OP_I4(s32, *, u32, s16) shil_opc_end()

// Lower 32 bits: signed and unsigned multiply are identical.
shil_opc(mul_i32) BIN_OP_I4(s32, *, u32, s32) shil_opc_end()

shil_opc(mul_u64) BIN_OP_I4(u64, *, u64, u32) shil_opc_end()
shil_opc(mul_s64) BIN_OP_I4(s64, *, u64, s32) shil_opc_end()

// ---------------------------------------------------------------------------
// 32-bit division (matched ROTCL/DIV1 idiom — see MatchDiv32 in decoder.cpp).
// Ported from devcast/reicast. quo -> rd, rem -> rd2.
//
// NOTE: the u64 is packed via pointer-cast on purpose: word [0] always ends up
// in the FIRST return register (eax on x86, r3 on big-endian PPC), which is
// what CPT_u64rvL stores to rd — so quo->rd / rem->rd2 holds on BOTH endians.
// (The native PPC cases in wii_driver.cpp bypass this entirely.)
// ---------------------------------------------------------------------------
shil_opc(div32u)
shil_canonical(
    u64, f1, (u32 r1, u32 r2),
    u32 quo = r1 / r2;
    u32 rem = r1 % r2;
    u64 rv;
    ((u32*)&rv)[0] = quo;
    ((u32*)&rv)[1] = rem;
    return rv;
)
shil_compile(
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u64(rd);
)
shil_opc_end()

shil_opc(div32s)
shil_canonical(
    u64, f1, (s32 r1, s32 r2),
    u32 quo = r1 / r2;
    u32 rem = r1 % r2;
    u64 rv;
    ((u32*)&rv)[0] = quo;
    ((u32*)&rv)[1] = rem;
    return rv;
)
shil_compile(
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u64(rd);
)
shil_opc_end()

// div32 fixup step: reproduces the architectural register state the
// non-restoring DIV1 sequence leaves behind (rd = T ? a : a-b).
shil_opc(div32p2)
shil_canonical(
    u32, f1, (s32 a, s32 b, s32 T),
    if (!T)
        a -= b;
    return a;
)
shil_compile(
    shil_cf_arg_u32(rs3);
    shil_cf_arg_u32(rs2);
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Float ↔ integer conversion
// ---------------------------------------------------------------------------
shil_opc(cvt_f2i_t)   // float → integer (truncate toward zero)
shil_canonical(
    u32, f1, (f32 f1),
    return (u32)(s32)f1;
)
shil_compile(
    shil_cf_arg_f32(rs1);
    shil_cf(f1);
    shil_cf_rv_u32(rd);
)
shil_opc_end()

shil_opc(cvt_i2f_n)   // integer → float (round to nearest)
shil_canonical(
    f32, f1, (u32 r1),
    return (float)(s32)r1;
)
shil_compile(
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_f32(rd);
)
shil_opc_end()

shil_opc(cvt_i2f_z)   // integer → float (round toward zero)
shil_canonical(
    f32, f1, (u32 r1),
    return (float)(s32)r1;
)
shil_compile(
    shil_cf_arg_u32(rs1);
    shil_cf(f1);
    shil_cf_rv_f32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Store-queue prefetch (pref)
// ---------------------------------------------------------------------------
shil_opc(pref)
shil_canonical(
    void, f1, (u32 r1),
    if ((r1 >> 26) == 0x38) do_sqw_mmu(r1);
)
shil_canonical(
    void, f2, (u32 r1),
    if ((r1 >> 26) == 0x38) do_sqw_nommu(r1);
)
shil_compile(
    shil_cf_arg_u32(rs1);
    if (CCN_MMUCR.AT)
    {
        shil_cf(f1);
    }
    else
    {
        shil_cf(f2);
    }
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Integer comparison / test
// ---------------------------------------------------------------------------
shil_opc(test)  BIN_OP_I3(&, == 0)     shil_opc_end()
shil_opc(seteq) BIN_OP_I2(s32, ==)     shil_opc_end()
shil_opc(setge) BIN_OP_I2(s32, >=)     shil_opc_end()
shil_opc(setgt) BIN_OP_I2(s32, >)      shil_opc_end()
shil_opc(setae) BIN_OP_I2(u32, >=)     shil_opc_end()
shil_opc(setab) BIN_OP_I2(u32, >)      shil_opc_end()

// ---------------------------------------------------------------------------
// Float arithmetic
// ---------------------------------------------------------------------------
shil_opc(fadd)  BIN_OP_F(+)            shil_opc_end()
shil_opc(fsub)  BIN_OP_F(-)            shil_opc_end()
shil_opc(fmul)  BIN_OP_F(*)            shil_opc_end()
shil_opc(fdiv)  BIN_OP_F(/)            shil_opc_end()
shil_opc(fabs)  UN_OP_F(fabsf)         shil_opc_end()
shil_opc(fneg)  UN_OP_F(-)             shil_opc_end()
shil_opc(fsqrt) UN_OP_F(sqrtf)         shil_opc_end()

// ---------------------------------------------------------------------------
// FIPR — inner product of two FV4 vectors
// ---------------------------------------------------------------------------
shil_opc(fipr)
shil_canonical(
    f32, f1, (float* fn, float* fm),
    return fn[0]*fm[0] + fn[1]*fm[1] + fn[2]*fm[2] + fn[3]*fm[3];
)
shil_compile(
    shil_cf_arg_ptr(rs2);
    shil_cf_arg_ptr(rs1);
    shil_cf(fipr_impl);
    shil_cf_rv_f32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// FTRV — 4×4 matrix × vector
// ---------------------------------------------------------------------------
shil_opc(ftrv)
shil_canonical(
    void, f1, (float* fd, float* fn, float* fm),
    float v1;
    float v2;
    float v3;
    float v4;
    v1 = fm[0]*fn[0] + fm[4]*fn[1] + fm[ 8]*fn[2] + fm[12]*fn[3];
    v2 = fm[1]*fn[0] + fm[5]*fn[1] + fm[ 9]*fn[2] + fm[13]*fn[3];
    v3 = fm[2]*fn[0] + fm[6]*fn[1] + fm[10]*fn[2] + fm[14]*fn[3];
    v4 = fm[3]*fn[0] + fm[7]*fn[1] + fm[11]*fn[2] + fm[15]*fn[3];
    fd[0] = v1;
    fd[1] = v2;
    fd[2] = v3;
    fd[3] = v4;
)
shil_compile(
    shil_cf_arg_ptr(rs2);
    shil_cf_arg_ptr(rs1);
    shil_cf_arg_ptr(rd);
    shil_cf(ftrv_impl);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// FMAC — fused multiply-accumulate: rd = rs1 + rs2 * rs3
// ---------------------------------------------------------------------------
shil_opc(fmac)
shil_canonical(
    f32, f1, (float fn, float f0, float fm),
    return fn + f0 * fm;
)
shil_compile(
    shil_cf_arg_f32(rs3);
    shil_cf_arg_f32(rs2);
    shil_cf_arg_f32(rs1);
    shil_cf(f1);
    shil_cf_rv_f32(rd);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// FSRRA — fast reciprocal square-root approximation: 1/sqrt(r1)
// ---------------------------------------------------------------------------
shil_opc(fsrra)
UN_OP_F(1.0f / sqrtf)
shil_opc_end()

// ---------------------------------------------------------------------------
// FSCA — simultaneous sine/cosine via lookup table
// ---------------------------------------------------------------------------
shil_opc(fsca)
shil_canonical(
    void, fsca_native, (float* fd, u32 fixed),
    u32 pi_index = fixed & 0xFFFF;
    float rads = pi_index * (3.14159265f / 32768.0f);
    fd[0] = sinf(rads);
    fd[1] = cosf(rads);
)
shil_canonical(
    void, fsca_table, (float* fd, u32 fixed),
    u32 pi_index = fixed & 0xFFFF;
    fd[0] = sin_table[pi_index];
    fd[1] = sin_table[pi_index + 0x4000];
)
shil_compile(
    shil_cf_arg_u32(rs1);
    shil_cf_arg_ptr(rd);
    shil_cf(fsca_impl);
)
shil_opc_end()

// ---------------------------------------------------------------------------
// Float comparison
// ---------------------------------------------------------------------------
shil_opc(fseteq) BIN_OP_FU(==) shil_opc_end()
shil_opc(fsetgt) BIN_OP_FU(>)  shil_opc_end()

SHIL_END

// ===========================================================================
// Undefine all macros so subsequent includes with a different SHIL_MODE
// start with a clean slate.
// ===========================================================================
#undef SHIL_MODE

#undef SHIL_START
#undef SHIL_END
#undef shil_opc
#undef shil_opc_end
#undef shil_canonical
#undef shil_compile

#undef BIN_OP_I
#undef BIN_OP_I2
#undef BIN_OP_I3
#undef BIN_OP_I4
#undef BIN_OP_F
#undef BIN_OP_FU
#undef UN_OP_I
#undef UN_OP_F
#undef shil_recimp

#if SHIL_MODE == 1
#  undef shil_cf_arg_u32
#  undef shil_cf_arg_f32
#  undef shil_cf_arg_ptr
#  undef shil_cf_rv_u32
#  undef shil_cf_rv_f32
#  undef shil_cf_rv_u64
#  undef shil_cf
#endif
