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
#include <stdlib.h>	// malloc — JIT_HOTBLOCKS lazily allocates its block table
#include "dc\sh4\sh4_opcode_list.h"
#include "dc\sh4\sh4_interpreter.h"	// sh4_GetTimeslice — preset-latched timeslice

#include "dc\sh4\sh4_registers.h"
#include "dc\sh4\ccn.h"
#include "dc\sh4\rec_v2\ngen.h"
#include "dc\mem\sh4_mem.h"
#include "dc\mem\sh4_internal_reg.h"	// sq_both — fastmem SQ trampoline fast path
#include "wii\wii_fastmem.h"		// PPC-MMU fastmem (FASTMEM preset)
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
extern "C" int get_bcache_preset();	// main.cpp: 0=off (default), 1=flat dispatch cache
extern "C" int get_dyn_ic_preset();	// main.cpp: 0=off (default), 1=call+jump, 2=+rts
extern "C" int get_fpu_pin_preset();	// main.cpp: 0=off (default), 1=pin fr[0..15] to f14..f29
extern "C" int get_jit_align_preset();	// main.cpp: 0=off (default), 1=32-byte-align block entries
extern "C" int get_ifb_flush_preset();	// main.cpp: 0=off (default, full bracket), 1=selective
extern "C" int get_ifb_probe_preset();	// main.cpp: 0=off (default), 1=count ifb sites per opcode

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
void* ngen_LinkBlock_Dynamic_IC_stub;	// DYN_IC preset: inline-cache fill
void* ngen_BlockCheckFail_stub;
void* loop_do_update_write;
void (*loop_code)() ;
void (*ngen_FailedToFindBlock)();

// Defined below — emits the addis half of an absolute address into rD and
// returns the low 16-bit displacement. Forward-declared so the block-check
// guard in ngen_Begin() can use it.
u32 ppc_addr_high(u32 rD,void* ptr);

struct
{
	bool has_jcond;

	// T-FORWARDING. The SH4's commonest shape by far is a flag producer
	// (cmp/tst/dt) immediately followed by a consumer (bt/bf, or the shop_jcond
	// of a delay-slot branch). sr_T is not pinned — GetIntReg only covers
	// r0..r15 minus r11 — so the producer stored T to the context and the
	// consumer loaded it straight back:
	//     stw r3,0x118(r30)      <- producer (value still in rarg0!)
	//     lwz r3,0x118(r30)      <- consumer, same address, same register
	// which is a load-hit-store stall on Broadway to recover a value that never
	// left the register. Set true by binop_end when it has just written sr_T,
	// and cleared before EVERY shil op, so it can only ever describe the
	// immediately preceding one — no cross-op liveness reasoning needed, and no
	// way for an intervening op to have clobbered rarg0 behind our back.
	// The store stays: sr_T is architectural state a later block may read.
	bool t_in_rarg0;

	void Reset()
	{
		has_jcond=false;
		t_in_rarg0=false;
	}
} compile_state;
u32 last_block;

// JIT_TFWD preset (main.cpp): forward sr_T from rarg0 to the branch that
// consumes it instead of storing then immediately reloading it. See the
// t_in_rarg0 comment in compile_state.
extern "C" int get_jit_tfwd_preset();

// JIT_FMOV preset (main.cpp): extend FPU_PIN "Phase B" (direct lfs/stfs to a
// pinned FR, no GPR bounce) to the FASTMEM shapes in shop_readm/shop_writem.
// Needs the matching float shapes in rec_fastmem_patch() below, which is why
// it is a separate switch from fpu_pin rather than part of it.
extern "C" int get_jit_fmov_preset();

// Forward decls: GPR/FPR allocation maps + flush/reload (defined later in this file).
ppc_ireg GetIntReg(u32 reg);
ppc_freg GetFloatReg(u32 reg);
void reg_flush_all();
void reg_reload_all();
void reg_flush_all_fpu();
void reg_reload_all_fpu();

// =======================
// BLOCK BEGIN/END
// =======================

void ngen_Begin(DecodedBlock* block,bool force_checks)
{
	compile_state.Reset();

	// ---------------------------------------------------------------------
	// Block-check guard (self-modifying code protection)
	//
	// force_checks comes from DoCheck() in driver.cpp, which is gated on the
	// JIT_SBP preset. When set, re-read the first byte of this block's SH4
	// source at entry and compare it against the value present at compile time.
	// A mismatch means the game overwrote its own code, so the translation is
	// stale — bail to rdv_BlockCheckFail, which clears the cache (dropping the
	// statically-patched links along with it) and recompiles from live memory.
	//
	// Emitted BEFORE the cycle decrement so a stale block never bills cycles.
	// rarg0/rarg1 are scratch at block entry (rarg0 is next_pc, which the
	// cycle-underflow path below overwrites anyway), so both are free here.
	// ---------------------------------------------------------------------
	if (force_checks && ngen_BlockCheckFail_stub)
	{
		u8* ptr = GetMemPtr(block->start, block->sh4_code_size ? block->sh4_code_size : 4);

		// DoCheck already probed this, but the mapping is the thing that makes
		// the compare safe — never emit a load we cannot justify.
		if (ptr)
		{
			u32 lo = ppc_addr_high(ppc_rarg1,(void*)ptr);
			ppc_lbz(ppc_rarg1,ppc_rarg1,lo);		// rarg1 = current source byte
			ppc_cmpli(ppc_cr0,ppc_rarg1,*ptr,0);	// vs the byte we compiled from

			ppc_label* check_ok=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq -> check_ok

			ppc_li(ppc_rarg0,block->start);			// rdv_BlockCheckFail(pc)
			ppc_jump(ngen_BlockCheckFail_stub);

			check_ok->MarkLabel();
		}
	}

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

//1 opcode (2 if bouncing a pinned FR through memory — see FPU_PIN below)
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
	// FPU_PIN landmine: shop_readm/writem/mov64 read/write ANY sh4_reg's raw
	// bit pattern through this generic helper, including FR-typed ones (e.g.
	// the fmov.s/fmov.d memory paths, which bounce the value through a GPR
	// since PPC750 has no direct GPR<->FPR move). If fr[] is pinned, the
	// authoritative bits live in a PPC FPR, not Sh4cntx.fr[] memory — so a
	// bare lwz here would read stale data. Fix: flush the pinned FPR to its
	// memory slot first, THEN read it back as an integer. One extra insn,
	// only on this int<->pinned-float crossing (the pure-float fast path
	// via ppc_sh_load_f32 never pays it).
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			u32 ofs=Sh4cntx.offset(sh4_reg);
			ppc_stfs(rf,ppc_contex,ofs);
			ppc_lwz(D,ppc_contex,ofs);
			return;
		}
	}
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
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			// Value already lives in a pinned PPC FPR; just move it.
			if ((u32)rf!=D)
				ppc_fmrx(D,rf,0);
			return;
		}
	}
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load_f32(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load_f32(D,prm._reg);
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
//1 opcode (2 if bouncing a pinned FR through memory — see FPU_PIN, ppc_sh_load)
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
	// FPU_PIN: symmetric fix to ppc_sh_load above. Write the raw bits to
	// memory as before, then reload the pinned FPR from that same slot so
	// it picks up the new value instead of going stale.
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			u32 ofs=Sh4cntx.offset(sh4_reg);
			ppc_stw(D,ppc_contex,ofs);
			ppc_lfs(rf,ppc_contex,ofs);
			return;
		}
	}
	ppc_stw(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_store(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_store(D,prm._reg);
}
void ppc_sh_store_f32(u32 D,u32 sh4_reg)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			// Destination is a pinned PPC FPR; update it in place.
			if ((u32)rf!=D)
				ppc_fmrx(rf,D,0);
			return;
		}
	}
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
// the context, and reg_fr_0..reg_fr_15 / reg_xf_0..reg_xf_15 are contiguous
// (sh4_if.h), so prm._reg+i is always the correct absolute register index.
// These load/store the i-th single-precision element.
//
// FPU_PIN preset: only fr[] is pinned (GetFloatReg returns invalid for the
// xf[] range — there aren't enough non-volatile PPC FPRs left to pin both
// banks), so an FV4/FV16 base in xf (e.g. ftrv's XMTRX operand) naturally
// falls through to memory here, unaffected.
u32 ppc_fvec_ofs(shil_param prm,u32 i)
{
	return Sh4cntx.offset(prm._reg) + i*4;
}
void ppc_fvec_load(u32 fd,shil_param prm,u32 i)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(prm._reg+i);
		if (rf!=ppc_finvalid)
		{
			if ((u32)rf!=fd)
				ppc_fmrx(fd,rf,0);
			return;
		}
	}
	ppc_lfs(fd,ppc_contex,ppc_fvec_ofs(prm,i));
}
void ppc_fvec_store(u32 fs,shil_param prm,u32 i)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(prm._reg+i);
		if (rf!=ppc_finvalid)
		{
			if ((u32)rf!=fs)
				ppc_fmrx(rf,fs,0);
			return;
		}
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
		case CPT_f32rv:
			ppc_sh_store_f32(ppc_frv0, *par);
			break;

		case CPT_u32rv:
		case CPT_u64rvL:
			ppc_sh_store(ppc_rrv0, *par);
			break;

		case CPT_u64rvH:
			ppc_sh_store(ppc_rrv1, *par);
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

	// ppc_sh_store reads rarg0, never writes it, so for an unpinned dest like
	// sr_T the value is still sitting in rarg0 for the next op to use. Gating
	// the flag HERE gates the whole optimisation: with JIT_TFWD off it never
	// gets set, and both consumers fall back to their original reload.
	compile_state.t_in_rarg0 = get_jit_tfwd_preset() &&
		op->rd.is_reg() && op->rd._reg==reg_sr_T;
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
	bop_d = (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg0;
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
	bop_d = (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg0;
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

// ---------------------------------------------------------------------------
// FPU_PIN: shadow-register-aware FLOAT operand resolution, mirroring the
// integer binop3_*/unop3_* scheme. Only for ops emitted as a SINGLE PPC FP
// instruction (fadds/fsubs/fmuls/fdivs/fabs/fneg/fmadds): such an instruction
// reads all sources then writes the dest atomically, so fop_d may safely
// alias a source. All of fr[0..15] is pinned when the preset is on, so the
// scalar arithmetic ops (operands always fr) collapse to ONE instruction;
// fpul/xf operands resolve to a scratch load exactly as before. With the
// preset off this degenerates to the legacy lfs/lfs/op/stfs sequence.
// ---------------------------------------------------------------------------
u32 fop_d, fop_a, fop_b;

// Resolve a float source: its pinned FPR (read in place, no copy), or load
// into scratch D. Never emits the fmr that ppc_sh_load_f32 would.
static u32 fsrc_or_load_i(u32 sh4_reg,u32 D)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
			return (u32)rf;
	}
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
	return D;
}
static u32 fsrc_or_load(shil_param prm,u32 D)
{
	verify(prm.is_reg());
	return fsrc_or_load_i(prm._reg,D);
}

static void fbop_resolve(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
	fop_a=fsrc_or_load(op->rs1,ppc_farg0);
	fop_b=fsrc_or_load(op->rs2,ppc_farg1);
	ppc_freg d=get_fpu_pin_preset()?GetFloatReg(op->rd._reg):ppc_finvalid;
	fop_d=(d!=ppc_finvalid)?(u32)d:(u32)ppc_farg0;
}
static void fuop_resolve(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	fop_a=fsrc_or_load(op->rs1,ppc_farg0);
	ppc_freg d=get_fpu_pin_preset()?GetFloatReg(op->rd._reg):ppc_finvalid;
	fop_d=(d!=ppc_finvalid)?(u32)d:(u32)ppc_farg0;
}
static void fbop_end(shil_opcode* op)
{
	// A pinned rd was written in place; only the scratch dest needs a store.
	// (Pinned regs are f14+, so fop_d==farg0 <=> rd is not pinned.)
	if (fop_d==ppc_farg0)
		ppc_sh_store_f32(ppc_farg0,op->rd);
}

// Resolve a float DEST to the FPR an op should write: the pinned FPR if rd is
// pinned (write in place, no trailing store), else `scratch`. Pair with
// fdst_store() which stores only when scratch was used.
static u32 fdst_reg(shil_param rd,u32 scratch)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(rd._reg);
		if (rf!=ppc_finvalid)
			return (u32)rf;
	}
	return scratch;
}
static void fdst_store(shil_param rd,u32 reg,u32 scratch)
{
	if (reg==scratch)
		ppc_sh_store_f32(scratch,rd);
}

