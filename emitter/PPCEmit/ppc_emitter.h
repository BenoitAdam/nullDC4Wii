// ppc_emitter.h - Helper file for the PPC JIT emitter
// Includes register definitions, opcode shortcuts, and pseudo-instructions
// for nullDCe (Dreamcast emulator for Wii / Broadway PPC 750CL)

#pragma once

#include "ppc_emit.h"

// ============================================================
//  Integer Register Numbers
// ============================================================
enum ppc_ireg
{
	ppc_r0   = 0,                              // volatile, used in linkage / special cases
	ppc_r1   = 1,  ppc_sp  = ppc_r1,          // stack pointer
	ppc_r2   = 2,                              // reserved (TOC on ELF, thread ptr on Wii)
	ppc_r3   = 3,  ppc_rrv0  = ppc_r3,  ppc_rarg0 = ppc_r3,
	ppc_r4   = 4,  ppc_rrv1  = ppc_r4,  ppc_rarg1 = ppc_r4,
	ppc_r5   = 5,  ppc_rarg2 = ppc_r5,
	ppc_r6   = 6,  ppc_rarg3 = ppc_r6,
	ppc_r7   = 7,  ppc_rarg4 = ppc_r7,
	ppc_r8   = 8,  ppc_rarg5 = ppc_r8,
	ppc_r9   = 9,  ppc_rarg6 = ppc_r9,
	ppc_r10  = 10, ppc_rarg7 = ppc_r10,
	ppc_r11  = 11,                             // volatile (used in PLT calls)
	ppc_r12  = 12,                             // volatile (used in PLT calls)
	ppc_r13  = 13,                             // non-volatile (small data area on ELF)
	ppc_r14  = 14,
	ppc_r15  = 15,
	ppc_r16  = 16,
	ppc_r17  = 17,
	ppc_r18  = 18,
	ppc_r19  = 19,
	ppc_r20  = 20,
	ppc_r21  = 21,
	ppc_r22  = 22,
	ppc_r23  = 23,
	ppc_r24  = 24,
	ppc_r25  = 25,
	ppc_r26  = 26,
	ppc_r27  = 27,
	ppc_r28  = 28,
	ppc_r29  = 29,
	ppc_r30  = 30,
	ppc_r31  = 31,

	// SPR field values used with mfspr / mtspr
	ppc_spr_xer  = 32,   // XER  (spr field = 1 << 5)
	ppc_spr_lr   = 256,  // LR   (spr field = 8 << 5)
	ppc_spr_ctr  = 288,  // CTR  (spr field = 9 << 5)
};

// ============================================================
//  Condition Register Fields
// ============================================================
enum ppc_cr_reg
{
	ppc_cr0 = 0,
	ppc_cr1 = 1,
	ppc_cr2 = 2,
	ppc_cr3 = 3,
	ppc_cr4 = 4,
	ppc_cr5 = 5,
	ppc_cr6 = 6,
	ppc_cr7 = 7,
};

// ============================================================
//  Floating-Point Register Numbers
// ============================================================
enum ppc_freg
{
	ppc_f0   = 0,
	ppc_f1   = 1,  ppc_frv0  = ppc_f1,  ppc_farg0  = ppc_f1,
	ppc_f2   = 2,  ppc_frv1  = ppc_f2,  ppc_farg1  = ppc_f2,
	ppc_f3   = 3,  ppc_frv2  = ppc_f3,  ppc_farg2  = ppc_f3,
	ppc_f4   = 4,  ppc_frv3  = ppc_f4,  ppc_farg3  = ppc_f4,
	ppc_f5   = 5,  ppc_farg4 = ppc_f5,
	ppc_f6   = 6,  ppc_farg5 = ppc_f6,
	ppc_f7   = 7,  ppc_farg6 = ppc_f7,
	ppc_f8   = 8,  ppc_farg7 = ppc_f8,
	ppc_f9   = 9,  ppc_farg8 = ppc_f9,
	ppc_f10  = 10, ppc_farg9 = ppc_f10,
	ppc_f11  = 11, ppc_farg10 = ppc_f11,
	ppc_f12  = 12, ppc_farg11 = ppc_f12,
	ppc_f13  = 13, ppc_farg12 = ppc_f13,
	ppc_f14  = 14,
	ppc_f15  = 15,
	ppc_f16  = 16,
	ppc_f17  = 17,
	ppc_f18  = 18,
	ppc_f19  = 19,
	ppc_f20  = 20,
	ppc_f21  = 21,
	ppc_f22  = 22,
	ppc_f23  = 23,
	ppc_f24  = 24,
	ppc_f25  = 25,
	ppc_f26  = 26,
	ppc_f27  = 27,
	ppc_f28  = 28,
	ppc_f29  = 29,
	ppc_f30  = 30,
	ppc_f31  = 31,
};

