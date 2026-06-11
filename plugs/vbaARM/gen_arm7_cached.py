#!/usr/bin/env python3
# Generator for the ARM7DI cached (threaded) interpreter.
#
# Emits arm7_cached_gen.inc, which arm7_cached.cpp #includes TWICE, once with
# GEN_DECODE defined (decoder: opcode -> dispatch label + operand fields) and
# once with GEN_LABELS defined (the computed-goto label bodies). Keeping both in
# one generated file guarantees the decode side and the label side never drift.
#
# Why a generator: the data-processing and LDR/STR spaces are a cross product of
# (operation x shifter-form) and (L,B,P,U,W,offset-form). Hand-writing ~150
# threaded labels is error-prone; this expands them mechanically from the same
# tables the hand-written .cpp uses (DO_*, FORM_* macros).
#
# Run:  python3 gen_arm7_cached.py > arm7_cached_gen.inc
#
# Operand field convention (struct ARM7CACHEOP in arm7_cached.cpp):
#   r0 = Rd (dest)   r1 = Rn (base/src1)   r2 = Rm (operand reg)
#   cond = opcode[31:28]
#   imm  = shift amount | Rs index | rotated immediate | raw opcode (mem ops)

import sys

# ── Data-processing operations ──────────────────────────────────────────────
# (name, DO_macro, opcode4, sets_flags, writes_rd)
# opcode4 is bits[24:21]; the S bit is bit 20. Compare ops (TST/TEQ/CMP/CMN)
# only exist in the S=1 encoding.
DP = [
    ("and",  "DO_AND",  0x0, False, True),
    ("ands", "DO_ANDS", 0x0, True,  True),
    ("eor",  "DO_EOR",  0x1, False, True),
    ("eors", "DO_EORS", 0x1, True,  True),
    ("sub",  "DO_SUB",  0x2, False, True),
    ("subs", "DO_SUBS", 0x2, True,  True),
    ("rsb",  "DO_RSB",  0x3, False, True),
    ("rsbs", "DO_RSBS", 0x3, True,  True),
    ("add",  "DO_ADD",  0x4, False, True),
    ("adds", "DO_ADDS", 0x4, True,  True),
    ("adc",  "DO_ADC",  0x5, False, True),
    ("adcs", "DO_ADCS", 0x5, True,  True),
    ("sbc",  "DO_SBC",  0x6, False, True),
    ("sbcs", "DO_SBCS", 0x6, True,  True),
    ("rsc",  "DO_RSC",  0x7, False, True),
    ("rscs", "DO_RSCS", 0x7, True,  True),
    ("tst",  "DO_TST",  0x8, True,  False),
    ("teq",  "DO_TEQ",  0x9, True,  False),
    ("cmp",  "DO_CMP",  0xA, True,  False),
    ("cmn",  "DO_CMN",  0xB, True,  False),
    ("orr",  "DO_ORR",  0xC, False, True),
    ("orrs", "DO_ORRS", 0xC, True,  True),
    ("mov",  "DO_MOV",  0xD, False, True),
    ("movs", "DO_MOVS", 0xD, True,  True),
    ("bic",  "DO_BIC",  0xE, False, True),
    ("bics", "DO_BICS", 0xE, True,  True),
    ("mvn",  "DO_MVN",  0xF, False, True),
    ("mvns", "DO_MVNS", 0xF, True,  True),
]

# ── Shifter forms (operand2) ────────────────────────────────────────────────
# (name, FORM_macro). Register forms vs immediate-operand form. imm-rotate is
# split into rot0/rotn so the carry path is branch-free.
FORMS = [
    ("lsli", "FORM_LSL_IMM"),
    ("lsri", "FORM_LSR_IMM"),
    ("asri", "FORM_ASR_IMM"),
    ("rori", "FORM_ROR_IMM"),
    ("lslr", "FORM_LSL_REG"),
    ("lsrr", "FORM_LSR_REG"),
    ("asrr", "FORM_ASR_REG"),
    ("rorr", "FORM_ROR_REG"),
    ("imm0", "FORM_IMM_ROT0"),
    ("immn", "FORM_IMM_ROTN"),
]