// Minimal float register->register move between two SH4 float regs (fmov
// FRm,FRn and each word of fmov.d / DR<->XD). Emits the fewest instructions
// for however the two ends are allocated:
//   both pinned  -> fmr        (or nothing when identical)
//   src pinned   -> stfs       (dst is xf/memory)
//   dst pinned   -> lfs        (src is xf/memory)
//   both memory  -> lfs f0 + stfs f0   (== legacy path; also the preset-off case)
static void fmove_reg(u32 dst_reg,u32 src_reg)
{
	ppc_freg sd=get_fpu_pin_preset()?GetFloatReg(src_reg):ppc_finvalid;
	ppc_freg dd=get_fpu_pin_preset()?GetFloatReg(dst_reg):ppc_finvalid;

	if (sd!=ppc_finvalid && dd!=ppc_finvalid)
	{
		if (sd!=dd)
			ppc_fmrx(dd,sd,0);
	}
	else if (sd!=ppc_finvalid)
		ppc_stfs(sd,ppc_contex,Sh4cntx.offset(dst_reg));
	else if (dd!=ppc_finvalid)
		ppc_lfs(dd,ppc_contex,Sh4cntx.offset(src_reg));
	else
	{
		ppc_lfs(ppc_f0,ppc_contex,Sh4cntx.offset(src_reg));
		ppc_stfs(ppc_f0,ppc_contex,Sh4cntx.offset(dst_reg));
	}
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

// ---------------------------------------------------------------------------
// JIT_CARRY: SH4 T bit <-> PPC XER.
//
// TWO BIT NUMBERINGS ARE IN PLAY HERE, and mixing them is the easy mistake.
// PowerPC documents SPR and CR fields in IBM numbering (bit 0 = MSB), so
// XER[SO]=0, XER[OV]=1, XER[CA]=2, CR0[LT,GT,EQ,SO]=0..3. rlwinm's SH/MB/ME
// are IBM-numbered too. The SH4 flags we are moving them to/from are described
// in ordinary value numbering: sr_T is value bit 0, SR.Q is value bit 8, SR.M
// is value bit 9.
//
// To land value bit `b` in value bit 0:      rlwinm rD,rS,32-b,31,31
// To take value bit 0 back up to value bit b: rlwimi rD,rS,b,31-b,31-b
//
// So XER[CA] (IBM 2 = value 29) extracts with SH=3 and is written with SH=29,
// which is also what emit_cr0_bit_to_rarg0 does for CR0 (index k is IBM, so
// value bit 31-k, so SH = k+1). SR.Q (value 8) needs SH=24 / SH=8 + MB=ME=23 —
// NOT 9 and 8. Getting that pair wrong is silent: the value comes out right
// and only Q/T are corrupted.
//
// These are the sequences Evoca uses in seta-gx's SH2 recompiler, retargeted:
// there SR (T included) is pinned in a GPR so the T traffic is register-only,
// here sr_T is always memory-resident, which costs the lwz/stw pair below.
// ---------------------------------------------------------------------------

// XER[CA] = sr_T (optionally inverted, for the borrow-polarity ops). `t` is a
// scratch GPR that must not alias either operand of the following adde/subfe.
static void emit_T_to_ca(u32 t,bool invert)
{
	ppc_sh_load(t,reg_sr_T);
	if (invert)
		ppc_xori(t,t,1);
	ppc_rlwinmx(t,t,29,2,2,0);	// value bit 0 -> XER[CA]
	ppc_mtxer(t);
}

// sr_T = XER[CA] (optionally inverted). SH4 subc/negc define T as a BORROW,
// PPC defines CA as "no borrow", hence the inversion on those.
static void emit_ca_to_T(u32 t,bool invert)
{
	ppc_mfxer(t);
	ppc_rlwinmx(t,t,3,31,31,0);	// XER[CA] -> value bit 0
	if (invert)
		ppc_xori(t,t,1);
	ppc_sh_store(t,reg_sr_T);
}

// sr_T = XER[OV], set by the OE=1 form of add/subf (addv/subv).
static void emit_ov_to_T(u32 t)
{
	ppc_mfxer(t);
	ppc_rlwinmx(t,t,2,31,31,0);	// XER[OV] -> value bit 0
	ppc_sh_store(t,reg_sr_T);
}

// Resolve the destination of a carry op. rd is always an SH4 GPR here, so it
// is pinned unless it is r11. Safe for the single-instruction shapes below
// (adde/subfe/subfze/addo/subfo read both sources then write the dest), which
// is why the dest may alias a source.
static u32 carry_dest(shil_opcode* op)
{
	ppc_ireg d=GetIntReg(op->rd._reg);
	return (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg3;
}

static void carry_dest_store(shil_opcode* op,u32 d)
{
	if (d==(u32)ppc_rarg3)
		ppc_sh_store(ppc_rarg3,op->rd);
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

// rd = (signed) rs1 <cond> rs2  -> 0/1
static void emit_setcc_signed(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,true);
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
}

// rd = (unsigned) rs1 <cond> rs2 -> 0/1
static void emit_setcc_unsigned(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,false);
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
}


void ngen_CC_Finish(shil_opcode* op) 
{ 
	CC_pars.clear(); 
}
void DoStatic(u32 pc)
{
	// MUST be exactly 3 instructions (12 bytes): ngen_LinkBlock_Static_stub
	// computes its patch site as LR-12, i.e. the START of this sequence, and
	// overwrites it with a direct `b compiled_target`. ppc_li is variable
	// length (1 insn when the value fits s16 OR its low half is 0 — e.g. a
	// block at 0x8C010000), which would make the stub patch the instruction
	// BEFORE the sequence — in a BET_Cond end, that is the other exit path's
	// branch. So emit the fixed addis+ori pair unconditionally.
	ppc_addis(ppc_rarg0,0,pc>>16);
	ppc_ori(ppc_rarg0,ppc_rarg0,(u16)pc);
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
			//printf("COND %d\n",block->BlockType&1);
			//die("not supported");
			u32 reg;
			if (compile_state.has_jcond)
			{
				reg=ppc_djump;
			}
			else if (compile_state.t_in_rarg0)
			{
				// The block's last op was the compare that produced T, and
				// nothing has run since — it is still in rarg0. Skip reloading
				// the value we stored two instructions ago (see t_in_rarg0).
				reg=ppc_rarg0;
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

		// ---- DYN_IC preset: per-site monomorphic inline cache ----------------
		// Both dispatch paths below end in `bctr` through a table entry: ~9
		// instructions and two dependent loads, with an indirect branch the
		// 750CL cannot predict. But most dynamic exits are monomorphic — the
		// SH4 `JSR @Rn` to a fixed callee, and especially the JSR->RTS;NOP
		// trampoline idiom that dominates 3D inner loops. For those, the target
		// never changes, so the table lookup re-derives a constant every time.
		//
		// So we put a 4-instruction guard in front of it, self-patched with the
		// target this site actually took the first time it ran:
		//
		//     xoris  rarg1, djump, hi(pc)   ; rarg1 = djump ^ (hi<<16)
		//     cmplwi cr0, rarg1, lo(pc)     ; ==0 iff djump == pc  (32-bit cmp,
		//     bne    generic                ;  two insns, no immediate load)
		//     b      target                 ; direct, statically predicted
		//   generic:
		//     <bcache / cache[] path, unchanged>
		//
		// Until it is patched, all four slots branch to `generic`, so a site is
		// always correct, just slow. Slot 0 is `bl` to the fill stub, which
		// compiles the target, rewrites the four words, and jumps into it.
		//
		// Staleness: the baked `b target` is exactly as safe as the direct `b`
		// DoStatic() patches in, and for the same reason — every invalidation
		// path is a FULL cache clear (see rdv_BlockCheckFail in driver.cpp),
		// which destroys these sites along with the code they point at.
		//
		// Monomorphic only: a site is patched once and never re-patched, so a
		// polymorphic site (typically BET_DynamicRet — one callee returning to
		// many callers) settles into paying 3 extra ALU/branch ops in front of
		// the normal lookup. That is why mode 1 leaves RTS alone; mode 2 opts
		// it in for A/B.
		{
			const int ic = get_dyn_ic_preset();
			const bool ic_here = ngen_LinkBlock_Dynamic_IC_stub &&
			                     ((ic >= 2) ||
			                      (ic == 1 && block->BlockType != BET_DynamicRet));
			if (ic_here)
			{
				// 4 words. `generic` is site+16, so each unpatched slot is a
				// plain forward branch onto it (LI is in instructions).
				ppc_call(ngen_LinkBlock_Dynamic_IC_stub);	// +0  -> xoris
				ppc_bx(3,0,0);					// +4  -> cmplwi
				ppc_bx(2,0,0);					// +8  -> bne generic
				ppc_bx(1,0,0);					// +12 -> b target
			}
		}
		// generic path starts here (the IC's fall-through target)

		if (get_bcache_preset())
		{
			// ---- BCACHE preset: flat single-cache-line dispatch ----
			// bm_bcache[] holds {addr, code} pairs value-mirrored from cache[]
			// (blockmanager.cpp), so the hit path touches ONE data cache line
			// (the 8-byte entry) instead of two (cache[] pointer + the heap
			// DynarecBlock it points at) and drops the lookups++ read-modify-
			// write. Policy note: the flat path does not feed the replacement
			// counters, so a bucket-colliding block steals the slot after +3
			// slow-path hits — the two then alternate, each getting the fast
			// path most of the time, instead of one paying bm_GetCode forever.
			//
			//   off   = ((addr>>2)&16383)*8 = (addr<<1) & 0x1FFF8  (one rlwinm)
			//   entry = &bm_bcache[off/8];
			//   if (entry->addr == addr) goto entry->code;
			//   else goto loop_no_update;             // full bm_GetCode
			//
			// Invariant (blockmanager.h): addr != 0xFFFFFFFF => code valid, so
			// no separate code==0 test. addr stays in rarg0 for the miss path.
			verify(sizeof(BlockCacheFlatEntry)==8 && BM_BLOCKLIST_COUNT==16384);

			ppc_rlwinmx(ppc_rarg1,ppc_rarg0,1,15,28,0);	// rarg1 = (addr<<1) & 0x1FFF8
			u32 lo=ppc_addr_high(ppc_rarg2,(void*)&bm_bcache[0]);
			ppc_addx(ppc_rarg2,ppc_rarg2,ppc_rarg1,0,0);	// rarg2 = &bm_bcache[idx] - lo
			ppc_lwz(ppc_rarg3,ppc_rarg2,lo);		// entry.addr
			ppc_cmp(ppc_cr0,ppc_rarg3,ppc_rarg0,0);		// == addr ?
			ppc_label* miss=ppc_CreateLabel();
			ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne miss
			ppc_lwz(ppc_rarg3,ppc_rarg2,lo+4);		// entry.code
			ppc_mtctr(ppc_rarg3);
			ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);		// bctr -> cached code
			miss->MarkLabel();
		}
		else
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

			// hit: lookups++ (keeps bm_GetCode's cache-replacement policy fed)
			// NOTE: must NOT use r0 as the addi target/source -- `addi rD,r0,imm`
			// treats rA=r0 as literal 0 (it's how ppc_li is built), so
			// `addi r0,r0,1` would emit `r0 = 0 + 1 = 1`, never incrementing the
			// loaded value. Use rarg1 (dead after the cache[idx] load above).
			ppc_lwz(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));
			ppc_addi(ppc_rarg1,ppc_rarg1,1);
			ppc_stw(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));

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
		// Gated like the BET_StaticJump trace below: printf goes to /ndclog.txt
		// on the SD card, and JIT_NEWOPS makes BET_StaticIntr common (every
		// ldc/ldc.l to SR now ends a block this way instead of falling back).
		if (get_debug_loop() == 1)
		{
			printf("BET: Interrupt !\n");
		}
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

// f14:f29 — fr[0..15] only. xf[] is NOT pinned: fr[16] already claims all 16
// of the PPC EABI's callee-saved FPRs available after reserving f0 (scratch)
// and f1-f13 (call-arg/return volatiles used throughout this file); only
// f30/f31 are left, nowhere near enough for another 16-wide bank. See the
// FPU_PIN comment block above reg_flush_all_fpu() for the full rationale.
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

// ============================================================================
// FPU_PIN preset (default OFF) — pins fr[0..15] to PPC f14..f29 for the whole
// session, the same scheme as STATIC_GPR_ALLOC but for the FPU file. xf[]
// stays memory-resident (see GetFloatReg's comment).
//
// Unlike GPR pinning, this is a RUNTIME preset (get_fpu_pin_preset()), not a
// compile-time #define: it's new and unproven, so it needs to be A/B-able on
// a shipped binary the way fastmem/bcache are, not just via recompile.
//
// Bracket points (mirrors reg_flush_all/reg_reload_all's list above, plus one
// FPU-specific case):
//   * shop_ifb              — interpreter handler reads/writes arbitrary FRs
//   * canonical fallback    — any un-natived shil op may touch fr[]/xf[]
//     directly (memory), including through ppc_sh_addr's &Sh4cntx.fr[n]
//     pointers handed to CPT_ptr canonical params — flushing here keeps
//     those pointers valid
//   * shop_sync_fpscr       — UpdateFPSCR() -> ChangeFP() swaps
//     fr_hex[i]<->xf_hex[i] IN MEMORY on an FPSCR.FR toggle (sh4_registers.cpp);
//     pinned fr must be flushed before the call (so the swap sees current
//     values) and reloaded after (fr[] now holds the new front bank)
//
// NOT bracketed: BET_*Intr (UpdateINTC -> Do_Exception). Real SH4 interrupts
// bank-swap GPRs (SR.RB) but never fr[]/xf[] — only an explicit FPSCR.FR
// write does that, which is shop_sync_fpscr's job above.
//
// The memory-bounce fix for shop_readm/writem/mov64 (which touch fr[] bit
// patterns through the generic int ppc_sh_load/ppc_sh_store, not these FPU
// helpers) lives centrally in those two functions — see their FPU_PIN
// comments earlier in this file.
// ============================================================================

void reg_flush_all_fpu()
{
	if (!get_fpu_pin_preset())
		return;
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg rf=GetFloatReg(i);
		ppc_stfs(rf,ppc_contex,Sh4cntx.offset(i));
	}
}
void reg_reload_all_fpu()
{
	if (!get_fpu_pin_preset())
		return;
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg rf=GetFloatReg(i);
		ppc_lfs(rf,ppc_contex,Sh4cntx.offset(i));
	}
}

// ============================================================================
// IFB_FLUSH preset + IFB_PROBE — narrowing the shop_ifb register bracket
// ============================================================================
// shop_ifb hands ONE 16-bit SH4 opcode to its interpreter handler
// (OpDesc[op]->oph). Because that handler could in principle read or write any
// guest register through Sh4cntx, the emitter has always bracketed the call
// with a FULL register-file spill: reg_flush_all() (15 stw) + reg_reload_all()
// (15 lwz), plus reg_flush_all_fpu()/reg_reload_all_fpu() (16 stfs + 16 lfs)
// when FPU_PIN is on. That is 30 memory ops — 62 with FPU_PIN — around an
// opcode that in practice touches two registers.
//
// Same shape as the fschg/sync_fpscr bug (a C call-out doing a whole-file
// spill for almost nothing) that was worth +12-13% on Wii, and it lands on a
// hot set. Only 28 opcodes reach ifb on this build, but they include div1,
// addc/subc/addv/subv, negc, rotcl/rotcr, cmp/str, xtrct, swap.b, mac.l/mac.w
// and tas.b. div1 is the expensive one: the SH4 has no divide instruction, so
// every 32-bit software divide is ~32x div1 (+ rotcl), i.e. on the order of
// 2000 memory ops of pure bracket per division under the full-spill code.
//
// What makes a narrower bracket safe: of the whole SH4 context, ONLY r0..r15
// (minus r11) and fr[0..15] are ever register-resident. sr, gbr, vbr, mac,
// fpul, fpscr, pr and the banked copies are always memory-authoritative, so a
// handler touching only those needs no bracket at all. The bracket therefore
// has to cover exactly the GPRs (and FPRs) that handler reads or writes.
//
// This is a CLOSED ALLOW-LIST, not a mask-derived guess: an opcode is narrowed
// only if it is named below with its exact register set. Everything else keeps
// the full bracket — trapa, sleep, illegal instructions, and every FPU op that
// fell back because FPSCR.PR selected double precision. Default-deny: an
// unlisted opcode is slow, never wrong, and a future opcode reaching ifb
// inherits the safe path automatically.
//
// TWO INDEPENDENT AXES. The bracket is not one decision but two — "which GPRs
// does this handler touch?" and "does it touch the float file at all?" — and
// they do not correlate. Collapsing them into a single "spill everything"
// sentinel (the first version of this code) made three groups overpay:
//
//   lds Rn,FPSCR      GPRs: just Rn      float: YES (ChangeFP swaps banks)
//   ldc Rn,SR         GPRs: ALL (bank)   float: no  (SR has no FP bank bit;
//                                                    SR.FD disables, not swaps)
//   fcnvds / fcnvsd   GPRs: none         float: YES
//
// An [IFB] probe run measured `lds Rn,FPSCR` at 265k/s during BIOS boot — 98.5%
// of all fallbacks in that phase — paying a 30-op GPR spill for a handler that
// writes no GPR at all. So ifb_bracket() returns the two axes separately.
//
// Entries are matched on the (mask, rez) PAIR from the opcode table rather
// than on a hand-written mask, so if sh4_opcode_list.cpp is ever retuned the
// pair simply stops matching and that opcode falls back to the full bracket.
//
// Exception caveat: a narrowed handler that raised an SH4 exception mid-op
// would bank-swap memory while our un-flushed pinned regs held the old values.
// That is why every opcode that can deliberately raise (trapa/sleep/illegal,
// the SR/FPSCR writers) stays on the full path. The remainder only fault the
// same way an ordinary shop_readm/shop_writem can, which this backend already
// assumes cannot touch context GPRs (see reg_flush_all()'s comment above).
// ============================================================================

// Opcode-table mask constants (private copies of the #defines in
// sh4_opcode_list.cpp, which are not exported in the header).
static const u32 IFB_M_NONE = 0xFFFF;
static const u32 IFB_M_N    = 0xF0FF;
static const u32 IFB_M_NM   = 0xF00F;
static const u32 IFB_M_IMM8 = 0xFF00;

// Every SH4 GPR — the mask a bank-swapping handler needs. r11 is included and
// simply drops out in reg_flush_mask(), since GetIntReg() never pins it.
static const u32 IFB_GPR_ALL = 0xFFFFu;

// What one ifb call-out has to spill, on the two independent axes.
struct IfbBracket
{
	u32  gprs;	// bitmask over SH4 r0..r15 (IFB_GPR_ALL = all of them)
	bool fpu;	// also spill fr[0..15] (only costs anything when FPU_PIN is on)
};

