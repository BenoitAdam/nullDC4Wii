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

# Which DP ops are LOGICAL (their *S form takes C from the shifter carry-out)
# vs ARITHMETIC (their *S form derives C from the ALU and ignores shifter carry).
LOGICAL = {"and","ands","eor","eors","tst","teq","orr","orrs",
           "mov","movs","bic","bics","mvn","mvns"}

# Register shifter forms (operand2 = Rm shifted). Split shift/rotate into its own
# uop (lbl_sh_<form> value-only, lbl_shc_<form> value + carry into reg[R_COUT]).
# `byreg` forms take the amount from Rs (reg[op->imm]); imm forms from op->imm.
SHIFT_FORMS = ["lsli", "lsri", "asri", "rori", "lslr", "lsrr", "asrr", "rorr"]


def shift_body(out, form, indent, writes_carry):
    """Emit the shifter computation for `form` into reg[R_SHIFT] (+reg[R_COUT]
    when writes_carry). Reads Rm = reg[op->r2]; amount from op->imm (imm forms)
    or reg[op->imm] (reg forms). Mirrors the original FORM_* macros exactly."""
    p = indent
    out.append(p + "u32 v = reg[op->r2].I;")
    byreg = form.endswith("r") and not form.endswith("i")  # *r vs *i
    # form names: lsli/lsri/asri/rori (imm), lslr/lsrr/asrr/rorr (reg)
    is_reg = form[-1] == "r"
    base = form[:-1]   # lsl/lsr/asr/ror
    cw = (lambda e: out.append(p + f"reg[R_COUT].I = ({e}) ? 1 : 0;")) if writes_carry \
         else (lambda e: None)
    if not is_reg:
        out.append(p + "u32 s = op->imm;")
        if base == "lsl":
            out.append(p + "if (s) {"); cw("(v >> (32 - s)) & 1")
            out.append(p + "  reg[R_SHIFT].I = v << s;")
            out.append(p + "} else { reg[R_SHIFT].I = v;" + ("" if writes_carry else "") + " }")
        elif base == "lsr":
            out.append(p + "if (s) {"); cw("(v >> (s - 1)) & 1")
            out.append(p + "  reg[R_SHIFT].I = v >> s;")
            out.append(p + "} else {"); cw("v & 0x80000000")
            out.append(p + "  reg[R_SHIFT].I = 0; }")
        elif base == "asr":
            out.append(p + "if (s) {"); cw("((s32)v >> (int)(s - 1)) & 1")
            out.append(p + "  reg[R_SHIFT].I = (u32)((s32)v >> (int)s);")
            out.append(p + "} else if (v & 0x80000000) {"); cw("1")
            out.append(p + "  reg[R_SHIFT].I = 0xFFFFFFFF;")
            out.append(p + "} else {"); cw("0")
            out.append(p + "  reg[R_SHIFT].I = 0; }")
        else:  # ror (s!=0) / rrx (s==0)
            out.append(p + "if (s) {"); cw("(v >> (s - 1)) & 1")
            out.append(p + "  reg[R_SHIFT].I = (v << (32 - s)) | (v >> s);")
            out.append(p + "} else { u32 cc = (u32)C_FLAG;"); cw("v & 1")
            out.append(p + "  reg[R_SHIFT].I = (v >> 1) | (cc << 31); }")
    else:
        out.append(p + "u32 s = reg[op->imm].I & 0xFF;")
        if base == "lsl":
            out.append(p + "if (s) {")
            out.append(p + "  if (s == 32) {"); cw("v & 1"); out.append(p + "    reg[R_SHIFT].I = 0;")
            out.append(p + "  } else if (s < 32) {"); cw("(v >> (32 - s)) & 1"); out.append(p + "    reg[R_SHIFT].I = v << s;")
            out.append(p + "  } else {"); cw("0"); out.append(p + "    reg[R_SHIFT].I = 0; }")
            out.append(p + "} else { reg[R_SHIFT].I = v; }")
        elif base == "lsr":
            out.append(p + "if (s) {")
            out.append(p + "  if (s == 32) {"); cw("v & 0x80000000"); out.append(p + "    reg[R_SHIFT].I = 0;")
            out.append(p + "  } else if (s < 32) {"); cw("(v >> (s - 1)) & 1"); out.append(p + "    reg[R_SHIFT].I = v >> s;")
            out.append(p + "  } else {"); cw("0"); out.append(p + "    reg[R_SHIFT].I = 0; }")
            out.append(p + "} else { reg[R_SHIFT].I = v; }")
        elif base == "asr":
            out.append(p + "if (s < 32) {")
            out.append(p + "  if (s) {"); cw("((s32)v >> (int)(s - 1)) & 1"); out.append(p + "    reg[R_SHIFT].I = (u32)((s32)v >> (int)s);")
            out.append(p + "  } else { reg[R_SHIFT].I = v; }")
            out.append(p + "} else if (v & 0x80000000) {"); cw("1"); out.append(p + "  reg[R_SHIFT].I = 0xFFFFFFFF;")
            out.append(p + "} else {"); cw("0"); out.append(p + "  reg[R_SHIFT].I = 0; }")
        else:  # ror
            out.append(p + "if (s) {")
            out.append(p + "  s &= 0x1f;")
            out.append(p + "  if (s) {"); cw("(v >> (s - 1)) & 1"); out.append(p + "    reg[R_SHIFT].I = (v << (32 - s)) | (v >> s);")
            out.append(p + "  } else {"); cw("v & 0x80000000"); out.append(p + "    reg[R_SHIFT].I = v; }")
            out.append(p + "} else {"); cw("v & 0x80000000"); out.append(p + "  reg[R_SHIFT].I = v; }")