// ============================================================
//  Branch BO field values (Branch Options)
// ============================================================
#define BO_DEC_NZ_FALSE  0x00  // Decrement CTR, branch if CTR!=0 and CR bit false
#define BO_DEC_Z_FALSE   0x02  // Decrement CTR, branch if CTR==0 and CR bit false
#define BO_FALSE         0x04  // Branch if CR bit false
#define BO_DEC_NZ_TRUE   0x08  // Decrement CTR, branch if CTR!=0 and CR bit true
#define BO_DEC_Z_TRUE    0x0A  // Decrement CTR, branch if CTR==0 and CR bit true
#define BO_TRUE          0x0C  // Branch if CR bit true
#define BO_DEC_NZ        0x10  // Decrement CTR, branch if CTR!=0 (ignore CR)
#define BO_DEC_Z         0x12  // Decrement CTR, branch if CTR==0 (ignore CR)
#define BO_ALWAYS        0x14  // Branch always

#define BO_HINT_TAKEN    0x01  // Branch prediction hint: likely taken

// ============================================================
//  Branch BI field values (CR bit selectors)
// ============================================================
// CR0
#define BI_CR0_LT   0
#define BI_CR0_GT   1
#define BI_CR0_EQ   2
#define BI_CR0_SO   3
#define BI_CR0_UN   3

// CR1
#define BI_CR1_LT   4
#define BI_CR1_GT   5
#define BI_CR1_EQ   6
#define BI_CR1_SO   7
#define BI_CR1_UN   7

// CR2
#define BI_CR2_LT   8
#define BI_CR2_GT   9
#define BI_CR2_EQ   10
#define BI_CR2_SO   11
#define BI_CR2_UN   11

// CR3
#define BI_CR3_LT   12
#define BI_CR3_GT   13
#define BI_CR3_EQ   14
#define BI_CR3_SO   15
#define BI_CR3_UN   15

// CR4
#define BI_CR4_LT   16
#define BI_CR4_GT   17
#define BI_CR4_EQ   18
#define BI_CR4_SO   19
#define BI_CR4_UN   19

// CR5
#define BI_CR5_LT   20
#define BI_CR5_GT   21
#define BI_CR5_EQ   22
#define BI_CR5_SO   23
#define BI_CR5_UN   23

// CR6
#define BI_CR6_LT   24
#define BI_CR6_GT   25
#define BI_CR6_EQ   26
#define BI_CR6_SO   27
#define BI_CR6_UN   27

// CR7
#define BI_CR7_LT   28
#define BI_CR7_GT   29
#define BI_CR7_EQ   30
#define BI_CR7_SO   31
#define BI_CR7_UN   31

// Helper: compute BI from CR field + bit index (0=LT,1=GT,2=EQ,3=SO)
#define BI_CR(crN, bit)  (((crN) * 4) + (bit))

// ============================================================
//  SPR Pseudo-instructions
// ============================================================

// Move to Link Register
#define ppc_mtlr(S)       ppc_mtspr(ppc_spr_lr,  (S))

// Move from Link Register
#define ppc_mflr(D)       ppc_mfspr((D), ppc_spr_lr)

// Move to Count Register
#define ppc_mtctr(S)      ppc_mtspr(ppc_spr_ctr, (S))

// Move from Count Register
#define ppc_mfctr(D)      ppc_mfspr((D), ppc_spr_ctr)

// Move to XER
#define ppc_mtxer(S)      ppc_mtspr(ppc_spr_xer, (S))

// Move from XER
#define ppc_mfxer(D)      ppc_mfspr((D), ppc_spr_xer)

// ============================================================
//  Common Branch Pseudo-instructions
// ============================================================

// Branch to Link Register (return)
#define ppc_blr()         ppc_bclrx(BO_ALWAYS, BI_CR0_EQ, 0)

// Branch and Link to Link Register (rarely used, but valid)
#define ppc_blrl()        ppc_bclrx(BO_ALWAYS, BI_CR0_EQ, 1)