// The GPR mask is the UNION of the handler's read and write sets: flushing a
// register it only reads is required (it must see a live value), and reloading
// one it never wrote just re-reads what we stored, so one mask drives both
// directions.
static IfbBracket ifb_bracket(u16 sh4op)
{
	const sh4_opcodelistentry* d = OpDesc[sh4op];
	const u32 Rn = 1u << ((sh4op >> 8) & 0xF);
	const u32 Rm = 1u << ((sh4op >> 4) & 0xF);
	const u32 R0 = 1u << 0;

	#define IFB_IS(mm,rr) (d->mask==(mm) && d->rez==(rr))
	#define IFB_B(g,f)    { IfbBracket b={(g),(f)}; return b; }

	// --- touches no GPR and no float -------------------------------------
	if (IFB_IS(IFB_M_NONE,0x0028)) IFB_B(0,false)	// clrmac      -> MACH/MACL
	if (IFB_IS(IFB_M_NONE,0x0038)) IFB_B(0,false)	// ldtlb       -> PTEH/PTEL

	// --- Rn only ----------------------------------------------------------
	if (IFB_IS(IFB_M_N,0x0002)) IFB_B(Rn,false)	// stc SR,Rn      (reads sr only)
	if (IFB_IS(IFB_M_N,0x4003)) IFB_B(Rn,false)	// stc.l SR,@-Rn  (reads sr only)
	if (IFB_IS(IFB_M_N,0x4024)) IFB_B(Rn,false)	// rotcl Rn
	if (IFB_IS(IFB_M_N,0x4025)) IFB_B(Rn,false)	// rotcr Rn
	if (IFB_IS(IFB_M_N,0x401B)) IFB_B(Rn,false)	// tas.b @Rn

	// --- Rn and Rm --------------------------------------------------------
	if (IFB_IS(IFB_M_NM,0x000F)) IFB_B(Rn|Rm,false)	// mac.l @Rm+,@Rn+
	if (IFB_IS(IFB_M_NM,0x400F)) IFB_B(Rn|Rm,false)	// mac.w @Rm+,@Rn+
	if (IFB_IS(IFB_M_NM,0x200C)) IFB_B(Rn|Rm,false)	// cmp/str Rm,Rn
	if (IFB_IS(IFB_M_NM,0x200D)) IFB_B(Rn|Rm,false)	// xtrct Rm,Rn
	if (IFB_IS(IFB_M_NM,0x3004)) IFB_B(Rn|Rm,false)	// div1 Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300A)) IFB_B(Rn|Rm,false)	// subc Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300B)) IFB_B(Rn|Rm,false)	// subv Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300E)) IFB_B(Rn|Rm,false)	// addc Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300F)) IFB_B(Rn|Rm,false)	// addv Rm,Rn
	if (IFB_IS(IFB_M_NM,0x6008)) IFB_B(Rn|Rm,false)	// swap.b Rm,Rn
	if (IFB_IS(IFB_M_NM,0x600A)) IFB_B(Rn|Rm,false)	// negc Rm,Rn

	// --- implicit R0 (GBR-relative byte ops) ------------------------------
	if (IFB_IS(IFB_M_IMM8,0xCC00)) IFB_B(R0,false)	// tst.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCD00)) IFB_B(R0,false)	// and.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCE00)) IFB_B(R0,false)	// xor.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCF00)) IFB_B(R0,false)	// or.b  #imm,@(R0,GBR)

	// --- FPSCR writers: Rn only, but the float file must be spilled -------
	// UpdateFPSCR -> ChangeFP() swaps fr_hex[i]<->xf_hex[i] IN MEMORY on an
	// FPSCR.FR toggle, so pinned fr[] must be written out before the call and
	// re-read after. Neither handler writes any GPR — it only sources the new
	// FPSCR value from Rn (post-incrementing it in the .l form).
	if (IFB_IS(IFB_M_N,0x406A)) IFB_B(Rn,true)	// lds Rn,FPSCR
	if (IFB_IS(IFB_M_N,0x4066)) IFB_B(Rn,true)	// lds.l @Rn+,FPSCR

	// --- SR writers: full GPR bank, but the float file is untouched -------
	// UpdateSR() bank-swaps r0..r7 <-> r0_bank..r7_bank in memory on an SR.RB
	// toggle, so every pinned GPR has to be coherent across the call. SR has no
	// FP bank bit — SR.FD disables the FPU, it does not swap fr[]/xf[] — so the
	// 32 float ops the old single-sentinel path emitted here were pure waste.
	if (IFB_IS(IFB_M_N,0x400E)) IFB_B(IFB_GPR_ALL,false)	// ldc Rn,SR
	if (IFB_IS(IFB_M_N,0x4007)) IFB_B(IFB_GPR_ALL,false)	// ldc.l @Rn+,SR

	// --- double-precision converts: float only, no GPR --------------------
	if (IFB_IS(IFB_M_N,0xF0BD)) IFB_B(0,true)	// fcnvds DRn,FPUL
	if (IFB_IS(IFB_M_N,0xF0AD)) IFB_B(0,true)	// fcnvsd FPUL,DRn

	#undef IFB_B
	#undef IFB_IS

	// Not on the list (trapa, sleep, illegal, double-precision FPU fallbacks):
	// the legacy everything-spill.
	IfbBracket full={IFB_GPR_ALL,true};
	return full;
}

// Memory ops one bracket costs, for the probe's reporting only.
static u32 ifb_bracket_memops(const IfbBracket& b)
{
	u32 n=0;
	for (u32 i=0;i<16;i++)
		if ((b.gprs&(1u<<i)) && GetIntReg(reg_r0+i)!=ppc_rinvalid)
			n+=2;					// one stw + one lwz
	if (b.fpu && get_fpu_pin_preset())
		n+=32;						// 16 stfs + 16 lfs
	return n;
}

// Flush/reload only the GPRs named in `gprs`. r11 is never pinned (GetIntReg
// returns invalid — the emitter keeps it as a volatile temp window), so it is
// already memory-coherent and drops out of the loop on its own.
static void reg_flush_mask(u32 gprs)
{
#if STATIC_GPR_ALLOC
	for (u32 i=0;i<16;i++)
	{
		if (!(gprs&(1u<<i)))
			continue;
		ppc_ireg ri=GetIntReg(reg_r0+i);
		if (ri!=ppc_rinvalid)
			ppc_stw(ri,ppc_contex,Sh4cntx.offset(reg_r0+i));
	}
#endif
}
static void reg_reload_mask(u32 gprs)
{
#if STATIC_GPR_ALLOC
	for (u32 i=0;i<16;i++)
	{
		if (!(gprs&(1u<<i)))
			continue;
		ppc_ireg ri=GetIntReg(reg_r0+i);
		if (ri!=ppc_rinvalid)
			ppc_lwz(ri,ppc_contex,Sh4cntx.offset(reg_r0+i));
	}
#endif
}

// ---------------------------------------------------------------------------
// IFB_PROBE — per-opcode execution counters
//
// Answers "is shop_ifb hot enough to be worth narrowing?" before committing to
// the A/B. Sites are keyed on the OpDesc[] ENTRY, not the raw opcode word, so
// all n/m variants of e.g. div1 collapse into one row and the table stays tiny
// (233 table entries exist in total; far fewer than 64 ever reach ifb).
//
// The counters live outside the code cache, so they survive recSh4_ClearCache()
// and keep accumulating across recompiles. ifb_probe_dump() is called once a
// second from the SPG stats block and resets them, so the numbers it prints are
// per-second rates.
// ---------------------------------------------------------------------------
#define IFB_SITE_MAX 64
struct IfbSite
{
	const sh4_opcodelistentry* desc;
	u32 hits;
	IfbBracket br;	// cached ifb_bracket() result, for the dump's cost columns
};
static IfbSite s_ifb_sites[IFB_SITE_MAX];
static u32 s_ifb_site_n = 0;
static u32 s_ifb_overflow = 0;

// Compile-time (not run-time) lookup: returns the counter this site should
// bump, or 0 if the registry is full.
static u32* ifb_probe_slot(u16 sh4op)
{
	const sh4_opcodelistentry* d = OpDesc[sh4op];

	for (u32 i=0;i<s_ifb_site_n;i++)
		if (s_ifb_sites[i].desc==d)
			return &s_ifb_sites[i].hits;

	if (s_ifb_site_n>=IFB_SITE_MAX)
	{
		s_ifb_overflow++;
		return 0;
	}

	IfbSite& s=s_ifb_sites[s_ifb_site_n];
	s.desc=d;
	s.hits=0;
	s.br=ifb_bracket(sh4op);
	return &s_ifb_sites[s_ifb_site_n++].hits;
}

extern "C" void ifb_probe_dump(double seconds)
{
	if (!get_ifb_probe_preset() || s_ifb_site_n==0 || seconds<=0.0)
		return;

	// Order by hit count, descending. Selection sort over <=64 entries once a
	// second is free, and it avoids dragging qsort into the build.
	u32 idx[IFB_SITE_MAX];
	u32 n=0;
	for (u32 i=0;i<s_ifb_site_n;i++)
		if (s_ifb_sites[i].hits)
			idx[n++]=i;

	if (n==0)
		return;

	for (u32 a=0;a<n;a++)
	{
		u32 best=a;
		for (u32 b=a+1;b<n;b++)
			if (s_ifb_sites[idx[b]].hits>s_ifb_sites[idx[best]].hits)
				best=b;
		u32 t=idx[a]; idx[a]=idx[best]; idx[best]=t;
	}

	// Cost of the legacy everything-spill, for the comparison column.
	IfbBracket full_br={IFB_GPR_ALL,true};
	const u32 full_cost = ifb_bracket_memops(full_br);

	u32 total=0, narrowed=0;
	double cost_full=0.0, cost_sel=0.0;
	for (u32 a=0;a<n;a++)
	{
		const IfbSite& s=s_ifb_sites[idx[a]];
		const u32 cost=ifb_bracket_memops(s.br);
		total += s.hits;
		cost_full += (double)s.hits*full_cost;
		cost_sel  += (double)s.hits*cost;
		if (cost<full_cost)
			narrowed += s.hits;
	}

	// Header states the preset actually in force. The old header did not, so a
	// log could not tell an ifb_flush=on run from an off one — which made the
	// first A/B unverifiable. Note both presets are read at CODEGEN time, so
	// this reflects what the compiled blocks were built with only if the preset
	// was set before the game launched.
	printf("[IFB] flush=%s fpu_pin=%s  %.2fs  total %u (%.0f/s)  narrowable %u (%.1f%%)  bracket memops/s: full %.0f -> sel %.0f (-%.1f%%)\n",
		get_ifb_flush_preset()?"ON":"OFF",
		get_fpu_pin_preset()?"ON":"OFF",
		seconds, total, total/seconds,
		narrowed, total?(narrowed*100.0/total):0.0,
		cost_full/seconds, cost_sel/seconds,
		cost_full>0.0?((cost_full-cost_sel)*100.0/cost_full):0.0);

	for (u32 a=0;a<n && a<12;a++)
	{
		const IfbSite& s=s_ifb_sites[idx[a]];
		u32 pop=0;
		for (u32 b=0;b<16;b++)
			if ((s.br.gprs&(1u<<b)) && GetIntReg(reg_r0+b)!=ppc_rinvalid)
				pop++;
		printf("[IFB]   %-34s %8u %8.0f/s  %2u gpr%s %3u ops (full %u)\n",
			s.desc->diss, s.hits, s.hits/seconds,
			pop, s.br.fpu?"+fpu":"    ",
			ifb_bracket_memops(s.br), full_cost);
	}

	if (s_ifb_overflow)
		printf("[IFB]   (%u distinct opcodes did not fit the %u-entry registry)\n",
			s_ifb_overflow, (u32)IFB_SITE_MAX);

	for (u32 i=0;i<s_ifb_site_n;i++)
		s_ifb_sites[i].hits=0;

	fflush(stdout);
}

// ---------------------------------------------------------------------------
// JIT_HOTBLOCKS probe (wii/main.cpp preset)
//
// Counts executions per compiled block, and once a second prints the hottest
// ones with their codegen density — PPC bytes emitted per SH4 opcode — then
// dumps the SH4 source and the generated PPC of the top block so the real
// hot-path codegen can be READ instead of guessed at.
//
// Costs 4 instructions at each block entry, so it is a diagnostic: leave it off
// for timing runs. The table is allocated lazily, so nothing costs MEM1 while
// the preset is off, and it is dropped whenever the code cache is flushed —
// every recorded ppc_start points into the cache that just went away.
//
// OBSERVER EFFECT: the counters live in their OWN dense u32 array, not inside
// the descriptor struct. ChuChu runs 5-10M block entries/s, so the emitted
// load/increment/store is touched millions of times a second; with the counter
// embedded in a 24-byte struct the working set was HOT_MAX*24 = 384 KB, past
// Broadway's 256 KB L2, and a good share of those accesses went to main memory.
// Split out, the hot array is HOT_MAX*4 = 64 KB and eight counters share a
// 32-byte line, so consecutively-compiled blocks (which tend to run together)
// share lines. The descriptors stay fat but are only touched at compile time
// and once a second by the dump. This matters because SPEED% in a hotblocks-on
// log is only worth comparing if the probe's own cost is small and stable.
// ---------------------------------------------------------------------------
extern "C" int get_hotblocks_preset();

struct HotBlock
{
	u32 sh4_pc;
	u32 sh4_size;	// bytes of SH4 source the block consumed
	u32 sh4_ops;
	u8* ppc_start;
	u32 ppc_size;	// bytes of HOT PPC body (filled in at the end of ngen_Compile)
	u32 ppc_cold;	// bytes of out-of-line mem slow paths FlushCold() appended
};

// ChuChu compiles ~10k blocks between cache clears; at 2048 the first run left
// 7894 untracked, which biases the census toward whatever compiled first.
// Descriptors 16384 * 20 B = 320 KB + counters 64 KB, allocated only when the
// preset is on.
#define HOT_MAX 16384
static HotBlock* s_hb = 0;
static u32* s_hb_hits = 0;	// dense counter array, indexed in step with s_hb
static u32 s_hb_n = 0;
static u32 s_hb_overflow = 0;
// Blocks already disassembled this session, so each distinct hot shape is
// dumped exactly once no matter which scene it turns hot in. Deliberately NOT
// cleared by hotblocks_reset(): a cache clear recompiles the same pc, and
// re-dumping identical code would just bury the new shapes.
#define HOT_DUMP_MAX 16
static u32 s_hb_dumped[HOT_DUMP_MAX];
static u32 s_hb_dumped_n = 0;

// Worst B/op dumped so far, and how many #1-by-hits dumps have been spent.
// See the selection logic in hotblocks_dump() for why both exist.
#define HOT_TOP_DUMPS 4
static u32    s_hb_top_dumps = 0;
static double s_hb_worst_bop = 0.0;

// Called from recSh4_ClearCache (dc/sh4/rec_v2/driver.cpp): the compiled code
// these entries describe no longer exists.
extern "C" void hotblocks_reset()
{
	s_hb_n = 0;
	s_hb_overflow = 0;
}

// Compile time: claim a slot for this block. Returns its index, or -1 if
// unavailable — the caller needs the index to reach both the descriptor and
// the counter, which live in separate arrays.
static s32 hotblocks_slot(DecodedBlock* block)
{
	if (!get_hotblocks_preset())
		return -1;

	if (!s_hb)
	{
		s_hb = (HotBlock*)malloc(sizeof(HotBlock)*HOT_MAX);
		s_hb_hits = (u32*)malloc(sizeof(u32)*HOT_MAX);
		if (!s_hb || !s_hb_hits)
		{
			free(s_hb);      s_hb = 0;
			free(s_hb_hits); s_hb_hits = 0;
			return -1;
		}
	}

	if (s_hb_n >= HOT_MAX)
	{
		s_hb_overflow++;
		return -1;
	}

	const s32 idx = (s32)s_hb_n++;
	HotBlock* h = &s_hb[idx];
	h->sh4_pc    = block->start;
	h->sh4_size  = block->sh4_code_size;
	h->sh4_ops   = block->opcodes;
	h->ppc_start = 0;
	h->ppc_size  = 0;
	h->ppc_cold  = 0;
	s_hb_hits[idx] = 0;
	return idx;
}