def emit_shift_labels(out):
    """Shift uops: compute operand2 into reg[R_SHIFT] (+ carry into reg[R_COUT]
    for the _shc variant), then fall through (pc_op++) to the op uop. Two carry
    flavors x two pcr flavors per form."""
    for form in SHIFT_FORMS:
        for carry in (False, True):
            for pcr in (False, True):
                nm = ("shc_" if carry else "sh_") + form + ("_pcr" if pcr else "")
                out.append(f"lbl_{nm}:")
                out.append("  {")
                out.append("    ARM7CACHEOP* op = pc_op;")
                if pcr:
                    # ARM7TDMI hardware quirk (verified against the
                    # arm7di-tests-dreamcast MOV_1 conformance test): when
                    # Rm==r15 is read as the shift operand, the PC value seen
                    # is current-instruction+8 for an IMMEDIATE shift amount,
                    # but current-instruction+12 for a REGISTER-specified
                    # shift amount (form ends in 'r') — the extra pipeline
                    # cycle needed to fetch the shift-amount register pushes
                    # PC one instruction further ahead.
                    pc_offset = 12 if form.endswith("r") else 8
                    out.append(f"    reg[15].I = dispatch_pc + {pc_offset};   /* Rm == r15 */")
                shift_body(out, form, "    ", carry)
                out.append("  }")
                # Fall through to the op uop (same instruction): no charge/advance.
                out.append("  pc_op++; goto *pc_op->dispatch;")