// Branch to Count Register
#define ppc_bctr()        ppc_bcctrx(BO_ALWAYS, BI_CR0_EQ, 0)

// Branch and Link to Count Register (call via CTR)
#define ppc_bctrl()       ppc_bcctrx(BO_ALWAYS, BI_CR0_EQ, 1)

// Conditional branch to LR helpers (CR0)
#define ppc_beqlr()       ppc_bclrx(BO_TRUE,  BI_CR0_EQ, 0)
#define ppc_bnelr()       ppc_bclrx(BO_FALSE, BI_CR0_EQ, 0)
#define ppc_bltlr()       ppc_bclrx(BO_TRUE,  BI_CR0_LT, 0)
#define ppc_bgtlr()       ppc_bclrx(BO_TRUE,  BI_CR0_GT, 0)

// ============================================================
//  Data Movement Pseudo-instructions
// ============================================================

// Move register to register:  rD = rS   (ori rD, rS, 0)
#define ppc_mr(D, S)      ppc_ori((D), (S), 0)

// Older alias kept for compatibility
#define ppc_mov(D, S)     ppc_mr((D), (S))

// No-operation
#define ppc_nop()         ppc_ori(ppc_r0, ppc_r0, 0)

// Load immediate 16-bit signed value (rA=0 form of addi)
#define ppc_li(D, IMM)    ppc_addi((D), ppc_r0, (IMM))

// Load immediate shifted (upper 16 bits, lower 16 = 0)
#define ppc_lis(D, IMM)   ppc_addis((D), ppc_r0, (IMM))

// Load immediate 32-bit value into register (two instructions)
// Note: IMM must be a compile-time constant (or expression).
// Uses r0-safe addis+ori pair.
#define ppc_li32(D, IMM) \
	do { \
		u32 _imm32 = (u32)(IMM); \
		ppc_lis((D), (u32)(_imm32 >> 16)); \
		ppc_ori((D), (D), (u32)(_imm32 & 0xFFFF)); \
	} while(0)

// Negate:  rD = -rA    (subfx rD, rA, r0, 0, 0)
#define ppc_neg(D, A)     ppc_negx((D), (A), 0, 0)

// Not:     rA = ~rS    (norx rA, rS, rS, 0)
#define ppc_not(A, S)     ppc_norx((A), (S), (S), 0)

// ============================================================
//  Comparison Pseudo-instructions (common shortcuts)
// ============================================================

// Compare word signed vs immediate, result in CR0
#define ppc_cmpwi(A, IMM)     ppc_cmpi(ppc_cr0, (A), (IMM), 0)

// Compare word unsigned vs immediate, result in CR0
#define ppc_cmplwi(A, UIMM)   ppc_cmpli(ppc_cr0, (A), (UIMM), 0)

// Compare word signed rA vs rB, result in CR0
#define ppc_cmpw(A, B)        ppc_cmp(ppc_cr0, (A), (B), 0)

// Compare word unsigned rA vs rB, result in CR0
#define ppc_cmplw(A, B)       ppc_cmpl(ppc_cr0, (A), (B), 0)

// ============================================================
//  Arithmetic Shortcuts (Rc=0, OE=0 common cases)
// ============================================================

#define ppc_add(D, A, B)      ppc_addx((D), (A), (B), 0, 0)
#define ppc_add_(D, A, B)     ppc_addx((D), (A), (B), 0, 1)  // sets CR0
#define ppc_addo(D, A, B)     ppc_addx((D), (A), (B), 1, 0)  // sets XER OV
#define ppc_subf(D, A, B)     ppc_subfx((D), (A), (B), 0, 0)
#define ppc_subf_(D, A, B)    ppc_subfx((D), (A), (B), 0, 1)
#define ppc_mullw(D, A, B)    ppc_mullwx((D), (A), (B), 0, 0)
#define ppc_mullw_(D, A, B)   ppc_mullwx((D), (A), (B), 0, 1)
#define ppc_divw(D, A, B)     ppc_divwx((D), (A), (B), 0, 0)
#define ppc_divwu(D, A, B)    ppc_divwux((D), (A), (B), 0, 0)

// ============================================================
//  Logical Shortcuts (Rc=0 common cases)
// ============================================================