static void hotblocks_disasm(const HotBlock& h)
{
	printf("[HOT] --- disasm pc=%08X : %u SH4 ops / %u B  ->  %u B hot + %u B cold ---\n",
		h.sh4_pc, h.sh4_ops, h.sh4_size, h.ppc_size, h.ppc_cold);

	printf("[HOT] SH4:\n");
	u32 n = h.sh4_size/2;
	if (n > 64) n = 64;
	for (u32 i=0;i<n;i++)
	{
		u32 a = h.sh4_pc + i*2;
		u16 o = ReadMem16(a);
		printf("[HOT]   %08X: %04X  %s\n", a, o, OpDesc[o]->diss);
	}
	if (h.sh4_size/2 > n)
		printf("[HOT]   ... (%u more)\n", h.sh4_size/2 - n);

	printf("[HOT] PPC hot body (%u B):\n", h.ppc_size);
	if (h.ppc_start)
	{
		const u32* w = (const u32*)h.ppc_start;
		u32 wn = h.ppc_size/4;
		if (wn > 160) wn = 160;
		for (u32 i=0;i<wn;i+=4)
		{
			printf("[HOT]   +%04X:", i*4);
			for (u32 j=0;j<4 && (i+j)<wn;j++)
				printf(" %08X", w[i+j]);
			printf("\n");
		}
		if (h.ppc_size/4 > wn)
			printf("[HOT]   ... (%u more words)\n", h.ppc_size/4 - wn);
	}
}

// Disassemble this block unless its pc has already been dumped. Returns true
// if it actually emitted a dump.
static bool hotblocks_dump_once(const HotBlock& h)
{
	if (s_hb_dumped_n >= HOT_DUMP_MAX)
		return false;
	for (u32 i=0;i<s_hb_dumped_n;i++)
		if (s_hb_dumped[i]==h.sh4_pc)
			return false;

	s_hb_dumped[s_hb_dumped_n++] = h.sh4_pc;
	hotblocks_disasm(h);
	return true;
}

extern "C" void hotblocks_dump(double seconds)
{
	if (!get_hotblocks_preset() || !s_hb || s_hb_n==0 || seconds<=0.0)
		return;

	// Totals first, so a block's share of all executed blocks is visible.
	double all_hits = 0.0, all_ppc = 0.0, all_ops = 0.0;
	for (u32 i=0;i<s_hb_n;i++)
	{
		all_hits += s_hb_hits[i];
		all_ppc  += (double)s_hb_hits[i] * s_hb[i].ppc_size;
		all_ops  += (double)s_hb_hits[i] * s_hb[i].sh4_ops;
	}

	if (all_hits <= 0.0)
		return;

	printf("[HOT] %.2fs  %u blocks tracked, %.0f entries/s, %.1f PPC B per SH4 op (execution-weighted)\n",
		seconds, s_hb_n, all_hits/seconds, all_ops>0.0?(all_ppc/all_ops):0.0);

	// Top 8 by hits. Repeated max-scan — 8 passes over <=HOT_MAX entries once a
	// second is free and avoids dragging a sort in.
	u32 top[8];
	u32 tn = 0;
	for (u32 a=0;a<8;a++)
	{
		u32 best = 0xFFFFFFFF;
		for (u32 i=0;i<s_hb_n;i++)
		{
			if (!s_hb_hits[i])
				continue;
			bool taken = false;
			for (u32 k=0;k<tn;k++)
				if (top[k]==i) { taken=true; break; }
			if (taken)
				continue;
			if (best==0xFFFFFFFF || s_hb_hits[i] > s_hb_hits[best])
				best = i;
		}
		if (best==0xFFFFFFFF)
			break;
		top[tn++] = best;
	}

	for (u32 a=0;a<tn;a++)
	{
		const HotBlock& h = s_hb[top[a]];
		const u32 hits = s_hb_hits[top[a]];
		printf("[HOT]   %u: pc=%08X %8u (%7.0f/s) %5.1f%%  sh4 %3u ops  hot %5u B  %5.1f B/op  cold %5u B\n",
			a+1, h.sh4_pc, hits, hits/seconds,
			hits*100.0/all_hits,
			h.sh4_ops, h.ppc_size,
			h.sh4_ops?((double)h.ppc_size/h.sh4_ops):0.0,
			h.ppc_cold);
	}

	if (s_hb_overflow)
		printf("[HOT]   (%u blocks did not fit the %u-entry table)\n", s_hb_overflow, (u32)HOT_MAX);

	// Full disassembly of the #1 block and of the worst codegen density among
	// the hot ones — usually different blocks, since #1 tends to be a tight
	// well-compiled loop while whatever burns the most PPC per SH4 op hides
	// further down (ChuChu's 8C1074E6: 4 ops -> 288 B, 72 B/op, at 8.7% of
	// entries in the HEAVY scene only).
	//
	// Dump each distinct pc once, rather than spending a fixed budget. A
	// "2 dumps per session" budget was wrong: it spent itself on the first two
	// [HOT] lines after boot — menus — so a block that only turns hot later in
	// gameplay could never be dumped at all, which is exactly why 8C1074E6 kept
	// being missed despite the preset being on the whole time.
	if (tn)
	{
		// #1 by hits is contextual only, so it gets a small fixed budget and
		// cannot starve the outlier hunt below.
		if (s_hb_top_dumps < HOT_TOP_DUMPS && hotblocks_dump_once(s_hb[top[0]]))
			s_hb_top_dumps++;

		u32 worst = top[0];
		double worst_bop = 0.0;
		for (u32 a=0;a<tn;a++)
		{
			const HotBlock& h = s_hb[top[a]];
			const double d = h.sh4_ops ? (double)h.ppc_size/h.sh4_ops : 0.0;
			if (d > worst_bop)
			{
				worst_bop = d;
				worst = top[a];
			}
		}

		// Only ever dump a NEW worst. Dumping every distinct worst-of-the-second
		// spent the whole budget on ordinary blocks from the boot/menu scenes,
		// so the actual outlier — 8C1074E6 at 72 B/op, which only turns hot in
		// the heavy scene — was never reached. Requiring each dump to beat the
		// previous worst converges on the outlier instead, and is self-limiting
		// because B/op has to strictly increase each time.
		if (worst_bop > s_hb_worst_bop)
		{
			s_hb_worst_bop = worst_bop;
			hotblocks_dump_once(s_hb[worst]);
		}
	}

	for (u32 i=0;i<s_hb_n;i++)
		s_hb_hits[i] = 0;

	fflush(stdout);
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
	u8         kind;    // 0=read scalar, 1=read pair(64), 2=write scalar, 3=write pair
	u8         sz;      // 1/2/4 (scalar)
	u8         rdreg;   // read: destination reg (rrv0 or pinned)
	u8         datareg; // write: data reg (rarg1 or pinned)
	u8         areg;    // address source reg (rarg0 or pinned)
	// FPU_PIN Phase B: when the read dest / write source is a pinned FR the
	// fast path uses lfs/stfs straight to/from the FPR (no GPR bounce). The
	// slow C path still deals in ints, so it bounces through the FR's own
	// context slot: read = ReadMem->rrvN->stw fofs->lfs fdreg; write =
	// stfs fdreg->fofs->lwz->WriteMem. fdreg==0xFF marks the plain int path
	// (must be set on EVERY registration — s_cold is reused, stale otherwise).
	// For pairs, the second FR is fdreg+1 and its slot is fofs+4 (fr[] is
	// contiguous and GetFloatReg maps fr[n]->f14+n).
	u8         fdreg;   // pinned FPR for the float bounce, or 0xFF = int path
	u16        fofs;    // context byte offset of the FR's memory slot
	void*      fuct;    // slow C function (ReadMem*/WriteMem*); 0 -> default by sz
};
// Sized so it cannot overflow within one block: ngen_Compile bails when
// emit_FreeSpace() < 16KB, so a block emits < 4096 PPC insns; each mem op's
// fast path is >=~8 insns, capping mem ops well under 1024.
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
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float dest (sz==4). Bounce rrv0 -> pinned FPR via its
				// context slot (no direct GPR->FPR move on PPC750).
				ppc_stw(ppc_rrv0,ppc_contex,c.fofs);
				ppc_lfs(c.fdreg,ppc_contex,c.fofs);
			}
			else if (c.sz==1)      ppc_extsbx(c.rdreg,ppc_rrv0,0);
			else if (c.sz==2) ppc_extshx(c.rdreg,ppc_rrv0,0);
			else if (c.rdreg!=ppc_rrv0) ppc_ori(c.rdreg,ppc_rrv0,0);
		}
		else if (c.kind==1)              // --- read pair (64): ReadMem64 -> rrv0:rrv1 ---
		{
			void* f=c.fuct? c.fuct : (void*)ReadMem64;
			ppc_call(f);
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float pair. rrv0=word[addr]->fdreg, rrv1=word[addr+4]
				// ->fdreg+1, each bounced through its own context slot.
				ppc_stw(ppc_rrv0,ppc_contex,c.fofs);
				ppc_lfs(c.fdreg,ppc_contex,c.fofs);
				ppc_stw(ppc_rrv1,ppc_contex,c.fofs+4);
				ppc_lfs(c.fdreg+1,ppc_contex,c.fofs+4);
			}
		}
		else if (c.kind==2)              // --- write scalar: WriteMem(addr,data) ---
		{
			if (c.areg!=ppc_rarg0)    ppc_ori(ppc_rarg0,c.areg,0);
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float source (sz==4). Bounce pinned FPR -> rarg1 via
				// its context slot, then the normal WriteMem32.
				ppc_stfs(c.fdreg,ppc_contex,c.fofs);
				ppc_lwz(ppc_rarg1,ppc_contex,c.fofs);
				ppc_call(&WriteMem32);
			}
			else
			{
				if (c.datareg!=ppc_rarg1) ppc_ori(ppc_rarg1,c.datareg,0);
				if (c.sz==1)      { ppc_andi(ppc_rarg1,ppc_rarg1,0xFF);   ppc_call(&WriteMem8); }
				else if (c.sz==2) { ppc_andi(ppc_rarg1,ppc_rarg1,0xFFFF); ppc_call(&WriteMem16); }
				else                ppc_call(&WriteMem32);
			}
		}
		else                             // --- write pair (64): WriteMem64 ---
		{
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float pair. fdreg=high->rarg2, fdreg+1=low->rarg3
				// (the WriteMem64(u32,u64) EABI regs), each via its context slot.
				ppc_stfs(c.fdreg,ppc_contex,c.fofs);
				ppc_lwz(ppc_rarg2,ppc_contex,c.fofs);
				ppc_stfs(c.fdreg+1,ppc_contex,c.fofs+4);
				ppc_lwz(ppc_rarg3,ppc_contex,c.fofs+4);
			}
			ppc_call(&WriteMem64);
		}

		ppc_bx(c.done,0);                // backward unconditional b -> done (join)
	}
	s_cold_n = 0;
}

// ============================================================================
// FASTMEM (PPC-MMU) — JIT side.  MMU/page-table/DSI-handler side is in
// wii/wii_fastmem.cpp; the FASTMEM preset (default OFF) gates everything.
//
// When active, shop_readm/shop_writem emit BRANCHLESS direct accesses
// through the low-EA window instead of the inline _vmem-table lookup:
//
//     rlwinm  rEA, rADDR, 0,3,31      ; EA = sh4_addr & 0x1FFFFFFF
//     [xori   rEA, rEA, 4-sz]         ; BE sub-word swizzle (sz<4)
//     l/st    ..., 0(rEA)             ; ONE access, no compare, no branch
//
// RAM (4 mirrors), VRAM (64-bit path) and AICA wave RAM are page-mapped, so
// those never fault. Anything else (MMIO, SQ-after-masking, BIOS, VRAM
// 32-bit path, TA FIFO, OC-RAM...) takes a DSI; rec_fastmem_patch() below
// decodes the faulting site and overwrites the access instruction with a
// `b trampoline` into s_fm_pool, then the DSI handler RETRIES the PC. The
// slow path therefore always runs in normal context (interrupts enabled) —
// MMIO with side effects such as GD-ROM DMA file I/O stays safe. After the
// one-time patch a site costs the same as the legacy cold path, while
// never-faulting sites stay at 2-4 instructions.
//
// The shapes are FIXED and the EA scratch register doubles as the shape
// discriminator for the fault-time decoder — any change to the emission in
// shop_readm/shop_writem MUST be mirrored here:
//
//   scalar read : rlwinm r4 / [xori r4] / lbz|lha|lwz rD,0(r4) [/ extsb rD]
//   pair   read : rlwinm r5 / lwz r3,0(r5) / lwz r4,4(r5)
//   scalar write: rlwinm r5 / [xori r5] / stb|sth|stw rS,0(r5)
//   pair   write: rlwinm r4 / stw r5,0(r4) / stw r6,4(r4)
//
// JIT_FMOV adds four FLOAT variants of the same four shapes, used when the
// value's SH4 register is a pinned FR (FPU_PIN) so the access can name the
// FPR directly instead of bouncing through a GPR and the context slot:
//
//   scalar read : rlwinm r4 / lfs  fD,0(r4)
//   pair   read : rlwinm r5 / lfs  fD,0(r5)  / lfs  fD+1,4(r5)
//   scalar write: rlwinm r5 / stfs fS,0(r5)
//   pair   write: rlwinm r4 / stfs fS,0(r4)  / stfs fS+1,4(r4)
//
// Scratch registers and displacements are identical to the integer shapes —
// only the opcode differs (lfs 48 / stfs 52 vs lwz 32 / stw 36) — so the
// decoder's existing read/write and scalar/pair discrimination is untouched.
// Float shapes are always 4 bytes wide, hence no xori and no andi mask.
//
// The rlwinm never clobbers the address register, so the trampoline can
// pass the ORIGINAL SH4 address (recovered from the decoded rlwinm source
// field) to the generic ReadMem/WriteMem dispatchers — this is what keeps
// P4/SQ correct even though masking folds 0xE0000000 onto 0x00000000.
//
// Store-queue writes are the geometry hot path and used to hit the inline
// table lookup, so a write site whose first fault lands in the area-0
// image (DAR < 0x04000000 — where masked SQ addresses land) gets an
// inlined "SQ? -> direct sq_both store" fast path ahead of the generic
// call. Every trampoline ends in a generic path, so polymorphic sites are
// always correct, merely slower.
//
// LIMITATION (harmless): if only the SECOND word of a pair faults (a
// non-8-aligned fmov.d straddling a region edge), the original address is
// gone and the trampoline dispatches on the MASKED EA+4. That is identical
// for every normal region and can only differ for P4/SQ — which cannot
// straddle-fault, because SQ is fully unmapped and the FIRST word always
// faults there.
// ============================================================================

extern u8 CodeCache[];
extern "C" int get_fastmem_preset();	// main.cpp: 0=off (default), 1=on

static inline bool fastmem_on()
{
	return g_wii_fastmem_active && get_fastmem_preset();
}

// Trampoline pool. MEM1 .bss like CodeCache, so `b` reach (±32 MB) always
// holds (checked at emit time anyway). Reset together with the block cache:
// patched sites die with their blocks, so their trampolines must too.
#define FM_POOL_SIZE (128*1024)
static u8  s_fm_pool[FM_POOL_SIZE] __attribute__((aligned(32)));
static u32 s_fm_pool_used   = 0;
static u32 s_fm_patch_count = 0;

// Exception-context diagnostics. printf/stdio is FORBIDDEN anywhere in the
// resumable DSI path (hardware lesson from the 2026-07-19 MMU experiment:
// newlib's stdout lock can invoke the thread scheduler with EE=0 and
// deadlock after the line is emitted). Failures leave a record here for
// the panic screen / post-mortem instead, and the patch ring below can be
// inspected from a RAM dump; the cumulative count is printed from NORMAL
// context on each cache clear (rec_fastmem_reset_pool).
static volatile u32 s_fm_fail_pc, s_fm_fail_insn, s_fm_fail_dar;
static volatile u32 s_fm_fail_reason;	// 1=pc outside cache 2=pool full 3=undecodable 4=branch range
#define FM_RING 32
static u32 s_fm_ring[FM_RING];		// last FM_RING patched site PCs
static u32 s_fm_ring_n;

