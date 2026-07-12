/*
	wii dynarec
	based on the mips one !

	PPC/Wii calling rules:
	Registers:
		32 32-bit gprs
			r0     volatile, temp
			r1     stack pointer, grows down
			r2     TOC (what ?) Pointer (who cares)
		    r3:10  volatile, first 8 params.r3 is also return value
		    r11    volatile (used as 'environment' pointers for calls by ptr .. what?)
		    r12    volatile (used for ming (mingw ?) & magic as well as linking)
		    r13:31 preserved (19 of em)
		
		32 64-bit fprs (single, vector or double)
		    f0     volatile, scratch
			f1:4   volatile, params, return
			f5:13  volatile, params
			f14:31 preserved (18 of em)
		LR  Link 
		XER (exception register)
		FPSCR
		CR (CR0:1;CR5:7 volatile, CR2:4 preserved)

	When calling 

	Call stack: 
		Return is stored on the link register and its saved at a specific location on function entry
		lr is stored on sp+4
		stack grows towards zero

	Improvements over original:
	  - BUG FIX: GPR restore loop in ngen_mainloop used ppc_r14+i instead of ppc_r13+i,
	             causing r13 to never be restored and r32 (invalid) to be written. Fixed.
	  - BUG FIX: ppc_lip<T> template overload was missing the destination register 'D'
	             parameter — it silently dropped it. Fixed signature to ppc_lip(u32 D, T*).
	  - Use static_cast<> instead of C-style casts for ppc_finvalid/ppc_rinvalid sentinels.
	  - Use snprintf instead of sprintf to guard against buffer overrun in dynarec filename.
	  - Removed redundant fflush(f) before fclose(f) (fclose already flushes).
	  - Fixed BET_StaticCall/BET_StaticJump: added proper braces around debug-log block
	    so indentation reflects actual control flow.
	  - Minor formatting/whitespace consistency improvements; no logic changes.
*/
#include "types.h"
#include <stddef.h>	// offsetof — used for jit_scratch context slot addressing
#include <math.h>	// sqrtf — fsqrt/fsrra native call targets
#include "dc\sh4\sh4_opcode_list.h"

#include "dc\sh4\sh4_registers.h"
#include "dc\sh4\sh4_interpreter.h"	// sh4_GetTimeslice — preset-latched timeslice
#include "dc\sh4\ccn.h"
#include "dc\sh4\rec_v2\ngen.h"
#include "dc\mem\sh4_mem.h"
#include "emitter\PPCEmit\ppc_emitter.h"

// wii_driver.cpp defines its own higher-level wrappers for these names.
// Undefine any macros from ppc_emitter.h that would clash with the
// local function definitions below.
#ifdef ppc_li
#  undef ppc_li
#endif
#ifdef ppc_lis
#  undef ppc_lis
#endif
#ifdef ppc_li32
#  undef ppc_li32
#endif
#ifdef ppc_mr
#  undef ppc_mr
#endif
#ifdef ppc_mov
#  undef ppc_mov
#endif
#ifdef ppc_nop
#  undef ppc_nop
#endif
#ifdef ppc_blr
#  undef ppc_blr
#endif
#ifdef ppc_bctr
#  undef ppc_bctr
#endif
#ifdef ppc_bctrl
#  undef ppc_bctrl
#endif
#ifdef ppc_b
#  undef ppc_b
#endif
#ifdef ppc_bl
#  undef ppc_bl
#endif
// These are defined as real functions below; ppc_bx is also a real function
// (overload of the auto-generated one) so guard it too.
#ifdef ppc_bx
#  undef ppc_bx
#endif

// Define "invalid" value for non-mapped-registery
const ppc_freg ppc_finvalid = static_cast<ppc_freg>(-1);  // sentinel: out-of-range (valid regs are 0..31)
const ppc_ireg ppc_rinvalid = static_cast<ppc_ireg>(-1);  // sentinel: out-of-range (valid regs are 0..31)

// This is defined in main.cpp
extern "C" int get_debug_loop();

// ppc_li: Loads a 32-bit immediate value into a PowerPC register.
void ppc_li(u32 D,u32 imm)
{
	if (is_s16(imm))
	{
		ppc_addi(D,0,imm);
		return;
	}
	else
	{
		ppc_addis(D,0,imm>>16);
		if ((u16)imm != 0) {
			ppc_ori(D,D,(u16)imm);
		}
	}
}

// =======================
// JUMP OFFSET CALCULATION
// =======================

snat ppc_jdiff_raw(void* dst)
{
	return (u8*)dst-(u8*)emit_GetCCPtr();
}
snat ppc_jdiff(void* dst)
{
	return ppc_jdiff_raw(dst)>>2;
}

void ppc_bx(void* dst,u32 LK)
{
	snat offs = ppc_jdiff_raw(dst);
	//offs must fit in 24 bits
	verify(offs<33554432 && offs>-33554432);
	offs>>=2;
	// does this work ? //
	ppc_bx(offs,0,LK);
}

void ppc_call(void* funct)
{
	ppc_bx(funct,1);
}
template<typename T> void ppc_call(T* dst) { return ppc_call((void*)dst); }

void ppc_jump(void* funct)
{
	ppc_bx(funct,0);
}
template<typename T> void ppc_jump(T* dst) { return ppc_jump((void*)dst); }
void ppc_call_and_jump(void* funct)
{
	ppc_call(funct);
	ppc_mtctr(ppc_r3);
	ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);  // bctr
}
template<typename T> void ppc_call_and_jump(T* dst) { return ppc_call_and_jump((void*)dst); }
void make_address_range_executable(void* addr, u32 size)
{
	//what gives?
	DCFlushRange(addr, size);
	ICInvalidateRange(addr, size);
}

void ppc_lip(u32 D,void* ptr)
{
	ppc_li(D,(u8*)ptr-(u8*)0);
}
template<typename T> void ppc_lip(u32 D, T* ptr) { return ppc_lip(D, (void*)ptr); }

ppc_ireg ppc_cycles = ppc_r29;
ppc_ireg ppc_contex = ppc_r30;
ppc_ireg ppc_djump = ppc_r31;
ppc_ireg ppc_next_pc = ppc_rarg0;

void ppc_emit(u32 insn)
{
	emit_Write32(insn);
}

void* loop_no_update;
void* ngen_LinkBlock_Static_stub;
void* ngen_LinkBlock_Dynamic_1st_stub;
void* ngen_LinkBlock_Dynamic_2nd_stub;
void* ngen_BlockCheckFail_stub;
void* loop_do_update_write;
void (*loop_code)() ;
void (*ngen_FailedToFindBlock)();

// Compare-and-branch fusion master switch. When 1, a set*/test/fset* whose
// destination is sr_T ALSO records which CR0 bit (and polarity) holds the
// fresh T value; if nothing clobbers CR0 or rewrites T before the block's
// BET_Cond exit, the exit branches on that CR0 bit directly instead of
// reloading T from the context and re-comparing (saving lwz+cmpi and, more
// importantly, the store->load hazard on the T context slot). Set to 0 to
// disable for A/B debugging.
#define FUSE_CMP_BRANCH 1

struct
{
	bool has_jcond;

	// cmp/branch fusion state: T's value is live in CR0.
	// t_in_cr0   — tracking is valid
	// t_cr0_bit  — BI_CR0_LT/GT/EQ bit index holding the condition
	// t_invert   — T = !bit instead of T = bit (setge/setae use inverted LT)
	bool t_in_cr0;
	u32  t_cr0_bit;
	bool t_invert;

	void Reset()
	{
		has_jcond=false;
		t_in_cr0=false;
		t_cr0_bit=0;
		t_invert=false;
	}
} compile_state;
u32 last_block;

// Forward decls: GPR/FPR allocation maps + flush/reload (defined later in
// this file).
ppc_ireg GetIntReg(u32 reg);
ppc_freg GetFloatReg(u32 reg);
void reg_flush_all();
void reg_reload_all();
void fpu_flush_all();
void fpu_reload_all();

// =======================
// BLOCK BEGIN/END
// =======================

void ngen_Begin(DecodedBlock* block,bool force_checks)
{
	compile_state.Reset();

	ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);
	
	ppc_label* jdst=ppc_CreateLabel();
	ppc_bcx(BO_FALSE,BI_CR0_LT,0,0,0);

	ppc_li(ppc_next_pc,block->start);
	ppc_jump(loop_do_update_write);

	jdst->MarkLabel();

	// No GPR reload here: pinned regs (r14..r28) hold the authoritative SH4 GPR
	// values continuously across blocks, so there is nothing to re-read. They are
	// loaded once in the mainloop prologue and only resynced with memory around
	// shop_ifb / the canonical fallback.
}

// =====================
// MEMORY ACCESS HELPERS
// =====================

// Static GPR allocation master switch. Full rationale at reg_flush_all().
// Must be defined before the first use below; set to 0 for all-memory mode.
#define STATIC_GPR_ALLOC 1

// Static FPU allocation master switch: fr0..fr15 pinned to f14..f29
// (callee-saved, so C calls preserve them). Mirrors STATIC_GPR_ALLOC:
// the pinned FPRs are the authoritative copy of fr0-15 for the whole JIT
// run; Sh4cntx.fr[] may be stale except at the fpu_flush/reload brackets
// (mainloop prologue, FPU-touching shop_ifb, canonical fallback, and
// sync_fpscr when FPSCR.FR can change — the fr<->xf bank swap happens in
// MEMORY inside UpdateFPSCR->ChangeFP). xf0-15 are NOT pinned and always
// live in memory. Set to 0 for all-memory mode (A/B debugging).
#define STATIC_FPU_ALLOC 1

//1 opcode
void ppc_sh_load(u32 D,u32 sh4_reg)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
	{
		// Value already lives in a pinned PPC register; just move it.
		if ((u32)ri!=D)
			ppc_ori(D,ri,0);	// mr D, ri
		return;
	}
#endif
	ppc_lwz(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load(D,prm._reg);
}
// Resolve a register source to the PPC reg an instruction can read directly:
// the pinned reg if `prm` is statically allocated, else load it into scratch
// `D` and return D. Lets single-instruction ops (cmp, and, add, ...) read
// pinned sources in place and skip the redundant `mr D,pinned` move. ONLY for
// ops whose dest is a scratch reg (or that don't write the source) — never when
// the op would clobber the pinned reg it just read.
static u32 src_or_load(u32 sh4_reg,u32 D)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
		return (u32)ri;