#define ppc_and(A, S, B)      ppc_andx((A), (S), (B), 0)
#define ppc_and_(A, S, B)     ppc_andx((A), (S), (B), 1)
#define ppc_or(A, S, B)       ppc_orx((A), (S), (B), 0)
#define ppc_or_(A, S, B)      ppc_orx((A), (S), (B), 1)
#define ppc_xor(A, S, B)      ppc_xorx((A), (S), (B), 0)
#define ppc_xor_(A, S, B)     ppc_xorx((A), (S), (B), 1)
#define ppc_nor(A, S, B)      ppc_norx((A), (S), (B), 0)
#define ppc_andc(A, S, B)     ppc_andcx((A), (S), (B), 0)
#define ppc_orc(A, S, B)      ppc_orcx((A), (S), (B), 0)
#define ppc_nand(A, S, B)     ppc_nandx((A), (S), (B), 0)
#define ppc_eqv(A, S, B)      ppc_eqvx((A), (S), (B), 0)

// ============================================================
//  Shift Shortcuts (Rc=0 common cases)
// ============================================================

#define ppc_slw(A, S, B)      ppc_slwx((A), (S), (B), 0)
#define ppc_srw(A, S, B)      ppc_srwx((A), (S), (B), 0)
#define ppc_sraw(A, S, B)     ppc_srawx((A), (S), (B), 0)
#define ppc_srawi(A, S, SH)   ppc_srawix((A), (S), (SH), 0)
#define ppc_cntlzw(A, S)      ppc_cntlzwx((A), (S), 0)
#define ppc_extsb(A, S)       ppc_extsbx((A), (S), 0)
#define ppc_extsh(A, S)       ppc_extshx((A), (S), 0)

// ============================================================
//  Rotate/Mask Shortcuts
// ============================================================

#define ppc_rlwinm(A, S, SH, MB, ME)  ppc_rlwinmx((A), (S), (SH), (MB), (ME), 0)
#define ppc_rlwimi(A, S, SH, MB, ME)  ppc_rlwimix((A), (S), (SH), (MB), (ME), 0)
#define ppc_rlwnm(A, S, B, MB, ME)    ppc_rlwnmx((A), (S), (B), (MB), (ME), 0)

// Common rotate patterns:
// Extract/clear bits [MB..ME] keeping others as 0:
#define ppc_clrlwi(A, S, n)   ppc_rlwinm((A), (S), 0,  (n), 31)   // clear left n bits
#define ppc_clrrwi(A, S, n)   ppc_rlwinm((A), (S), 0,  0, 31-(n)) // clear right n bits
#define ppc_slwi(A, S, n)     ppc_rlwinm((A), (S), (n), 0, 31-(n))
#define ppc_srwi(A, S, n)     ppc_rlwinm((A), (S), 32-(n), (n), 31)
#define ppc_rotlwi(A, S, n)   ppc_rlwinm((A), (S), (n), 0, 31)

// ============================================================
//  Floating-Point Shortcuts (Rc=0 common cases)
// ============================================================

#define ppc_fadd(D, A, B)     ppc_faddx((D), (A), (B), 0)
#define ppc_fsub(D, A, B)     ppc_fsubx((D), (A), (B), 0)
#define ppc_fmul(D, A, C)     ppc_fmulx((D), (A), (C), 0)
#define ppc_fdiv(D, A, B)     ppc_fdivx((D), (A), (B), 0)
#define ppc_fmr(D, B)         ppc_fmrx((D), (B), 0)
#define ppc_fneg(D, B)        ppc_fnegx((D), (B), 0)
#define ppc_fabs(D, B)        ppc_fabsx((D), (B), 0)
#define ppc_fnabs(D, B)       ppc_fnabsx((D), (B), 0)
#define ppc_frsp(D, B)        ppc_frspx((D), (B), 0)   // round to single
#define ppc_fctiwz(D, B)      ppc_fctiwzx((D), (B), 0) // convert fp->int truncate
#define ppc_fsqrt(D, B)       ppc_fsqrtx((D), (B), 0)
#define ppc_fsel(D, A, C, B)  ppc_fselx((D), (A), (B), (C), 0)
#define ppc_fmadd(D, A, C, B) ppc_fmaddx((D), (A), (B), (C), 0)
#define ppc_fmsub(D, A, C, B) ppc_fmsubx((D), (A), (B), (C), 0)
#define ppc_fnmsub(D, A, C, B) ppc_fnmsubx((D), (A), (B), (C), 0)  // D = B - A*C
#define ppc_frsqrte(D, B)     ppc_frsqrtex((D), (B), 0)  // D ~ 1/sqrt(B), >=5 bits
#define ppc_fres(D, B)        ppc_fresx((D), (B), 0)     // D ~ 1/B, >=8 bits
#define ppc_fadds(D, A, B)    ppc_faddsx((D), (A), (B), 0)
#define ppc_fsubs(D, A, B)    ppc_fsubsx((D), (A), (B), 0)
#define ppc_fmuls(D, A, C)    ppc_fmulsx((D), (A), (C), 0)
#define ppc_fdivs(D, A, B)    ppc_fdivsx((D), (A), (B), 0)

