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

void binop3_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());

	// rs1 source
	ppc_ireg a=GetIntReg(op->rs1._reg);
	if (a!=ppc_rinvalid)
		bop_a=a;
	else
	{
		ppc_sh_load(ppc_rarg0,op->rs1);
		bop_a=ppc_rarg0;
	}

	// rs2 source
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

	// rd dest
	ppc_ireg d=GetIntReg(op->rd._reg);
	bop_d = (d!=ppc_rinvalid) ? (u32)d : ppc_rarg0;
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

// rd = (signed) rarg0 <cond> rarg1  -> 0/1
static void emit_setcc_signed(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	ppc_cmp(ppc_cr0,ppc_rarg0,ppc_rarg1,0);
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
}

// rd = (unsigned) rarg0 <cond> rarg1 -> 0/1
static void emit_setcc_unsigned(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	ppc_cmpl(ppc_cr0,ppc_rarg0,ppc_rarg1,0);
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

// =====================
// OPERATION COMPILATION
// =====================

// ngen_CompileBlock: Main Block Compilation Loop
DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks)
{
	// Bail out early if there isn't enough space for a worst-case block
	if (emit_FreeSpace() < 16384) // 16*1024
		return 0;
	
	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();
	
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

				if (op->rs1.is_imm())
				{
					void* ptr=_vmem_read_const(op->rs1._imm,isram,op->flags);
					if (isram)
					{
						if (op->flags==1)
						{
							ppc_lbz(ppc_r3,ppc_r4,ppc_addr_high(ppc_r4,ptr));
							ppc_extsbx(ppc_r3,ppc_r3,0);
						}	
						else if (op->flags==2)
							ppc_lha(ppc_r3,ppc_r4,ppc_addr_high(ppc_r4,ptr));
						else if (op->flags==4)
							ppc_lwz(ppc_r3,ppc_r4,ppc_addr_high(ppc_r4,ptr));
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
					ppc_sh_load(ppc_rarg0,op->rs1);
					if (op->rs3.is_imm())
					{
						verify(op->rs3.is_imm_s16());
						ppc_addi(ppc_rarg0,ppc_rarg0,op->rs3._imm);
					}
					else if (op->rs3.is_r32i())
					{
						ppc_sh_load(ppc_rarg1,op->rs3);
						ppc_addx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
					}
					else if (!op->rs3.is_null())
					{
						die("invalid rs3");
					}
				}

				if (!isram)
				{
					switch(op->flags)
					{
					case 1:
						if (!fuct) fuct=(void*)ReadMem8;
						ppc_call(fuct);
						ppc_extsbx(ppc_rrv0,ppc_rrv0,0);
						break;
					case 2:
						if (!fuct) fuct=(void*)ReadMem16;
						ppc_call(fuct);
						ppc_extshx(ppc_rrv0,ppc_rrv0,0);
						break;
					case 4:
						if (!fuct) fuct=(void*)ReadMem32;
						ppc_call(fuct);
						break;
					case 8:
						if (!fuct) fuct=(void*)ReadMem64;
						ppc_call(fuct);
						break;
					default:
						verify(false);
					}
				}
				
				ppc_sh_store(ppc_rrv0,op->rd);

				if (op->flags==8)
				{
					ppc_sh_store(ppc_rrv1,op->rd._reg+1);
				}
			}
			break;

		case shop_writem:
			{
				ppc_sh_load(ppc_rarg0,op->rs1);
				

				if (op->flags==8)
				{
					ppc_sh_load(ppc_rarg2,op->rs2);
					ppc_sh_load(ppc_rarg3,op->rs2._reg+1);
				}
				else
					ppc_sh_load(ppc_rarg1,op->rs2);

				if (op->rs3.is_imm())
				{
					verify(op->rs3.is_imm_s16());
					ppc_addi(ppc_rarg0,ppc_rarg0,op->rs3._imm);
				}
				else if (op->rs3.is_r32i())
				{
					ppc_sh_load(ppc_rarg3,op->rs3);

					ppc_addx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0,0);
				}
				else if (!op->rs3.is_null())
				{
					printf("rs3: %08X\n",op->rs3.type);
					die("invalid rs3");
				}

				switch(op->flags)
				{
				case 1:
					ppc_andi(ppc_rarg1,ppc_rarg1,0xFF);
					ppc_call(&WriteMem8);
					break;
				case 2:
					ppc_andi(ppc_rarg1,ppc_rarg1,0xFFFF);
					ppc_call(&WriteMem16);
					break;
				case 4:
					ppc_call(&WriteMem32);
					break;
				case 8:
					ppc_call(&WriteMem64);
					break;
				default:
					die("invalid size on memwrite");
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
		case shop_add: binop3_start(op); ppc_addx(bop_d,bop_a,bop_b,0,0); binop3_end(op); break;
		case shop_sub: binop3_start(op); ppc_subfx(bop_d,bop_b,bop_a,0,0); binop3_end(op); break;

		case shop_or:  binop3_start(op); ppc_orx(bop_d,bop_a,bop_b,0);  binop3_end(op); break;
		case shop_and: binop3_start(op); ppc_andx(bop_d,bop_a,bop_b,0); binop3_end(op); break;
		case shop_xor: binop3_start(op); ppc_xorx(bop_d,bop_a,bop_b,0); binop3_end(op); break;

		case shop_shl: binop3_start(op); ppc_slwx(bop_d,bop_a,bop_b,0);  binop3_end(op); break;
		case shop_shr: binop3_start(op); ppc_srwx(bop_d,bop_a,bop_b,0);  binop3_end(op); break;
		case shop_sar: binop3_start(op); ppc_srawx(bop_d,bop_a,bop_b,0); binop3_end(op); break;
		case shop_mul_i32: binop3_start(op); ppc_mullwx(bop_d,bop_a,bop_b,0,0); binop3_end(op); break;


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
		case shop_test:	// rd = (r1 & r2) == 0
			binop_start(op);
			ppc_andx(ppc_rarg0,ppc_rarg0,ppc_rarg1,1);	// and. sets CR0
			emit_cr0_bit_to_rarg0(BI_CR0_EQ);
			binop_end(op);
			break;
		case shop_seteq: binop_start(op); emit_setcc_signed(op,BI_CR0_EQ,false); break;
		case shop_setgt: binop_start(op); emit_setcc_signed(op,BI_CR0_GT,false); break;
		case shop_setge: binop_start(op); emit_setcc_signed(op,BI_CR0_LT,true);  break; // >= == !<
		case shop_setab: binop_start(op); emit_setcc_unsigned(op,BI_CR0_GT,false); break;
		case shop_setae: binop_start(op); emit_setcc_unsigned(op,BI_CR0_LT,true);  break; // >= == !<

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