#endif
	ppc_sh_load(D,sh4_reg);
	return D;
}
static u32 src_or_load(shil_param prm,u32 D)
{
	verify(prm.is_reg());
	return src_or_load(prm._reg,D);
}
void ppc_sh_load_f32(u32 D,u32 sh4_reg)
{
#if STATIC_FPU_ALLOC
	ppc_freg fi=GetFloatReg(sh4_reg);
	if (fi!=ppc_finvalid)
	{
		// Value lives in a pinned FPR; register move instead of a load.
		if ((u32)fi!=D)
			ppc_fmrx(D,fi,0);
		return;
	}
#endif
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load_f32(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load_f32(D,prm._reg);
}
// Resolve a float source to the FPR an instruction can read directly: the
// pinned reg if statically allocated, else load into scratch D and return D.
// Same contract as src_or_load: ONLY for ops that won't clobber the returned
// pinned reg mid-sequence.
static u32 fsrc_or_load(u32 sh4_reg,u32 D)
{
#if STATIC_FPU_ALLOC
	ppc_freg fi=GetFloatReg(sh4_reg);
	if (fi!=ppc_finvalid)
		return (u32)fi;
#endif
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
	return D;
}
static u32 fsrc_or_load(shil_param prm,u32 D)
{
	verify(prm.is_reg());
	return fsrc_or_load(prm._reg,D);
}
void ppc_sh_load_u16(u32 D,u32 sh4_reg)
{
	ppc_lhz(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load_u16(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load_u16(D,prm._reg);
}
//1 opcode
void ppc_sh_addr(u32 D,u32 sh4_reg)
{
	ppc_addi(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_addr(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_addr(D,prm._reg);
}
//1 opcode
void ppc_sh_store(u32 D,u32 sh4_reg)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
	{
		// Destination is a pinned PPC register; update it in place.
		if ((u32)ri!=D)
			ppc_ori(ri,D,0);	// mr ri, D
		return;
	}
#endif
	ppc_stw(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_store(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_store(D,prm._reg);
}
void ppc_sh_store_f32(u32 D,u32 sh4_reg)
{
#if STATIC_FPU_ALLOC
	ppc_freg fi=GetFloatReg(sh4_reg);
	if (fi!=ppc_finvalid)
	{
		// Destination is a pinned FPR; update it in place.
		if ((u32)fi!=D)
			ppc_fmrx(fi,D,0);
		return;
	}
#endif
	ppc_stfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
u32 ppc_addr_high(u32 rD,void* ptr)
{
	unat diff=(u8*)ptr-(u8*)0;
	u32 rv=(s32)(s16)diff;
	diff-=rv;
	ppc_addis(rD,0,diff>>16);

	return rv;
}

void ppc_sh_store_f32(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_store_f32(D,prm._reg);
}

// --- Vector float element access -------------------------------------------
// For a float param (scalar FMT_F32, or vector FMT_V4/FMT_V16), prm._reg/_imm
// holds the BASE fr/xf register index. Element i lives at offset(base)+i*4 in
// the context. These load/store the i-th single-precision element.
//
// With STATIC_FPU_ALLOC, fr-based elements resolve to their pinned FPR
// (fmr / direct read); xf-based elements (e.g. the ftrv XMTRX) still hit
// memory, which stays authoritative for the un-pinned bank.
u32 ppc_fvec_ofs(shil_param prm,u32 i)
{
	return Sh4cntx.offset(prm._reg) + i*4;
}
// Pinned FPR for element i of a vector param, or ppc_finvalid.
ppc_freg ppc_fvec_pin(shil_param prm,u32 i)
{
#if STATIC_FPU_ALLOC
	return GetFloatReg(prm._reg + i);
#else
	return ppc_finvalid;
#endif
}
void ppc_fvec_load(u32 fd,shil_param prm,u32 i)
{
	ppc_freg p=ppc_fvec_pin(prm,i);
	if (p!=ppc_finvalid)
	{
		if ((u32)p!=fd)
			ppc_fmrx(fd,p,0);
		return;
	}
	ppc_lfs(fd,ppc_contex,ppc_fvec_ofs(prm,i));
}
// Source resolution without the copy: returns the pinned FPR to read in
// place, or loads into scratch fd and returns fd. Same aliasing contract as
// fsrc_or_load.
u32 ppc_fvec_src(shil_param prm,u32 i,u32 fd)
{
	ppc_freg p=ppc_fvec_pin(prm,i);
	if (p!=ppc_finvalid)
		return (u32)p;
	ppc_lfs(fd,ppc_contex,ppc_fvec_ofs(prm,i));
	return fd;
}
void ppc_fvec_store(u32 fs,shil_param prm,u32 i)
{
	ppc_freg p=ppc_fvec_pin(prm,i);
	if (p!=ppc_finvalid)
	{
		if ((u32)p!=fs)
			ppc_fmrx(p,fs,0);
		return;
	}
	ppc_stfs(fs,ppc_contex,ppc_fvec_ofs(prm,i));
}

// ==========================
// CALLING CONVENTION ADAPTER
// ==========================

struct CC_PS
{
	CanonicalParamType type;
	shil_param* par;
};
vector<CC_PS> CC_pars;
void ngen_CC_Start(shil_opcode* op) 
{ 
	CC_pars.clear();
}
void ngen_CC_Param(shil_opcode* op,shil_param* par,CanonicalParamType tp)
{
	switch(tp)
	{
		// Return-value stores MUST go to context MEMORY, never to a pinned
		// register: the canonical fallback is bracketed reg_flush_all() ...
		// reg_reload_all(), and these stores are emitted between the call and
		// the reload. A store into a pinned reg would be clobbered by the
		// reload (which re-reads the context); a store into the context is
		// exactly what the reload picks up.
		case CPT_f32rv:
			ppc_stfs(ppc_frv0, ppc_contex, Sh4cntx.offset(par->_reg));
			break;

		case CPT_u32rv:
		case CPT_u64rvL:
			ppc_stw(ppc_rrv0, ppc_contex, Sh4cntx.offset(par->_reg));
			break;

		case CPT_u64rvH:
			ppc_stw(ppc_rrv1, ppc_contex, Sh4cntx.offset(par->_reg));
			break;

		case CPT_u32:
		case CPT_ptr:
		case CPT_f32:
			{
				CC_PS t={tp,par};
				CC_pars.push_back(t);
			}
			break;

		default:
			die("invalid tp");
	}
}
void ngen_CC_Call(shil_opcode*op,void* function) 
{
	u32 rd_fp=ppc_farg0;
	u32 rd_gpr=ppc_rarg0;
	for (int i=CC_pars.size();i-->0;)
	{
		if (CC_pars[i].type==CPT_ptr)
		{
			ppc_sh_addr(rd_gpr,*CC_pars[i].par);
		}
		else
		{
			if (CC_pars[i].par->is_reg())
			{
				if (CC_pars[i].type==CPT_f32)
				{
					ppc_sh_load_f32(rd_fp,*CC_pars[i].par);
					rd_fp++;
				}
				else
				{
					ppc_sh_load(rd_gpr,*CC_pars[i].par);
				}
			}
			else
				ppc_li(rd_gpr,CC_pars[i].par->_imm);
		}
		rd_gpr++;
	}
	//printf("used reg r0 to r%d, %d params, calling %08X\n",rd-1,CC_pars.size(),function);
	ppc_call(function);
}

// =================
// BINARY OPERATIONS
// =================

void binop_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());

	verify(op->rs1.is_reg());
	//verify(!op->rs2.is_imm() || op->rs2.is_imm_s16());
	
	ppc_sh_load(ppc_rarg0,op->rs1);

	if (op->rs2.is_imm())
	{
		ppc_li(ppc_rarg1,op->rs2._imm);
	}
	else if (op->rs2.is_reg())
	{
		ppc_sh_load(ppc_rarg1,op->rs2);
	}
}

void binop_end(shil_opcode* op)
{
	ppc_sh_store(ppc_rarg0,op->rd);
}

// ---------------------------------------------------------------------------
// Shadow-register-aware operand resolution (for SINGLE-instruction ops only).
//
// With static GPR allocation, an operand that is a pinned SH4 reg already lives
// in a PPC register, so the op can read/write it in place instead of bouncing
// through rarg0/rarg1. binop3_start() resolves rs1/rs2/rd into the PPC regs the
// op should use:
//   bop_a = source reg for rs1   (pinned reg, or rarg0 holding a loaded/imm value)
//   bop_b = source reg for rs2   (pinned reg, or rarg1 holding a loaded/imm value)
//   bop_d = dest reg for rd      (pinned reg, or rarg0 scratch)
//
// SAFETY: only valid for ops emitted as a SINGLE PPC instruction (add, sub,
// and, or, xor, shl, shr, sar, mul_i32). Such an instruction reads both sources
// then writes the dest atomically, so bop_d may safely alias bop_a or bop_b.
// Multi-instruction ops (mul_u16/64, ror, shld, shad, set*, conversions) must
// NOT use this — they clobber scratch mid-sequence; they keep using
// binop_start()/binop_end() with the rarg0/rarg1 scratch window.
// ---------------------------------------------------------------------------
u32 bop_d, bop_a, bop_b;

// Resolve rs1 -> bop_a (source) and rd -> bop_d (dest). Shared by the plain
// register form and the immediate-folded form.
void bop_resolve_a_d(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());

	ppc_ireg a=GetIntReg(op->rs1._reg);
	if (a!=ppc_rinvalid)
		bop_a=a;
	else
	{
		ppc_sh_load(ppc_rarg0,op->rs1);
		bop_a=ppc_rarg0;
	}

	ppc_ireg d=GetIntReg(op->rd._reg);
	bop_d = (d!=ppc_rinvalid) ? (u32)d : ppc_rarg0;
}

// Resolve rs2 -> bop_b (source). Materialises an immediate into rarg1.
void bop_resolve_b(shil_opcode* op)
{
	if (op->rs2.is_imm())
	{
		ppc_li(ppc_rarg1,op->rs2._imm);
		bop_b=ppc_rarg1;
	}
	else
	{
		ppc_ireg b=GetIntReg(op->rs2._reg);
		if (b!=ppc_rinvalid)
			bop_b=b;
		else
		{
			ppc_sh_load(ppc_rarg1,op->rs2);
			bop_b=ppc_rarg1;
		}
	}
}

void binop3_start(shil_opcode* op)
{
	verify(!op->rs2.is_null());
	bop_resolve_a_d(op);
	bop_resolve_b(op);
}

void binop3_end(shil_opcode* op)
{
	// If rd is pinned the op already wrote it in place; only a scratch dest
	// needs storing back to the context.
	if (bop_d==ppc_rarg0 && GetIntReg(op->rd._reg)==ppc_rinvalid)
		ppc_sh_store(ppc_rarg0,op->rd);
}

void binop_start_fpu(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());

	verify(op->rs1.is_reg());
	verify(op->rs2.is_reg());

	ppc_sh_load_f32(ppc_farg0,op->rs1);
	ppc_sh_load_f32(ppc_farg1,op->rs2);
}

void binop_end_fpu(shil_opcode* op)
{
	ppc_sh_store_f32(ppc_farg0,op->rd);
}

// Pinned-FPR lookup honoring the master switch (GetFloatReg itself is
// unconditional). All STATIC_FPU_ALLOC-aware codegen must use this.
static ppc_freg fpin(u32 reg)
{
#if STATIC_FPU_ALLOC
	return GetFloatReg(reg);
#else
	return ppc_finvalid;
#endif
}

// Pinned-aware resolution for SINGLE-instruction float ops (fadds/fsubs/
// fmuls/fdivs/fabs/fneg/fmadds): sources are read in place from pinned FPRs
// (or loaded to farg scratch), dest writes a pinned FPR directly (or farg0 +
// context store). Same aliasing rule as binop3: one instruction reads both
// sources then writes the dest atomically, so fop_d may alias fop_a/fop_b.
u32 fop_d, fop_a, fop_b;

void fbinop3_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());
	verify(op->rs2.is_reg());

	fop_a=fsrc_or_load(op->rs1,ppc_farg0);
	fop_b=fsrc_or_load(op->rs2,ppc_farg1);

	ppc_freg d=fpin(op->rd._reg);
	fop_d=(d!=ppc_finvalid)?(u32)d:ppc_farg0;
}

void fbinop3_end(shil_opcode* op)
{
	if (fop_d==ppc_farg0 && fpin(op->rd._reg)==ppc_finvalid)
		ppc_stfs(ppc_farg0,ppc_contex,Sh4cntx.offset(op->rd._reg));
}

void funop3_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());
	verify(op->rd.is_reg());

	fop_a=fsrc_or_load(op->rs1,ppc_farg0);

	ppc_freg d=fpin(op->rd._reg);
	fop_d=(d!=ppc_finvalid)?(u32)d:ppc_farg0;
}

void funop3_end(shil_opcode* op)
{
	fbinop3_end(op);
}

// =================
// UNARY OPERATIONS
// =================

// Unary integer: loads rs1 -> rarg0, operation writes rarg0, stores rarg0 -> rd
void unop_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rd.is_reg());

	if (op->rs1.is_imm())
		ppc_li(ppc_rarg0,op->rs1._imm);
	else
	{
		verify(op->rs1.is_reg());
		ppc_sh_load(ppc_rarg0,op->rs1);
	}
}

void unop_end(shil_opcode* op)
{
	ppc_sh_store(ppc_rarg0,op->rd);
}

// Shadow-register-aware unary resolution (SINGLE-instruction ops only:
// neg, not, ext_s8, ext_s16). Same safety rule as binop3_*: the op must be one
// PPC instruction so bop_d may alias bop_a. Sets bop_a (source) / bop_d (dest).
void unop3_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rd.is_reg());

	if (op->rs1.is_imm())
	{
		ppc_li(ppc_rarg0,op->rs1._imm);
		bop_a=ppc_rarg0;
	}
	else
	{
		verify(op->rs1.is_reg());
		ppc_ireg a=GetIntReg(op->rs1._reg);
		if (a!=ppc_rinvalid)
			bop_a=a;
		else
		{
			ppc_sh_load(ppc_rarg0,op->rs1);
			bop_a=ppc_rarg0;
		}
	}

	ppc_ireg d=GetIntReg(op->rd._reg);
	bop_d = (d!=ppc_rinvalid) ? (u32)d : ppc_rarg0;
}

void unop3_end(shil_opcode* op)
{
	if (bop_d==ppc_rarg0 && GetIntReg(op->rd._reg)==ppc_rinvalid)
		ppc_sh_store(ppc_rarg0,op->rd);
}

// Unary fpu: loads rs1 -> farg0, operation writes farg0, stores farg0 -> rd
void unop_start_fpu(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());
	verify(op->rd.is_reg());

	ppc_sh_load_f32(ppc_farg0,op->rs1);
}

void unop_end_fpu(shil_opcode* op)
{
	ppc_sh_store_f32(ppc_farg0,op->rd);
}

// ===================================================
// COMPARE -> BOOLEAN (branchless CR0 bit extraction)
// ===================================================
//
// SH4 set* ops produce a full 0/1 u32 in rd. We compare rarg0,rarg1 into CR0
// (signed cmp or unsigned cmpl) then extract the desired CR0 bit into rarg0:
//
//   mfcr  rarg0          ; rarg0[CR0 field] = LT GT EQ SO at bits 0..3
//   rlwinm rarg0,rarg0,(bit+1),31,31  ; rotate wanted bit to position 31, mask to 1
//
// CR0 occupies the top 4 bits of CR (bit 0=LT,1=GT,2=EQ,3=SO). After mfcr the
// LT bit is in word-bit 0, so the bit for BI_CR0_xx index 'b' sits at word-bit
// 'b'. A left-rotate of (b+1) brings it to bit 31; mask MB=ME=31 keeps just it.
static void emit_cr0_bit_to_rarg0(u32 cr0_bit_index)
{
	ppc_mfcr(ppc_rarg0);
	ppc_rlwinmx(ppc_rarg0,ppc_rarg0,cr0_bit_index+1,31,31,0);
}

// Compare rs1 against rs2 into CR0. cmp/cmpl/cmpi/cmpli can read ANY register,
// so when an operand already lives in a pinned PPC reg we compare it in place
// and skip the move into rarg0/rarg1 — only spilled operands (or immediates
// that don't fit the cmp-immediate forms) need a scratch load. Folds a small
// immediate rs2 into cmpi/cmpli (signed uses cmpi+s16, unsigned cmpli+u16).
//
// The follow-on emit_cr0_bit_to_rarg0 does mfcr rarg0, which clobbers ONLY
// rarg0 (scratch) and never the pinned source regs, so comparing in place is
// safe regardless of which operands were elided.
static void emit_cmp_into_cr0(shil_opcode* op,bool is_signed)
{
	// rs1 in a pinned reg -> use it directly; else load into rarg0 scratch.
	u32 a=src_or_load(op->rs1,ppc_rarg0);

	if (op->rs2.is_imm() && (is_signed ? op->rs2.is_imm_s16() : op->rs2.is_imm_u16()))
	{
		if (is_signed)
			ppc_cmpi(ppc_cr0,a,op->rs2._imm,0);
		else
			ppc_cmpli(ppc_cr0,a,op->rs2._imm,0);
		return;
	}

	// rs2 in a pinned reg -> use directly; immediate -> li into rarg1; else load.
	u32 b;
	if (op->rs2.is_imm())
		{ ppc_li(ppc_rarg1,op->rs2._imm); b=ppc_rarg1; }
	else
		b=src_or_load(op->rs2,ppc_rarg1);

	if (is_signed)
		ppc_cmp(ppc_cr0,a,b,0);
	else
		ppc_cmpl(ppc_cr0,a,b,0);
}

// cmp/branch fusion: after a set*-class op comparing into CR0 and writing T,
// remember which CR0 bit holds T (nothing after the compare — mfcr/rlwinm/
// xori/stw — touches CR0, so the state stays live). Non-T destinations still
// clobbered CR0, so they invalidate instead.
static void fusion_record(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
#if FUSE_CMP_BRANCH
	if (op->rd.is_r32i() && op->rd._reg==reg_sr_T)
	{
		compile_state.t_in_cr0=true;
		compile_state.t_cr0_bit=cr0_bit_index;
		compile_state.t_invert=invert;
	}
	else
		compile_state.t_in_cr0=false;
#endif
}