// JIT_FMOV tripwire. The preset needs BOTH fpu_pin and fastmem to be on to do
// anything, and silently emits the legacy GPR bounce otherwise — exactly the
// way to waste a hardware A/B run on a switch that never engaged. This counts
// float shapes actually emitted and announces the first one, so the log says
// plainly whether the thing under test is in the build that ran.
static u32 s_fm_float_sites = 0;
static void fm_note_float_site()
{
	// Compile time, normal context: printf is OK here (never in the DSI path).
	if (s_fm_float_sites++ == 0)
		printf("[fmov] JIT_FMOV active: emitting lfs/stfs fastmem shapes\n");
}

// Float sites that actually FAULTED and got a trampoline built. Emitted-vs-
// patched is the distinction that matters: emitting a float shape exercises
// nothing but the emitter, while a patch is the only proof that the DSI
// decoder and its trampolines — the exception-context code, where a mis-decode
// is a panic rather than a slow path — have been down the float path at all.
// A log showing thousands of float shapes emitted and ZERO patched means the
// risky half of JIT_FMOV is still untested, however well the game ran.
static u32 s_fm_float_patches = 0;

static void rec_fastmem_reset_pool()
{
	// Runs in normal context (bm_Reset / recSh4_ClearCache) — printf is OK.
	if (s_fm_patch_count)
		printf("[fastmem] cache clear: %u sites were patched (%u/%u B pool),"
		       " %u float shapes emitted / %u float sites patched\n",
		       s_fm_patch_count, s_fm_pool_used, FM_POOL_SIZE,
		       s_fm_float_sites, s_fm_float_patches);
	s_fm_pool_used     = 0;
	s_fm_patch_count   = 0;
	s_fm_ring_n        = 0;
	s_fm_float_sites   = 0;	// the sites die with the cache, like the patches
	s_fm_float_patches = 0;
}

// --- minimal raw-word emitter --------------------------------------------
// Deliberately independent of the global emit_ptr/LastAddr state: this runs
// INSIDE the DSI handler and must not disturb an in-progress compilation.
// (Also: integer code only — MSR.FP is off in the exception context.)
static u32* fm_p;
static void fm_w(u32 i)                            { *fm_p++ = i; }
static u32  fm_dform(u32 op6,u32 rt,u32 ra,u32 dd) { return (op6<<26)|(rt<<21)|(ra<<16)|(dd&0xFFFF); }
static void fm_mr(u32 dst,u32 src)                 { if (dst!=src) fm_w(0x7C000378|(src<<21)|(dst<<16)|(src<<11)); }
static bool fm_branch(void* target,u32 lk)
{
	snat disp=(u8*)target-(u8*)fm_p;
	if (disp>=33554432 || disp<-33554432)
		return false;
	fm_w(0x48000000|((u32)disp&0x03FFFFFC)|lk);
	return true;
}

// SQ fast-path address: reg = &sq_both[(areg & 0x3F) ^ swz]  (same layout as
// the _vmem direct block: BE u32 words, sub-word swizzle folded into addr).
// (NB: the scratch register parameter must not be called `r` —
// sh4_registers.h #defines that to Sh4cntx.r.)
static void fm_sq_addr(u32 reg,u32 areg,u32 swz)
{
	fm_w(0x54000000|(areg<<21)|(reg<<16)|(0<<11)|(26<<6)|(31<<1));	// rlwinm reg,areg,0,26,31
	if (swz)
		fm_w(0x68000000|(reg<<21)|(reg<<16)|swz);		// xori reg,reg,swz
	u32 sq=(u32)(uintptr_t)sq_both;
	s32 lo=(s32)(s16)sq;
	u32 hi=((u32)(sq-(u32)lo))>>16;
	fm_w(fm_dform(15,reg,reg,hi));					// addis reg,reg,hi
	fm_w(fm_dform(14,reg,reg,(u32)lo));				// addi  reg,reg,lo
}

// JIT_FMOV: context byte offset of the pinned FR named by a float shape's
// register field. GetFloatReg maps fr[n] -> f14+n and fr[] is contiguous, so
// the slot follows from the register number alone — which matters because this
// runs in exception context, where Sh4Context::offset() (a switch through
// Sh4_int_GetRegisterPtr plus verify()) is not callable. `pair` additionally
// requires fD+1 to be inside the pinned bank, so a mis-decode can never make
// the trampoline write past fr[15]. Returns -1 when the register is outside
// the bank (no emitted shape does that; this is the belt-and-braces check).
static int fm_fr_ofs(u32 fd,bool pair)
{
	const u32 lastf = pair ? (u32)(ppc_f14+14) : (u32)(ppc_f14+15);
	if (fd < (u32)ppc_f14 || fd > lastf)
		return -1;
	return (int)(SH4CTX_OFS_FR + (fd-(u32)ppc_f14)*4);
}

// Locate the governing `rlwinm rEA, rADDR, 0,3,31` within the 3 insns
// before the fault site; returns rADDR (which still holds the ORIGINAL
// SH4 address — the shapes never clobber it) or -1 if absent (not ours).
static int fm_find_areg(u32* site,u32 ea_reg)
{
	for (int back=1;back<=3;back++)
	{
		u32 i=site[-back];
		if ((i&0xFC1FFFFF)==(0x54000000|(ea_reg<<16)|0x00FE))
			return (int)((i>>21)&31);
	}
	return -1;
}

