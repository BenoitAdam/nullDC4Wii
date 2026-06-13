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
		ppc_ori(D,D,(u16)imm);
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

struct
{
	bool has_jcond;

	void Reset()
	{
		has_jcond=false;
	}
} compile_state;
u32 last_block;

// Forward decls: GPR allocation map + flush/reload (defined later in this file).
ppc_ireg GetIntReg(u32 reg);
void reg_flush_all();
void reg_reload_all();

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
void ppc_sh_load_f32(u32 D,u32 sh4_reg)
{
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
// Float registers are NOT pinned (GetFloatReg is unused), so these always hit
// memory — exactly what the vector ops need.
u32 ppc_fvec_ofs(shil_param prm,u32 i)
{
	return Sh4cntx.offset(prm._reg) + i*4;
}
void ppc_fvec_load(u32 fd,shil_param prm,u32 i)
{
	ppc_lfs(fd,ppc_contex,ppc_fvec_ofs(prm,i));
}
void ppc_fvec_store(u32 fs,shil_param prm,u32 i)
{
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

// Load rs1 into rarg0, then compare against rs2 into CR0. Folds a small
// immediate rs2 into cmpi/cmpli (signed uses cmpi+s16, unsigned cmpli+u16),
// otherwise loads rs2 into rarg1 and uses the register cmp/cmpl. Used by the
// set*/test helpers below. rarg0 is left holding rs1 (clobbered by the
// following mfcr in emit_cr0_bit_to_rarg0, which is fine).
static void emit_cmp_into_cr0(shil_opcode* op,bool is_signed)
{
	ppc_sh_load(ppc_rarg0,op->rs1);

	if (op->rs2.is_imm() && (is_signed ? op->rs2.is_imm_s16() : op->rs2.is_imm_u16()))
	{
		if (is_signed)
			ppc_cmpi(ppc_cr0,ppc_rarg0,op->rs2._imm,0);
		else
			ppc_cmpli(ppc_cr0,ppc_rarg0,op->rs2._imm,0);
		return;
	}

	if (op->rs2.is_imm())
		ppc_li(ppc_rarg1,op->rs2._imm);
	else
		ppc_sh_load(ppc_rarg1,op->rs2);

	if (is_signed)
		ppc_cmp(ppc_cr0,ppc_rarg0,ppc_rarg1,0);
	else
		ppc_cmpl(ppc_cr0,ppc_rarg0,ppc_rarg1,0);
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
			//printf("COND %d\n",block->BlockType&1);
			//die("not supported");
			u32 reg;
			if (compile_state.has_jcond)
			{
				reg=ppc_djump;
			}
			else
			{
				reg=ppc_rarg0;
				ppc_sh_load(ppc_rarg0,reg_sr_T);
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
	// Bail out early if there isn't enough space for a worst-case block
	if (emit_FreeSpace() < 16384) // 16*1024
		return 0;
	
	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();

	ColdReset();          // mem-op cold (slow) fragments deferred to block end
	ngen_Begin(block,force_checks);

	for (size_t i = 0; i < block->oplist.size(); i++)
	{
		shil_opcode* op=&block->oplist[i];
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
						ppc_sh_load(ppc_rarg0,op->rs1);
						ppc_addi(ppc_rarg0,ppc_rarg0,op->rs3._imm);
					}
					else if (op->rs3.is_r32i())
					{
						ppc_sh_load(ppc_rarg0,op->rs1);
						ppc_sh_load(ppc_rarg1,op->rs3);
						ppc_addx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
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
						ppc_addi(ppc_rarg1,ppc_rarg1,lo);
						ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
						ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);	// ptr (≠r0)
						ppc_cmpi(ppc_cr0,ppc_rarg2,0,0);		// ptr == 0 ?

						// beq -> cold (forward, not-taken: fast path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq cold (MMIO)

						// --- fast direct path (fall-through) ---
						ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// shift (iirf dead after)
						ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
						ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);	// mirror mask
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
						ppc_addi(ppc_rarg1,ppc_rarg1,lo);
						ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
						// rarg2 = ptr = iirf & ~0x1F  (clear low 5 bits: ME=26)
						// NOTE: ptr goes in rarg2 (NOT r0) — load-indexed treats a
						// base of r0 as literal zero, which would drop the pointer.
						ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);
						ppc_cmpi(ppc_cr0,ppc_rarg2,0,0);		// ptr == 0 ?

						// beq -> cold (forward, not-taken: fast path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq cold (MMIO)

						// --- fast direct path (fall-through) ---
						// shift = iirf & 0x1F ; eff = (addr<<sh)>>sh  (into rarg0)
						ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// r0 = shift (0..31)
						ppc_slwx(ppc_rarg0,areg,ppc_r0,0);		// reads areg -> rarg0
						ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
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
						else
							ppc_lwzx(rdreg,ppc_rarg2,ppc_rarg0);

						// done = join point; slow path (registered below) bounces here.
						ColdFrag& c=s_cold[s_cold_n++];
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=0; c.sz=(u8)sz; c.rdreg=(u8)rdreg;
						c.areg=(u8)areg; c.fuct=fuct;
					}
				}

				ppc_sh_store(rdreg,op->rd);

				if (op->flags==8)
				{
					ppc_sh_store(ppc_rrv1,op->rd._reg+1);
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
					ppc_sh_load(ppc_rarg0,op->rs1);
					ppc_addi(ppc_rarg0,ppc_rarg0,op->rs3._imm);
				}
				else if (op->rs3.is_r32i())
				{
					ppc_sh_load(ppc_rarg0,op->rs1);
					ppc_sh_load(ppc_rarg3,op->rs3);
					ppc_addx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0,0);
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
				u32 datareg = ppc_rarg1;
				if (op->flags==8)
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
					ppc_addi(ppc_rarg1,ppc_rarg1,lo);
					ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
					// Extract shift BEFORE masking iirf in place to ptr.
					ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// r0 = shift
					ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,0,26,0);	// rarg1 = ptr (≠r0)
					ppc_cmpi(ppc_cr0,ppc_rarg1,0,0);		// ptr == 0 ?

					// beq -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq cold (MMIO)

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
					ppc_addi(ppc_rarg2,ppc_rarg2,lo);
					ppc_lwzx(ppc_rarg2,ppc_rarg2,ppc_r0);		// rarg2 = iirf
					// rarg3 = ptr = iirf & ~0x1F  (ptr NOT in r0: store-indexed
					// treats base r0 as literal zero, dropping the pointer)
					ppc_rlwinmx(ppc_rarg3,ppc_rarg2,0,0,26,0);
					ppc_cmpi(ppc_cr0,ppc_rarg3,0,0);		// ptr == 0 ?

					// beq -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq cold (MMIO)

					// --- fast direct path (fall-through) ---
					// shift = iirf & 0x1F ; eff = (addr<<sh)>>sh  (in rarg2 scratch)
					ppc_andi(ppc_r0,ppc_rarg2,0x1F);		// r0 = shift (iirf still in rarg2)
					ppc_slwx(ppc_rarg2,areg,ppc_r0,0);		// rarg2 = addr<<sh (reads areg; overwrites iirf, no longer needed)
					ppc_srwx(ppc_rarg2,ppc_rarg2,ppc_r0,0);		// rarg2 = (addr<<sh)>>sh
					if (sz<4)
						ppc_xori(ppc_rarg2,ppc_rarg2,4-sz);	// big-endian sub-word swizzle
					// *(T*)(ptr+eff) = data  (ptr=rarg3, eff=rarg2, data=datareg).
					// datareg is rs2's pinned reg (read-only here) or rarg1.
					if (sz==1)
						ppc_stbx(datareg,ppc_rarg3,ppc_rarg2);
					else if (sz==2)
						ppc_sthx(datareg,ppc_rarg3,ppc_rarg2);
					else
						ppc_stwx(datareg,ppc_rarg3,ppc_rarg2);

					ColdFrag& c=s_cold[s_cold_n++];
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=2; c.sz=(u8)sz; c.datareg=(u8)datareg; c.areg=(u8)areg;
				}
			}
			break;

		case shop_ifb:
			{
				reg_flush_all();
				if (op->rs1._imm)
				{
					ppc_li(ppc_rarg0,op->rs2._imm);
					ppc_sh_store(ppc_rarg0,reg_nextpc);
				}
				ppc_li(ppc_rarg0,op->rs3._imm);
				ppc_call(OpDesc[op->rs3._imm]->oph);
				reg_reload_all();
			}
			break;
			
		case shop_jdyn:
			{
				ppc_sh_load(ppc_djump,op->rs1);
				
				if (op->rs2.is_imm())
				{
					if (op->rs2.is_imm_s16())
					{
						ppc_addi(ppc_djump,ppc_djump,op->rs2._imm);
					}
					else
					{
						ppc_li(ppc_rarg0,op->rs2._imm);
						ppc_addx(ppc_djump,ppc_djump,ppc_rarg0,0,0);
					}
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

				ppc_sh_load(ppc_rarg0,op->rs1);
				ppc_sh_load(ppc_rarg1,op->rs1._reg+1);

				ppc_sh_store(ppc_rarg0,op->rd);
				ppc_sh_store(ppc_rarg1,op->rd._reg+1);
			}
			break;

		case shop_mov32:
			{
				verify(op->rd.is_r32());

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


		case shop_fadd: binop_start_fpu(op); ppc_faddsx(ppc_farg0,ppc_farg0,ppc_farg1,0); binop_end_fpu(op); break;
		case shop_fsub: binop_start_fpu(op); ppc_fsubsx(ppc_farg0,ppc_farg0,ppc_farg1,0); binop_end_fpu(op); break;
		case shop_fmul: binop_start_fpu(op); ppc_fmulsx(ppc_farg0,ppc_farg0,ppc_farg1,0); binop_end_fpu(op); break;
		case shop_fdiv: binop_start_fpu(op); ppc_fdivsx(ppc_farg0,ppc_farg0,ppc_farg1,0); binop_end_fpu(op); break;

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

		// --- Integer comparisons / test ---------------------------------------
		// (set*/test do their own operand load + immediate folding internally.)
		case shop_test:	// rd = (r1 & r2) == 0
			ppc_sh_load(ppc_rarg0,op->rs1);
			if (op->rs2.is_imm_u16())
				ppc_andi(ppc_rarg0,ppc_rarg0,op->rs2._imm);	// andi. sets CR0
			else
			{
				if (op->rs2.is_imm())
					ppc_li(ppc_rarg1,op->rs2._imm);
				else
					ppc_sh_load(ppc_rarg1,op->rs2);
				ppc_andx(ppc_rarg0,ppc_rarg0,ppc_rarg1,1);	// and. sets CR0
			}
			emit_cr0_bit_to_rarg0(BI_CR0_EQ);
			binop_end(op);
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
		case shop_fabs: unop_start_fpu(op); ppc_fabsx(ppc_farg0,ppc_farg0,0); unop_end_fpu(op); break;
		case shop_fneg: unop_start_fpu(op); ppc_fnegx(ppc_farg0,ppc_farg0,0); unop_end_fpu(op); break;

		// --- Float comparisons (rd is an integer 0/1) -------------------------
		case shop_fseteq:
			binop_start_fpu(op);
			ppc_fcmpu(ppc_cr0,ppc_farg0,ppc_farg1);
			emit_cr0_bit_to_rarg0(BI_CR0_EQ);
			ppc_sh_store(ppc_rarg0,op->rd);
			break;
		case shop_fsetgt:
			binop_start_fpu(op);
			ppc_fcmpu(ppc_cr0,ppc_farg0,ppc_farg1);
			emit_cr0_bit_to_rarg0(BI_CR0_GT);
			ppc_sh_store(ppc_rarg0,op->rd);
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
		// fmadds(D,A,B,C) = A*C + B  ->  rs2*rs3 + rs1
		case shop_fmac:
			ppc_sh_load_f32(ppc_f0,op->rs1);		// f0 = rs1 (addend)
			ppc_sh_load_f32(ppc_f1,op->rs2);		// f1 = rs2
			ppc_sh_load_f32(ppc_f2,op->rs3);		// f2 = rs3
			ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,ppc_f2,0);	// f0 = f1*f2 + f0
			ppc_sh_store_f32(ppc_f0,op->rd);
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
				ppc_sh_load(ppc_rarg0,op->rs1);			// rarg0 = fpul value
				ppc_rlwinmx(ppc_rarg0,ppc_rarg0,2,14,29,0);	// rarg0 = (idx & 0xFFFF) * 4
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
		// Plain call — no GPR flush needed. NOTE: if STATIC_FPU_ALLOC is ever
		// enabled, UpdateFPSCR()->ChangeFP() swaps fr[]<->xf[] in memory and this
		// must be bracketed with reg_flush_all()/reg_reload_all() like sync_sr.
		case shop_sync_fpscr:
			ppc_call(&UpdateFPSCR);
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
			// itself is register-aware and unaffected.
			reg_flush_all();
			shil_chf[op->op](op);
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

			//cycles
			ppc_li(ppc_cycles,SH4_TIMESLICE);

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
			ppc_addi(ppc_cycles,ppc_cycles,SH4_TIMESLICE);	//add cycles ...

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