def emit_dp_labels(out):
    """Label bodies for every (operation x form) data-processing variant.

    Two variants per (op, form):
      lbl_dp_<op>_<form>      plain: never touches reg[15]
      lbl_dp_<op>_<form>_pcr  pc-read: materializes reg[15]=dispatch_pc+8 first
                              (used when Rn==15 or Rm==15)
    The rare dest==15 (PC write) case is handled inline in BOTH variants: it
    writes the new PC into dispatch_pc and re-dispatches via dp_pc_done.
    """
    for (name, do, _op4, sets_flags, writes_rd) in DP:
        for (fname, form) in FORMS:
            for pcread in (False, True):
                label = f"dp_{name}_{fname}" + ("_pcr" if pcread else "")
                # `dest` (Rd) only referenced by ops that write Rd (+ the dest==15
                # path); compare ops don't. `base` (Rn) read by all but MOV/MVN.
                uses_base = name not in ("mov", "movs", "mvn", "mvns")
                out.append(f"lbl_{label}:")
                out.append("  COND_GUARD();")
                if pcread:
                    # Rn/Rm reference r15 -> need the ARM prefetch value.
                    out.append("  SET_PC_READ();")
                out.append("  {")
                out.append("    ARM7CACHEOP* op = pc_op;")
                if writes_rd:
                    out.append("    int dest = op->r0;")
                if uses_base:
                    out.append("    int base = op->r1;")
                out.append("    u32 value;")
                out.append("    C_OUT = C_FLAG;")
                out.append(f"    {form}")
                if writes_rd:
                    # dest==15 PC write: compute, place the new PC into dispatch_pc.
                    out.append("    if (dest == 15) {")
                    out.append(f"      {do}")
                    if sets_flags:
                        out.append("      clockTicks++;")
                        out.append("      CPUSwitchMode(reg[17].I & 0x1f, false);")
                    out.append("      dispatch_pc = reg[15].I & 0xFFFFFFFC;")
                    out.append("      goto dp_pc_done;")
                    out.append("    } else {")
                    out.append(f"      {do}")
                    out.append("    }")
                else:
                    out.append(f"    {do}")
                out.append("  }")
                out.append("  ADVANCE();")
    # shared PC-write continuation: dispatch_pc already holds the new PC.
    out.append("dp_pc_done:")
    out.append("  DISPATCH_PC();")


def emit_dp_decode(out):
    """Decoder: map a data-processing opcode to its label + operands."""
    # form selection happens at decode time from opcode bits.
    out.append("  /* data processing: cond 00 I opcode4 S Rn Rd operand2 */")
    out.append("  {")
    out.append("    u32 op4 = (opcode >> 21) & 0xF;")
    out.append("    u32 sbit = (opcode >> 20) & 1;")
    out.append("    cop->r0 = (opcode >> 12) & 0xF;   /* Rd */")
    out.append("    cop->r1 = (opcode >> 16) & 0xF;   /* Rn */")
    out.append("    cop->r2 = opcode & 0xF;           /* Rm */")
    # pcr = does this op READ r15? imm form: only Rn (operand1). reg form: Rn or Rm.
    # When true we route to the _pcr label variant that materializes reg[15].
    out.append("    int rn15 = (((opcode >> 16) & 0xF) == 15);")
    out.append("    int rm15 = ((opcode & 0xF) == 15);")
    out.append("    if (opcode & 0x02000000) {")
    out.append("      /* immediate operand2 */")
    out.append("      u32 imm8 = opcode & 0xFF;")
    out.append("      u32 rot  = ((opcode >> 8) & 0xF) * 2;")
    out.append("      cop->imm = rot ? ((imm8 >> rot) | (imm8 << (32 - rot))) : imm8;")
    out.append("      int rotn = rot != 0;")
    out.append("      int pcr = rn15;   /* immediate form: only Rn can be r15 */")
    out.append("      switch ((op4 << 1) | sbit) {")
    for (name, do, op4, sets_flags, writes_rd) in DP:
        key = (op4 << 1) | (1 if sets_flags else 0)
        out.append(f"      case 0x{key:02x}: cop->dispatch = pcr")
        out.append(f"        ? (rotn ? &&lbl_dp_{name}_immn_pcr : &&lbl_dp_{name}_imm0_pcr)")
        out.append(f"        : (rotn ? &&lbl_dp_{name}_immn     : &&lbl_dp_{name}_imm0); break;")
    out.append("      default: cop->dispatch = &&lbl_undefined; break;")
    out.append("      }")
    out.append("    } else {")
    out.append("      /* register operand2 */")
    out.append("      u32 shtype = (opcode >> 5) & 3;   /* 0 LSL 1 LSR 2 ASR 3 ROR */")
    out.append("      u32 byreg  = (opcode >> 4) & 1;   /* shift by register */")
    out.append("      if (byreg) {")
    out.append("        cop->imm = (opcode >> 8) & 0xF;   /* Rs */")
    out.append("      } else {")
    out.append("        cop->imm = (opcode >> 7) & 0x1F;  /* shift amount */")
    out.append("      }")
    out.append("      int fsel = (byreg << 2) | shtype;  /* 0..3 imm, 4..7 reg */")
    out.append("      int pcr = rn15 || rm15;   /* reg form: Rn or Rm can be r15 */")
    out.append("      switch ((op4 << 1) | sbit) {")
    regforms = ["lsli", "lsri", "asri", "rori", "lslr", "lsrr", "asrr", "rorr"]
    for (name, do, op4, sets_flags, writes_rd) in DP:
        key = (op4 << 1) | (1 if sets_flags else 0)
        out.append(f"      case 0x{key:02x}:")
        out.append("        switch (fsel) {")
        for i, ff in enumerate(regforms):
            out.append(f"        case {i}: cop->dispatch = pcr ? "
                       f"&&lbl_dp_{name}_{ff}_pcr : &&lbl_dp_{name}_{ff}; break;")
        out.append("        }")
        out.append("        break;")
    out.append("      default: cop->dispatch = &&lbl_undefined; break;")
    out.append("      }")
    out.append("    }")
    out.append("  }")