// rd = (signed) rs1 <cond> rs2  -> 0/1
static void emit_setcc_signed(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,true);
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
	fusion_record(op,cr0_bit_index,invert);
}

// rd = (unsigned) rs1 <cond> rs2 -> 0/1
static void emit_setcc_unsigned(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,false);
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
	fusion_record(op,cr0_bit_index,invert);
}

// Ops that provably leave CR0 untouched AND do not write sr_T, so tracked
// fusion state survives them. Everything else (memory ops: cmpli + possible
// cold-path C call; and/test: record-form andi.; any op that calls out;
// ifb/sync/pref; the set* family, which re-records itself) invalidates.
// XER.CA-only ops (adc/sbc/negc via addic/adde/subfe/addze, ror via subfic,
// sar/shad via srawi) are CR0-safe — CA is not CR0.
static bool fusion_op_safe(shil_opcode* op)
{
	// Any write to sr_T outside the recording set* breaks the mapping.
	if ((op->rd.is_r32i()  && op->rd._reg==reg_sr_T) ||
	    (op->rd2.is_r32i() && op->rd2._reg==reg_sr_T))
		return false;

	switch(op->op)
	{
	case shop_mov32: case shop_mov64:
	case shop_add: case shop_sub: case shop_or: case shop_xor:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror:
	case shop_shld: case shop_shad:
	case shop_neg: case shop_not: case shop_ext_s8: case shop_ext_s16:
	case shop_mul_i32: case shop_mul_u16: case shop_mul_s16:
	case shop_mul_u64: case shop_mul_s64:
	case shop_div32u: case shop_div32s: case shop_div32p2:
	case shop_adc: case shop_sbc: case shop_negc: case shop_swaplb:
	case shop_jdyn: case shop_jcond:
	case shop_fadd: case shop_fsub: case shop_fmul: case shop_fdiv:
	case shop_fabs: case shop_fneg: case shop_fmac:
	case shop_fipr: case shop_ftrv: case shop_fsca:
	case shop_cvt_f2i_t: case shop_cvt_i2f_n: case shop_cvt_i2f_z:
		return true;
	default:
		return false;
	}
}


void ngen_CC_Finish(shil_opcode* op) 
{ 
	CC_pars.clear(); 
}
void DoStatic(u32 pc)
{
	ppc_li(ppc_rarg0,pc);
	ppc_call(ngen_LinkBlock_Static_stub);
}

// ====================================
// ngen_End: Block Exit Code Generation
// ====================================

void ngen_End(DecodedBlock* block)
{
	// No blanket GPR flush here: the pinned PPC registers (r14..r28) are the
	// authoritative copy of the SH4 GPRs and stay live across straight-line
	// block transitions (static/dynamic jumps just flow into the next block).
	// The exception is the interrupt path (BET_*Intr) below, which calls
	// UpdateINTC -> Do_Exception (reads r[15], swaps the register bank); that
	// case brackets the call with reg_flush_all()/reg_reload_all() locally.
	// shop_ifb and the canonical fallback similarly self-bracket.
	switch(block->BlockType)
	{
	case BET_Cond_0:
	case BET_Cond_1:
		{
#if FUSE_CMP_BRANCH
			// Fused exit: the set*/test that produced T is still live in CR0
			// (no clobber since), so branch on its bit directly — skips the
			// T reload + cmpi AND the store->load hazard on the T slot.
			// Branch taken when T == (BlockType&1); the CR0 bit equals
			// T ^ t_invert, so the wanted bit value is (BlockType&1)^invert.
			// Only for the plain (non-jcond) form: jcond captures T into
			// djump EARLY (before a delay slot that may rewrite T), and the
			// tracked CR0 state may be newer than that capture.
			if (!compile_state.has_jcond && compile_state.t_in_cr0)
			{
				u32 want=(block->BlockType&1)^(compile_state.t_invert?1u:0u);

				ppc_label* jtrue=ppc_CreateLabel();
				ppc_bcx(want?BO_TRUE:BO_FALSE,compile_state.t_cr0_bit,0,0,0);

				DoStatic(block->NextBlock);
				jtrue->MarkLabel();
				DoStatic(block->BranchBlock);
				break;
			}
#endif
			//printf("COND %d\n",block->BlockType&1);
			//die("not supported");
			u32 reg;
			if (compile_state.has_jcond)
			{
				reg=ppc_djump;
			}
			else
			{
				// cmpi reads any reg: if T is pinned, compare it in place.
				reg=src_or_load(reg_sr_T,ppc_rarg0);
			}

			ppc_cmpi(ppc_cr0,reg,block->BlockType&1,0);

			ppc_label* jtrue=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);

			DoStatic(block->NextBlock);
			jtrue->MarkLabel();
			DoStatic(block->BranchBlock);
		}
		break;

	case BET_DynamicCall:
	case BET_DynamicJump:
	case BET_DynamicRet:
		//printf("Dynamic !\n");
		//mov reg,djump
		ppc_ori(ppc_rarg0,ppc_djump,0);  // mr rarg0, djump
		{
			// Inline bm_GetCode's fast path (bm_CheckCache) before falling back
			// to the loop_no_update machinery:
			//   idx    = (addr>>2) & (16384-1);
			//   cached = cache[idx];                  // DynarecBlock*
			//   if (cached->addr==addr && cached->code) { cached->lookups++;
			//       goto cached->code; }
			//   else goto loop_no_update;             // full bm_GetCode
			//
			// Byte offset into cache[] = idx*4 = ((addr>>2)&16383)<<2
			//                          = addr & 0xFFFC  (single rlwinm).
			// DynarecBlock layout (PPC32): code@0, addr@4, lookups@8.
			// addr stays in rarg0 for the miss path (bm_GetCode arg).
			ppc_rlwinmx(ppc_rarg1,ppc_rarg0,0,16,29,0);	// rarg1 = addr & 0xFFFC
			u32 lo=ppc_addr_high(ppc_rarg2,(void*)&cache[0]);
			ppc_addi(ppc_rarg2,ppc_rarg2,lo);		// rarg2 = &cache
			ppc_lwzx(ppc_rarg2,ppc_rarg2,ppc_rarg1);	// rarg2 = cache[idx]

			ppc_lwz(ppc_rarg3,ppc_rarg2,offsetof(DynarecBlock,addr));
			ppc_cmp(ppc_cr0,ppc_rarg3,ppc_rarg0,0);	// cached->addr == addr ?
			ppc_label* miss1=ppc_CreateLabel();
			ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne miss

			ppc_lwz(ppc_rarg3,ppc_rarg2,offsetof(DynarecBlock,code));
			ppc_cmpi(ppc_cr0,ppc_rarg3,0,0);		// code == 0 ? (empty_block)
			ppc_label* miss2=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq miss

#if BM_JIT_LOOKUP_STATS
			// hit: lookups++ (keeps bm_GetCode's cache-replacement policy fed)
			// NOTE: must NOT use r0 as the addi target/source -- `addi rD,r0,imm`
			// treats rA=r0 as literal 0 (it's how ppc_li is built), so
			// `addi r0,r0,1` would emit `r0 = 0 + 1 = 1`, never incrementing the
			// loaded value. Use rarg1 (dead after the cache[idx] load above).
			ppc_lwz(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));
			ppc_addi(ppc_rarg1,ppc_rarg1,1);
			ppc_stw(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));
#endif
			// (BM_JIT_LOOKUP_STATS==0: no counter upkeep on the hot dispatch
			// path; bm_GetCode uses MRU promotion instead — blockmanager.h.)

			ppc_mtctr(ppc_rarg3);
			ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);		// bctr -> cached code

			miss1->MarkLabel();
			miss2->MarkLabel();
		}
		//jmp no update
		ppc_jump(loop_no_update);
		break;

	case BET_StaticIntr:
	case BET_DynamicIntr:
		printf("BET: Interrupt !\n");
		{
			u32 reg;
			if (block->BlockType==BET_StaticIntr)
			{
				ppc_li(ppc_rarg0,block->BranchBlock);
				reg=ppc_rarg0;
			}
			else
			{
				reg=ppc_djump;
			}
			ppc_sh_store(reg,reg_nextpc);
			// UpdateINTC -> Do_Exception reads r[15] (sgr=r[15]) and swaps the
			// register bank (sr.RB=1; UpdateSR). Pinned GPRs must be coherent in
			// memory for the read, and re-read afterwards to pick up the swap.
			reg_flush_all();
			ppc_call(&UpdateINTC);
			reg_reload_all();

			ppc_sh_load(ppc_next_pc,reg_nextpc);
			ppc_jump(loop_no_update);
		}
		break;

	case BET_StaticCall:
	case BET_StaticJump:
		if (get_debug_loop() == 1)
		{
			printf("Static 0x%08X!\n", block->BranchBlock);
		}
		DoStatic(block->BranchBlock);
		break;

	default:
		printf("END TYPE: %d\n",block->BlockType);
		die("wtfh end type\n");
	}
}

//f14:f29
ppc_freg GetFloatReg(u32 reg)
{
	if (reg>=reg_fr_0 && reg<=reg_fr_15)
	{
		return (ppc_freg)((reg-reg_fr_0)+ppc_f14);
	}

	return ppc_finvalid;
}

//r14:r28 (shr11 missing)
ppc_ireg GetIntReg(u32 reg)
{
	if (reg>=reg_r0 && reg<=reg_r15 && reg!=reg_r11)
	{
		if (reg>=reg_r11) reg--;

		return (ppc_ireg)((reg-reg_r0 )+ppc_r14);
	}

	return ppc_rinvalid;
}
// ============================================================================
// Static GPR allocation
//
// A fixed subset of SH4 integer registers is pinned to PPC callee-saved GPRs
// (r14..r28) for the whole emulation session. The mapping is block-invariant
// (no per-block colouring) — GetIntReg() defines it. SH4 r0..r15 (except r11,
// which is skipped because the recompiler needs a volatile temp window) map to
// r14..r28.
//
// Memory image vs register:
//   * The pinned PPC regs are the AUTHORITATIVE copy of the SH4 GPRs for the
//     whole JIT run. They are loaded once in the mainloop prologue and stay
//     live across blocks; Sh4cntx.r[] may be stale at any point.
//   * Sh4cntx.r[] is made coherent on demand only around code paths that read
//     or write context GPRs directly, each self-bracketing flush -> call ->
//     reload.
//
// Why we don't blanket flush/reload around ReadMem/WriteMem:
//   r14..r28 are callee-saved under the PPC EABI, so an ordinary C call
//   preserves them, and ReadMem/WriteMem touch only guest memory, not SH4
//   context GPRs.
//
// The flush/reload points are:
//   * shop_ifb              — interpreter handler reads/writes arbitrary GPRs
//   * canonical fallback    — e.g. sync_sr -> UpdateSR (SR.RB bank swap)
//   * BET_*Intr block end   — UpdateINTC -> Do_Interrupt (reads r[15], bank swap)
//   * mainloop UpdateSystem — CONDITIONAL: the split UpdateSystem_no_event()
//                             runs the GPR-free peripheral cascade + pending
//                             check unflushed every timeslice; only when it
//                             reports a pending interrupt do we flush, call
//                             UpdateSystem_handle_event(), and reload.
//
// STATIC_GPR_ALLOC (defined near the top of this file) can be set to 0 to
// disable the whole scheme (all accesses go through memory, as before) —
// useful for A/B debugging.
// ============================================================================