// Decode the faulting fastmem site at pc, build its trampoline, patch the
// site. Returns 0 on success; nonzero makes the DSI handler chain to the
// libogc panic screen. Called from WiiFastmem_HandleDSI (exception context:
// interrupts off, FPU off — keep this integer-only and printf-free except
// on the failure/debug paths).
int rec_fastmem_patch(unsigned int pc)
{
	u32 dar;
	asm volatile("mfspr %0,19" : "=r"(dar));	// DAR = faulting (masked) EA

	// Only faults inside the code cache can be fastmem sites.
	if (pc < (u32)(uintptr_t)&CodeCache[0] || pc >= (u32)(uintptr_t)&CodeCache[0]+CODE_SIZE)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=0; s_fm_fail_dar=dar; s_fm_fail_reason=1;
		return 1;
	}

	u32* site=(u32*)pc;
	const u32 insn=*site;
	const u32 op6=insn>>26;
	const u32 rt =(insn>>21)&31;	// rD (loads) / rS (stores)
	const u32 ra =(insn>>16)&31;	// base = EA scratch = shape discriminator
	const u32 dd =insn&0xFFFF;

	if (s_fm_pool_used + 32*sizeof(u32) > sizeof(s_fm_pool))
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=2;
		return 1;
	}

	fm_p=(u32*)&s_fm_pool[s_fm_pool_used];
	u32* const tramp=fm_p;
	bool ok=false;

	if ((op6==32||op6==42||op6==34) && ra==ppc_rarg1 && dd==0)
	{
		// --- scalar read: lbz/lha/lwz rD,0(r4). Replicates ColdFrag kind 0.
		int areg=fm_find_areg(site,ra);
		if (areg>=0)
		{
			ok=true;
			fm_mr(ppc_rarg0,(u32)areg);
			if (op6==34)		// lbz — ReadMem8, then extsb rD,r3
			{
				ok&=fm_branch((void*)ReadMem8,1);
				fm_w(0x7C000774|(ppc_rrv0<<21)|(rt<<16));
			}
			else if (op6==42)	// lha — ReadMem16, then extsh rD,r3
			{
				ok&=fm_branch((void*)ReadMem16,1);
				fm_w(0x7C000734|(ppc_rrv0<<21)|(rt<<16));
			}
			else			// lwz — ReadMem32
			{
				ok&=fm_branch((void*)ReadMem32,1);
				fm_mr(rt,ppc_rrv0);
			}
			// Resume after the load; a trailing in-line extsb (sz==1
			// shape) re-runs on the already-extended value — idempotent.
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==32 && ra==ppc_rarg2 && rt==ppc_rrv0 && dd==0)
	{
		// --- pair read, first word: lwz r3,0(r5). Address still in r3
		// (the pair shape always sources rarg0), so ReadMem64 needs no
		// setup and returns r3:r4 = high:low exactly as the shape does.
		if (fm_find_areg(site,ra)==ppc_rarg0)
		{
			ok=fm_branch((void*)ReadMem64,1);
			ok&=fm_branch(site+2,0);	// skip the second lwz too
		}
	}
	else if (op6==32 && ra==ppc_rarg2 && rt==ppc_rrv1 && dd==4)
	{
		// --- pair read, second word alone (straddling pair; see LIMITATION).
		if (fm_find_areg(site,ra)>=0)
		{
			ok=true;
			fm_mr(ppc_rarg3,ppc_rrv0);			// save word[addr]
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg2,4));	// addi r3,r5,4 (masked EA+4)
			ok&=fm_branch((void*)ReadMem32,1);
			fm_mr(ppc_rrv1,ppc_rrv0);
			fm_mr(ppc_rrv0,ppc_rarg3);
			ok&=fm_branch(site+1,0);
		}
	}
	else if ((op6==36||op6==44||op6==38) && ra==ppc_rarg2 && dd==0)
	{
		// --- scalar write: stb/sth/stw rS,0(r5). Replicates ColdFrag kind 2,
		// with an inlined SQ fast path for area-0-image faults.
		int areg=fm_find_areg(site,ra);
		if (areg>=0)
		{
			const u32 sz=(op6==38)?1:(op6==44)?2:4;
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|((u32)areg<<21)|(ppc_rarg2<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r5,areg,6,26,31 (addr>>26)
				fm_w(0x2C000000|(ppc_rarg2<<16)|0x38);		// cmpwi r5,0x38 — SQ region?
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic (patched below)
				fm_sq_addr(ppc_rarg2,(u32)areg,(sz<4)?(4-sz):0);
				fm_w(fm_dform(op6,rt,ppc_rarg2,0));		// st{b,h,w} rS,0(r5)
				ok&=fm_branch(site+1,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;	// resolve bne -> generic
			}
			fm_mr(ppc_rarg0,(u32)areg);
			fm_mr(ppc_rarg1,rt);
			if (sz==1)      fm_w(0x70000000|(ppc_rarg1<<21)|(ppc_rarg1<<16)|0xFF);	// andi. r4,r4,0xFF
			else if (sz==2) fm_w(0x70000000|(ppc_rarg1<<21)|(ppc_rarg1<<16)|0xFFFF);
			ok&=fm_branch((sz==1)?(void*)WriteMem8:(sz==2)?(void*)WriteMem16:(void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==36 && ra==ppc_rarg1 && rt==ppc_rarg2 && dd==0)
	{
		// --- pair write, first word: stw r5,0(r4). Address still in r3,
		// data in r5:r6 — the exact WriteMem64(u32,u64) EABI registers.
		if (fm_find_areg(site,ra)==ppc_rarg0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				// fmov.d bursts into the SQ are THE geometry path.
				fm_w(0x54000000|(ppc_rarg0<<21)|(ppc_rarg1<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r4,r3,6,26,31
				fm_w(0x2C000000|(ppc_rarg1<<16)|0x38);		// cmpwi r4,0x38
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic
				fm_sq_addr(ppc_rarg1,ppc_rarg0,0);		// 8-aligned: no swizzle
				fm_w(fm_dform(36,ppc_rarg2,ppc_rarg1,0));	// stw r5,0(r4)
				fm_w(fm_dform(36,ppc_rarg3,ppc_rarg1,4));	// stw r6,4(r4)
				ok&=fm_branch(site+2,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;
			}
			ok&=fm_branch((void*)WriteMem64,1);
			ok&=fm_branch(site+2,0);
		}
	}
	else if (op6==36 && ra==ppc_rarg1 && rt==ppc_rarg3 && dd==4)
	{
		// --- pair write, second word alone (straddling pair; see LIMITATION).
		if (fm_find_areg(site,ra)>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg1,4));	// addi r3,r4,4 (masked EA+4)
			fm_mr(ppc_rarg1,ppc_rarg3);
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	// ---- JIT_FMOV float shapes (lfs op6=48 / stfs op6=52) -------------------
	// Same EA scratch registers and displacements as the integer shapes above
	// — the opcode alone tells them apart — so the read/write and scalar/pair
	// discrimination is unchanged. rt is an FPR number here, not a GPR.
	//
	// These cases are deliberately NOT gated on get_jit_fmov_preset(): a site
	// is only ever EMITTED with the preset on, but if the preset were flipped
	// off while blocks compiled under it are still in the cache, gating here
	// would turn a perfectly valid site into an undecodable one — a panic
	// instead of a slow path. A decoder must accept everything the emitter can
	// produce, unconditionally.
	//
	// The slow C dispatchers deal in integers, so each trampoline bounces
	// through the FR's OWN context slot (fm_fr_ofs) — the one place that is
	// safe to clobber, since it is the slot the FR would flush to anyway.
	// f14..f31 are callee-saved under the PPC EABI, so a pinned FR survives
	// the ReadMem/WriteMem call; that is why the pair straddle cases need none
	// of the save/restore their integer counterparts do.
	else if (op6==48 && ra==ppc_rarg1 && dd==0)
	{
		// --- scalar float read: lfs fD,0(r4)  (fmov.s, always 4 bytes).
		int areg=fm_find_areg(site,ra);
		int fofs=fm_fr_ofs(rt,false);
		if (areg>=0 && fofs>=0)
		{
			ok=true;
			fm_mr(ppc_rarg0,(u32)areg);
			ok&=fm_branch((void*)ReadMem32,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,fofs(r30)
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==48 && ra==ppc_rarg2 && dd==0)
	{
		// --- pair float read, first word: lfs fD,0(r5). Address is still in
		// r3 (the pair shape always sources rarg0), so ReadMem64 needs no
		// setup; it returns r3:r4 = word[addr]:word[addr+4] -> fD:fD+1.
		int fofs=fm_fr_ofs(rt,true);
		if (fm_find_areg(site,ra)==ppc_rarg0 && fofs>=0)
		{
			ok=fm_branch((void*)ReadMem64,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(36,ppc_rrv1,ppc_contex,(u32)fofs+4));	// stw r4,fofs+4(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,  fofs(r30)
			fm_w(fm_dform(48,rt+1,ppc_contex,(u32)fofs+4));		// lfs fD+1,fofs+4(r30)
			ok&=fm_branch(site+2,0);				// skip the second lfs
		}
	}
	else if (op6==48 && ra==ppc_rarg2 && dd==4)
	{
		// --- pair float read, second word alone (straddling; see LIMITATION).
		// The first word is already in fD-1, which the call cannot disturb.
		int fofs=fm_fr_ofs(rt,false);
		if (fm_find_areg(site,ra)>=0 && fofs>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg2,4));		// addi r3,r5,4 (masked EA+4)
			ok&=fm_branch((void*)ReadMem32,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,fofs(r30)
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==52 && ra==ppc_rarg2 && dd==0)
	{
		// --- scalar float write: stfs fS,0(r5), with the same inlined SQ
		// fast path the integer scalar write gets. sz is always 4 here, so
		// there is no sub-word swizzle and no andi mask.
		int areg=fm_find_areg(site,ra);
		int fofs=fm_fr_ofs(rt,false);
		if (areg>=0 && fofs>=0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|((u32)areg<<21)|(ppc_rarg2<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r5,areg,6,26,31
				fm_w(0x2C000000|(ppc_rarg2<<16)|0x38);		// cmpwi r5,0x38 — SQ region?
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic (patched below)
				fm_sq_addr(ppc_rarg2,(u32)areg,0);
				fm_w(fm_dform(52,rt,ppc_rarg2,0));		// stfs fS,0(r5)
				ok&=fm_branch(site+1,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;	// resolve bne -> generic
			}
			fm_mr(ppc_rarg0,(u32)areg);				// before r4 is clobbered below
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg1,ppc_contex,(u32)fofs));	// lwz  r4,fofs(r30)
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==52 && ra==ppc_rarg1 && dd==0)
	{
		// --- pair float write, first word: stfs fS,0(r4). Address still in
		// r3; WriteMem64(u32,u64) wants r3 + r5:r6, so only the data moves.
		// fmov.d bursts into the SQ are THE geometry path, hence the inline.
		int fofs=fm_fr_ofs(rt,true);
		if (fm_find_areg(site,ra)==ppc_rarg0 && fofs>=0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|(ppc_rarg0<<21)|(ppc_rarg1<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r4,r3,6,26,31
				fm_w(0x2C000000|(ppc_rarg1<<16)|0x38);		// cmpwi r4,0x38
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic
				fm_sq_addr(ppc_rarg1,ppc_rarg0,0);		// 8-aligned: no swizzle
				fm_w(fm_dform(52,rt,ppc_rarg1,0));		// stfs fS,  0(r4)
				fm_w(fm_dform(52,rt+1,ppc_rarg1,4));		// stfs fS+1,4(r4)
				ok&=fm_branch(site+2,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;
			}
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,  fofs(r30)
			fm_w(fm_dform(52,rt+1,ppc_contex,(u32)fofs+4));		// stfs fS+1,fofs+4(r30)
			fm_w(fm_dform(32,ppc_rarg2,ppc_contex,(u32)fofs));	// lwz  r5,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg3,ppc_contex,(u32)fofs+4));	// lwz  r6,fofs+4(r30)
			ok&=fm_branch((void*)WriteMem64,1);
			ok&=fm_branch(site+2,0);				// skip the second stfs
		}
	}
	else if (op6==52 && ra==ppc_rarg1 && dd==4)
	{
		// --- pair float write, second word alone (straddling; see LIMITATION).
		int fofs=fm_fr_ofs(rt,false);
		if (fm_find_areg(site,ra)>=0 && fofs>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg1,4));		// addi r3,r4,4 (before r4 dies)
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg1,ppc_contex,(u32)fofs));	// lwz  r4,fofs(r30)
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}

	if (!ok)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=3;
		return 1;
	}

	const u32 bytes=(u32)((u8*)fm_p-(u8*)tramp);
	s_fm_pool_used += (bytes+31)&~31u;
	make_address_range_executable(tramp,bytes);

	// Patch the faulting access with an unconditional branch to the trampoline.
	snat disp=(u8*)tramp-(u8*)site;
	if (disp>=33554432 || disp<-33554432)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=4;
		return 1;
	}
	*site=0x48000000|((u32)disp&0x03FFFFFC);
	make_address_range_executable(site,sizeof(u32));

	s_fm_ring[s_fm_ring_n++ & (FM_RING-1)]=pc;
	s_fm_patch_count++;
	if (op6==48 || op6==52)		// lfs / stfs — a JIT_FMOV float shape faulted
		s_fm_float_patches++;
	return 0;
}

// ngen_CompileBlock: Main Block Compilation Loop
DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks)
{
	// Bail out early if there isn't enough space for a worst-case block
	if (emit_FreeSpace() < 16384) // 16*1024
		return 0;

	// JIT_ALIGN preset: pad the entry to the next 32-byte L1 line so every
	// block start (hence every branch/link target landing on one) begins on a
	// clean cache-line boundary. CodeCache itself is 32-byte aligned, so the
	// low 5 bits of emit_GetCCPtr() equal the intra-line offset. The padding
	// sits between the previous block's tail — which ALWAYS ends in an
	// unconditional branch (ngen_End) or a cold fragment's `b done`, never a
	// fall-through — and this entry, so the nops are never executed; they also
	// fall within the line the previous block's flush already rounds up to, so
	// no extra I-cache handling is needed. <=7 nops/block against a 6 MB cache.
	if (get_jit_align_preset())
	{
		while (((u32)(uintptr_t)emit_GetCCPtr() & 31) != 0)
			ppc_emit(0x60000000);	// ppc nop (ori r0,r0,0)
	}

	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();

	ColdReset();          // mem-op cold (slow) fragments deferred to block end
	ngen_Begin(block,force_checks);

	// JIT_HOTBLOCKS: bump this block's execution counter. Emitted AFTER
	// ngen_Begin so it counts real body executions — past the SMC check and the
	// cycle-underflow exit — at a point where rarg0/rarg1 are still scratch
	// (same reasoning as the block-check guard inside ngen_Begin).
	const s32 hb = hotblocks_slot(block);
	if (hb>=0)
	{
		u32 lo = ppc_addr_high(ppc_rarg1,(void*)&s_hb_hits[hb]);
		ppc_lwz(ppc_rarg0,ppc_rarg1,lo);
		ppc_addi(ppc_rarg0,ppc_rarg0,1);
		ppc_stw(ppc_rarg0,ppc_rarg1,lo);
	}

	for (size_t i = 0; i < block->oplist.size(); i++)
	{
		shil_opcode* op=&block->oplist[i];

		// T-FORWARDING: latch whether the PREVIOUS op left sr_T in rarg0, then
		// clear, so this op starts clean and only its own binop_end can set the
		// flag again. Anything that runs between a producer and a consumer
		// therefore invalidates the forward automatically.
		const bool t_prev = compile_state.t_in_rarg0;
		compile_state.t_in_rarg0 = false;

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
				// Address source for the runtime fast path (set in the rs1-reg
				// branch below). Defaults to rarg0 (the const/imm + pair paths).
				u32 areg = ppc_rarg0;

				// FPU_PIN Phase B: when the destination is a pinned FR, load the
				// value STRAIGHT into the FPR (lfs/lfsx) instead of into a GPR and
				// then bouncing it through memory. Only for RUNTIME addresses
				// (rs1 reg): a compile-time-known address is either resolved to a
				// direct RAM pointer or dispatched through a C call that returns
				// in a GPR, so neither wants a float shape.
				// fmov.s = flags 4 scalar (rd float); fmov.d = flags 8 pair
				// (rd, rd+1 both pinned fr). xf-based pairs (unpinned) fall back.
				//
				// JIT_FMOV extends this to the fastmem path. Those shapes are
				// FIXED — rec_fastmem_patch() decodes them at DSI time — so the
				// float variants only get EMITTED while the preset is on, but
				// the decoder accepts them unconditionally (see the comment on
				// its float cases: a site outlives the switch that made it).
				bool fdirect=false;
				u32  fd0=0, fd1=0, fofs=0;
				if (get_fpu_pin_preset() && op->rs1.is_reg()
				    && (!fastmem_on() || get_jit_fmov_preset()))
				{
					if (op->flags==8)
					{
						ppc_freg a=GetFloatReg(op->rd._reg);
						ppc_freg b=GetFloatReg(op->rd._reg+1);
						if (a!=ppc_finvalid && b!=ppc_finvalid)
						{ fdirect=true; fd0=(u32)a; fd1=(u32)b; fofs=Sh4cntx.offset(op->rd._reg); }
					}
					else if (op->flags==4 && op->rd.is_r32f())
					{
						ppc_freg a=GetFloatReg(op->rd._reg);
						if (a!=ppc_finvalid)
						{ fdirect=true; fd0=(u32)a; fofs=Sh4cntx.offset(op->rd._reg); }
					}
				}

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
							ppc_lwz(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
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

				if (!isram && fastmem_on())
				{
					// ---- FASTMEM: branchless access through the MMU window.
					// Shapes are FIXED — rec_fastmem_patch() decodes them at
					// DSI time (the EA scratch register is the shape
					// discriminator), so keep emission and decoder in sync.
					if (op->rs1.is_imm())
					{
						// Compile-time-known MMIO (fuct set above, address
						// already materialized in rarg0): call the handler
						// directly — never emit a faulting access for a
						// constant MMIO address. This is strictly cheaper
						// than the legacy always-miss 0x8C compare + cold
						// branch pair (think SB_ISTNRM poll loops).
						void* f=fuct;
						if (!f) f=(op->flags==1)?(void*)ReadMem8:(op->flags==2)?(void*)ReadMem16:(void*)ReadMem32;
						ppc_call(f);
						if (op->flags==1)      ppc_extsbx(rdreg,ppc_rrv0,0);
						else if (op->flags==2) ppc_extshx(rdreg,ppc_rrv0,0);
						else if (rdreg!=ppc_rrv0) ppc_ori(rdreg,ppc_rrv0,0);
					}
					else if (op->flags==8)
					{
						// Pair (fmov.d): address is always pre-loaded in
						// rarg0. EA scratch MUST be rarg2. word[addr]->rrv0
						// (high), word[addr+4]->rrv1 (low), matching the
						// ReadMem64 slow path register-for-register.
						ppc_rlwinmx(ppc_rarg2,ppc_rarg0,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (fdirect)
						{
							// JIT_FMOV: straight into the pinned FPRs. Same EA
							// scratch (rarg2) and same displacements, so the
							// decoder tells this from the integer pair only by
							// the opcode (lfs vs lwz).
							fm_note_float_site();
							ppc_lfs(fd0,ppc_rarg2,0);		// fault site A
							ppc_lfs(fd1,ppc_rarg2,4);		// fault site B
						}
						else
						{
							ppc_lwz(ppc_rrv0,ppc_rarg2,0);		// fault site A
							ppc_lwz(ppc_rrv1,ppc_rarg2,4);		// fault site B
						}
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);
						// Scalar: EA scratch MUST be rarg1. areg (pinned or
						// rarg0) still holds the original SH4 address at the
						// load — the fault decoder relies on that.
						ppc_rlwinmx(ppc_rarg1,areg,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (sz<4)
							ppc_xori(ppc_rarg1,ppc_rarg1,4-sz);	// BE sub-word swizzle
						if (fdirect)
						{
							fm_note_float_site();
							ppc_lfs(fd0,ppc_rarg1,0);		// fault site (sz==4 only)
						}
						else if (sz==1)
						{
							ppc_lbz(rdreg,ppc_rarg1,0);		// fault site
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
							ppc_lha(rdreg,ppc_rarg1,0);		// fault site
						else
							ppc_lwz(rdreg,ppc_rarg1,0);		// fault site
					}
				}
				else if (!isram)
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
						if (fdirect)
						{
							// FPU_PIN: load both words STRAIGHT into the pinned FPRs
							// (word[addr]->fd0, word[addr+4]->fd1); no GPR/rrv bounce.
							ppc_lfsx(fd0,ppc_rarg2,ppc_rarg0);	// word[addr]   -> fd0
							ppc_addi(ppc_rarg0,ppc_rarg0,4);
							ppc_lfsx(fd1,ppc_rarg2,ppc_rarg0);	// word[addr+4] -> fd1
						}
						else
						{
							ppc_lwzx(ppc_rarg3,ppc_rarg2,ppc_rarg0);	// word[addr]
							ppc_addi(ppc_rarg0,ppc_rarg0,4);
							ppc_lwzx(ppc_rrv1,ppc_rarg2,ppc_rarg0);	// word[addr+4] -> r4
							ppc_ori(ppc_rrv0,ppc_rarg3,0);			// r3 = word[addr]
						}

						ColdFrag& c=s_cold[s_cold_n++];
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=1; c.fuct=fuct;
						c.fdreg=fdirect?(u8)fd0:0xFF; c.fofs=(u16)fofs;
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);

						// Fast path for the CACHED main-RAM window only (P1, top byte
						// 0x8C -> 0x8C000000..0x8CFFFFFF = 16 MB of mem_b). This is the
						// overwhelmingly common data region; MMIO, uncached mirrors and
						// every other region fall to the cold path.
						//
						//   if ((addr>>24)==0x8C) {
						//       native = addr + (mem_b.data - 0x8C000000);   // 1 addis
						//       if (sz<4) native ^= 4-sz;                    // BE swizzle
						//       rv = *(T*)native;                            // 1 load
						//   } else rv = ReadMem<sz>(addr);                   // cold
						//
						// mem_b.data is 64 KB-aligned and set by _vmem_reserve() during
						// init (BEFORE any block compiles), and 0x8C000000 is 64 KB-
						// aligned too, so the delta's low 16 bits are 0 -> a single
						// addis (adds delta>>16 << 16) forms the exact native address.
						const u32 delta   = (u32)(uintptr_t)mem_b.data - 0x8C000000u;
						const u32 delta_hi= (delta>>16)&0xFFFF;		// low 16 of delta are 0

						// top byte == 0x8C ?  (rlwinm r0,areg,8,24,31 == srwi 24)
						ppc_rlwinmx(ppc_r0,areg,8,24,31,0);		// r0 = addr>>24
						ppc_cmpi(ppc_cr0,ppc_r0,0x8C,0);		// == 0x8C ?

						// bne -> cold (forward, not-taken: RAM path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne cold (not RAM)

						// --- fast direct RAM path (fall-through) ---
						// native = addr + delta  (single addis). rarg0 = native addr.
						ppc_addis(ppc_rarg0,areg,delta_hi);
						// big-endian sub-word swizzle folded into the address
						// (fdirect is float sz==4 only, so it never swizzles)
						if (sz<4)
							ppc_xori(ppc_rarg0,ppc_rarg0,4-sz);
						// rv = *(T*)native   (d-form, base=rarg0 != r0)
						if (fdirect)
							ppc_lfs(fd0,ppc_rarg0,0);	// FPU_PIN: straight into the FPR
						else if (sz==1)
						{
							ppc_lbz(rdreg,ppc_rarg0,0);
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
						{
							ppc_lhz(rdreg,ppc_rarg0,0);
							ppc_extshx(rdreg,rdreg,0);
						}
						else
							ppc_lwz(rdreg,ppc_rarg0,0);

						// done = join point; cold path (kind=0) rematerialises rarg0
						// from areg and calls ReadMem<sz>, then branches here.
						ColdFrag& c=s_cold[s_cold_n++];
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=0; c.sz=(u8)sz; c.rdreg=(u8)rdreg;
						c.areg=(u8)areg; c.fuct=fuct;
						c.fdreg=fdirect?(u8)fd0:0xFF; c.fofs=(u16)fofs;
					}
				}

				// FPU_PIN direct path already landed the value in the pinned
				// FPR(s) via lfs/lfsx, so skip the GPR->context stores entirely.
				// (fdirect is only ever set on the legacy runtime path above; the
				// fastmem / const-imm / int paths keep rdreg and store normally.)
				if (!fdirect)
				{
					ppc_sh_store(rdreg,op->rd);

					if (op->flags==8)
					{
						ppc_sh_store(ppc_rrv1,op->rd._reg+1);
					}
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

				// FPU_PIN Phase B (symmetric to shop_readm): when the data source
				// is a pinned FR, store STRAIGHT from the FPR (stfs/stfsx) instead
				// of bouncing it into a GPR first. fmov.s = flags 4 scalar,
				// fmov.d = flags 8 pair. JIT_FMOV extends it to fastmem, whose
				// float shapes rec_fastmem_patch() learned — see the shop_readm
				// gate above. Unlike the read side there is no rs1 check: a
				// store's address is always a runtime register here.
				bool fdirect=false;
				u32  fs0=0, fs1=0, fofs=0;
				if (get_fpu_pin_preset() && (!fastmem_on() || get_jit_fmov_preset()))
				{
					if (op->flags==8)
					{
						ppc_freg a=GetFloatReg(op->rs2._reg);
						ppc_freg b=GetFloatReg(op->rs2._reg+1);
						if (a!=ppc_finvalid && b!=ppc_finvalid)
						{ fdirect=true; fs0=(u32)a; fs1=(u32)b; fofs=Sh4cntx.offset(op->rs2._reg); }
					}
					else if (op->flags==4 && op->rs2.is_r32f())
					{
						ppc_freg a=GetFloatReg(op->rs2._reg);
						if (a!=ppc_finvalid)
						{ fdirect=true; fs0=(u32)a; fofs=Sh4cntx.offset(op->rs2._reg); }
					}
				}

				// Scalar data operand: if rs2 is pinned, the fast path can store
				// DIRECTLY from its pinned PPC reg (the fast path only reads the
				// data reg, never clobbers it), so we skip the pre-split
				// ppc_sh_load(rarg1,rs2) mov and load rarg1 only on the slow MMIO
				// path (where the WriteMem ABI requires data in rarg1). The 64-bit
				// pair keeps the rarg2:rarg3 ABI as before.
				u32 datareg = ppc_rarg1;
				if (fdirect)
				{
					// data stays in the pinned FPR(s) fs0[/fs1]; no GPR preload.
				}
				else if (op->flags==8)
				{
					ppc_sh_load(ppc_rarg2,op->rs2);
					ppc_sh_load(ppc_rarg3,op->rs2._reg+1);
				}
				else
				{
#if STATIC_GPR_ALLOC
					ppc_ireg dp=op->rs2.is_reg()?GetIntReg(op->rs2._reg):ppc_rinvalid;
					if (dp!=ppc_rinvalid)
						datareg=(u32)dp;		// store straight from pinned reg
					else
#endif
						ppc_sh_load(ppc_rarg1,op->rs2);	// scratch: load now
				}

				if (fastmem_on())
				{
					// ---- FASTMEM: branchless store through the MMU window.
					// Shapes are FIXED — rec_fastmem_patch() decodes them at
					// DSI time (EA scratch register = shape discriminator).
					// areg/datareg (or rarg0 + rarg2:rarg3 for pairs) still
					// hold the original values at the store — the fault
					// decoder and trampolines rely on that.
					if (op->flags==8)
					{
						// Pair: address in rarg0, data in rarg2:rarg3 =
						// high:low. EA scratch MUST be rarg1.
						ppc_rlwinmx(ppc_rarg1,ppc_rarg0,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (fdirect)
						{
							// JIT_FMOV: straight from the pinned FPRs; the
							// rarg2:rarg3 preload above was skipped.
							fm_note_float_site();
							ppc_stfs(fs0,ppc_rarg1,0);		// fault site A
							ppc_stfs(fs1,ppc_rarg1,4);		// fault site B
						}
						else
						{
							ppc_stw(ppc_rarg2,ppc_rarg1,0);		// fault site A
							ppc_stw(ppc_rarg3,ppc_rarg1,4);		// fault site B
						}
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);
						// Scalar: EA scratch MUST be rarg2. No data masking
						// needed on the direct path — stb/sth store the low
						// byte/halfword by definition (the trampoline's C
						// call path re-applies the andi like ColdFrag does).
						ppc_rlwinmx(ppc_rarg2,areg,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (sz<4)
							ppc_xori(ppc_rarg2,ppc_rarg2,4-sz);	// BE sub-word swizzle
						if (fdirect)
						{
							fm_note_float_site();
							ppc_stfs(fs0,ppc_rarg2,0);		// fault site (sz==4 only)
						}
						else if (sz==1)
							ppc_stb(datareg,ppc_rarg2,0);		// fault site
						else if (sz==2)
							ppc_sth(datareg,ppc_rarg2,0);		// fault site
						else
							ppc_stw(datareg,ppc_rarg2,0);		// fault site
					}
				}
				else
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
					if (fdirect)
					{
						// FPU_PIN: store both words STRAIGHT from the pinned FPRs
						// (fs0=high->word[addr], fs1=low->word[addr+4]).
						ppc_stfsx(fs0,ppc_rarg1,ppc_rarg0);	// word[addr]   = fs0
						ppc_addi(ppc_rarg0,ppc_rarg0,4);
						ppc_stfsx(fs1,ppc_rarg1,ppc_rarg0);	// word[addr+4] = fs1
					}
					else
					{
						ppc_stwx(ppc_rarg2,ppc_rarg1,ppc_rarg0);	// word[addr]   = high
						ppc_addi(ppc_rarg0,ppc_rarg0,4);
						ppc_stwx(ppc_rarg3,ppc_rarg1,ppc_rarg0);	// word[addr+4] = low
					}

					ColdFrag& c=s_cold[s_cold_n++];
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=3;
					c.fdreg=fdirect?(u8)fs0:0xFF; c.fofs=(u16)fofs;
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
					// *(T*)(ptr+eff) = data  (ptr=rarg3, eff=r0, data=datareg).
					// datareg is rs2's pinned reg (read-only here) or rarg1.
					// (fdirect is float sz==4 only, so it never swizzles.)
					if (fdirect)
						ppc_stfsx(fs0,ppc_rarg3,ppc_r0);	// FPU_PIN: straight from the FPR
					else if (sz==1)
						ppc_stbx(datareg,ppc_rarg3,ppc_r0);
					else if (sz==2)
						ppc_sthx(datareg,ppc_rarg3,ppc_r0);
					else
						ppc_stwx(datareg,ppc_rarg3,ppc_r0);

					ColdFrag& c=s_cold[s_cold_n++];
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=2; c.sz=(u8)sz; c.datareg=(u8)datareg; c.areg=(u8)areg;
					c.fdreg=fdirect?(u8)fs0:0xFF; c.fofs=(u16)fofs;
				}
			}
			break;

		case shop_ifb:
			{
				const u16 sh4op=(u16)op->rs3._imm;

				// IFB_PROBE: bump this opcode's counter. rarg0/rarg1 are
				// scratch at a shil-op boundary, and this runs before the
				// bracket so it cannot disturb a flushed value.
				if (get_ifb_probe_preset())
				{
					u32* slot=ifb_probe_slot(sh4op);
					if (slot)
					{
						u32 lo=ppc_addr_high(ppc_rarg1,(void*)slot);
						ppc_lwz(ppc_rarg0,ppc_rarg1,lo);
						ppc_addi(ppc_rarg0,ppc_rarg0,1);
						ppc_stw(ppc_rarg0,ppc_rarg1,lo);
					}
				}

				// IFB_FLUSH: spill only what this opcode's handler actually
				// touches, on both axes independently — the GPRs named by its
				// allow-list entry, and the float file only if it can disturb
				// fr[]/xf[]. Preset off => the legacy everything-spill, which
				// is also what an unlisted opcode gets.
				IfbBracket br={IFB_GPR_ALL,true};
				if (get_ifb_flush_preset())
					br=ifb_bracket(sh4op);

				reg_flush_mask(br.gprs);
				if (br.fpu)
					reg_flush_all_fpu();		// self-gates on FPU_PIN

				if (op->rs1._imm)
				{
					ppc_li(ppc_rarg0,op->rs2._imm);
					ppc_sh_store(ppc_rarg0,reg_nextpc);
				}
				ppc_li(ppc_rarg0,op->rs3._imm);
				ppc_call(OpDesc[op->rs3._imm]->oph);

				if (br.fpu)
					reg_reload_all_fpu();
				reg_reload_mask(br.gprs);
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
				// Delay-slot branches capture T here, before the delay slot runs.
				// When the compare that produced it is the op right before this
				// one, it is still in rarg0 — take it from there instead of
				// reloading the value we just stored.
				if (t_prev && op->rs1.is_reg() && op->rs1._reg==reg_sr_T)
					ppc_ori(ppc_djump,ppc_rarg0,0);		// mr djump,rarg0
				else
					ppc_sh_load(ppc_djump,op->rs1);
			}
			break;
			
		case shop_mov64:
			{
				verify(op->rd.is_r64());
				verify(op->rs1.is_r64());

				// mov64 is always a float pair (FMT_F64: fmov.d / DR<->XD moves).
				// Under FPU_PIN move each word register-to-register (fmr) instead
				// of bouncing both words through GPRs + memory. fmove_reg emits
				// the minimal form per word and, with the preset off, degenerates
				// to the legacy lfs f0/stfs f0 scratch bounce (no GPR round-trip,
				// which is also strictly fewer insns than the old code).
				fmove_reg(op->rd._reg,   op->rs1._reg);
				fmove_reg(op->rd._reg+1, op->rs1._reg+1);
			}
			break;

		case shop_mov32:
			{
				verify(op->rd.is_r32());

				// fmov FRm,FRn (both float): move register-to-register. fmove_reg
				// picks fmr / stfs / lfs / scratch-bounce by how each end is
				// allocated. The MIXED cases (flds FRm,FPUL = float->int, fsts
				// FPUL,FRn = int->float, or imm->float) fall through to the GPR
				// path below, where the pinning-aware ppc_sh_load/ppc_sh_store
				// already bounce a pinned FR through its memory slot correctly.
				if (op->rd.is_r32f() && op->rs1.is_r32f())
				{
					fmove_reg(op->rd._reg,op->rs1._reg);
					break;
				}

				// Resolve dest: a pinned (integer) rd is written in place; a
				// non-pinned or float-typed rd uses rarg0 + a context store.
				ppc_ireg rdr = op->rd.is_r32i() ? GetIntReg(op->rd._reg) : ppc_rinvalid;
				u32 dst = (rdr!=ppc_rinvalid) ? (u32)rdr : (u32)ppc_rarg0;

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


		// Single-instruction FP binops: with FPU_PIN, fbop_resolve reads pinned
		// fr operands in place and writes a pinned fr dest in place, collapsing
		// the whole op to ONE instruction (was lfs/lfs/op/stfs). Preset off ->
		// fop_a/fop_b are farg0/farg1 scratch loads and fbop_end stores back,
		// exactly the legacy binop_start_fpu/binop_end_fpu sequence.
		case shop_fadd: fbop_resolve(op); ppc_faddsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fsub: fbop_resolve(op); ppc_fsubsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fmul: fbop_resolve(op); ppc_fmulsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fdiv: fbop_resolve(op); ppc_fdivsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;

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

		// --- Shifts with dynamic SH4 semantics (branchless) -------------------
		case shop_ror:
			// rd = rotr(r1, r2&31). PPC rotates left; rotl by (32-(r2&31)).
			// rlwnm rotate amount is taken from rB[27:31] (mod 32), so passing
			// 32 when (r2&31)==0 is harmless (32 mod 32 == 0).
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

		// --- Carry / overflow arithmetic (JIT_CARRY preset) -------------------
		// All of these used to be shop_ifb call-outs because SHIL had no way to
		// say "produce a value AND the new T bit". Scratch budget: sources go
		// to rarg0/rarg1, the T<->XER shuffle uses rarg2, the dest is a pinned
		// GPR or rarg3 — no two of those can alias.

		case shop_adc:		// addc Rm,Rn : Rn = Rn + Rm + T ; T = carry
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				emit_T_to_ca(ppc_rarg2,false);
				ppc_addex(d,a,b,0,0);			// adde d,a,b
				emit_ca_to_T(ppc_rarg2,false);
				carry_dest_store(op,d);
			}
			break;

		case shop_sbc:		// subc Rm,Rn : Rn = Rn - Rm - T ; T = borrow
			{
				// subfe d,b,a computes ~b + a + CA = a - b - 1 + CA, so CA
				// carries the INVERTED T going in and comes back as "no
				// borrow" — hence invert on both ends.
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				emit_T_to_ca(ppc_rarg2,true);
				ppc_subfex(d,b,a,0,0);			// subfe d,b,a
				emit_ca_to_T(ppc_rarg2,true);
				carry_dest_store(op,d);
			}
			break;

		case shop_negc:		// negc Rm,Rn : Rn = 0 - Rm - T ; T = borrow
			{
				// rs1 is Rm here (DM_NEGC), rs2 is the T input which
				// emit_T_to_ca reads straight from the context.
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 d=carry_dest(op);
				emit_T_to_ca(ppc_rarg2,true);
				ppc_subfzex(d,a,0,0);			// subfze d,a  = ~a + CA
				emit_ca_to_T(ppc_rarg2,true);
				carry_dest_store(op,d);
			}
			break;

		case shop_addv:		// addv Rm,Rn : Rn = Rn + Rm ; T = signed overflow
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				ppc_addx(d,a,b,1,0);			// addo d,a,b
				emit_ov_to_T(ppc_rarg2);
				carry_dest_store(op,d);
			}
			break;

		case shop_subv:		// subv Rm,Rn : Rn = Rn - Rm ; T = signed overflow
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				ppc_subfx(d,b,a,1,0);			// subfo d,b,a = a - b
				emit_ov_to_T(ppc_rarg2);
				carry_dest_store(op,d);
			}
			break;

		case shop_cmpstr:	// cmp/str Rm,Rn : T = any byte lane is equal
			{
				// Classic has-zero-byte test on (Rn ^ Rm):
				//   x = (t - 0x01010101) & ~t ; rotl 1 ; & 0x01010101 ; != 0
				// The mask has its MSB clear, so the `and.` result can never be
				// negative and CR0[GT] is exactly "non-zero".
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);

				ppc_xorx(ppc_rarg2,a,b,0);			// rarg2 = t
				ppc_li(ppc_rarg3,0x01010101);
				ppc_subfx(ppc_r0,ppc_rarg3,ppc_rarg2,0,0);	// r0 = t - const
				ppc_andcx(ppc_r0,ppc_r0,ppc_rarg2,0);		// r0 &= ~t
				ppc_rlwinmx(ppc_r0,ppc_r0,1,0,31,0);		// rotl 1
				ppc_andx(ppc_r0,ppc_r0,ppc_rarg3,1);		// and. -> CR0
				emit_cr0_bit_to_rarg0(BI_CR0_GT);
				binop_end(op);					// rd is sr_T
			}
			break;

		case shop_swaplb:	// swap.b Rm,Rn : swap the two LOW bytes
			{
				// rlwimi rotate n places source bit (i+n)%32 into dest bit i:
				// bits 16..23 come from 24..31 (n=8), 24..31 from 16..23 (n=24).
				// Built in scratch so a dest that aliases the source is safe.
				u32 s=src_or_load(op->rs1,ppc_rarg0);
				ppc_rlwinmx(ppc_rarg1,s,0,0,15,0);	// keep the upper half
				ppc_rlwimix(ppc_rarg1,s,8,16,23,0);
				ppc_rlwimix(ppc_rarg1,s,24,24,31,0);
				ppc_sh_store(ppc_rarg1,op->rd);
			}
			break;

		case shop_div1:		// div1 Rm,Rn : one non-restoring division step
			{
				// q_new = q ^ oldQ ^ CA ^ 1, where q = MSB(Rn) before the
				// shift and CA is the carry out of the unified add:
				//     sel  = oldQ ^ M          (1 => add Rm, 0 => subtract)
				//     rmx  = sel ? Rm : ~Rm    (mask = sel-1)
				//     cin  = !sel
				//     Rn   = ((Rn<<1)|T) + rmx + cin
				// The M term cancels out of the usual `q ^ M ^ carry ^ !sel`
				// because sel already carries it. T = (q_new == M).
				//
				// Rm is captured into rmx BEFORE Rn is shifted, so `div1 Rn,Rn`
				// behaves like the hardware.
				const u32 t0=ppc_rarg0;		// SR.status, live to the end
				const u32 t1=ppc_rarg1;		// M, live to the T test
				const u32 t2=ppc_rarg2;		// oldQ, then general scratch
				const u32 t3=ppc_rarg3;		// q accumulator
				const u32 t4=ppc_r0;		// rmx
				const u32 t5=ppc_rarg4;		// sel

				// Rn is updated in place when it is pinned (rd == rs1 always
				// holds for DM_ADC), else through rarg7.
				ppc_ireg dpin=GetIntReg(op->rd._reg);
				u32 A;
				bool a_scratch=false;
				if (dpin!=ppc_rinvalid && op->rs1.is_reg() && op->rs1._reg==op->rd._reg)
					A=(u32)dpin;
				else
				{
					ppc_sh_load(ppc_rarg7,op->rs1);
					A=ppc_rarg7;
					a_scratch=true;
				}
				u32 B=src_or_load(op->rs2,ppc_rarg6);

				ppc_sh_load(t0,reg_sr_status);
				ppc_rlwinmx(t1,t0,23,31,31,0);		// M    = SR value bit 9
				ppc_rlwinmx(t2,t0,24,31,31,0);		// oldQ = SR value bit 8
				ppc_xorx(t5,t2,t1,0);			// sel  = oldQ ^ M
				ppc_addi(t4,t5,-1);			// mask = ~0 when sel==0
				ppc_xorx(t4,B,t4,0);			// rmx  = Rm ^ mask
				ppc_xori(t5,t5,1);			// cin  = !sel
				ppc_rlwinmx(t5,t5,29,2,2,0);		// -> XER[CA]
				ppc_mtxer(t5);				// (nothing below touches XER
									//  until the adde)

				ppc_rlwinmx(t3,A,1,31,31,0);		// q = MSB(Rn), pre-shift
				ppc_xorx(t3,t3,t2,0);			// q ^= oldQ

				ppc_sh_load(t2,reg_sr_T);
				ppc_rlwinmx(A,A,1,0,30,0);		// Rn <<= 1
				ppc_orx(A,A,t2,0);			// Rn |= T
				ppc_addex(A,A,t4,0,0);			// Rn += rmx + cin

				ppc_mfxer(t2);
				ppc_rlwinmx(t2,t2,3,31,31,0);		// CA
				ppc_xorx(t3,t3,t2,0);			// q ^= CA
				ppc_xori(t3,t3,1);			// q_new = q ^ oldQ ^ CA ^ 1
				ppc_xorx(t2,t3,t1,0);
				ppc_xori(t2,t2,1);			// T = (q_new == M)
				ppc_sh_store(t2,reg_sr_T);
				ppc_rlwimix(t0,t3,8,23,23,0);		// SR.Q (value bit 8) = q_new
				ppc_sh_store(t0,reg_sr_status);

				if (a_scratch)
					ppc_sh_store(A,op->rd);
			}
			break;

		// --- Multiply-accumulate (JIT_MAC preset) -----------------------------
		// mac.l / mac.w. The two @Rm+/@Rn+ reads are NOT here — the decoder
		// issues them as ordinary shop_readm ops (DM_MAC), which is most of the
		// win: on the old shop_ifb path they went through the interpreter's
		// ReadMem and never saw fastmem. What is left is the 64-bit accumulate.
		//
		// Shape, identical for both widths:
		//     r3..r7 = a, b, old MACL, old MACH, SR.status   (kept live)
		//     r8..r11 = the S==0 result, stored unconditionally
		//     then, only if SR.S is set, call sh4_mac_l/w with r3..r7 to redo
		//     it properly from the ORIGINAL accumulator and overwrite.
		// Passing the old accumulator to the helper instead of letting it read
		// the context is what lets the store happen before the test, so the
		// common path needs one conditional branch and no unconditional one.
		case shop_mac_l:
		case shop_mac_w:
			{
				const bool is_l = (op->op==shop_mac_l);

				ppc_sh_load(ppc_rarg0,op->rs1);			// a
				ppc_sh_load(ppc_rarg1,op->rs2);			// b
				ppc_sh_load(ppc_rarg2,reg_macl);		// old MACL
				ppc_sh_load(ppc_rarg3,reg_mach);		// old MACH
				ppc_sh_load(ppc_rarg4,op->rs3);			// SR.status

				// Product, sign-extended to 64 bits. mac.w's operands are
				// already sign-extended 16-bit values (shop_readm size 2 emits
				// lha), so mullw gives the whole signed product and the high
				// word is just its sign.
				ppc_mullwx(ppc_rarg5,ppc_rarg0,ppc_rarg1,0,0);
				if (is_l)
					ppc_mulhwx(ppc_rarg6,ppc_rarg0,ppc_rarg1,0);
				else
					ppc_srawix(ppc_rarg6,ppc_rarg5,31,0);

				ppc_addcx(ppc_rarg7,ppc_rarg2,ppc_rarg5,0,0);	// MACL + lo
				ppc_addex(ppc_r11,ppc_rarg3,ppc_rarg6,0,0);	// MACH + hi + CA
				ppc_sh_store(ppc_rarg7,op->rd);			// -> MACL
				ppc_sh_store(ppc_r11,op->rd2);			// -> MACH

				// SR.S is value bit 1. Saturation mode is vanishingly rare —
				// rare enough that the old interpreter died on it — so this is
				// a predicted-not-taken forward branch over a call.
				ppc_andi(ppc_r0,ppc_rarg4,2);
				ppc_label* done=ppc_CreateLabel();
				ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq -> done
				if (is_l)
					ppc_call(sh4_mac_l);
				else
					ppc_call(sh4_mac_w);
				done->MarkLabel();
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

		// --- Unary float (single instruction -> in-place under FPU_PIN) -------
		case shop_fabs: fuop_resolve(op); ppc_fabsx(fop_d,fop_a,0); fbop_end(op); break;
		case shop_fneg: fuop_resolve(op); ppc_fnegx(fop_d,fop_a,0); fbop_end(op); break;

		// --- Float comparisons (rd is an integer 0/1) -------------------------
		// fcmpu reads any FPR, so pinned operands compare in place; only rd
		// (an int reg) goes through rarg0 as before.
		case shop_fseteq:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_EQ);
				ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;
		case shop_fsetgt:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_GT);
				ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		// --- Float<->int conversion -------------------------------------------
		// All conversions bounce integer<->double bit patterns through the
		// per-context jit_scratch slot (8 bytes, 8-aligned), addressed via
		// ppc_contex. This avoids relying on a stack red zone (the devkitPPC
		// EABI does not guarantee one below sp).
		case shop_cvt_f2i_t:	// (s32)truncate(f)
			{
				// rs1 float read in place (pinned) or into farg0; rd is int.
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 so = (u32)offsetof(Sh4Context, jit_scratch);
				ppc_fctiwzx(ppc_f0,a,0);			// f0[low32] = (s32)trunc(a)
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
				// Narrow straight into the pinned rd (no trailing fmr/stfs).
				u32 d=fdst_reg(op->rd,ppc_farg0);
				ppc_frspx(d,ppc_f0,0);				// narrow to single
				fdst_store(op->rd,d,ppc_farg0);
			}
			break;

		// --- FMAC: rd = rs1 + rs2 * rs3  (scalar) -----------------------------
		// fmadds(D,A,B,C) = A*C + B  ->  rs2*rs3 + rs1.  Single instruction, so
		// under FPU_PIN read all three pinned sources in place and write the
		// pinned dest in place (was 3 lfs + op + stfs). NOTE for SH4 fmac, rd
		// aliases rs3 (== FR0-accumulate form is different; here rd is a real
		// fr), and fmadds reads A,B,C before writing D, so aliasing is safe.
		case shop_fmac:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_f0);	// addend (rs1)
				u32 b=fsrc_or_load(op->rs2,ppc_f1);
				u32 c=fsrc_or_load(op->rs3,ppc_f2);
				u32 d=fdst_reg(op->rd,ppc_f0);
				// fmaddsx(D,A,B,C) = A*C + B. Want d = rs2*rs3 + rs1 = b*c + a,
				// so A=b, C=c, B=a -> fmaddsx(d, b, a, c). (Matches the legacy
				// fmaddsx(f0,f1,f0,f2) = f1*f2 + f0 arg pattern D,rs2,rs1,rs3.)
				ppc_fmaddsx(d,b,a,c,0);			// d = b*c + a
				fdst_store(op->rd,d,ppc_f0);
			}
			break;

		// --- FIPR: rd = dot(v1, v2) over 4 elements ---------------------------
		// rs1,rs2 are FV4 vectors (base fr); rd is the 4th element slot.
		case shop_fipr:
			{
				ppc_fvec_load(ppc_f1,op->rs1,0);
				ppc_fvec_load(ppc_f2,op->rs2,0);
				ppc_fmulsx(ppc_f0,ppc_f1,ppc_f2,0);		// acc = a0*b0
				for (u32 i=1;i<4;i++)
				{
					ppc_fvec_load(ppc_f1,op->rs1,i);
					ppc_fvec_load(ppc_f2,op->rs2,i);
					ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,ppc_f2,0);	// acc = ai*bi + acc
				}
				ppc_sh_store_f32(ppc_f0,op->rd);
			}
			break;

		// --- FTRV: rd(vec4) = matrix(rs2) x vec(rs1) --------------------------
		// rs1 = FV4 vector (fr base), rs2 = XMTRX (xf base), rd = FV4 (aliases rs1).
		//   out[i] = m[i]*v[0] + m[4+i]*v[1] + m[8+i]*v[2] + m[12+i]*v[3]
		// Results go to f4..f7 first because rd aliases rs1 (must finish reads
		// before any store).
		case shop_ftrv:
			{
				// Load the input vector once into f8..f11.
				for (u32 j=0;j<4;j++)
					ppc_fvec_load(ppc_f8+j,op->rs1,j);

				for (u32 i=0;i<4;i++)
				{
					ppc_fvec_load(ppc_f1,op->rs2,i);		// m[i]
					ppc_fmulsx(ppc_f0,ppc_f1,ppc_f8,0);		// out = m[i]*v0
					for (u32 j=1;j<4;j++)
					{
						ppc_fvec_load(ppc_f1,op->rs2,j*4+i);	// m[j*4+i]
						ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,ppc_f8+j,0);	// out += m*vj
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
				ppc_lfsx(ppc_f0,ppc_rarg1,ppc_rarg0);		// sin_table[idx]
				ppc_fvec_store(ppc_f0,op->rd,0);
				// +0x4000 entries * 4 bytes = +0x10000 (doesn't fit addi s16;
				// use addis to add 1<<16).
				ppc_addis(ppc_rarg0,ppc_rarg0,1);		// rarg0 += 0x10000
				ppc_lfsx(ppc_f1,ppc_rarg1,ppc_rarg0);		// sin_table[idx+0x4000]
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
		// No GPR flush needed (this never touches r[]). FPU_PIN: UpdateFPSCR()
		// -> ChangeFP() swaps fr_hex[i]<->xf_hex[i] IN MEMORY on an FPSCR.FR
		// toggle (sh4_registers.cpp), so pinned fr must be flushed before the
		// call (so the swap sees current values, not stale ones) and reloaded
		// after (fr[] now holds the new front bank). reg_flush/reload_all_fpu
		// are no-ops when the preset is off.
		case shop_sync_fpscr:
			reg_flush_all_fpu();
			ppc_call(&UpdateFPSCR);
			reg_reload_all_fpu();
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
			// handlers (notably sync_sr -> UpdateSR) mutate context GPRs directly
			// (e.g. SR.RB bank switch), so pinned regs must be coherent in memory
			// across the call and re-read afterwards. The CC param marshalling
			// itself is register-aware and unaffected. FPU_PIN: any un-natived
			// float op reads/writes fr[]/xf[] directly too (including via
			// ppc_sh_addr's &Sh4cntx.fr[n] pointers for CPT_ptr canonical
			// params), so pinned fr needs the same bracket.
			reg_flush_all();
			reg_flush_all_fpu();
			shil_chf[op->op](op);
			reg_reload_all();
			reg_reload_all_fpu();
			break;
      
		}
	}

	ngen_End(block);

	// Split point for JIT_HOTBLOCKS: everything up to here is the block's HOT
	// body, everything FlushCold() adds after it is out-of-line mem slow paths
	// that usually never execute. Lumping them together makes a block with big
	// cold fragments look like bad codegen when its hot path may be fine, so
	// the two are reported separately.
	u8 *const _hb_cold_start = (u8*)emit_GetCCPtr();

	FlushCold();          // emit the out-of-line mem slow paths after the block tail
	make_address_range_executable((u8*)rv, (u8*)emit_GetCCPtr()-(u8*)rv);

	// Hot body and cold fragments recorded separately — B/op is built on the HOT
	// size only, since that is what actually executes.
	if (hb>=0)
	{
		u32 hot = (u32)(_hb_cold_start-(u8*)rv);
		// Discount the probe's own 4-instruction counter so the B/op figure
		// describes the codegen, not the measurement. On a 3-op block those
		// 16 bytes are 5 B/op of pure observer effect.
		s_hb[hb].ppc_start = (u8*)rv;
		s_hb[hb].ppc_size  = hot>16 ? hot-16 : hot;
		s_hb[hb].ppc_cold  = (u32)((u8*)emit_GetCCPtr()-_hb_cold_start);
	}

	return rv;
}