def emit_op_body(out, name, do, sets_flags, writes_rd, *,
                 op2, cout, pcr, pcwrite):
    """Emit one data-processing op-uop body.
      op2     : C expression for operand2 ('reg[op->r2].I' or 'op->imm')
      cout    : C expression for shifter carry, or None (arith / non-flag ops)
      pcr     : materialize reg[15]=dispatch_pc+8 (Rn==r15) before the op
      pcwrite : this is the Rd==15 variant -> set dispatch_pc + re-dispatch
    """
    uses_base = name not in ("mov", "movs", "mvn", "mvns")
    # PC-write variants always materialize reg[15]: an operand may be Rm/Rn==15
    # (e.g. MOV pc,pc-relative) and these are rare, so unconditional is fine.
    if pcr or pcwrite:
        out.append("  SET_PC_READ();")
    out.append("  {")
    out.append("    ARM7CACHEOP* op = pc_op;")
    if writes_rd and not pcwrite:
        out.append("    int dest = op->r0;")
    if uses_base:
        out.append("    int base = op->r1;")
    out.append(f"    u32 value = {op2};")
    if cout is not None:
        out.append(f"    bool C_OUT = {cout};")
    if pcwrite:
        # Rd is r15 by construction; write into reg[15] then dispatch_pc.
        out.append("    int dest = 15;")
        out.append(f"    {do}")
        if sets_flags:
            out.append("    clockTicks++;")
            out.append("    CPUSwitchMode(reg[17].I & 0x1f, false);")
        out.append("    dispatch_pc = reg[15].I & 0xFFFFFFFC;")
        out.append("  }")
        out.append("  DISPATCH_PC();")
    else:
        out.append(f"    {do}")
        out.append("  }")
        out.append("  ADVANCE();")