void reg_flush_all()
{
#if STATIC_GPR_ALLOC
	for (u32 i=reg_r0;i<=reg_r15;i++)
	{
		ppc_ireg ri=GetIntReg(i);
		if (ri!=ppc_rinvalid)
			ppc_stw(ri,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}
void reg_reload_all()
{
#if STATIC_GPR_ALLOC
	for (u32 i=reg_r0;i<=reg_r15;i++)
	{
		ppc_ireg ri=GetIntReg(i);
		if (ri!=ppc_rinvalid)
			ppc_lwz(ri,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}

// FPU counterparts. Flush/reload points (all self-bracketing):
//   * mainloop prologue      — initial load (reload only)
//   * shop_ifb, 0xFxxx ops   — interpreter FPU handlers read/write fr[] memory
//   * canonical fallback     — e.g. fsca_impl writes through a CPT_ptr into fr[]
//   * shop_sync_fpscr        — UpdateFPSCR->ChangeFP swaps fr[]<->xf[] in
//                              MEMORY when FPSCR.FR changes (skipped when the
//                              decoder marks the op as FR-safe, e.g. fschg)
// The BET_*Intr / sync_sr paths do NOT need FPU brackets: interrupt entry and
// the SR.RB bank swap touch only integer state, never fr[]/xf[].
void fpu_flush_all()
{
#if STATIC_FPU_ALLOC
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg fi=GetFloatReg(i);
		if (fi!=ppc_finvalid)
			ppc_stfs(fi,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}
void fpu_reload_all()
{
#if STATIC_FPU_ALLOC
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg fi=GetFloatReg(i);
		if (fi!=ppc_finvalid)
			ppc_lfs(fi,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}
void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);

// Native call targets for fsqrt/fsrra. Broadway has no hardware fsqrt and its
// frsqrte estimate (~5-bit) is too imprecise (it distorted the BIOS swirl), so
// these route to the accurate libm path via a single f32->f32 call. Matches the
// canonical UN_OP_F(sqrtf) / UN_OP_F(1.0f/sqrtf) semantics exactly.
static f32 rec_fsqrt(f32 x)  { return sqrtf(x); }
static f32 rec_fsrra(f32 x)  { return 1.0f / sqrtf(x); }

// =====================
// OPERATION COMPILATION
// =====================

// ── Cold-out-of-line slow paths for memory ops ──────────────────────────────
// The mem fast path is the common case (direct RAM/VRAM access). To keep it a
// straight fall-through with a SINGLE not-taken branch, we emit:
//
//     <addr/ptr setup>
//     cmpi ptr,0
//     beq  cold        ; forward, PREDICTED NOT-TAKEN -> fast path falls through
//     <fast access>
//   done:              ; (next op continues here, no branch on the fast path)
//     ...rest of block...
//   ngen_End
//   cold:              ; emitted out-of-line, AFTER the block tail (never
//     <slow MMIO call>   ;  fall-through reached; only via the beq)
//     b done           ; backward unconditional, jumps into the normal stream
//
// Each fast path registers a ColdFrag describing its slow body; FlushCold()
// emits them after ngen_End and back-patches the forward beq (16-bit) to the
// cold entry. The `b done` is a backward branch with a known target, emitted
// directly via ppc_bx(void*,LK) (no patch needed).
struct ColdFrag
{
	ppc_label* beq;     // the forward beq site to patch to the cold entry
	void*      done;    // join point in the main stream (backward target)
	u8         kind;    // 0=read scalar, 1=read pair(64), 2=write scalar, 3=write pair,
	                    // 5=read f32 -> pinned FPR, 6=write f32 from pinned FPR
	u8         sz;      // 1/2/4 (scalar)
	u8         rdreg;   // read: destination reg (rrv0 or pinned GPR; kind 5: pinned FPR)
	u8         datareg; // write: data reg (rarg1 or pinned GPR; kind 6: pinned FPR)
	u8         areg;    // address source reg (rarg0 or pinned)
	void*      fuct;    // slow C function (ReadMem*/WriteMem*); 0 -> default by sz
};
// Sized so it cannot overflow within one block: the decoder caps a block at
// ~224 guest ops (SH4_TIMESLICE/2 cycles), and each mem op registers at most
// one frag, so <=~224 << 1024. (The emit-space bail in ngen_Compile is sized
// for the worst case of ~224 interpreter-fallback ops with full GPR+FPU
// flush/reload brackets, ~80 insns each.)
static ColdFrag s_cold[1024];
static u32      s_cold_n;

static void ColdReset() { s_cold_n = 0; }

static void FlushCold()
{
	for (u32 i = 0; i < s_cold_n; i++)
	{
		ColdFrag& c = s_cold[i];
		// The forward beq uses a 16-bit (B-form) displacement. Cold fragments sit
		// after the block tail; for a normal (<32KB) block this is in range, but
		// guard against silent truncation -> wrong target on a pathologically
		// large block. (ngen_Compile bails blocks needing >16KB, so this holds.)
		snat beq_disp = (u8*)emit_GetCCPtr() - (u8*)c.beq;
		verify(beq_disp >= 0 && beq_disp < 32768);
		c.beq->MarkLabel();              // patch the forward beq -> here (cold entry)

		if (c.kind==0)                   // --- read scalar: ReadMem(addr)->rrv0 ---
		{
			if (c.areg!=ppc_rarg0) ppc_ori(ppc_rarg0,c.areg,0);   // mr rarg0,areg
			void* f=c.fuct;
			if (!f) f=(c.sz==1)?(void*)ReadMem8:(c.sz==2)?(void*)ReadMem16:(void*)ReadMem32;
			ppc_call(f);
			if (c.sz==1)      ppc_extsbx(c.rdreg,ppc_rrv0,0);
			else if (c.sz==2) ppc_extshx(c.rdreg,ppc_rrv0,0);
			else if (c.rdreg!=ppc_rrv0) ppc_ori(c.rdreg,ppc_rrv0,0);
		}
		else if (c.kind==1)              // --- read pair (64): ReadMem64 -> rrv0:rrv1 ---
		{
			void* f=c.fuct? c.fuct : (void*)ReadMem64;
			ppc_call(f);
		}
		else if (c.kind==2)              // --- write scalar: WriteMem(addr,data) ---
		{
			if (c.areg!=ppc_rarg0)    ppc_ori(ppc_rarg0,c.areg,0);
			if (c.datareg!=ppc_rarg1) ppc_ori(ppc_rarg1,c.datareg,0);
			if (c.sz==1)      { ppc_andi(ppc_rarg1,ppc_rarg1,0xFF);   ppc_call(&WriteMem8); }
			else if (c.sz==2) { ppc_andi(ppc_rarg1,ppc_rarg1,0xFFFF); ppc_call(&WriteMem16); }
			else                ppc_call(&WriteMem32);
		}
		else if (c.kind==5)              // --- read f32 -> pinned FPR (fmov.s load) ---
		{
			// ReadMem32 returns in rrv0; no direct GPR->FPR move on Broadway,
			// so bounce through the context jit_scratch slot.
			if (c.areg!=ppc_rarg0) ppc_ori(ppc_rarg0,c.areg,0);
			void* f=c.fuct? c.fuct : (void*)ReadMem32;
			ppc_call(f);
			{
				u32 so=(u32)offsetof(Sh4Context,jit_scratch);
				ppc_stw(ppc_rrv0,ppc_contex,so);
				ppc_lfs(c.rdreg,ppc_contex,so);
			}
		}
		else if (c.kind==6)              // --- write f32 from pinned FPR (fmov.s store) ---
		{
			// WriteMem32 wants the raw bits in rarg1: stfs (bit-exact) to
			// jit_scratch, reload as integer.
			if (c.areg!=ppc_rarg0) ppc_ori(ppc_rarg0,c.areg,0);
			{
				u32 so=(u32)offsetof(Sh4Context,jit_scratch);
				ppc_stfs(c.datareg,ppc_contex,so);
				ppc_lwz(ppc_rarg1,ppc_contex,so);
			}
			ppc_call(&WriteMem32);
		}
		else                             // --- write pair (64): WriteMem64 ---
		{
			ppc_call(&WriteMem64);
		}

		ppc_bx(c.done,0);                // backward unconditional b -> done (join)
	}
	s_cold_n = 0;
}

// ngen_CompileBlock: Main Block Compilation Loop
DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks)
{
	// Bail out early if there isn't enough space for a worst-case block.
	// Worst case is ~224 interpreter-fallback ops each bracketed by the full
	// GPR (15+15) + FPU (16+16) flush/reload — ~80 insns per op, ~72KB. The
	// old 16KB bound predated the FPU brackets.
	if (emit_FreeSpace() < 81920) // 80*1024
		return 0;
	
	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();

	ColdReset();          // mem-op cold (slow) fragments deferred to block end
	ngen_Begin(block,force_checks);

	for (size_t i = 0; i < block->oplist.size(); i++)
	{
		shil_opcode* op=&block->oplist[i];

#if FUSE_CMP_BRANCH
		// Kill the tracked T<->CR0 mapping across any op that might clobber
		// CR0 or rewrite T; the set* family re-records inside its own case.
		if (compile_state.t_in_cr0 && !fusion_op_safe(op))
			compile_state.t_in_cr0=false;
#endif

		switch(op->op)
		{

		case shop_readm:
			{
				void* fuct=0;
				bool isram=false;
				verify(op->rs1.is_imm() || op->rs1.is_r32i());

				// Land the loaded value DIRECTLY in rd's pinned PPC register when
				// possible, so the trailing ppc_sh_store(rrv0,rd) becomes a
				// store-to-self (elided) instead of an `ori rN,r3,0` move. The slow
				// MMIO call returns in rrv0, so that path copies rrv0->rdreg once.
				// 64-bit pair loads keep the rrv0:rrv1 ABI (handled below).
				u32 rdreg = ppc_rrv0;
#if STATIC_GPR_ALLOC
				if (op->flags!=8)
				{
					ppc_ireg rp=GetIntReg(op->rd._reg);
					if (rp!=ppc_rinvalid)
						rdreg=(u32)rp;
				}
#endif
				// Float destination (fmov.s @Rm,FRn / float literal load): when
				// rd is a pinned fr, land the value STRAIGHT in the FPR with
				// lfs/lfsx on the fast path (no GPR->context->FPR bounce); the
				// MMIO cold path (kind 5) bounces via jit_scratch instead.
				ppc_freg frd = ppc_finvalid;
				if (op->flags==4 && op->rd.is_r32f())
					frd = fpin(op->rd._reg);
				// Address source for the runtime fast path (set in the rs1-reg
				// branch below). Defaults to rarg0 (the const/imm + pair paths).
				u32 areg = ppc_rarg0;

				if (op->rs1.is_imm())
				{
					void* ptr=_vmem_read_const(op->rs1._imm,isram,op->flags);
					if (isram)
					{
						if (op->flags==1)
						{
							ppc_lbz(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (op->flags==2)
							ppc_lha(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
						else if (op->flags==4)
						{
							if (frd!=ppc_finvalid)
								ppc_lfs(frd,ppc_r4,ppc_addr_high(ppc_r4,ptr));	// literal -> pinned FPR
							else
								ppc_lwz(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
						}
						else
						{
							die("Invalid mem read size");
						}
					}
					else
					{
						ppc_li(ppc_rarg0,op->rs1._imm);
						fuct=ptr;
					}
				}
				else
				{
					// `areg` is the address SOURCE for the fast-path index/mask ops.
					// For a plain @Rn (rs3 null) with rs1 pinned, source the address
					// straight from its pinned reg -- no `ori rarg0,pinned,0` mov.
					// The fast path only READS areg (writing scratch), and the slow
					// path materializes rarg0 from it. Any offset (imm/reg) needs a
					// mutated address, so fall back to pre-loading rarg0 as before.
					areg = ppc_rarg0;
					if (op->rs3.is_imm())
					{
						verify(op->rs3.is_imm_s16());
						// addr = rs1 + imm -> addi can read pinned rs1 in place and
						// write scratch rarg0, skipping the `mr rarg0,rs1` load.
						u32 a1=src_or_load(op->rs1,ppc_rarg0);
						ppc_addi(ppc_rarg0,a1,op->rs3._imm);
					}
					else if (op->rs3.is_r32i())
					{
						// addr = rs1 + rs3 -> add reads both sources in place.
						u32 a1=src_or_load(op->rs1,ppc_rarg0);
						u32 a3=src_or_load(op->rs3,ppc_rarg1);
						ppc_addx(ppc_rarg0,a1,a3,0,0);
					}
					else if (op->rs3.is_null())
					{
#if STATIC_GPR_ALLOC
						// Only the scalar fast path threads areg; the 64-bit pair
						// path still reads rarg0, so keep pre-loading it there.
						ppc_ireg ap=(op->flags!=8 && op->rs1.is_reg())
						              ?GetIntReg(op->rs1._reg):ppc_rinvalid;
						if (ap!=ppc_rinvalid)
							areg=(u32)ap;		// address lives in pinned reg
						else
#endif
							ppc_sh_load(ppc_rarg0,op->rs1);
					}
					else
					{
						die("invalid rs3");
					}
				}

				if (!isram)
				{
					// Inline the _vmem_readt fast path (direct RAM/VRAM) for
					// runtime addresses. The MMIO path falls back to the C
					// dispatcher.
					//
					//   iirf = _vmem_MemInfo_ptr[addr>>24];
					//   ptr  = iirf & ~0x1F;
					//   if (ptr) { sh=iirf&0x1F; a=(addr<<sh)>>sh;
					//              if (sz<4) a^=4-sz; rv=*(T*)(ptr+a); }
					//   else  rv = ReadMem<sz>(addr);   // slow
					//
					// addr is in rarg0 on entry. Scratch: r0, rarg1, rarg2.
					if (op->flags==8)
					{
						// 64-bit pair load (fmov.d / sz=1 pair fmov). Replicates
						// the BE *(u64*)p load of _vmem_readt<u64>: the u64 is
						// returned in r3:r4 = high:low, so word[addr] -> rrv0
						// (-> rd) and word[addr+4] -> rrv1 (-> rd+1), matching
						// the ReadMem64 slow path register-for-register.
						ppc_rlwinmx(ppc_r0,ppc_rarg0,10,22,29,0);	// (addr>>24)*4
						u32 lo=ppc_addr_high(ppc_rarg1,(void*)&_vmem_MemInfo_ptr[0]);
						if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
						ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
						ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);	// ptr (≠r0)
						ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (MMIO)

						// blt -> cold (forward, not-taken: fast path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

						// --- fast direct path (fall-through) ---
						// shift count = iirf directly (ptr >=0x40-aligned -> iirf's
						// low 6 bits, which slw/srw use, equal the 5-bit shift field).
						ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);
						ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);	// mirror mask
						ppc_lwzx(ppc_rarg3,ppc_rarg2,ppc_rarg0);	// word[addr]
						ppc_addi(ppc_rarg0,ppc_rarg0,4);
						ppc_lwzx(ppc_rrv1,ppc_rarg2,ppc_rarg0);	// word[addr+4] -> r4
						ppc_ori(ppc_rrv0,ppc_rarg3,0);			// r3 = word[addr]

						ColdFrag& c=s_cold[s_cold_n++];
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=1; c.fuct=fuct;
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);

						// areg = address source (rarg0, or rs1's pinned reg for a
						// plain @Rn). The index + mirror-mask READ areg and write the
						// rarg0/r0 scratch, so a pinned areg is never clobbered.
						// r0 = (addr>>24)*4   (byte index into the void* table)
						ppc_rlwinmx(ppc_r0,areg,10,22,29,0);
						// rarg1 = &_vmem_MemInfo_ptr
						u32 lo=ppc_addr_high(ppc_rarg1,(void*)&_vmem_MemInfo_ptr[0]);
						if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
						ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
						// rarg2 = ptr = iirf & ~0x1F  (clear low 5 bits: ME=26)
						// NOTE: ptr goes in rarg2 (NOT r0) — load-indexed treats a
						// base of r0 as literal zero, which would drop the pointer.
						ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);
						// MMIO test: ptr==0 <=> iirf<0x20 (the only set bits would be
						// the low-5 shift field). Compare iirf directly so this doesn't
						// depend on the ptr mask above.
						ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (UIMM=0x20,L=0)

						// blt -> cold (forward, not-taken: fast path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

						// --- fast direct path (fall-through) ---
						// eff = (addr<<sh)>>sh. The shift count is iirf's low bits:
						// ptr is >=0x40-aligned so iirf[26:31] (the 6 bits slw/srw use)
						// equal the 5-bit shift field exactly -> feed iirf directly,
						// no andi 0x1F needed.
						ppc_slwx(ppc_rarg0,areg,ppc_rarg1,0);		// reads areg -> rarg0
						ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);
						// big-endian sub-word swizzle
						if (sz<4)
							ppc_xori(ppc_rarg0,ppc_rarg0,4-sz);
						// load rdreg = *(T*)(ptr + addr)  (ptr in rarg2, addr in rarg0)
						if (sz==1)
						{
							ppc_lbzx(rdreg,ppc_rarg2,ppc_rarg0);
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
						{
							ppc_lhzx(rdreg,ppc_rarg2,ppc_rarg0);
							ppc_extshx(rdreg,rdreg,0);
						}
						else if (frd!=ppc_finvalid)
							ppc_lfsx(frd,ppc_rarg2,ppc_rarg0);	// straight into pinned FPR
						else
							ppc_lwzx(rdreg,ppc_rarg2,ppc_rarg0);

						// done = join point; slow path (registered below) bounces here.
						ColdFrag& c=s_cold[s_cold_n++];
						c.beq=cold; c.done=emit_GetCCPtr();
						if (frd!=ppc_finvalid)
						{
							c.kind=5; c.sz=4; c.rdreg=(u8)frd;
						}
						else
						{
							c.kind=0; c.sz=(u8)sz; c.rdreg=(u8)rdreg;
						}
						c.areg=(u8)areg; c.fuct=fuct;
					}
				}

				// Float-direct loads already put the value in the pinned FPR;
				// everything else lands in rdreg and is stored to rd here
				// (store-to-self elided for a pinned integer rd).
				if (frd==ppc_finvalid)
					ppc_sh_store(rdreg,op->rd);

				if (op->flags==8)
				{
					ppc_sh_store(ppc_rrv1,op->rd._reg+1);
#if STATIC_FPU_ALLOC
					// Pair destination may be a pinned fr pair (fmov.d @Rm,DRn):
					// the words above went to the CONTEXT via integer stores, so
					// refresh the pinned FPR copies from it.
					{
						ppc_freg p0=fpin(op->rd._reg), p1=fpin(op->rd._reg+1);
						if (p0!=ppc_finvalid) ppc_lfs(p0,ppc_contex,Sh4cntx.offset(op->rd._reg));
						if (p1!=ppc_finvalid) ppc_lfs(p1,ppc_contex,Sh4cntx.offset(op->rd._reg+1));
					}
#endif
				}
			}
			break;

		case shop_writem:
			{
				// Compute the FULL effective address first, THEN load the data.
				// (The old order loaded data first; the rs3 register-index path
				// uses rarg3 as scratch, which CLOBBERED the 64-bit data low
				// word for indexed pair stores like "fmov.d FRm,@(R0,Rn)".)
				//
				// `areg` is the address SOURCE for the scalar fast-path index/mask
				// ops. For a plain @Rn (rs3 null) with rs1 pinned we source it
				// straight from the pinned reg (no `ori rarg0,pinned,0` mov); the
				// fast path only reads areg, the slow path materializes rarg0.
				u32 areg = ppc_rarg0;
				if (op->rs3.is_imm())
				{
					verify(op->rs3.is_imm_s16());
					// addr = rs1 + imm: addi reads pinned rs1 in place -> rarg0.
					u32 a1=src_or_load(op->rs1,ppc_rarg0);
					ppc_addi(ppc_rarg0,a1,op->rs3._imm);
				}
				else if (op->rs3.is_r32i())
				{
					// addr = rs1 + rs3: add reads both sources in place -> rarg0.
					u32 a1=src_or_load(op->rs1,ppc_rarg0);
					u32 a3=src_or_load(op->rs3,ppc_rarg3);
					ppc_addx(ppc_rarg0,a1,a3,0,0);
				}
				else if (op->rs3.is_null())
				{
#if STATIC_GPR_ALLOC
					// Scalar only; the 64-bit pair path still reads rarg0.
					ppc_ireg ap=(op->flags!=8 && op->rs1.is_reg())
					              ?GetIntReg(op->rs1._reg):ppc_rinvalid;
					if (ap!=ppc_rinvalid)
						areg=(u32)ap;
					else
#endif
						ppc_sh_load(ppc_rarg0,op->rs1);
				}
				else
				{
					printf("rs3: %08X\n",op->rs3.type);
					die("invalid rs3");
				}

				// Scalar data operand: if rs2 is pinned, the fast path can store
				// DIRECTLY from its pinned PPC reg (the fast path only reads the
				// data reg, never clobbers it), so we skip the pre-split
				// ppc_sh_load(rarg1,rs2) mov and load rarg1 only on the slow MMIO
				// path (where the WriteMem ABI requires data in rarg1). The 64-bit
				// pair keeps the rarg2:rarg3 ABI as before.
				// Pinned FLOAT data (fmov.s FRm,@Rn): the fast path stores the
				// FPR straight to guest memory with stfsx (bit-exact); the MMIO
				// cold path (kind 6) bounces via jit_scratch.
				u32 datareg = ppc_rarg1;
				ppc_freg fdata = ppc_finvalid;
				if (op->flags==8)
				{
#if STATIC_FPU_ALLOC
					// Pinned fr words are authoritative: refresh the context
					// copies the integer pair loads below read from.
					{
						ppc_freg p0=fpin(op->rs2._reg), p1=fpin(op->rs2._reg+1);
						if (p0!=ppc_finvalid) ppc_stfs(p0,ppc_contex,Sh4cntx.offset(op->rs2._reg));
						if (p1!=ppc_finvalid) ppc_stfs(p1,ppc_contex,Sh4cntx.offset(op->rs2._reg+1));
					}
#endif
					ppc_sh_load(ppc_rarg2,op->rs2);
					ppc_sh_load(ppc_rarg3,op->rs2._reg+1);
				}
				else
				{
					if (op->flags==4 && op->rs2.is_r32f())
						fdata=fpin(op->rs2._reg);
					if (fdata==ppc_finvalid)
					{
#if STATIC_GPR_ALLOC
						ppc_ireg dp=op->rs2.is_reg()?GetIntReg(op->rs2._reg):ppc_rinvalid;
						if (dp!=ppc_rinvalid)
							datareg=(u32)dp;		// store straight from pinned reg
						else
#endif
							ppc_sh_load(ppc_rarg1,op->rs2);	// scratch: load now
					}
				}

				// Inline the _vmem_writet fast path (direct RAM/VRAM) for runtime
				// addresses. MMIO falls back to the C call.
				//
				//   iirf = _vmem_MemInfo_ptr[addr>>24];
				//   ptr  = iirf & ~0x1F;
				//   if (ptr) { sh=iirf&0x1F; a=(addr<<sh)>>sh;
				//              if (sz<4) a^=4-sz; *(T*)(ptr+a)=data; }
				//   else  WriteMem<sz>(addr,data);   // slow
				//
				// On entry rarg0=addr, rarg1=data (or rarg2:rarg3 = high:low for
				// 64-bit). addr/data MUST survive to the slow call.
				if (op->flags==8)
				{
					// 64-bit pair store (fmov.d / sz=1 pair fmov). Data is in
					// rarg2:rarg3 (r5:r6) = high:low — exactly the EABI registers
					// WriteMem64(u32,u64) wants, so the slow path needs no moves.
					// Direct path replicates the BE *(u64*)p store of
					// _vmem_writet<u64>: word[addr]=high, word[addr+4]=low.
					// Lookup may only use r0/rarg1 as scratch.
					ppc_rlwinmx(ppc_r0,ppc_rarg0,10,22,29,0);	// (addr>>24)*4
					u32 lo=ppc_addr_high(ppc_rarg1,(void*)&_vmem_MemInfo_ptr[0]);
					if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
					ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
					// Extract shift first. NOTE: ppc_andi is the RECORD form (andi.)
					// and clobbers CR0, so the MMIO compare MUST come AFTER it (and
					// before the rlwinm overwrites iirf in rarg1).
					ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// r0 = shift (clobbers CR0)
					ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (sets CR0 last)
					ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,0,26,0);	// rarg1 = ptr (≠r0; Rc=0)

					// blt -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

					// --- fast direct path (fall-through; addr masked here only) ---
					ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
					ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);	// mirror mask
					ppc_stwx(ppc_rarg2,ppc_rarg1,ppc_rarg0);	// word[addr]   = high
					ppc_addi(ppc_rarg0,ppc_rarg0,4);
					ppc_stwx(ppc_rarg3,ppc_rarg1,ppc_rarg0);	// word[addr+4] = low

					ColdFrag& c=s_cold[s_cold_n++];
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=3;
				}
				else
				{
					const u32 sz=op->flags;
					verify(sz==1||sz==2||sz==4);

					// r0 = (addr>>24)*4   (byte index into the void* table).
					// areg is the address source (rarg0, or rs1's pinned reg for a
					// plain @Rn); the index + mirror-mask READ it, writing scratch.
					ppc_rlwinmx(ppc_r0,areg,10,22,29,0);
					// rarg2 = &_vmem_MemInfo_ptr
					u32 lo=ppc_addr_high(ppc_rarg2,(void*)&_vmem_MemInfo_ptr[0]);
					if (lo) ppc_addi(ppc_rarg2,ppc_rarg2,lo);
					ppc_lwzx(ppc_rarg2,ppc_rarg2,ppc_r0);		// rarg2 = iirf
					// rarg3 = ptr = iirf & ~0x1F  (ptr NOT in r0: store-indexed
					// treats base r0 as literal zero, dropping the pointer)
					ppc_rlwinmx(ppc_rarg3,ppc_rarg2,0,0,26,0);
					// MMIO test on iirf directly (iirf<0x20 <=> ptr==0); keeps iirf
					// in rarg2 alive as the shift count below.
					ppc_cmpli(ppc_cr0,ppc_rarg2,0x20,0);		// iirf < 0x20 ?

					// blt -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

					// --- fast direct path (fall-through) ---
					// eff = (addr<<sh)>>sh into r0 (free after the index above);
					// shift count is iirf in rarg2 directly (ptr >=0x40-aligned ->
					// iirf's low 6 bits == the 5-bit field, no andi needed).
					ppc_slwx(ppc_r0,areg,ppc_rarg2,0);		// r0 = addr<<sh (reads areg)
					ppc_srwx(ppc_r0,ppc_r0,ppc_rarg2,0);		// r0 = (addr<<sh)>>sh
					if (sz<4)
						ppc_xori(ppc_r0,ppc_r0,4-sz);		// big-endian sub-word swizzle
					// *(T*)(ptr+eff) = data  (ptr=rarg3, eff=r0, data=datareg
					// or the pinned FPR fdata). datareg/fdata is read-only here.
					if (sz==1)
						ppc_stbx(datareg,ppc_rarg3,ppc_r0);
					else if (sz==2)
						ppc_sthx(datareg,ppc_rarg3,ppc_r0);
					else if (fdata!=ppc_finvalid)
						ppc_stfsx(fdata,ppc_rarg3,ppc_r0);	// FPR -> guest mem, bit-exact
					else
						ppc_stwx(datareg,ppc_rarg3,ppc_r0);

					ColdFrag& c=s_cold[s_cold_n++];
					c.beq=cold; c.done=emit_GetCCPtr();
					if (fdata!=ppc_finvalid)
					{
						c.kind=6; c.sz=4; c.datareg=(u8)fdata;
					}
					else
					{
						c.kind=2; c.sz=(u8)sz; c.datareg=(u8)datareg;
					}
					c.areg=(u8)areg;
				}
			}
			break;

		case shop_ifb:
			{
				// FPU opcodes (0xFxxx: PR=1 doubles, etc.) executed in the
				// interpreter read/write fr[] in MEMORY, so the pinned FPRs
				// must be coherent across the call. Integer fallbacks (trapa,
				// div1, mac.*, ...) never touch fr[] — skip the FPU bracket.
				// (FPSCR writers — lds/ldc FPSCR — are all decoded natively
				// with sync_fpscr, never via ifb.)
				bool fpu_op=((op->rs3._imm>>12)==0xF);

				reg_flush_all();
				if (fpu_op) fpu_flush_all();
				if (op->rs1._imm)
				{
					ppc_li(ppc_rarg0,op->rs2._imm);
					ppc_sh_store(ppc_rarg0,reg_nextpc);
				}
				ppc_li(ppc_rarg0,op->rs3._imm);
				ppc_call(OpDesc[op->rs3._imm]->oph);
				if (fpu_op) fpu_reload_all();
				reg_reload_all();
			}
			break;
			
		case shop_jdyn:
			{
				if (op->rs2.is_imm())
				{
					// djump = rs1 + imm: read pinned rs1 in place into the addi/add,
					// so the `mr djump,rs1` load is folded into the arithmetic.
					u32 a1=src_or_load(op->rs1,ppc_djump);
					if (op->rs2.is_imm_s16())
					{
						ppc_addi(ppc_djump,a1,op->rs2._imm);
					}
					else
					{
						ppc_li(ppc_rarg0,op->rs2._imm);
						ppc_addx(ppc_djump,a1,ppc_rarg0,0,0);
					}
				}
				else
				{
					// djump = rs1 (no offset): a plain move is unavoidable.
					ppc_sh_load(ppc_djump,op->rs1);
				}
			}
			break;
			
		case shop_jcond:
			{
				compile_state.has_jcond=true;
				ppc_sh_load(ppc_djump,op->rs1);
			}
			break;
			
		case shop_mov64:
			{
				verify(op->rd.is_r64());
				verify(op->rs1.is_r64());

				// Per-word move honoring pinned fr words. F64 params are
				// fr/xf pairs (base index in _reg): dr<->dr is two fmr,
				// dr<->xd mixes one pinned side with one memory side, and
				// xd<->xd keeps the old integer lwz/stw path. stfs/lfs
				// round-trip the exact bit pattern, so raw integer data moved
				// through fmov.d (memcpy idiom) stays intact.
				for (u32 w=0;w<2;w++)
				{
					u32 sreg=op->rs1._reg+w, dreg=op->rd._reg+w;
					ppc_freg fs=fpin(sreg), fd=fpin(dreg);
					if (fs!=ppc_finvalid && fd!=ppc_finvalid)
					{
						if (fs!=fd)
							ppc_fmrx(fd,fs,0);
					}
					else if (fs!=ppc_finvalid)
						ppc_stfs(fs,ppc_contex,Sh4cntx.offset(dreg));
					else if (fd!=ppc_finvalid)
						ppc_lfs(fd,ppc_contex,Sh4cntx.offset(sreg));
					else
					{
						ppc_lwz(ppc_rarg0,ppc_contex,Sh4cntx.offset(sreg));
						ppc_stw(ppc_rarg0,ppc_contex,Sh4cntx.offset(dreg));
					}
				}
			}
			break;

		case shop_mov32:
			{
				verify(op->rd.is_r32());

				// --- Float-typed operands under STATIC_FPU_ALLOC -------------
				// A pinned fr on either side makes the old integer lwz/stw of
				// the context slot stale/insufficient; route through the FPR:
				//   fmov FRm,FRn          -> fmr           (both pinned)
				//   flds FRm,FPUL         -> stfs pin,ctx  (src pinned)
				//   fsts FPUL,FRn         -> lfs pin,ctx   (dst pinned)
				//   fldi0/1 FRn           -> li+stw+lfs via jit_scratch
				// stfs/lfs round-trip bit patterns exactly, so this is safe
				// for raw integer data moved through float regs.
				{
					ppc_freg fs = op->rs1.is_r32f() ? fpin(op->rs1._reg) : ppc_finvalid;
					ppc_freg fd = op->rd.is_r32f()  ? fpin(op->rd._reg)  : ppc_finvalid;

					if (fs!=ppc_finvalid && fd!=ppc_finvalid)
					{
						if (fs!=fd)
							ppc_fmrx(fd,fs,0);
						break;
					}
					if (fs!=ppc_finvalid)
					{
						ppc_ireg di = op->rd.is_r32i() ? GetIntReg(op->rd._reg) : ppc_rinvalid;
						if (di!=ppc_rinvalid)
						{
							// pinned fr -> pinned GPR: no direct FPR->GPR move
							// on this CPU; bounce through jit_scratch.
							u32 so=(u32)offsetof(Sh4Context,jit_scratch);
							ppc_stfs(fs,ppc_contex,so);
							ppc_lwz(di,ppc_contex,so);
						}
						else
							ppc_stfs(fs,ppc_contex,Sh4cntx.offset(op->rd._reg));
						break;
					}
					if (fd!=ppc_finvalid)
					{
						if (op->rs1.is_imm())
						{
							u32 so=(u32)offsetof(Sh4Context,jit_scratch);
							ppc_li(ppc_rarg0,op->rs1._imm);
							ppc_stw(ppc_rarg0,ppc_contex,so);
							ppc_lfs(fd,ppc_contex,so);
						}
						else
						{
							ppc_ireg si = op->rs1.is_r32i() ? GetIntReg(op->rs1._reg) : ppc_rinvalid;
							if (si!=ppc_rinvalid)
							{
								// pinned GPR -> pinned fr: bounce via scratch.
								u32 so=(u32)offsetof(Sh4Context,jit_scratch);
								ppc_stw(si,ppc_contex,so);
								ppc_lfs(fd,ppc_contex,so);
							}
							else
								// context-resident source (fpul, xf, ...)
								ppc_lfs(fd,ppc_contex,Sh4cntx.offset(op->rs1._reg));
						}
						break;
					}
				}

				// --- Integer / non-pinned-float path (unchanged) --------------
				// Resolve dest: a pinned (integer) rd is written in place; a
				// non-pinned or float-typed rd uses rarg0 + a context store.
				ppc_ireg rdr = op->rd.is_r32i() ? GetIntReg(op->rd._reg) : ppc_rinvalid;
				u32 dst = (rdr!=ppc_rinvalid) ? (u32)rdr : ppc_rarg0;

				if (op->rs1.is_imm())
				{
					ppc_li(dst,op->rs1._imm);
				}
				else if (op->rs1.is_r32())
				{
					// reg -> reg. If rs1 is pinned, move/keep it directly.
					ppc_ireg rsr = op->rs1.is_r32i() ? GetIntReg(op->rs1._reg) : ppc_rinvalid;
					if (rsr!=ppc_rinvalid)
					{
						if ((u32)rsr!=dst)
							ppc_ori(dst,rsr,0);	// mr dst, rsr  (skipped if same reg)
					}
					else
					{
						ppc_sh_load(dst,op->rs1);
					}
				}
				else
				{
					die("Invalid mov32 size");
				}

				// Store back only when we used the scratch reg (non-pinned rd).
				if (dst==ppc_rarg0 && rdr==ppc_rinvalid)
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		// Single-instruction binops — read/write pinned regs directly (binop3).
		// Single-instruction binops. When rs2 is a small immediate we fold it
		// straight into the PPC immediate-form instruction, skipping the
		// ppc_li(rarg1,imm) materialisation. addi/subi use a SIGNED 16-bit
		// field; andi./ori/xori use UNSIGNED 16-bit.
		case shop_add:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_s16())
				ppc_addi(bop_d,bop_a,op->rs2._imm);		// d = a + imm
			else
				{ bop_resolve_b(op); ppc_addx(bop_d,bop_a,bop_b,0,0); }
			binop3_end(op);
			break;
		case shop_sub:
			// SH4 sub = rs1 - rs2; fold imm as a + (-imm) when -imm fits s16.
			bop_resolve_a_d(op);
			if (op->rs2.is_imm() && is_s16(0u-op->rs2._imm))
				ppc_addi(bop_d,bop_a,0u-op->rs2._imm);		// d = a - imm
			else
				{ bop_resolve_b(op); ppc_subfx(bop_d,bop_b,bop_a,0,0); }
			binop3_end(op);
			break;

		case shop_or:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_ori(bop_d,bop_a,op->rs2._imm);
			else
				{ bop_resolve_b(op); ppc_orx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_and:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_andi(bop_d,bop_a,op->rs2._imm);		// andi. (Rc=1, harmless)
			else
				{ bop_resolve_b(op); ppc_andx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_xor:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_xori(bop_d,bop_a,op->rs2._imm);
			else
				{ bop_resolve_b(op); ppc_xorx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;

		// Shifts: fold a constant shift amount (0..31) into rlwinm/srawi.
		case shop_shl:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
			{
				u32 s=op->rs2._imm&0x1F;
				ppc_rlwinmx(bop_d,bop_a,s,0,31-s,0);		// slwi: rotl s, mask [0..31-s]
			}
			else
				{ bop_resolve_b(op); ppc_slwx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_shr:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
			{
				u32 s=op->rs2._imm&0x1F;
				ppc_rlwinmx(bop_d,bop_a,(32-s)&31,s,31,0);	// srwi: rotl 32-s, mask [s..31]
			}
			else
				{ bop_resolve_b(op); ppc_srwx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_sar:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
				ppc_srawix(bop_d,bop_a,op->rs2._imm&0x1F,0);	// srawi
			else
				{ bop_resolve_b(op); ppc_srawx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_mul_i32:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_s16())
				ppc_mulli(bop_d,bop_a,op->rs2._imm);		// d = a * imm
			else
				{ bop_resolve_b(op); ppc_mullwx(bop_d,bop_a,bop_b,0,0); }
			binop3_end(op);
			break;


		// Scalar FP arithmetic: with STATIC_FPU_ALLOC the common all-pinned
		// case is a single instruction reading/writing f14..f29 in place —
		// no lfs/stfs, no load-hit-store on the context slots.
		case shop_fadd: fbinop3_start(op); ppc_faddsx(fop_d,fop_a,fop_b,0); fbinop3_end(op); break;
		case shop_fsub: fbinop3_start(op); ppc_fsubsx(fop_d,fop_a,fop_b,0); fbinop3_end(op); break;
		case shop_fmul: fbinop3_start(op); ppc_fmulsx(fop_d,fop_a,fop_b,0); fbinop3_end(op); break;
		case shop_fdiv: fbinop3_start(op); ppc_fdivsx(fop_d,fop_a,fop_b,0); fbinop3_end(op); break;

		// --- Additional native integer binops ---------------------------------
		case shop_mul_u16:
			// rd = (u16)r1 * (u16)r2  (low 32 bits). Zero-extend both, mullw.
			binop_start(op);
			ppc_rlwinmx(ppc_rarg0,ppc_rarg0,0,16,31,0);	// clrlwi rarg0,rarg0,16
			ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,16,31,0);	// clrlwi rarg1,rarg1,16
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
			binop_end(op);
			break;
		case shop_mul_s16:
			// rd = (s16)r1 * (s16)r2  (low 32 bits). Sign-extend both, mullw.
			binop_start(op);
			ppc_extshx(ppc_rarg0,ppc_rarg0,0);
			ppc_extshx(ppc_rarg1,ppc_rarg1,0);
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
			binop_end(op);
			break;

		// --- 64-bit multiply: low -> rd (macl) / high -> rd2 (mach) -----------
		// NOTE: the decoder puts the high word in op->rd2 (reg_mach), NOT in
		// op->rd._reg+1. reg_mach actually PRECEDES reg_macl in the enum, so
		// rd._reg+1 == reg_pr — the old code corrupted PR and never wrote mach,
		// which broke booting.
		case shop_mul_u64:
			// 64-bit unsigned product of two 32-bit values.
			binop_start(op);
			// high word first (mulhwu) since low word overwrites an input reg.
			ppc_mulhwux(ppc_rarg2,ppc_rarg0,ppc_rarg1,0);	// hi = mulhwu(r1,r2)
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);	// lo = mullw(r1,r2)
			ppc_sh_store(ppc_rarg0,op->rd);
			ppc_sh_store(ppc_rarg2,op->rd2);
			break;
		case shop_mul_s64:
			// 64-bit signed product of two 32-bit values.
			binop_start(op);
			ppc_mulhwx(ppc_rarg2,ppc_rarg0,ppc_rarg1,0);	// hi = mulhw(r1,r2)
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);	// lo = mullw(r1,r2)
			ppc_sh_store(ppc_rarg0,op->rd);
			ppc_sh_store(ppc_rarg2,op->rd2);
			break;

		// --- 32-bit division (matched ROTCL/DIV1 idiom): quo->rd, rem->rd2 ----
		// Broadway has hardware divide; remainder = dividend - quo*divisor.
		// Division by zero gives an undefined register result on PPC (no trap),
		// which is fine — matched sequences are compiler-generated divisions.
		case shop_div32u:
			binop_start(op);				// rarg0=dividend, rarg1=divisor
			ppc_divwux(ppc_rarg2,ppc_rarg0,ppc_rarg1,0,0);	// quo = divwu
			ppc_mullwx(ppc_rarg3,ppc_rarg2,ppc_rarg1,0,0);	// quo*divisor
			ppc_subfx(ppc_rarg3,ppc_rarg3,ppc_rarg0,0,0);	// rem = dividend - quo*divisor
			ppc_sh_store(ppc_rarg2,op->rd);
			ppc_sh_store(ppc_rarg3,op->rd2);
			break;
		case shop_div32s:
			// divw truncates toward zero, same as the C canonical.
			binop_start(op);
			ppc_divwx(ppc_rarg2,ppc_rarg0,ppc_rarg1,0,0);	// quo = divw
			ppc_mullwx(ppc_rarg3,ppc_rarg2,ppc_rarg1,0,0);
			ppc_subfx(ppc_rarg3,ppc_rarg3,ppc_rarg0,0,0);
			ppc_sh_store(ppc_rarg2,op->rd);
			ppc_sh_store(ppc_rarg3,op->rd2);
			break;
		case shop_div32p2:
			// rd = T ? a : a-b  (non-restoring remainder fixup).
			// PRECONDITION: T (rs3) is 0/1 — guaranteed because the decoder
			// emits "and T,quo,1" immediately before this op. mask = T-1 maps
			// 0 -> 0xFFFFFFFF (apply b) and 1 -> 0 (keep a). Branchless.
			binop_start(op);				// rarg0=a, rarg1=b
			ppc_sh_load(ppc_rarg2,op->rs3);			// T
			ppc_addi(ppc_rarg2,ppc_rarg2,-1);		// mask = T-1
			ppc_andx(ppc_rarg1,ppc_rarg1,ppc_rarg2,0);	// b &= mask
			ppc_subfx(ppc_rarg0,ppc_rarg1,ppc_rarg0,0,0);	// a -= (T ? 0 : b)
			binop_end(op);
			break;

		// --- addc/subc/negc: carry chains via XER.CA (branchless) --------------
		// These were interpreter fallbacks (shop_ifb: full pinned-reg flush +
		// C call + reload, ~35+ insns) — now 6-8 insns inline. T is
		// architecturally 0/1; XER.CA is seeded from it:
		//   addic  r0,T,-1  ->  CA = (T != 0) = T        (adc)
		//   subfic r0,T,0   ->  CA = (T == 0) = !T       (sbc/negc: CA = NOT borrow)
		// adde/subfe then produce result + carry-out in ONE instruction (safe
		// to target a pinned rd even when it aliases a source), and li+addze
		// extracts CA as a 0/1 integer for the T output. None of addic/subfic/
		// adde/addze touch CR0 — only XER.CA.
		case shop_adc:	// rd = rs1 + rs2 + T ; rd2 = carry out
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				ppc_sh_load(ppc_rarg2,op->rs3);			// T (not pinned)
				ppc_ireg d=GetIntReg(op->rd._reg);
				u32 rd=(d!=ppc_rinvalid)?(u32)d:ppc_rarg0;
				ppc_addic(ppc_r0,ppc_rarg2,-1,0);		// CA = T (result discarded)
				ppc_addex(rd,a,b,0,0);				// rd = a+b+CA, CA = carry
				ppc_li(ppc_rarg2,0);
				ppc_addzex(ppc_rarg2,ppc_rarg2,0,0);		// rarg2 = CA
				ppc_sh_store(ppc_rarg2,op->rd2);		// T out
				if (rd==ppc_rarg0 && d==ppc_rinvalid)
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		case shop_sbc:	// rd = rs1 - rs2 - T ; rd2 = borrow out
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				ppc_sh_load(ppc_rarg2,op->rs3);			// T
				ppc_ireg d=GetIntReg(op->rd._reg);
				u32 rd=(d!=ppc_rinvalid)?(u32)d:ppc_rarg0;
				ppc_subfic(ppc_r0,ppc_rarg2,0);			// CA = !T
				ppc_subfex(rd,b,a,0,0);				// rd = a-b-1+CA = a-b-T; CA = !borrow
				ppc_li(ppc_rarg2,0);
				ppc_addzex(ppc_rarg2,ppc_rarg2,0,0);		// rarg2 = CA
				ppc_xori(ppc_rarg2,ppc_rarg2,1);		// borrow = !CA
				ppc_sh_store(ppc_rarg2,op->rd2);		// T out
				if (rd==ppc_rarg0 && d==ppc_rinvalid)
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		case shop_negc:	// rd = 0 - rs1 - T ; rd2 = borrow out   (rs2 = T)
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				ppc_sh_load(ppc_rarg2,op->rs2);			// T
				ppc_ireg d=GetIntReg(op->rd._reg);
				u32 rd=(d!=ppc_rinvalid)?(u32)d:ppc_rarg0;
				ppc_subfic(ppc_r0,ppc_rarg2,0);			// CA = !T
				ppc_li(ppc_rarg3,0);
				ppc_subfex(rd,a,ppc_rarg3,0,0);			// rd = 0-a-1+CA = -a-T
				ppc_li(ppc_rarg2,0);
				ppc_addzex(ppc_rarg2,ppc_rarg2,0,0);
				ppc_xori(ppc_rarg2,ppc_rarg2,1);		// borrow = !CA
				ppc_sh_store(ppc_rarg2,op->rd2);		// T out
				if (rd==ppc_rarg0 && d==ppc_rinvalid)
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		case shop_swaplb:	// swap.b: rd = (r1 & 0xFFFF0000) | low-byte swap
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				// Build in rarg1: a is a pinned reg or rarg0, never rarg1, so
				// the inserts keep reading a valid source. 3 rotate/insert ops.
				ppc_rlwinmx(ppc_rarg1,a,8,16,23,0);		// (r1 & 0xFF) << 8
				ppc_rlwimix(ppc_rarg1,a,24,24,31,0);		// |= (r1 >> 8) & 0xFF
				ppc_rlwimix(ppc_rarg1,a,0,0,15,0);		// |= r1 & 0xFFFF0000
				ppc_sh_store(ppc_rarg1,op->rd);
			}
			break;

		// --- Shifts with dynamic SH4 semantics (branchless) -------------------
		case shop_ror:
			// rd = rotr(r1, r2&31). PPC rotates left; rotl by (32-(r2&31)).
			if (op->rs2.is_imm())
			{
				// Constant rotate (this is also swap.w, decoded as ror #16):
				// single rlwinm, reads/writes pinned regs in place. n==0 gives
				// rotl 0 == plain move, still correct.
				u32 n=op->rs2._imm&0x1F;
				bop_resolve_a_d(op);
				ppc_rlwinmx(bop_d,bop_a,(32-n)&31,0,31,0);
				binop3_end(op);
				break;
			}
			// Dynamic amount: rlwnm rotate count is taken from rB[27:31]
			// (mod 32), so passing 32 when (r2&31)==0 is harmless (32%32==0).
			binop_start(op);
			ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,27,31,0);	// rarg1 = r2 & 0x1F
			ppc_subfic(ppc_rarg1,ppc_rarg1,32);			// rarg1 = 32 - (r2&31)
			ppc_rlwnmx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,31,0);	// rotl, full mask
			binop_end(op);
			break;
		case shop_shld:
			// SH4 logical dynamic shift (branchless):
			//   left  = r1 << (r2 & 0x1F)
			//   rcnt  = (-r2) & 0x1F ;  if rcnt==0 force 32 so srw yields 0
			//   right = r1 >> rcnt
			//   res   = (r2 >= 0) ? left : right
			// (When r2<0 and (r2&31)==0 SH4 yields 0; srw by 32 gives 0.)
			{
				binop_start(op);				// rarg0=r1, rarg1=r2
				// left = r1 << (r2 & 0x1F)
				ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,27,31,0);	// rarg2 = r2 & 0x1F
				ppc_slwx(ppc_rarg3,ppc_rarg0,ppc_rarg2,0);	// rarg3 = left
				// rcnt = (-r2) & 0x1F
				ppc_negx(ppc_rarg2,ppc_rarg1,0,0);		// -r2
				ppc_rlwinmx(ppc_rarg2,ppc_rarg2,0,27,31,0);	// rcnt = (-r2)&0x1F
				// force 32 when rcnt==0:  isZero=(rcnt-1)>>31 ; rcnt += isZero*32
				ppc_addi(ppc_r0,ppc_rarg2,-1);			// r0 = rcnt-1
				ppc_rlwinmx(ppc_r0,ppc_r0,1,31,31,0);		// r0 = (rcnt==0)?1:0  (sign bit -> bit31)
				ppc_rlwinmx(ppc_r0,ppc_r0,5,0,31,0);		// r0 <<= 5  -> 32 when zero
				ppc_orx(ppc_rarg2,ppc_rarg2,ppc_r0,0);		// rcnt |= 32 when zero
				ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg2,0);	// rarg0 = right
				// select: mask = (r2<0)? ~0 : 0
				ppc_srawix(ppc_r0,ppc_rarg1,31,0);
				ppc_andx(ppc_rarg0,ppc_rarg0,ppc_r0,0);		// right & mask
				ppc_andcx(ppc_rarg3,ppc_rarg3,ppc_r0,0);	// left & ~mask
				ppc_orx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0);
				binop_end(op);
			}
			break;
		case shop_shad:
			// SH4 arithmetic dynamic shift (branchless):
			//   left  = r1 << (r2 & 0x1F)
			//   rcnt  = (-r2) & 0x1F ;  if rcnt==0 force 31 (sign fill)
			//   right = (s32)r1 >> rcnt
			//   res   = (r2 >= 0) ? left : right
			{
				binop_start(op);				// rarg0=r1, rarg1=r2
				ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,27,31,0);	// r2 & 0x1F
				ppc_slwx(ppc_rarg3,ppc_rarg0,ppc_rarg2,0);	// rarg3 = left
				// rcnt = (-r2) & 0x1F
				ppc_negx(ppc_rarg2,ppc_rarg1,0,0);
				ppc_rlwinmx(ppc_rarg2,ppc_rarg2,0,27,31,0);	// rcnt
				// force 31 when rcnt==0:  isZero=(rcnt-1)>>31 ; rcnt += isZero*31
				ppc_addi(ppc_r0,ppc_rarg2,-1);
				ppc_rlwinmx(ppc_r0,ppc_r0,1,31,31,0);		// (rcnt==0)?1:0
				ppc_mulli(ppc_r0,ppc_r0,31);			// 31 when zero else 0
				ppc_addx(ppc_rarg2,ppc_rarg2,ppc_r0,0,0);	// rcnt = 31 in zero-case
				ppc_srawx(ppc_rarg0,ppc_rarg0,ppc_rarg2,0);	// rarg0 = right (arithmetic)
				// select on sign of r2
				ppc_srawix(ppc_r0,ppc_rarg1,31,0);
				ppc_andx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
				ppc_andcx(ppc_rarg3,ppc_rarg3,ppc_r0,0);
				ppc_orx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0);
				binop_end(op);
			}
			break;

		// --- Integer comparisons / test ---------------------------------------
		// (set*/test do their own operand load + immediate folding internally.)
		case shop_test:	// rd = (r1 & r2) == 0
			{
				// and./andi. read any reg and write a scratch dest (rarg0), so we
				// can source pinned rs1/rs2 in place and skip the loading mr's. The
				// result (CR0) goes through mfcr->rarg0; rarg0 as the and dest is
				// dead-on-arrival so clobbering a pinned source is impossible.
				u32 a1=src_or_load(op->rs1,ppc_rarg0);

				if (op->rs2.is_imm_u16())
					ppc_andi(ppc_rarg0,a1,op->rs2._imm);		// andi. sets CR0
				else
				{
					u32 b;
					if (op->rs2.is_imm()) { ppc_li(ppc_rarg1,op->rs2._imm); b=ppc_rarg1; }
					else b=src_or_load(op->rs2,ppc_rarg1);
					ppc_andx(ppc_rarg0,a1,b,1);			// and. sets CR0
				}
				emit_cr0_bit_to_rarg0(BI_CR0_EQ);
				binop_end(op);
				fusion_record(op,BI_CR0_EQ,false);
			}
			break;
		case shop_seteq: emit_setcc_signed(op,BI_CR0_EQ,false); break;
		case shop_setgt: emit_setcc_signed(op,BI_CR0_GT,false); break;
		case shop_setge: emit_setcc_signed(op,BI_CR0_LT,true);  break; // >= == !<
		case shop_setab: emit_setcc_unsigned(op,BI_CR0_GT,false); break;
		case shop_setae: emit_setcc_unsigned(op,BI_CR0_LT,true);  break; // >= == !<

		// --- Unary integer ----------------------------------------------------
		case shop_neg:    unop3_start(op); ppc_negx(bop_d,bop_a,0,0);    unop3_end(op); break;
		case shop_not:    unop3_start(op); ppc_norx(bop_d,bop_a,bop_a,0); unop3_end(op); break;
		case shop_ext_s8: unop3_start(op); ppc_extsbx(bop_d,bop_a,0);    unop3_end(op); break;
		case shop_ext_s16:unop3_start(op); ppc_extshx(bop_d,bop_a,0);    unop3_end(op); break;

		// --- Unary float ------------------------------------------------------
		case shop_fabs: funop3_start(op); ppc_fabsx(fop_d,fop_a,0); funop3_end(op); break;
		case shop_fneg: funop3_start(op); ppc_fnegx(fop_d,fop_a,0); funop3_end(op); break;

		// --- Float comparisons (rd is an integer 0/1) -------------------------
		case shop_fseteq:
			// NaN: fcmpu sets UN, EQ stays 0 -> false, matching the canonical.
			// fcmpu reads any FPR, so pinned sources compare in place.
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_EQ);
				ppc_sh_store(ppc_rarg0,op->rd);
				fusion_record(op,BI_CR0_EQ,false);
			}
			break;
		case shop_fsetgt:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_GT);
				ppc_sh_store(ppc_rarg0,op->rd);
				fusion_record(op,BI_CR0_GT,false);
			}
			break;

		// --- Float<->int conversion -------------------------------------------
		// All conversions bounce integer<->double bit patterns through the
		// per-context jit_scratch slot (8 bytes, 8-aligned), addressed via
		// ppc_contex. This avoids relying on a stack red zone (the devkitPPC
		// EABI does not guarantee one below sp).
		case shop_cvt_f2i_t:	// (s32)truncate(f)
			{
				unop_start_fpu(op);
				u32 so = (u32)offsetof(Sh4Context, jit_scratch);
				ppc_fctiwzx(ppc_f0,ppc_farg0,0);		// f0[low32] = (s32)trunc(farg0)
				ppc_stfd(ppc_f0,ppc_contex,so);			// spill the double
				ppc_lwz(ppc_rarg0,ppc_contex,so+4);		// low word (BE) = integer result
				ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;
		case shop_cvt_i2f_n:	// (float)(s32)  round-to-nearest
		case shop_cvt_i2f_z:	// (float)(s32)  (s32->f32 is identical for both modes here)
			// Classic PPC s32->double magic-constant trick:
			//   d = bitcast(0x43300000_00000000 | (u32)(r1 ^ 0x80000000)) - 0x4330000080000000.0
			// d is the exact (double)(s32)r1; frsp narrows to single.
			{
				unop_start(op);					// rarg0 = r1
				u32 so = (u32)offsetof(Sh4Context, jit_scratch);
				// Build the biased value double: hi=0x43300000, lo=r1^0x80000000
				ppc_xoris(ppc_rarg0,ppc_rarg0,0x8000);		// flip sign bit
				ppc_addis(ppc_rarg1,0,0x4330);			// rarg1 = 0x43300000 (lis)
				ppc_stw(ppc_rarg1,ppc_contex,so);		// scratch.hi
				ppc_stw(ppc_rarg0,ppc_contex,so+4);		// scratch.lo
				ppc_lfd(ppc_f0,ppc_contex,so);			// f0 = magic|value (double)
				// Build the subtrahend 0x4330000080000000 in the same slot.
				ppc_addis(ppc_rarg1,0,0x4330);
				ppc_stw(ppc_rarg1,ppc_contex,so);
				ppc_addis(ppc_rarg1,0,0x8000);
				ppc_stw(ppc_rarg1,ppc_contex,so+4);
				ppc_lfd(ppc_farg1,ppc_contex,so);		// farg1 = 0x4330000080000000
				ppc_fsubx(ppc_f0,ppc_f0,ppc_farg1,0);		// f0 = (double)(s32)r1
				ppc_frspx(ppc_farg0,ppc_f0,0);			// narrow to single
				ppc_sh_store_f32(ppc_farg0,op->rd);
			}
			break;

		// --- FMAC: rd = rs1 + rs2 * rs3  (scalar) -----------------------------
		// fmadds(D,A,B,C) = A*C + B  ->  rs2*rs3 + rs1. Single instruction, so
		// a pinned rd may alias any pinned source safely.
		case shop_fmac:
			{
				u32 a1=fsrc_or_load(op->rs1,ppc_f0);		// addend
				u32 a2=fsrc_or_load(op->rs2,ppc_f1);
				u32 a3=fsrc_or_load(op->rs3,ppc_f2);
				ppc_freg dp=fpin(op->rd._reg);
				u32 d=(dp!=ppc_finvalid)?(u32)dp:ppc_f0;
				ppc_fmaddsx(d,a2,a1,a3,0);			// d = a2*a3 + a1
				if (dp==ppc_finvalid)
					ppc_sh_store_f32(ppc_f0,op->rd);
			}
			break;

		// --- FIPR: rd = dot(v1, v2) over 4 elements ---------------------------
		// rs1,rs2 are FV4 vectors (base fr); rd is the 4th element slot.
		// All-pinned (the normal case): 4 arithmetic instructions, no loads.
		case shop_fipr:
			{
				u32 a=ppc_fvec_src(op->rs1,0,ppc_f1);
				u32 b=ppc_fvec_src(op->rs2,0,ppc_f2);
				ppc_fmulsx(ppc_f0,a,b,0);			// acc = a0*b0
				for (u32 i=1;i<4;i++)
				{
					a=ppc_fvec_src(op->rs1,i,ppc_f1);
					b=ppc_fvec_src(op->rs2,i,ppc_f2);
					ppc_fmaddsx(ppc_f0,a,ppc_f0,b,0);	// acc = ai*bi + acc
				}
				ppc_sh_store_f32(ppc_f0,op->rd);
			}
			break;

		// --- FTRV: rd(vec4) = matrix(rs2) x vec(rs1) --------------------------
		// rs1 = FV4 vector (fr base, pinned), rs2 = XMTRX (xf base, ALWAYS in
		// memory — xf is not pinned), rd = FV4 (aliases rs1).
		//   out[i] = m[i]*v[0] + m[4+i]*v[1] + m[8+i]*v[2] + m[12+i]*v[3]
		// Results stash in f4..f7 because rd aliases rs1: every read of v[]
		// completes before the store loop writes the (possibly pinned) rd.
		case shop_ftrv:
			{
				// Resolve the input vector: pinned regs read in place, else
				// loaded once into f8..f11.
				u32 v[4];
				for (u32 j=0;j<4;j++)
					v[j]=ppc_fvec_src(op->rs1,j,ppc_f8+j);

				for (u32 i=0;i<4;i++)
				{
					ppc_fvec_load(ppc_f1,op->rs2,i);		// m[i]
					ppc_fmulsx(ppc_f0,ppc_f1,v[0],0);		// out = m[i]*v0
					for (u32 j=1;j<4;j++)
					{
						ppc_fvec_load(ppc_f1,op->rs2,j*4+i);	// m[j*4+i]
						ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,v[j],0);	// out += m*vj
					}
					ppc_fmrx(ppc_f4+i,ppc_f0,0);			// stash out[i]
				}
				for (u32 i=0;i<4;i++)
					ppc_fvec_store(ppc_f4+i,op->rd,i);
			}
			break;

		// fsqrt / fsrra are handled below via accurate native calls (the
		// frsqrte estimate was too imprecise — it distorted the BIOS swirl).

		// --- FSCA: rd[0]=sin_table[idx], rd[1]=sin_table[idx+0x4000] ----------
		// idx = rs1 & 0xFFFF. Table entries are f32 (4 bytes).
		case shop_fsca:
			{
				// idx*4 = (fpul & 0xFFFF)*4: rlwinm reads pinned rs1 in place -> rarg0.
				u32 fp=src_or_load(op->rs1,ppc_rarg0);
				ppc_rlwinmx(ppc_rarg0,fp,2,14,29,0);		// rarg0 = (idx & 0xFFFF) * 4
				// rarg1 = &sin_table  (split hi/lo via lis+addi pattern)
				u32 lo=ppc_addr_high(ppc_rarg1,(void*)&sin_table[0]);
				ppc_addi(ppc_rarg1,ppc_rarg1,lo);		// rarg1 = base of sin_table
				// lfsx can target any FPR: land the table entries directly in
				// the pinned rd pair when available.
				ppc_freg p0=ppc_fvec_pin(op->rd,0);
				u32 d0=(p0!=ppc_finvalid)?(u32)p0:ppc_f0;
				ppc_lfsx(d0,ppc_rarg1,ppc_rarg0);		// sin_table[idx]
				if (p0==ppc_finvalid)
					ppc_fvec_store(ppc_f0,op->rd,0);
				// +0x4000 entries * 4 bytes = +0x10000 (doesn't fit addi s16;
				// use addis to add 1<<16).
				ppc_addis(ppc_rarg0,ppc_rarg0,1);		// rarg0 += 0x10000
				ppc_freg p1=ppc_fvec_pin(op->rd,1);
				u32 d1=(p1!=ppc_finvalid)?(u32)p1:ppc_f1;
				ppc_lfsx(d1,ppc_rarg1,ppc_rarg0);		// sin_table[idx+0x4000]
				if (p1==ppc_finvalid)
					ppc_fvec_store(ppc_f1,op->rd,1);
			}
			break;

		// --- SR / FPSCR sync, prefetch, sqrt: direct native calls ------------
		// sync_sr: write SR side-effects. UpdateSR() -> ChangeGPR() swaps
		// r[]<->r_bank[] IN MEMORY on an SR.RB change, so pinned GPRs must be
		// flushed before and reloaded after. The bool return (interrupt pending)
		// is intentionally ignored here: every SH4 op that emits sync_sr ends its
		// block with BET_*Intr, whose native handler runs UpdateINTC and
		// dispatches. (This is pseudo-core: keep it self-contained, do not rely
		// on the generic fallback.)
		case shop_sync_sr:
			reg_flush_all();
			ppc_call(&UpdateSR);
			reg_reload_all();
			break;

		// sync_fpscr: UpdateFPSCR() (rounding mode + possible FR bank swap).
		// No GPR flush needed. With STATIC_FPU_ALLOC, UpdateFPSCR()->ChangeFP()
		// swaps fr[]<->xf[] IN MEMORY when FPSCR.FR changes, so the pinned fr
		// regs are flushed before / reloaded after. The decoder sets flags=1
		// for emitters that CANNOT flip FR (fschg toggles only the SZ bit) —
		// those keep the cheap plain call.
		case shop_sync_fpscr:
			if (op->flags&1)
				ppc_call(&UpdateFPSCR);
			else
			{
				fpu_flush_all();
				ppc_call(&UpdateFPSCR);
				fpu_reload_all();
			}
			break;

		// pref: store-queue prefetch. Only addresses in the SQ region trigger a
		// store-queue flush — the canonical guards with `if ((addr>>26)==0x38)`
		// BEFORE calling do_sqw (do_sqw itself assumes an SQ address). For any
		// other address pref is a no-op. The MMU vs no-MMU variant is chosen at
		// COMPILE time from CCN_MMUCR.AT. addr in rarg0.
		case shop_pref:
			{
				ppc_sh_load(ppc_rarg0,op->rs1);
				ppc_rlwinmx(ppc_rarg1,ppc_rarg0,6,26,31,0);	// rarg1 = addr >> 26
				ppc_cmpi(ppc_cr0,ppc_rarg1,0x38,0);		// SQ region?
				ppc_label* skip=ppc_CreateLabel();
				ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne skip (not SQ -> no-op)
				if (CCN_MMUCR.AT)
					ppc_call(&do_sqw_mmu);			// do_sqw reloads rarg0=addr arg
				else
					ppc_call(&do_sqw_nommu);
				skip->MarkLabel();
			}
			break;

		// fsqrt / fsrra: single f32->f32 calls (accurate libm path).
		// arg in farg0 (f1), result in frv0 (f1) per PPC FP calling convention.
		case shop_fsqrt:
			ppc_sh_load_f32(ppc_farg0,op->rs1);
			ppc_call(&rec_fsqrt);
			ppc_sh_store_f32(ppc_frv0,op->rd);
			break;
		case shop_fsrra:
			ppc_sh_load_f32(ppc_farg0,op->rs1);
			ppc_call(&rec_fsrra);
			ppc_sh_store_f32(ppc_frv0,op->rd);
			break;

		default:
			//canonical fallback ~
      if (!shil_chf[op->op]) {
          printf("OH CRAP %d\n", op->op);
          die("Recompiler doesn't know about that opcode");
      }
			// Bracket the canonical fallback with flush/reload: some fallback
			// handlers mutate context state directly (sync_sr -> UpdateSR bank
			// switch for GPRs; fsca_impl writes fr[] through a CPT_ptr for
			// FPRs), so pinned regs must be coherent in memory across the call
			// and re-read afterwards. Return-value stores inside the CC code
			// intentionally target MEMORY (see ngen_CC_Param) so the reloads
			// pick them up rather than clobbering them.
			reg_flush_all();
			fpu_flush_all();
			shil_chf[op->op](op);
			fpu_reload_all();
			reg_reload_all();
			break;
      
		}
	}

	ngen_End(block);

	FlushCold();          // emit the out-of-line mem slow paths after the block tail
	make_address_range_executable((u8*)rv, (u8*)emit_GetCCPtr()-(u8*)rv);
	return rv;
}



