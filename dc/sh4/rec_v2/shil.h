#pragma once
#include "dc/sh4/sh4_if.h"

struct shil_opcode;
typedef void shil_chfp(shil_opcode* op);
extern shil_chfp* shil_chf[];
// Opcode names, generated from the same list as the shop_ enum (SHIL_MODE 4).
// Diagnostics: lets the JIT backend name the ops it has no native case for.
extern const char* shil_opcode_name[];

// shil_opcode::flags bit for shop_sync_fpscr. Set by the `fschg` decoder,
// which toggles FPSCR.SZ (bit 20) and NOTHING else: FPSCR.FR cannot change,
// so no FP bank swap is possible, and RM/DN are untouched so the host float
// status register needs no update either. That lets a backend skip the whole
// UpdateFPSCR() call. `frchg` (toggles FR) and `ldc Rn,FPSCR` (arbitrary
// value) must NOT set it.
#define SYNC_FPSCR_SZ_ONLY 1

enum shil_param_type
{
    // 2 bits used for base type
    FMT_NULL        = 0,
    FMT_IMM         = 1,
    FMT_I32         = 2,
    FMT_F32         = 3,
    FMT_F64         = 4,
    FMT_V4          = 5,
    FMT_V16         = 6,

    FMT_REG_BASE    = FMT_I32,
    FMT_VECTOR_BASE = FMT_V4,

    FMT_MASK        = 0xFFFF,
};

/*
	formats : 16u 16s 32u 32s, 32f, 64f
	param types: r32, r64
*/


#define SHIL_MODE 0
#include "shil_canonical.h"

// Returns a pointer to the physical register storage for a given register index.
//this should be really removed ...
u32* GetRegPtr(u32 reg);

struct shil_param
{
    shil_param()
        : _imm(0xFFFFFFFF), type(FMT_NULL)
    {}

    // Construct from a raw type + immediate/register index.
    // BUG FIX: original code called placement-new then immediately
    // overwrote _imm, losing the register-type setup from the delegated
    // constructor.  We now delegate properly and only fall back to
    // storing a raw immediate when the type is truly FMT_IMM.
    shil_param(u32 paramType, u32 imm)
    {
        if (paramType >= FMT_REG_BASE)
        {
            // Treat imm as a Sh4RegType
            *this = shil_param(static_cast<Sh4RegType>(imm));
            // Allow callers to override the inferred type (e.g. force FMT_I32)
            // only when they pass something more specific than FMT_NULL.
            // In practice the Sh4RegType constructor already sets the right
            // type, so we just honour that.
        }
        else
        {
            type  = paramType;
            _imm  = imm;
        }
    }

    // Implicit conversion from Sh4RegType is intentional — the entire
    // decoder passes bare register enum values as shil_param arguments.
    shil_param(Sh4RegType reg)
        : _imm(0xFFFFFFFF), type(FMT_NULL)
    {
        if (reg >= reg_fr_0 && reg <= reg_xf_15)
        {
            type = FMT_F32;
            _reg = reg;
        }
        else if (reg >= regv_dr_0 && reg <= regv_dr_14)
        {
            type = FMT_F64;
            _imm = (reg - regv_dr_0) * 2 + reg_fr_0;
        }
        else if (reg >= regv_xd_0 && reg <= regv_xd_14)
        {
            type = FMT_F64;
            _imm = (reg - regv_xd_0) * 2 + reg_xf_0;
        }
        else if (reg >= regv_fv_0 && reg <= regv_fv_12)
        {
            type = FMT_V4;
            _imm = (reg - regv_fv_0) * 4 + reg_fr_0;
        }
        else if (reg == regv_xmtrx)
        {
            type = FMT_V16;
            _imm = reg_xf_0;
        }
        else
        {
            type = FMT_I32;
            _reg = reg;
        }
    }

    union
    {
        u32        _imm;
        Sh4RegType _reg;
    };
    u32 type;

    // -------------------------------------------------------------------------
    // Type predicates
    // -------------------------------------------------------------------------
    bool is_null()  const { return type == FMT_NULL; }
    bool is_imm()   const { return type == FMT_IMM;  }
    bool is_reg()   const { return type >= FMT_REG_BASE; }

    bool is_r32i()  const { return type == FMT_I32; }
    bool is_r32f()  const { return type == FMT_F32; }
    bool is_r64f()  const { return type == FMT_F64; }

    // Returns the vector width (4 or 16) if this is a vector register, else 0.
    u32  is_r32fv() const { return (type >= FMT_VECTOR_BASE) ? static_cast<u32>(count()) : 0u; }

    bool is_r32()   const { return is_r32i() || is_r32f(); }
    bool is_r64()   const { return is_r64f(); }

    bool is_vector() const { return type >= FMT_VECTOR_BASE; }

    // -------------------------------------------------------------------------
    // Immediate range helpers
    // -------------------------------------------------------------------------
    bool is_imm_s8()  const { return is_imm() && is_s8(_imm);  }
    bool is_imm_u8()  const { return is_imm() && is_u8(_imm);  }
    bool is_imm_s16() const { return is_imm() && is_s16(_imm); }
    bool is_imm_u16() const { return is_imm() && is_u16(_imm); }

    // -------------------------------------------------------------------------
    // Register pointer / offset
    // -------------------------------------------------------------------------
    u32* reg_ptr() const { verify(is_reg()); return GetRegPtr(_reg); }
    u32  reg_ofs() const
    {
        verify(is_reg());
        return static_cast<u32>(
            reinterpret_cast<u8*>(GetRegPtr(_reg)) -
            reinterpret_cast<u8*>(GetRegPtr(reg_xf_0))
        );
    }

    // -------------------------------------------------------------------------
    // Count of underlying 32-bit hardware registers consumed by this param.
    // FMT_NULL / FMT_IMM / FMT_I32 / FMT_F32 → 1
    // FMT_F64  → 2
    // FMT_V4   → 4
    // FMT_V16  → 16
    // -------------------------------------------------------------------------
    int count() const
    {
        switch (type)
        {
            case FMT_F64:  return 2;
            case FMT_V4:   return 4;
            case FMT_V16:  return 16;
            default:       return 1;
        }
    }
};

struct shil_opcode
{
    shilop op;
    u32    Flow;
    u32    flags;

    shil_param rd, rd2;
    shil_param rs1, rs2, rs3;
};