// ============================================================
//  Unconditional Branches (common forms)
// ============================================================

// Branch relative (offset in bytes, will be shifted >>2 internally)
// ppc_b(offset): offset is in instruction words from current PC
#define ppc_b(LI)             ppc_bx((LI), 0, 0)   // branch relative
#define ppc_ba(LI)            ppc_bx((LI), 1, 0)   // branch absolute
#define ppc_bl(LI)            ppc_bx((LI), 0, 1)   // branch and link (call)
#define ppc_bla(LI)           ppc_bx((LI), 1, 1)   // branch and link absolute

// Conditional branches (relative, AA=0, LK=0)
#define ppc_beq(BI, BD)       ppc_bcx(BO_TRUE,  BI_CR0_EQ, (BD), 0, 0)
#define ppc_bne(BI, BD)       ppc_bcx(BO_FALSE, BI_CR0_EQ, (BD), 0, 0)
#define ppc_blt(BI, BD)       ppc_bcx(BO_TRUE,  BI_CR0_LT, (BD), 0, 0)
#define ppc_bgt(BI, BD)       ppc_bcx(BO_TRUE,  BI_CR0_GT, (BD), 0, 0)
#define ppc_ble(BI, BD)       ppc_bcx(BO_FALSE, BI_CR0_GT, (BD), 0, 0)
#define ppc_bge(BI, BD)       ppc_bcx(BO_FALSE, BI_CR0_LT, (BD), 0, 0)
#define ppc_bso(BI, BD)       ppc_bcx(BO_TRUE,  BI_CR0_SO, (BD), 0, 0)
#define ppc_bns(BI, BD)       ppc_bcx(BO_FALSE, BI_CR0_SO, (BD), 0, 0)

// CTR-based loop helpers
#define ppc_bdnz(BD)          ppc_bcx(BO_DEC_NZ, BI_CR0_EQ, (BD), 0, 0)
#define ppc_bdz(BD)           ppc_bcx(BO_DEC_Z,  BI_CR0_EQ, (BD), 0, 0)

// ============================================================
//  Cache / Memory Barrier Pseudo-instructions
// ============================================================

// Data cache block zero (useful for zeroing stack frames fast)
#define ppc_dcbz(A, B)        ppc_emit(0x7C0007EC | ((0x1f & (u32)(A))<<16) | ((0x1f & (u32)(B))<<11))

// Instruction sync (flush pipeline, needed after JIT code writes)
#define ppc_isync()           ppc_emit(0x4C00012C)

// ============================================================
//  Label / Patch Infrastructure
// ============================================================

// ppc_label is placed at the emit pointer BEFORE the branch instruction.
// After emitting the target code, call MarkLabel() to patch the 16-bit
// displacement back into the branch word.
//
// Usage:
//   ppc_label* lbl = ppc_CreateLabel();
//   ppc_bcx(BO_TRUE, BI_CR0_EQ, 0, 0, 0);  // placeholder displacement
//   ... emit target code ...
//   lbl->MarkLabel();
//
struct ppc_label
{
	// Patch the 16-bit branch displacement in the instruction word
	// at this label's address.
	void MarkLabel()
	{
		u32* pt  = (u32*)this;
		u32  offs = (u32)((u8*)emit_GetCCPtr() - (u8*)pt);
		// Displacement is in bytes, already a multiple of 4.
		// Fits in bits [15:2] of the branch instruction word.
		*pt |= (offs & 0xFFFC);
	}

	// Patch for an absolute 26-bit branch (bx AA=0)
	void MarkLabelLong()
	{
		u32* pt  = (u32*)this;
		u32  offs = (u32)((u8*)emit_GetCCPtr() - (u8*)pt);
		*pt |= (offs & 0x03FFFFFC);
	}
};

// Returns a label token pointing at the *current* emit position.
// Call this BEFORE emitting the branch instruction you want to patch.
static inline ppc_label* ppc_CreateLabel()
{
	return (ppc_label*)emit_GetCCPtr();
}