void ngen_ResetBlocks()
{
}

void* FASTCALL ngen_LinkBlock_Static(u32 pc,u32* patch)
{
	next_pc=pc;
	
	DynarecCodeEntry* rv=rdv_FindOrCompile();
	
	emit_ptr=patch;
	{
		ppc_jump(rv);
	}
	emit_ptr=0;

	make_address_range_executable(patch, 1*sizeof(u32));

	return (void*)rv;
}

// =========
// MAIN LOOP
// =========

void ngen_mainloop()
{
	if (loop_code==0)
	{

		loop_code=(void(*)())emit_GetCCPtr();
		{
			/*
			create stack frame, push regsters, etc ..
			*/
			u32 stac_alloc_size=8+20*4;
			ppc_mfspr(ppc_r0,ppc_spr_lr);
			ppc_addi(ppc_sp,ppc_sp,-stac_alloc_size);
			
			//store link register
			ppc_stw(ppc_r0,ppc_sp,stac_alloc_size+4);

			//store gprs r13..r31 (19 preserved regs per ABI)
			// Layout: sp+[stac_alloc_size-4] = r13, sp+[stac_alloc_size-8] = r14, ...
			for (int i=0;i<19;i++)
			{
				ppc_stw(ppc_r13+i,ppc_sp,stac_alloc_size-4-i*4);
			}

			/*
			pre load registers/counters etc ..
			*/

			//cntx base
			ppc_lip(ppc_contex,&Sh4cntx);

			// Load the pinned SH4 GPRs ONCE here, after ppc_contex is valid and
			// before the loop_no_update re-entry point below. They then stay live
			// in r14..r28 for the entire JIT run (no per-block reload/flush); the
			// only resync points are shop_ifb and the canonical fallback.
			reg_reload_all();

			// Same for the pinned FPU regs (fr0..fr15 -> f14..f29): loaded once,
			// resynced only at the FPU flush/reload points (see fpu_flush_all).
			fpu_reload_all();

			// Cycle budget per timeslice. Latched from the accuracy preset
			// (FAST=1792 / BALANCED=896 / ACCURATE=448) rather than the fixed
			// SH4_TIMESLICE: recSh4_Run() applies the preset before this code
			// is emitted, and UpdateSystem_no_event() advances TMU/PVR by the
			// same s_timeslice, so the two stay consistent. Baked in at emit
			// time — the mainloop is generated once per session.
			const s32 jit_timeslice = sh4_GetTimeslice();

			//cycles
			ppc_li(ppc_cycles,jit_timeslice);

			//and pc!
			ppc_sh_load(ppc_next_pc,reg_nextpc);

			//no_update
			loop_no_update=emit_GetCCPtr();

			//handy function !
			ppc_call_and_jump(bm_GetCode);

			//do_update_write
			loop_do_update_write=emit_GetCCPtr();


			//next_pc _MUST_ be on ram since update system uses it for interrupt processing
			ppc_sh_store(ppc_next_pc,reg_nextpc);
			ppc_addi(ppc_cycles,ppc_cycles,jit_timeslice);	//add cycles (preset-latched timeslice)

			// Split UpdateSystem: the GPR-free peripheral cascade + interrupt
			// pending-check runs every timeslice WITHOUT flushing the pinned
			// GPRs. Only if it reports a pending interrupt (rv != 0) do we flush,
			// run the GPR-touching handler (Do_Interrupt / bank swap), and reload.
			ppc_call(UpdateSystem_no_event);
			ppc_cmpi(ppc_cr0,ppc_rrv0,0,0);			// rv == 0 ?
			ppc_label* no_intr=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq no_intr (skip handler)
			{
				reg_flush_all();
				ppc_call(UpdateSystem_handle_event);
				reg_reload_all();
			}
			no_intr->MarkLabel();
			ppc_sh_load(ppc_next_pc,reg_nextpc);
			//
			ppc_jump(loop_no_update);
			//right

      /*
      Claude AI says it's dead end after looking in dump dynarec_XXX.bin
			ppc_lbz(ppc_rarg0,ppc_rarg0,ppc_addr_high(ppc_rarg0,(void*)&sh4_int_bCpuRun));
			ppc_sh_load(ppc_next_pc,reg_nextpc);

			ppc_cmpi(ppc_cr0,ppc_rarg0,1,0);	//set flags

			//does this even work ?
			//ppc_bcx(BO_TRUE,BI_CR0_EQ,ppc_jdiff(loop_no_update),0,0);
			

			//write back registers and stuff ...

			//cleanup
			
			Clean up the stack frame and return ...
			

			//restore link register
			ppc_lwz(ppc_r0,ppc_sp,stac_alloc_size+4);
			ppc_mtlr(ppc_r0);

			//restore gprs 13 .. 31
			for (int i=0;i<19;i++)
			{
				ppc_lwz(ppc_r13+i,ppc_sp,stac_alloc_size-4-i*4);
			}

			

			//destroy stack frame
			ppc_addi(ppc_sp,ppc_sp,stac_alloc_size);

			//return
			ppc_bclrx(BO_ALWAYS,BI_CR0_EQ,0);  // blr
      */

		} //that was mainloop


		//ngen_FailedToFindBlock
		ngen_FailedToFindBlock=(void(*)())emit_GetCCPtr();
		{
			ppc_call_and_jump(&rdv_FailedToFindBlock);
		}

    // ====================
    // STATIC BLOCK LINKING
    // ====================

		ngen_LinkBlock_Static_stub=emit_GetCCPtr();
		{
			//not used for now
			ppc_mfspr(ppc_rarg1,ppc_spr_lr);
			
			ppc_addi(ppc_rarg1,ppc_rarg1,(u32)-12);
			ppc_call_and_jump(&ngen_LinkBlock_Static);
		}

		ngen_LinkBlock_Dynamic_1st_stub=emit_GetCCPtr();
		{
			//not used for now
		}

		ngen_LinkBlock_Dynamic_2nd_stub=emit_GetCCPtr();
		{
			//not used for now
		}

		ngen_BlockCheckFail_stub=emit_GetCCPtr();
		{
			ppc_call_and_jump(&rdv_BlockCheckFail);
		}

		//Make _SURE_ this code is not overwriten !
		emit_SetBaseAddr();
		
		char file[512];
		snprintf(file, sizeof(file), "dynarec_%08X.bin", (u32)(uintptr_t)loop_code);
		char* path=GetEmuPath(file);

		FILE* f = fopen(path, "wb");
		free(path);

		if (f)
		{
			fwrite((void*)loop_code, 1, CODE_SIZE - emit_FreeSpace(), f);
			fclose(f);  // fclose flushes; explicit fflush is redundant
		}
		
    // CACHE COHERENCY
    make_address_range_executable((u8*)loop_code, (u8*)emit_GetCCPtr()-(u8*)loop_code);
	}

	loop_code();
}

void ngen_GetFeatures(ngen_features* dst)
{
	dst->InterpreterFallback=false;
	dst->OnlyDynamicEnds=false;
}