# ── Single data transfer (LDR/STR/LDRB/STRB) ────────────────────────────────
# Per the project decision these get full per-variant labels. The addressing
# mode (P/U/W) and offset form (immediate vs register-shifted) are fixed per
# opcode, so we predecode them. Operand fields:
#   r0 = Rd, r1 = Rn, imm = computed offset is NOT possible (register offsets
#   depend on live regs), so we keep the raw opcode in imm and compute the
#   offset in-label from the predecoded variant (no runtime decode of P/U/W/B/L
#   since the label already encodes them).
#
# Variant key bits: L(20) B(22) P(24) U(23) W(21) I(25). For I=1 the offset is a
# register shifted by an immediate (shtype in bits[6:5], amount in [11:7]).

def ldr_variants():
    """Yield (labelname, L, B, P, U, W, ireg, shtype_or_None)."""
    for L in (0, 1):
        for B in (0, 1):
            for P in (0, 1):
                for U in (0, 1):
                    # writeback: P=0 (post) always writes back; P=1 honors W.
                    for W in (0, 1):
                        # immediate offset form
                        nm = f"mem_{'ldr' if L else 'str'}{'b' if B else ''}_i_{P}{U}{W}"
                        yield (nm, L, B, P, U, W, 0, None)
                        # register-shifted offset form, one per shift type
                        for sh in range(4):
                            shn = ["lsl", "lsr", "asr", "ror"][sh]
                            nm = f"mem_{'ldr' if L else 'str'}{'b' if B else ''}_r{shn}_{P}{U}{W}"
                            yield (nm, L, B, P, U, W, 1, sh)


def emit_mem_offset(out, ireg, shtype):
    """Compute u32 'offset' from the predecoded variant."""
    if not ireg:
        out.append("    u32 offset = op->imm & 0xFFF;")
    else:
        out.append("    u32 rm = reg[op->imm & 0xF].I;")
        out.append("    u32 sa = (op->imm >> 7) & 0x1F;")
        if shtype == 0:    # LSL
            out.append("    u32 offset = rm << sa;")
        elif shtype == 1:  # LSR
            out.append("    u32 offset = sa ? (rm >> sa) : 0;")
        elif shtype == 2:  # ASR
            out.append("    u32 offset = sa ? (u32)((s32)rm >> (int)sa) "
                       ": ((rm & 0x80000000) ? 0xFFFFFFFF : 0);")
        else:              # ROR / RRX
            out.append("    u32 offset = sa ? ((rm >> sa) | (rm << (32 - sa))) "
                       ": ((((u32)C_FLAG) << 31) | (rm >> 1));")


def emit_mem_labels(out):
    # Two variants per LDR/STR form: plain and _pcr (materializes reg[15] when
    # Rn==15 (literal-pool / pc-relative) or Rm==15 (register offset)).
    for (nm, L, B, P, U, W, ireg, shtype) in ldr_variants():
        for pcread in (False, True):
            label = nm + ("_pcr" if pcread else "")
            out.append(f"lbl_{label}:")
            out.append("  COND_GUARD();")
            if pcread:
                out.append("  SET_PC_READ();")
            out.append("  {")
            out.append("    ARM7CACHEOP* op = pc_op;")
            out.append("    int dest = op->r0;")
            out.append("    int base = op->r1;")
            emit_mem_offset(out, ireg, shtype)
            out.append("    u32 rn = reg[base].I;")
            if P:  # pre-indexed: apply offset before access
                out.append(f"    u32 address = rn {'+' if U else '-'} offset;")
            else:  # post-indexed: access at rn
                out.append("    u32 address = rn;")
            if L:  # load
                if B:
                    out.append("    u32 data = CPUReadByte(address);")
                else:
                    out.append("    u32 data = CPUReadMemory(address);")
                # writeback / post-index base update
                if not P:  # post: always writeback (unless dest==base)
                    out.append("    if (dest != base) reg[base].I = "
                               f"rn {'+' if U else '-'} offset;")
                elif W:    # pre with writeback
                    out.append("    if (dest != base) reg[base].I = address;")
                out.append("    reg[dest].I = data;")
                out.append(f"    clockTicks += 3 + CPUUpdateTicksAccess{'16' if B else '32'}(address);")
                # LDR to PC: place the loaded value into dispatch_pc and re-dispatch.
                out.append("    if (dest == 15) {")
                out.append("      clockTicks += 2;")
                out.append("      dispatch_pc = reg[15].I & 0xFFFFFFFC;")
                out.append("      goto dp_pc_done;")
                out.append("    }")
            else:  # store
                if P and W:  # pre with writeback updates base before the store
                    out.append("    reg[base].I = address;")
                if B:
                    out.append("    CPUWriteByte(address, reg[dest].I & 0xFF);")
                else:
                    out.append("    CPUWriteMemory(address, reg[dest].I);")
                if not P:  # post: writeback
                    out.append(f"    reg[base].I = rn {'+' if U else '-'} offset;")
                out.append(f"    clockTicks += 2 + CPUUpdateTicksAccess{'16' if B else '32'}(address);")
            out.append("  }")
            out.append("  ADVANCE();")