void ngen_ResetBlocks()
{
	// Block-cache clear: every fastmem-patched site just died with its
	// block, so drop their trampolines too (bm_Reset calls this from
	// recSh4_ClearCache and bm_Init).
	rec_fastmem_reset_pool();
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

// The `bl` word ngen_End() emitted into slot 0 of an IC site, recomputed for a
// given site address. Used to prove the site still exists before patching it.
static inline u32 ic_site_bl_word(const u32* site)
{
	snat offs = ((u8*)ngen_LinkBlock_Dynamic_IC_stub - (u8*)site) >> 2;
	return 0x48000000u | ((0xffffffu & (u32)offs) << 2) | 1u;
}

// DYN_IC preset: fill a dynamic exit's inline cache.
//
// Reached by the `bl` in slot 0 of the site emitted in ngen_End(). `pc` is the
// target the site actually branched to (rarg0 = djump, set just before the
// site); `site` is the address of slot 0, recovered from LR by the stub.
//
// We compile/find the target, overwrite the four slots with the guard shown in
// ngen_End(), and return the target so the stub's `bctr` jumps into it — this
// call's dispatch is not wasted.
//
// Called at most once per site: after the rewrite slot 0 is no longer a `bl`,
// so a target change just falls through to the generic path forever after.
void* FASTCALL ngen_LinkBlock_Dynamic_IC(u32 pc,u32* site)
{
	next_pc=pc;

	DynarecCodeEntry* rv=rdv_FindOrCompile();

	// rdv_FindOrCompile may have cleared and refilled the code cache (a full
	// clear resets LastAddr and recompiles from scratch). That destroys every
	// patched site, this one included — `site` would now point into code that
	// has been overwritten by an unrelated block. Detect it and patch nothing;
	// the site is gone anyway and the caller is returning into fresh code.
	if (*site != ic_site_bl_word(site))
		return (void*)rv;

	emit_ptr=site;
	{
		ppc_xoris(ppc_rarg1,ppc_djump,pc>>16);		// rarg1 = djump ^ (hi<<16)
		ppc_cmpli(ppc_cr0,ppc_rarg1,(u16)pc,0);		// cmplwi rarg1, lo
		ppc_bcx(BO_FALSE,BI_CR0_EQ,2,0,0);		// bne +8 -> generic (site+16)
		ppc_jump(rv);					// b target
	}
	emit_ptr=0;

	make_address_range_executable(site, 4*sizeof(u32));

	// [DYN_IC] engagement counter — served its purpose (Wii-measured
	// 2026-09-07, see dyn-ic-preset memory) and is quieted now that this
	// preset ships default-on. Re-enable if the fill path is ever suspect
	// again: costs one increment per SITE (not per traversal), never on
	// the hot path.
#if 0
	{
		static u32 ic_patched=0;
		if ((++ic_patched % 1000)==0)
			printf("[DYN_IC] %u sites patched\n", ic_patched);
	}
#endif

	return (void*)rv;
}

// =========
// MAIN LOOP
// =========

void ngen_mainloop()
{
	// FASTMEM preset: set up the MMU window + DSI handler before the first
	// block compiles. Idempotent, checks the preset itself, and leaves
	// g_wii_fastmem_active=0 on any failure (emission then stays legacy).
	// Called here (not recSh4_Init) so the game preset file has been
	// applied and _vmem_reserve() has placed the guest arrays.
	WiiFastmem_Init();

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
			// Same, for the FPU_PIN preset's fr[0..15] -> f14..f29 (no-op if off).
			reg_reload_all_fpu();

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

		// DYN_IC preset: inline-cache fill stub. Entered by the `bl` in slot 0
		// of an unpatched site, so LR = site+4 and rarg0 = the target PC (the
		// `mr rarg0,djump` that precedes every site). Hand both to the patcher
		// and jump into whatever it returns.
		ngen_LinkBlock_Dynamic_IC_stub=emit_GetCCPtr();
		{
			ppc_mfspr(ppc_rarg1,ppc_spr_lr);
			ppc_addi(ppc_rarg1,ppc_rarg1,(u32)-4);
			ppc_call_and_jump(&ngen_LinkBlock_Dynamic_IC);
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