def emit_dp_labels(out):
    """Data-processing op uops. operand2 is read from reg[op->r2] (the decoder
    sets r2 = Rm, or R_SHIFT(46) when a shift uop precedes) or from op->imm.

    Variants per op:
      _reg            register operand2 (reg[op->r2])
      _imm0 / _immn   immediate operand2 (rot==0 / rot!=0)
      LOGICAL *S ops also split the register form into:
         _reg_creg    C_OUT from reg[R_COUT]  (a shift uop ran)
         _reg_cflag   C_OUT from C_FLAG       (plain Rm, no shift)
      each optionally suffixed _pcr (Rn==r15) and, for writes_rd ops, a
      separate _pc / _pcs (Rd==r15) PC-write variant.
    """
    for (name, do, _op4, sets_flags, writes_rd) in DP:
        is_logical_flags = sets_flags and name in LOGICAL
        for pcr in (False, True):
            sfx = "_pcr" if pcr else ""
            # ---- register-operand variants ----
            if is_logical_flags:
                out.append(f"lbl_dp_{name}_reg_creg{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="reg[op->r2].I", cout="reg[R_COUT].I != 0",
                             pcr=pcr, pcwrite=False)
                out.append(f"lbl_dp_{name}_reg_cflag{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="reg[op->r2].I", cout="C_FLAG",
                             pcr=pcr, pcwrite=False)
            else:
                # Non-logical ops (incl. arithmetic-S) derive C from the ALU,
                # not the shifter -> their DO_ macro never reads C_OUT.
                out.append(f"lbl_dp_{name}_reg{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="reg[op->r2].I", cout=None,
                             pcr=pcr, pcwrite=False)
            # ---- immediate-operand variants (carry: rot0=C_FLAG, rotn=bit31) ----
            if sets_flags and name in LOGICAL:
                out.append(f"lbl_dp_{name}_imm0{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="op->imm", cout="C_FLAG", pcr=pcr, pcwrite=False)
                out.append(f"lbl_dp_{name}_immn{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="op->imm", cout="(op->imm & 0x80000000) != 0",
                             pcr=pcr, pcwrite=False)
            else:
                out.append(f"lbl_dp_{name}_imm0{sfx}:")
                out.append(f"lbl_dp_{name}_immn{sfx}:")
                emit_op_body(out, name, do, sets_flags, writes_rd,
                             op2="op->imm", cout=None, pcr=pcr, pcwrite=False)
        # ---- PC-write variants (Rd==r15): one plain, one S (mode restore) ----
        # Only logical-flag ops' DO_*S macro reads C_OUT; everything else derives
        # C from the ALU (or doesn't set flags), so leave cout=None to avoid an
        # unused-variable. (For S-with-PC the flags are reloaded from SPSR by
        # CPUSwitchMode anyway, so the value is moot -- this just silences the warning.)
        pc_cout = "C_FLAG" if is_logical_flags else None
        if writes_rd:
            out.append(f"lbl_dp_{name}_pc:")
            emit_op_body(out, name, do, sets_flags, writes_rd,
                         op2="reg[op->r2].I", cout=pc_cout, pcr=False, pcwrite=True)
            out.append(f"lbl_dp_{name}_pc_imm:")
            emit_op_body(out, name, do, sets_flags, writes_rd,
                         op2="op->imm", cout=pc_cout, pcr=False, pcwrite=True)
    # Shared PC-write continuation used by the LDR-to-PC path (emit_mem_labels):
    # dispatch_pc already holds the loaded/computed PC.
    out.append("dp_pc_done:")
    out.append("  DISPATCH_PC();")


def emit_dp_decode(out):
    """Decoder: map a data-processing opcode to its uop chain.

    Immediate operand2 -> a single op uop (_imm0/_immn, or _pc_imm for Rd==15).
    Register operand2:
      * plain Rm (LSL #0, no register shift) -> a single op uop reading reg[Rm].
      * any real shift/rotate -> a SHIFT uop (which the entrypoint/cond-guard
        falls through to) that writes reg[R_SHIFT], then an op uop reading
        reg[R_SHIFT]. The shift uop occupies `cop`; the op uop is the next slot
        and ARM7_CACHE_LAST_OP is bumped by one extra.
    Rd==15 routes to the _pc / _pc_imm PC-write op variants.
    """
    regforms = ["lsli", "lsri", "asri", "rori", "lslr", "lsrr", "asrr", "rorr"]
    out.append("  /* data processing: cond 00 I opcode4 S Rn Rd operand2 */")
    out.append("  {")
    out.append("    u32 op4 = (opcode >> 21) & 0xF;")
    out.append("    u32 sbit = (opcode >> 20) & 1;")
    out.append("    u32 rd = (opcode >> 12) & 0xF;")
    out.append("    u32 rn = (opcode >> 16) & 0xF;")
    out.append("    u32 rm = opcode & 0xF;")
    out.append("    int rn15 = (rn == 15);")
    out.append("    int rm15 = (rm == 15);")
    out.append("    int pcwrite = (rd == 15);")
    out.append("    u32 key = (op4 << 1) | sbit;")
    out.append("")
    out.append("    if (opcode & 0x02000000) {")
    out.append("      /* ---- immediate operand2 (single op uop) ---- */")
    out.append("      u32 imm8 = opcode & 0xFF;")
    out.append("      u32 rot  = ((opcode >> 8) & 0xF) * 2;")
    out.append("      cop->r0 = rd; cop->r1 = rn;")
    out.append("      cop->imm = rot ? ((imm8 >> rot) | (imm8 << (32 - rot))) : imm8;")
    out.append("      int rotn = rot != 0;")
    out.append("      switch (key) {")
    for (name, do, op4, sets_flags, writes_rd) in DP:
        k = (op4 << 1) | (1 if sets_flags else 0)
        if writes_rd:
            out.append(f"      case 0x{k:02x}: cop->dispatch = pcwrite ? &&lbl_dp_{name}_pc_imm")
            out.append(f"        : rn15 ? (rotn ? &&lbl_dp_{name}_immn_pcr : &&lbl_dp_{name}_imm0_pcr)")
            out.append(f"        :        (rotn ? &&lbl_dp_{name}_immn     : &&lbl_dp_{name}_imm0); break;")
        else:
            out.append(f"      case 0x{k:02x}: cop->dispatch =")
            out.append(f"        rn15 ? (rotn ? &&lbl_dp_{name}_immn_pcr : &&lbl_dp_{name}_imm0_pcr)")
            out.append(f"        :      (rotn ? &&lbl_dp_{name}_immn     : &&lbl_dp_{name}_imm0); break;")
    out.append("      default: cop->dispatch = &&lbl_undefined; break;")
    out.append("      }")
    out.append("    } else {")
    out.append("      /* ---- register operand2 ---- */")
    out.append("      u32 shtype = (opcode >> 5) & 3;   /* 0 LSL 1 LSR 2 ASR 3 ROR */")
    out.append("      u32 byreg  = (opcode >> 4) & 1;   /* shift by register */")
    out.append("      u32 samt   = (opcode >> 7) & 0x1F;")
    out.append("      /* 'plain Rm': LSL #0 with no register shift -> no shift uop. */")
    out.append("      int plain = (!byreg && shtype == 0 && samt == 0);")
    out.append("      int fsel = (byreg << 2) | shtype;  /* 0..7 index into regforms */")
    out.append("      if (plain) {")
    out.append("        /* op uop reads reg[Rm] directly; carry (logical-S) from C_FLAG. */")
    out.append("        /* pcr if EITHER operand reads r15 (Rm is read directly here). */")
    out.append("        int pcr = rn15 || rm15;")
    out.append("        cop->r0 = rd; cop->r1 = rn; cop->r2 = rm;")
    out.append("        switch (key) {")
    for (name, do, op4, sets_flags, writes_rd) in DP:
        k = (op4 << 1) | (1 if sets_flags else 0)
        logical = sets_flags and name in LOGICAL
        reg_lbl = (f"lbl_dp_{name}_reg_cflag" if logical else f"lbl_dp_{name}_reg")
        if writes_rd:
            out.append(f"        case 0x{k:02x}: cop->dispatch = pcwrite ? &&lbl_dp_{name}_pc")
            out.append(f"          : pcr ? &&{reg_lbl}_pcr : &&{reg_lbl}; break;")
        else:
            out.append(f"        case 0x{k:02x}: cop->dispatch = pcr ? &&{reg_lbl}_pcr : &&{reg_lbl}; break;")
    out.append("        default: cop->dispatch = &&lbl_undefined; break;")
    out.append("        }")
    out.append("      } else {")
    out.append("        /* SHIFT uop in `cop`, op uop in the next slot. */")
    out.append("        ARM7CACHEOP* opu = &ARM7_CACHE[ARM7_CACHE_LAST_OP + 1];")
    out.append("        ARM7_CACHE_LAST_OP++;   /* extra slot for the op uop */")
    out.append("        opu->cond = cop->cond;")
    out.append("        opu->r0 = rd; opu->r1 = rn; opu->r2 = R_SHIFT;")
    out.append("        cop->r2 = rm;")
    out.append("        cop->imm = byreg ? ((opcode >> 8) & 0xF) : samt;  /* Rs | amount */")
    out.append("        /* select shift uop (carry-writing iff op is logical-S) and op uop */")
    out.append("        switch (key) {")
    for (name, do, op4, sets_flags, writes_rd) in DP:
        k = (op4 << 1) | (1 if sets_flags else 0)
        logical = sets_flags and name in LOGICAL
        shpfx = "shc_" if logical else "sh_"
        op_lbl = (f"lbl_dp_{name}_reg_creg" if logical else f"lbl_dp_{name}_reg")
        out.append(f"        case 0x{k:02x}:")
        # shift uop: pick form (+_pcr if Rm==15); op uop: op_lbl (+_pcr if Rn==15)
        out.append("          switch (fsel) {")
        for i, ff in enumerate(regforms):
            out.append(f"          case {i}: cop->dispatch = rm15 ? &&lbl_{shpfx}{ff}_pcr : &&lbl_{shpfx}{ff}; break;")
        out.append("          }")
        if writes_rd:
            out.append(f"          opu->dispatch = pcwrite ? &&lbl_dp_{name}_pc")
            out.append(f"            : rn15 ? &&{op_lbl}_pcr : &&{op_lbl};")
        else:
            out.append(f"          opu->dispatch = rn15 ? &&{op_lbl}_pcr : &&{op_lbl};")
        out.append("          break;")
    out.append("        default: cop->dispatch = &&lbl_undefined; ARM7_CACHE_LAST_OP--; break;")
    out.append("        }")
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
    emit_shift_labels(out)
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