def emit_mem_decode(out):
    # Build a lookup keyed by the full 6-bit (L,B,P,U,W,I); for I=1 we further
    # key on the 2-bit shift type. Collect into dicts so the emitted switch is
    # unambiguous and exhaustive.
    imm_map = {}            # key6 -> label   (I=0)
    reg_map = {}            # key6 -> {shtype: label}  (I=1)
    for (nm, L, B, P, U, W, ireg, shtype) in ldr_variants():
        key6 = (L << 5) | (B << 4) | (P << 3) | (U << 2) | (W << 1) | (1 if ireg else 0)
        if not ireg:
            imm_map[key6] = nm
        else:
            reg_map.setdefault(key6, {})[shtype] = nm

    out.append("  /* single data transfer: cond 01 I P U B W L Rn Rd offset */")
    out.append("  {")
    out.append("    cop->r0 = (opcode >> 12) & 0xF;   /* Rd */")
    out.append("    cop->r1 = (opcode >> 16) & 0xF;   /* Rn */")
    out.append("    cop->imm = opcode;                /* raw: offset + Rm/shift */")
    # NOTE: avoid names _L/_B/_U/_P/_W/_I -- those collide with <ctype.h> macros
    # on devkitPPC/libogc (and break as 'expected unqualified-id').
    out.append("    u32 mL = (opcode >> 20) & 1;")
    out.append("    u32 mW = (opcode >> 21) & 1;")
    out.append("    u32 mB = (opcode >> 22) & 1;")
    out.append("    u32 mU = (opcode >> 23) & 1;")
    out.append("    u32 mP = (opcode >> 24) & 1;")
    out.append("    u32 mI = (opcode >> 25) & 1;")
    out.append("    u32 mShtype = (opcode >> 5) & 3;")
    out.append("    u32 mKey6 = (mL<<5)|(mB<<4)|(mP<<3)|(mU<<2)|(mW<<1)|mI;")
    # pcr: imm form reads Rn only; register-offset form reads Rn or Rm.
    out.append("    int rn15 = (((opcode >> 16) & 0xF) == 15);")
    out.append("    int rm15 = ((opcode & 0xF) == 15);")
    out.append("    int pcr = mI ? (rn15 || rm15) : rn15;")
    out.append("    switch (mKey6) {")
    for key6 in sorted(imm_map):
        nm = imm_map[key6]
        out.append(f"    case 0x{key6:02x}: cop->dispatch = pcr ? "
                   f"&&lbl_{nm}_pcr : &&lbl_{nm}; break;")
    for key6 in sorted(reg_map):
        out.append(f"    case 0x{key6:02x}:")
        out.append("      switch (mShtype) {")
        for sh in range(4):
            lbl = reg_map[key6].get(sh)
            if lbl:
                out.append(f"      case {sh}: cop->dispatch = pcr ? "
                           f"&&lbl_{lbl}_pcr : &&lbl_{lbl}; break;")
        out.append("      }")
        out.append("      break;")
    out.append("    default: cop->dispatch = &&lbl_undefined; break;")
    out.append("    }")
    out.append("  }")


def main():
    out = []
    out.append("// AUTO-GENERATED by gen_arm7_cached.py -- DO NOT EDIT BY HAND.")
    out.append("// Regenerate: python3 gen_arm7_cached.py > arm7_cached_gen.inc")
    out.append("")
    out.append("#if defined(GEN_LABELS)")
    emit_dp_labels(out)
    emit_mem_labels(out)
    out.append("#endif // GEN_LABELS")
    out.append("")
    out.append("#if defined(GEN_DECODE_DP)")
    emit_dp_decode(out)
    out.append("#endif")
    out.append("")
    out.append("#if defined(GEN_DECODE_MEM)")
    emit_mem_decode(out)
    out.append("#endif")
    out.append("")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
