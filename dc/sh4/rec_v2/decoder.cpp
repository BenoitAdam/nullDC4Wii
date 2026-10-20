/*
	Ugly, hacky, bad code
	It decodes sh4 opcodes too
*/

#include "types.h"
#include "decoder.h"
#include "shil.h"
#include "ngen.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/mem/sh4_mem.h"
#include "decoder_opcodes.h"

#define NullAddress 0xFFFFFFFF

static shil_param mk_imm(u32 immv)
{
	return shil_param(FMT_IMM,immv);
}
static shil_param mk_reg(Sh4RegType reg)
{
	return shil_param(reg);
}
static shil_param mk_regi(int reg)
{
	return mk_reg((Sh4RegType)reg);
}
enum NextDecoderOperation
{
	NDO_NextOp,		//pc+=2
	NDO_End,		//End the block, Type = BlockEndType
	NDO_Delayslot,	//pc+=2, NextOp=DelayOp
	NDO_Jump,		//pc=JumpAddr,NextOp=JumpOp
};

void DecodedBlock::Setup(u32 rpc)
{
	start=rpc;
	cycles=0;
	opcodes=0;

	BranchBlock=0xFFFFFFFF;
	NextBlock=0xFFFFFFFF;
	idle_hash=0;

	BlockType=BET_SCL_Intr;
	oplist.clear();
}

// ----------------------------------------------------------------------------
// IDLE_KAMUI preset (wii/main.cpp; flycastPS3 other_fix.md 2.1 / P89).
// Kamui2's vsync wait loop:
//     do { hook(7); [hook(12);] count--; } while (vbl == old && count);
//   4D0B E407     jsr @r13 / mov #7,r4      hook(7)
//   63F2 6232 72FF 2322                     count--  (a store: no pure-loop skip)
//   53F1 6232 32E0 8B03                     bf out if vbl changed
//   61F2 6312 2338 8BF1                     loop while count != 0
// The x30 idle rule still lets it run ~13 turns a slice. Matched here as the
// 4-opcode block that closes the loop (the fall-through of the "bf +3"),
// reading BACKWARDS from that block's last word: the first read is the block's
// own bf, and its target is exactly the form's first word, so every read stays
// inside the loop it branches to. A whole loop, never "jsr; mov #7,r4" alone
// (CvS2 has 53 of those), and not Reicast's half-block hashes (one matched
// plain function returns). Word 0 = length.
// ----------------------------------------------------------------------------
#define IDLE_KAMUI_MAX 16
static const u16 idle_kamui[][IDLE_KAMUI_MAX + 1] = {
	// Hook 7: CvS2, Spider-Man, Project Justice, Power Stone 2, Donald Duck, Rayman 2, Re-Volt, Ecco, DOA2.
	{ 14, 0x4D0B, 0xE407, 0x63F2, 0x6232, 0x72FF, 0x2322, 0x53F1, 0x6232, 0x32E0, 0x8B03,
	  0x61F2, 0x6312, 0x2338, 0x8BF1 },
	// Hooks 7 and 12: Ikaruga, Rez, Border Down, Sonic Shuffle.
	{ 16, 0x4E0B, 0xE407, 0x4E0B, 0xE40C, 0x63F2, 0x6232, 0x72FF, 0x2322, 0x53F1, 0x6232, 0x32D0, 0x8B03,
	  0x61F2, 0x6312, 0x2338, 0x8BEF },
	// Hooks 7 and 12, other registers: Quake III Arena, Phantasy Star Online Ver. 2.
	{ 16, 0x4E0B, 0xE407, 0x4E0B, 0xE40C, 0x62F2, 0x6322, 0x73FF, 0x2232, 0x52F1, 0x6322, 0x33D0, 0x8B03,
	  0x61F2, 0x6312, 0x2338, 0x8BEF },
};
#define IDLE_KAMUI_FORMS (sizeof(idle_kamui) / sizeof(idle_kamui[0]))

// The form whose loop this 4-opcode block closes, or -1.
static int idle_kamui_form(u32 addr, u32 opcodes)
{
	if (opcodes != 4)
		return -1;
	for (u32 f = 0; f < IDLE_KAMUI_FORMS; f++)
	{
		const u32 n = idle_kamui[f][0];
		u32 i = n;
		while (i > 0 && ReadMem16(addr + 6 - (n - i) * 2) == idle_kamui[f][i])
			i--;
		if (i == 0)
			return (int)f;
	}
	return -1;
}

extern "C" int get_idle_kamui_preset();

// ----------------------------------------------------------------------------
// IDLE_SHINOBI preset (wii/main.cpp; flycastPS3 other_fix.md 2.2 / P94).
// The Shinobi library's idle fiber walks a 16-slot task table forever:
//     for (;;) for (i = 0; i < 16; i++) if (task[i].ready) task[i].fn(...);
// Its blocks store, so no pure-loop rule skips it. Matched as the 4-opcode
// test block that the for-init jumps to once per walk:
//     E310 51F1 3133 8BE1   mov #16,r3 / mov.l @(4,r15),r1 / cmp/ge r3,r1 / bf
// plus the 26 words from the bf target (word 0) up to it. Read BACKWARDS from
// the block's own bf, whose target is word 0, so every read stays in the loop.
// Words 12-13 are the table address (a mov.l @(disp,pc) literal) and differ
// per game. The block gets idle_hash = 16: a debt of 15 slices, paid one slice
// per block entry, and dropped by the next dispatched interrupt (mainloop in
// wii_driver.cpp), which is what makes a task ready.
// ----------------------------------------------------------------------------
#define IDLE_TASK_WORDS 30
#define IDLE_TASK_TABLE 12
static const u16 idle_task_loop[IDLE_TASK_WORDS] = {
	0x51F1, 0x4108, 0x4100, 0xD304, 0x313C, 0x1F12, 0x6212, 0x2228, 0x8B04, 0xA00C, 0x0009, 0x0000, 0x0000, 0x0000,
	0x53F2, 0x6232, 0x54F2, 0x5441, 0x420B, 0x0009, 0x63F2, 0x230B, 0x2F32, 0x52F1, 0x7201, 0x1F21,
	0xE310, 0x51F1, 0x3133, 0x8BE1 };

// True when this 4-opcode block is the task loop's test.
static bool idle_task_rule(u32 addr, u32 opcodes)
{
	if (opcodes != 4)
		return false;
	for (u32 i = IDLE_TASK_WORDS; i-- > 0;)
		if ((i < IDLE_TASK_TABLE || i > IDLE_TASK_TABLE + 1) &&
		    ReadMem16(addr + 8 - (IDLE_TASK_WORDS - i) * 2) != idle_task_loop[i])
			return false;
	return true;
}

extern "C" int get_idle_shinobi_preset();
extern "C" int get_idle_x3_preset();

DecodedBlock block;
struct
{
	NextDecoderOperation NextOp;
	NextDecoderOperation DelayOp;
	NextDecoderOperation JumpOp;
	u32 JumpAddr;
	u32 NextAddr;
	BlockEndType BlockType;

	struct
	{
		bool FPR64;	//64 bit fpu opcodes
		bool FSZ64;	//64 bit fpu moves
		bool RoundToZero;	//false -> Round to nearest.
		u32 rpc;
		bool is_delayslot;
	}cpu;

	ngen_features ngen;

	struct
	{
		bool has_readm;
		bool has_writem;
		bool has_fpu;
	} info;

	void Setup(u32 rpc,fpscr_type fpu_cfg)
	{
		cpu.rpc=rpc;
		cpu.is_delayslot=false;
		cpu.FPR64=fpu_cfg.PR;
		cpu.FSZ64=fpu_cfg.SZ;
		cpu.RoundToZero=fpu_cfg.RM==1;
		verify(fpu_cfg.RM<2);
		//what about fp/fs ?

		NextOp=NDO_NextOp;
		BlockType=BET_SCL_Intr;
		JumpAddr=0xFFFFFFFF;
		NextAddr=0xFFFFFFFF;

		info.has_readm=false;
		info.has_writem=false;
		info.has_fpu=false;
	}
} state;

static void dec_fallback(u32 op)
{
	shil_opcode opcd;
	opcd.op=shop_ifb;

	opcd.rs1=shil_param(FMT_IMM,OpDesc[op]->NeedPC());

	opcd.rs2=shil_param(FMT_IMM,state.cpu.rpc+2);
	opcd.rs3=shil_param(FMT_IMM,op);
	block.oplist.push_back(opcd);
}

// JIT_NEWOPS preset (wii/main.cpp). Nine opcodes that always took the shop_ifb
// interpreter path despite the code to compile them already existing. Eight
// have hand-written decoder handlers further down this file that were never
// referenced from OpDesc[].rec_oph; the ninth, stc SR,<REG_N>, reaches
// dec_generic's DM_ReadSRF mode, which likewise had no table row pointing at
// it. Both dispatch paths consult this list, so with the preset off all nine
// fall back exactly as they did before.
extern "C" int get_jit_newops_preset();

static bool dec_is_newop(u32 op)
{
	switch(op&0xF0FF)
	{
	case 0x0002:	//stc SR,<REG_N>   (via dec_generic's DM_ReadSRF, not a rec_oph)
	case 0x4003:	//stc.l SR,@-<REG_N>
	case 0x4007:	//ldc.l @<REG_N>+,SR
	case 0x400E:	//ldc <REG_N>,SR
	case 0x401B:	//tas.b @<REG_N>
	case 0x4024:	//rotcl <REG_N>
	case 0x4025:	//rotcr <REG_N>
	case 0x4066:	//lds.l @<REG_N>+,FPSCR
	case 0x406A:	//lds <REG_N>,FPSCR
		return true;
	}
	return false;
}

// JIT_CARRY preset (wii/main.cpp). The SH4's carry/overflow arithmetic --
// addc/subc/negc/addv/subv/div1 -- plus cmp/str, xtrct, swap.b and clrmac.
// Every one of these took the shop_ifb interpreter path because SHIL had no
// opcode that could express "produce a value AND the new T bit". The shil ops
// now exist (shop_adc/sbc/negc/addv/subv/div1/cmpstr/swaplb, see
// dc/sh4/rec_v2/shil_canonical.h) with native PPC codegen behind them; the
// table rows for these opcodes carry a `decode` word that reaches dec_generic
// below. With the preset off, this gate sends them back to the interpreter so
// the change is a clean A/B.
//
// The div1 semantics also CHANGE with this preset: the interpreter's
// i0011_nnnn_mmmm_0100 never applied the `q ^ M ^ carry` step, so its Q (and
// therefore T) was wrong whenever the bit shifted out of Rn was set, or
// whenever M was 1 (signed division). shop_div1 implements the architectural
// sequence. This is invisible in most games because the 32-step rotcl/div1
// idiom is pattern-matched away by MatchDiv32 -- only the unmatchable
// prologue/epilogue div1s ever execute (see the [DIV] probe).
extern "C" int get_jit_carry_preset();

static bool dec_is_carryop(u32 op)
{
	switch(op&0xF00F)
	{
	case 0x300E:	//addc <REG_M>,<REG_N>
	case 0x300F:	//addv <REG_M>,<REG_N>
	case 0x300A:	//subc <REG_M>,<REG_N>
	case 0x300B:	//subv <REG_M>,<REG_N>
	case 0x3004:	//div1 <REG_M>,<REG_N>
	case 0x600A:	//negc <REG_M>,<REG_N>
	case 0x6008:	//swap.b <REG_M>,<REG_N>
	case 0x200C:	//cmp/str <REG_M>,<REG_N>
	case 0x200D:	//xtrct <REG_M>,<REG_N>
		return true;
	}
	return op==0x0028;	//clrmac
}

// JIT_MAC preset (wii/main.cpp). mac.l / mac.w — the last two opcodes with no
// dynarec path at all. Kept OFF the jit_carry switch on purpose: these are the
// only ones in the set that touch MEMORY, so if something goes wrong on
// hardware you want to be able to bisect them separately.
//
// Two wins, one of them not about speed:
//   * The two @Rm+/@Rn+ reads become ordinary shop_readm ops, so they go
//     through FASTMEM. On the shop_ifb path they went through the interpreter's
//     ReadMem, which does the full address decode every time.
//   * Saturation (SR.S=1) actually works now. The interpreter's mac.l does
//     `verify(sr.S == 0)` and DIES; its mac.w prints a line and skips the whole
//     instruction — including the two register post-increments, so the guest's
//     pointers silently stop advancing. Neither has ever been hit in practice,
//     which is why it went unnoticed.
extern "C" int get_jit_mac_preset();

static bool dec_is_macop(u32 op)
{
	//mac.l @<REG_M>+,@<REG_N>+ / mac.w @<REG_M>+,@<REG_N>+
	return (op&0xF00F)==0x000F || (op&0xF00F)==0x400F;
}

#if 1
/*
#define		FMT_I32 OMG!THIS!IS!WRONG++!!
#define 	FMT_F32 OMG!THIS!IS!WRONG++!!
#define		FMT_F32 OMG!THIS!IS!WRONG++!!
#define 	FMT_TYPE OMG!THIS!IS!WRONG++!!

#define 	FMT_REG OMG!THIS!IS!WRONG++!!
#define 	FMT_IMM OMG!THIS!IS!WRONG++!!

#define 	FMT_PARAM OMG!THIS!IS!WRONG++!!
#define 	FMT_MASK OMG!THIS!IS!WRONG++!!
*/

static void dec_DynamicSet(u32 regbase,u32 offs=0)
{
	if (offs==0)
		block.Emit(shop_jdyn,reg_pc_dyn,mk_reg((Sh4RegType)regbase));
	else
		block.Emit(shop_jdyn,reg_pc_dyn,mk_reg((Sh4RegType)regbase),mk_imm(offs));
}

static void dec_End(u32 dst,BlockEndType flags,bool delay)
{
	if (state.ngen.OnlyDynamicEnds && flags == BET_StaticJump)
	{
		block.Emit(shop_mov32,mk_reg(reg_nextpc),mk_imm(dst));
		dec_DynamicSet(reg_nextpc);
		dec_End(NullAddress,BET_DynamicJump,delay);
		return;
	}

	if (state.ngen.OnlyDynamicEnds)
	{
		verify(flags == BET_DynamicJump);
	}

	state.BlockType=flags;
	state.NextOp=delay?NDO_Delayslot:NDO_End;
	state.DelayOp=NDO_End;
	state.JumpAddr=dst;
	state.NextAddr=state.cpu.rpc+2+(delay?2:0);
}

#define GetN(str) ((str>>8) & 0xf)
#define GetM(str) ((str>>4) & 0xf)
#define GetImm4(str) ((str>>0) & 0xf)
#define GetImm8(str) ((str>>0) & 0xff)
#define GetSImm8(str) ((s8)((str>>0) & 0xff))
#define GetImm12(str) ((str>>0) & 0xfff)
#define GetSImm12(str) (((s16)((GetImm12(str))<<4))>>4)


#define SR_STATUS_MASK 0x700083F2
#define SR_T_MASK 1

static u32 dec_jump_simm8(u32 op)
{
	return state.cpu.rpc + GetSImm8(op)*2 + 4;
}
static u32 dec_jump_simm12(u32 op)
{
	return state.cpu.rpc + GetSImm12(op)*2 + 4;
}
static u32 dec_set_pr()
{
	u32 retaddr=state.cpu.rpc + 4;
	block.Emit(shop_mov32,reg_pr,mk_imm(retaddr));
	return retaddr;
}
static void dec_write_sr(shil_param src)
{
	block.Emit(shop_and,mk_reg(reg_sr_status),src,mk_imm(SR_STATUS_MASK));
	block.Emit(shop_and,mk_reg(reg_sr_T),src,mk_imm(SR_T_MASK));
}
//bf <bdisp8>
sh4dec(i1000_1011_iiii_iiii)
{
	dec_End(dec_jump_simm8(op),BET_Cond_0,false);
}
//bf.s <bdisp8>
sh4dec(i1000_1111_iiii_iiii)
{
	block.Emit(shop_jcond,reg_pc_dyn,reg_sr_T);
	dec_End(dec_jump_simm8(op),BET_Cond_0,true);
}
//bt <bdisp8>
sh4dec(i1000_1001_iiii_iiii)
{
	dec_End(dec_jump_simm8(op),BET_Cond_1,false);
}
//bt.s <bdisp8>
sh4dec(i1000_1101_iiii_iiii)
{
	block.Emit(shop_jcond,reg_pc_dyn,reg_sr_T);
	dec_End(dec_jump_simm8(op),BET_Cond_1,true);
}
//bra <bdisp12>
sh4dec(i1010_iiii_iiii_iiii)
{
	dec_End(dec_jump_simm12(op),BET_StaticJump,true);
}
//braf <REG_N>
sh4dec(i0000_nnnn_0010_0011)
{
	u32 n = GetN(op);

	dec_DynamicSet(reg_r0+n,state.cpu.rpc + 4);
	dec_End(NullAddress,BET_DynamicJump,true);
}
//jmp @<REG_N>
sh4dec(i0100_nnnn_0010_1011)
{
	u32 n = GetN(op);

	dec_DynamicSet(reg_r0+n);
	dec_End(NullAddress,BET_DynamicJump,true);
}
//bsr <bdisp12>
sh4dec(i1011_iiii_iiii_iiii)
{
	//TODO: set PR
	dec_set_pr();
	dec_End(dec_jump_simm12(op),BET_StaticCall,true);
}
//bsrf <REG_N>
sh4dec(i0000_nnnn_0000_0011)
{
	u32 n = GetN(op);
	//TODO: set PR
	u32 retaddr=dec_set_pr();
	dec_DynamicSet(reg_r0+n,retaddr);
	dec_End(NullAddress,BET_DynamicCall,true);
}
//jsr @<REG_N>
sh4dec(i0100_nnnn_0000_1011) 
{
	u32 n = GetN(op);

	//TODO: Set pr
	dec_set_pr();
	dec_DynamicSet(reg_r0+n);
	dec_End(NullAddress,BET_DynamicCall,true);
}
//rts
sh4dec(i0000_0000_0000_1011)
{
	dec_DynamicSet(reg_pr);
	dec_End(NullAddress,BET_DynamicRet,true);
}
//rte
sh4dec(i0000_0000_0010_1011)
{
	//TODO: Write SR, Check intr
	dec_write_sr(reg_ssr);
	block.Emit(shop_sync_sr);
	dec_DynamicSet(reg_spc);
	dec_End(NullAddress,BET_DynamicIntr,true);
}
//trapa #<imm>
sh4dec(i1100_0011_iiii_iiii)
{
	//TODO: ifb
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress,BET_DynamicJump,false);
}
//sleep
sh4dec(i0000_0000_0001_1011)
{
	//TODO: ifb
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress,BET_DynamicJump,false);
}

//illegal instruction handler
void dec_illegalOp(u32 op)
{
	printf("WARNING: Illegal instruction %04x at pc %08x\n", op, state.cpu.rpc);
	dec_fallback(op);
	dec_DynamicSet(reg_nextpc);
	dec_End(NullAddress, BET_DynamicJump, false);
}

//ldc.l @<REG_N>+,SR
sh4dec(i0100_nnnn_0000_0111)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read from memory into temp
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_temp, rn, shil_param(), 4);
	
	// Increment pointer
	block.Emit(shop_add, rn, rn, mk_imm(4));
	
	// Write temp to SR
	dec_write_sr(reg_temp);
	block.Emit(shop_sync_sr);
	
	// Check interrupts
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticIntr, false);
}

//ldc <REG_N>,SR
sh4dec(i0100_nnnn_0000_1110)
{
	u32 n = GetN(op);
	
	// Write SR from register
	dec_write_sr(mk_regi(reg_r0 + n));
	block.Emit(shop_sync_sr);
	
	// Check for interrupts if not in delay slot
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticIntr, false);
}

//nop !
sh4dec(i0000_0000_0000_1001)
{
}

//fschg
sh4dec(i1111_0011_1111_1101)
{
	//fpscr.SZ is bit 20
	block.Emit(shop_xor,reg_fpscr,reg_fpscr,mk_imm(1<<20));
	// SZ-only: flag it so a backend can skip UpdateFPSCR() entirely (see
	// SYNC_FPSCR_SZ_ONLY in shil.h, consumed by the Wii backend's JIT_FSCHG
	// preset). Measured at ~1.3 M/s -- 2.3 per transformed vertex -- in ChuChu
	// mouse mania, of which ~0.05% actually changed FP banks. Dreamcast T&L
	// brackets its vertex loop with fschg to get pair-width fmov, so every
	// other one was paying a C call wrapped in a full pinned-FPU spill/reload
	// for no state change at all.
	//
	// The 5th positional argument of Emit() is `flags` (see decoder.h); the
	// three shil_param()s before it are rd/rs1/rs2, which sync_fpscr does not
	// use. frchg and the two ldc..FPSCR forms deliberately do NOT set this.
	block.Emit(shop_sync_fpscr,shil_param(),shil_param(),shil_param(),SYNC_FPSCR_SZ_ONLY);
	state.cpu.FSZ64=!state.cpu.FSZ64;
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//frchg
sh4dec(i1111_1011_1111_1101)
{
	//fpscr.FR is bit 21
	block.Emit(shop_xor, reg_fpscr, reg_fpscr, mk_imm(1 << 21));
	
	// Si shop_sync_fpscr existe:
	block.Emit(shop_sync_fpscr);
	// SINON utilisez:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//rotcl <REG_N>
sh4dec(i0100_nnnn_0010_0100)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Save old T
	block.Emit(shop_mov32, reg_temp, reg_sr_T);
	
	// Set T to MSB (bit 31)
	block.Emit(shop_shr, reg_sr_T, rn, mk_imm(31));
	
	// Shift left
	block.Emit(shop_shl, rn, rn, mk_imm(1));
	
	// OR in old T (at bit 0)
	block.Emit(shop_or, rn, rn, reg_temp);
}

//rotcr <REG_N>
sh4dec(i0100_nnnn_0010_0101)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Save old T shifted to bit 31
	block.Emit(shop_shl, reg_temp, reg_sr_T, mk_imm(31));
	
	// Set T to LSB (bit 0)
	block.Emit(shop_and, reg_sr_T, rn, mk_imm(1));
	
	// Shift right
	block.Emit(shop_shr, rn, rn, mk_imm(1));
	
	// OR in old T (at bit 31)
	block.Emit(shop_or, rn, rn, reg_temp);
}

//tas.b @<REG_N>
sh4dec(i0100_nnnn_0001_1011)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read byte
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_temp, rn, shil_param(), 1);
	
	// Set T flag if zero
	block.Emit(shop_seteq, reg_sr_T, reg_temp, mk_imm(0));
	
	// Set bit 7
	block.Emit(shop_or, reg_temp, reg_temp, mk_imm(0x80));
	
	// Write back
	state.info.has_writem = true;
	block.Emit(shop_writem, shil_param(), rn, reg_temp, 1);
}

//stc.l SR,@-<REG_N>
sh4dec(i0100_nnnn_0000_0011)
{
	u32 n = GetN(op);
	
	// Combine SR status and T flag into temp
	block.Emit(shop_mov32, reg_temp, reg_sr_status);
	block.Emit(shop_or, reg_temp, reg_temp, reg_sr_T);
	
	// Write to memory with predecrement
	shil_param rn = mk_regi(reg_r0 + n);
	state.info.has_writem = true;
	block.Emit(shop_writem, shil_param(), rn, reg_temp, 4, mk_imm(-4));
	
	// Decrement pointer
	block.Emit(shop_add, rn, rn, mk_imm(-4));
}

//ldc <REG_N>,FPSCR
sh4dec(i0100_nnnn_0110_1010)
{
	u32 n = GetN(op);
	
	// Load FPSCR from register
	block.Emit(shop_mov32, reg_fpscr, mk_regi(reg_r0 + n));
	
	// Sync FPSCR
	// Si shop_sync_fpscr existe:
	block.Emit(shop_sync_fpscr);
	// SINON:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}

//ldc.l @<REG_N>+,FPSCR
sh4dec(i0100_nnnn_0110_0110)
{
	u32 n = GetN(op);
	shil_param rn = mk_regi(reg_r0 + n);
	
	// Read from memory
	state.info.has_readm = true;
	block.Emit(shop_readm, reg_fpscr, rn, shil_param(), 4);
	
	// Increment pointer
	block.Emit(shop_add, rn, rn, mk_imm(4));
	
	// Sync FPSCR
	// IF shop_sync_fpscr exist :
	block.Emit(shop_sync_fpscr);
	// ELSE:
	// dec_fallback(op);
	
	if (!state.cpu.is_delayslot)
		dec_End(state.cpu.rpc + 2, BET_StaticJump, false);
}


#endif

const Sh4RegType SREGS[] =
{
	reg_mach,
	reg_macl,
	reg_pr,
	reg_sgr,
	NoReg,
	reg_fpul,
	reg_fpscr,
	NoReg,

	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	NoReg,
	reg_dbr,
};

const Sh4RegType CREGS[] =
{
	reg_sr,
	reg_gbr,
	reg_vbr,
	reg_ssr,
	reg_spc,
	NoReg,
	NoReg,
	NoReg,

	reg_r0_Bank,
	reg_r1_Bank,
	reg_r2_Bank,
	reg_r3_Bank,
	reg_r4_Bank,
	reg_r5_Bank,
	reg_r6_Bank,
	reg_r7_Bank,
};

void dec_param(DecParam p,shil_param& r1,shil_param& r2, u32 op)
{
	switch(p)
	{
		//constants
	case PRM_PC_D8_x2:
		r1=mk_imm((state.cpu.rpc+4)+(GetImm8(op)<<1));
		break;

	case PRM_PC_D8_x4:
		r1=mk_imm(((state.cpu.rpc+4)&0xFFFFFFFC)+(GetImm8(op)<<2));
		break;
	
	case PRM_ZERO:
		r1= mk_imm(0);
		break;

	case PRM_ONE:
		r1= mk_imm(1);
		break;

	case PRM_TWO:
		r1= mk_imm(2);
		break;

	case PRM_TWO_INV:
		r1= mk_imm(~2);
		break;

	case PRM_ONE_F32:
		r1= mk_imm(0x3f800000);
		break;

	//imms
	case PRM_SIMM8:
		r1=mk_imm(GetSImm8(op));
		break;
	case PRM_UIMM8:
		r1=mk_imm(GetImm8(op));
		break;

	//direct registers
	case PRM_R0:
		r1=mk_reg(reg_r0);
		break;

	case PRM_RN:
		r1=mk_regi(reg_r0+GetN(op));
		break;

	case PRM_RM:
		r1=mk_regi(reg_r0+GetM(op));
		break;

	case PRM_FRN_SZ:
		if (state.cpu.FSZ64)
		{
			int rx=GetN(op)/2;
			if (GetN(op)&1)
				rx+=regv_xd_0;
			else
				rx+=regv_dr_0;

			r1=mk_regi(rx);
			break;
		}
		FALLTHROUGH;	//32bit size: same register as PRM_FRN
	case PRM_FRN:
		r1=mk_regi(reg_fr_0+GetN(op));
		break;

	case PRM_FRM_SZ:
		if (state.cpu.FSZ64)
		{
			int rx=GetM(op)/2;
			if (GetM(op)&1)
				rx+=regv_xd_0;
			else
				rx+=regv_dr_0;

			r1=mk_regi(rx);
			break;
		}
		FALLTHROUGH;	//32bit size: same register as PRM_FRM
	case PRM_FRM:
		r1=mk_regi(reg_fr_0+GetM(op));
		break;

	case PRM_FPUL:
		r1=mk_regi(reg_fpul);
		break;

	case PRM_FPN:	//float pair, 3 bits
		r1=mk_regi(regv_dr_0+GetN(op)/2);
		break;

	case PRM_FVN:	//float quad, 2 bits
		r1=mk_regi(regv_fv_0+GetN(op)/4);
		break;

	case PRM_FVM:	//float quad, 2 bits
		r1=mk_regi(regv_fv_0+(GetN(op)&0x3));
		break;

	case PRM_XMTRX:	//float matrix, 0 bits
		r1=mk_regi(regv_xmtrx);
		break;

	case PRM_FRM_FR0:
		r1=mk_regi(reg_fr_0+GetM(op));
		r2=mk_regi(reg_fr_0);
		break;

	case PRM_SR_T:
		r1=mk_regi(reg_sr_T);
		break;

	case PRM_SR_STATUS:
		r1=mk_regi(reg_sr_status);
		break;

	case PRM_SREG:	//FPUL/FPSCR/MACH/MACL/PR/DBR/SGR
		r1=mk_regi(SREGS[GetM(op)]);
		break;
	case PRM_CREG:	//SR/GBR/VBR/SSR/SPC/<RM_BANK>
		r1=mk_regi(CREGS[GetM(op)]);
		break;
	
	//reg/imm reg/reg
	case PRM_RN_D4_x1:
	case PRM_RN_D4_x2:
	case PRM_RN_D4_x4:
		{
			u32 shft=p-PRM_RN_D4_x1;
			r1=mk_regi(reg_r0+GetN(op));
			r2=mk_imm(GetImm4(op)<<shft);
		}
		break;

	case PRM_RN_R0:
		r1=mk_regi(reg_r0+GetN(op));
		r2=mk_regi(reg_r0);
		break;

	case PRM_RM_D4_x1:
	case PRM_RM_D4_x2:
	case PRM_RM_D4_x4:
		{
			u32 shft=p-PRM_RM_D4_x1;
			r1=mk_regi(reg_r0+GetM(op));
			r2=mk_imm(GetImm4(op)<<shft);
		}
		break;

	case PRM_RM_R0:
		r1=mk_regi(reg_r0+GetM(op));
		r2=mk_regi(reg_r0);
		break;

	case PRM_GBR_D8_x1:
	case PRM_GBR_D8_x2:
	case PRM_GBR_D8_x4:
		{
			u32 shft=p-PRM_GBR_D8_x1;
			r1=mk_regi(reg_gbr);
			r2=mk_imm(GetImm8(op)<<shft);
		}
		break;

	default:
		die("Nop supported param used");
	}
}

// ============================================================================
// 32-bit division idiom detection (ported from devcast/reicast)
//
// Compilers emit 32/32 division on SH4 as DIV0U/DIV0S followed by exactly
// 32 repetitions of "ROTCL Rq / DIV1 Rm,Rn" (64 instructions). MatchDiv32
// scans forward and verifies the full idiom with consistent registers:
//   reg1 = the ROTCL register (quotient accumulator / dividend)
//   reg2 = DIV1's Rm          (divisor)
//   reg3 = DIV1's Rn          (working remainder)
// A perfect match (score 65) lets the recompiler emit one real divide
// (shop_div32u/s + T/quotient adjust + shop_div32p2 remainder fixup) and skip
// the 64 guest opcodes. Anything else falls back to the interpreter (ifb).
// ============================================================================
#define DIV1_KEY      0x3004
#define ROTCL_KEY     0x4024
#define DIV_MASK_N    0xF0FF	// masks out the n field
#define DIV_MASK_N_M  0xF00F	// masks out both n and m fields

static Sh4RegType div_som_reg1;
static Sh4RegType div_som_reg2;
static Sh4RegType div_som_reg3;

// DIV32_FIX preset (wii/main.cpp): emit the 64/32 one's-complement replacement
// (Flycast master / flycastPS3 other_fix.md 3.1) instead of the 32/32 one that
// dropped the high word. Read at compile time, together with the Wii backend.
extern "C" int get_div32_fix_preset();

static u32 MatchDiv32(u32 pc, Sh4RegType& reg1, Sh4RegType& reg2, Sh4RegType& reg3)
{
	const bool fix = get_div32_fix_preset() != 0;
	u32 v_pc = pc;
	u32 match = 1;
	for (int i = 0; i < 32; i++)
	{
		u16 opcode = ReadMem16(v_pc);
		v_pc += 2;
		if ((opcode & DIV_MASK_N) == ROTCL_KEY)
		{
			if (reg1 == NoReg)
				reg1 = (Sh4RegType)GetN(opcode);
			else if (reg1 != (Sh4RegType)GetN(opcode))
				break;
			match++;
		}
		else
			break;

		opcode = ReadMem16(v_pc);
		v_pc += 2;
		if ((opcode & DIV_MASK_N_M) == DIV1_KEY)
		{
			if (reg2 == NoReg)
				reg2 = (Sh4RegType)GetM(opcode);
			else if (reg2 != (Sh4RegType)GetM(opcode))
				break;

			if (reg2 == reg1)
				break;

			if (reg3 == NoReg)
				reg3 = (Sh4RegType)GetN(opcode);
			else if (reg3 != (Sh4RegType)GetN(opcode))
				break;

			if (reg3 == reg1)
				break;

			// DIV32_FIX: div1 Rn,Rn divides by a value that changes every step;
			// the single divide below assumes a fixed divisor.
			if (fix && reg3 == reg2)
				break;

			match++;
		}
		else
			break;
	}

	return match;
}

// How far the last MatchDiv32 got before it broke, for the [DIV1] probe below.
static u32 s_div_match_len = 0;

static bool MatchDiv32u(u32 op, u32 pc)
{
	if (settings.dynarec.safemode)
		return false;

	div_som_reg1 = NoReg;
	div_som_reg2 = NoReg;
	div_som_reg3 = NoReg;

	// 1 (div0u) + 32 rotcl + 32 div1 = 65 on a perfect match
	s_div_match_len = MatchDiv32(pc + 2, div_som_reg1, div_som_reg2, div_som_reg3);
	return s_div_match_len == 65;
}

static bool MatchDiv32s(u32 op, u32 pc)
{
	if (settings.dynarec.safemode)
		return false;

	// DIV0S Rm,Rn fixes the divisor / working-remainder registers up front.
	div_som_reg1 = NoReg;
	div_som_reg2 = (Sh4RegType)GetM(op);
	div_som_reg3 = (Sh4RegType)GetN(op);

	s_div_match_len = MatchDiv32(pc + 2, div_som_reg1, div_som_reg2, div_som_reg3);
	return s_div_match_len == 65;
}

// ---------------------------------------------------------------------------
// [DIV1] probe. When the idiom above matches, all 64 guest opcodes collapse
// into one native divide and no div1 ever reaches the interpreter. Castlevania
// issues ~11.5k div1/s through the shop_ifb path, so its divisions are NOT the
// shape MatchDiv32 expects. This dumps the real instruction window around the
// first few offending sites, plus how far the matcher got, so the actual shape
// can be identified. Shares the ifb_probe preset — the same runs that want the
// [IFB] breakdown want this. Self-quieting: DIV1_DUMP_MAX sites, then silent.
// ---------------------------------------------------------------------------
extern "C" int get_ifb_probe_preset();

// One line per distinct site, matches included — the whole point is to see
// whether the real 32-step loops collapse, which a failure-only probe cannot
// show. Separate budgets so div0 sites and div1 sites can't starve each other.
#define DIV_SITE_MAX 16
static u32 s_div0_sites[DIV_SITE_MAX];
static u32 s_div1_sites[DIV_SITE_MAX];
static u32 s_div0_site_n = 0;
static u32 s_div1_site_n = 0;

// Returns true the first time this pc is seen (and records it).
static bool dec_div_site_new(u32* tab, u32& n, u32 pc)
{
	for (u32 i=0;i<n;i++)
		if (tab[i]==pc)
			return false;
	if (n>=DIV_SITE_MAX)
		return false;
	tab[n++]=pc;
	return true;
}

// Every div0u/div0s the decoder sees, matched or not.
static void dec_div0_probe(u32 pc, bool is_unsigned, bool matched)
{
	if (!get_ifb_probe_preset())
		return;
	if (!dec_div_site_new(s_div0_sites, s_div0_site_n, pc))
		return;

	if (matched)
	{
		printf("[DIV] div0%c pc=%08X MATCHED 65/65 -> native divide (reg1=%d reg2=%d reg3=%d)\n",
			is_unsigned ? 'u' : 's', pc,
			(int)div_som_reg1, (int)div_som_reg2, (int)div_som_reg3);
	}
	else
	{
		printf("[DIV] div0%c pc=%08X failed at %u/65 -> next op %04X (%s)\n",
			is_unsigned ? 'u' : 's', pc, s_div_match_len,
			ReadMem16(pc+2), OpDesc[ReadMem16(pc+2)]->diss);
	}
}

// A div1 about to take the interpreter path.
static void dec_div1_probe(u32 op, u32 pc)
{
	if ((op & DIV_MASK_N_M) != DIV1_KEY || !get_ifb_probe_preset())
		return;
	if (!dec_div_site_new(s_div1_sites, s_div1_site_n, pc))
		return;

	printf("[DIV] div1 ifb pc=%08X (prev %04X, next %04X)\n",
		pc, ReadMem16(pc-2), ReadMem16(pc+2));
}

bool dec_generic(u32 op)
{
	DecMode mode;DecParam d;DecParam s;shilop natop;u32 e;
	if (OpDesc[op]->decode==0)
		return false;

	// JIT_NEWOPS gate for the opcodes this preset enables through the generic
	// decoder rather than a rec_oph handler (stc SR,<REG_N> -> DM_ReadSRF).
	// Off => fall back to the interpreter exactly as before.
	if (dec_is_newop(op) && !get_jit_newops_preset())
		return false;

	// JIT_CARRY gate -- see dec_is_carryop above.
	if (dec_is_carryop(op) && !get_jit_carry_preset())
		return false;

	// JIT_MAC gate -- see dec_is_macop above.
	if (dec_is_macop(op) && !get_jit_mac_preset())
		return false;

	u64 inf=OpDesc[op]->decode;

	e=(u32)(inf>>32);
	mode=(DecMode)((inf>>24)&0xFF);
	d=(DecParam)((inf>>16)&0xFF);
	s=(DecParam)((inf>>8)&0xFF);
	natop=(shilop)((inf>>0)&0xFF);

	//if ((op&0xF00F)>=0x4005 && op<0x8000)
	{
	//	return false;
	}

	bool transfer_64=false;
	if (op>=0xF000)
	{
		state.info.has_fpu=true;
		//return false;//fpu off for now
		if (state.cpu.FPR64 /*|| state.cpu.FSZ64*/)
			return false;

		if (state.cpu.FSZ64 && (d==PRM_FRN_SZ || d==PRM_FRM_SZ || s==PRM_FRN_SZ || s==PRM_FRM_SZ))
		{
			transfer_64=true;
		}
	}

	shil_param rs1,rs2,rs3,rd;

	dec_param(s,rs2,rs3,op);
	dec_param(d,rs1,rs3,op);

	switch(mode)
	{
	case DM_ReadSRF:
		// Read SR with flags combined
		block.Emit(shop_mov32, rs1, reg_sr_status);
		block.Emit(shop_or, rs1, rs1, reg_sr_T);
		break;
	
	case DM_ADC:
		// Add/subtract with carry
		block.Emit(natop, rs1, rs1, rs2, 0, mk_reg(reg_sr_T), mk_reg(reg_sr_T));
		break;
	
	case DM_NEGC:
		// Negate with carry: 0 - src - T
		block.Emit(natop, rs1, rs2, mk_reg(reg_sr_T), 0, shil_param(), mk_reg(reg_sr_T));
		break;

	case DM_OVF:
		// addv/subv: value -> rd, signed overflow -> T. No T input.
		block.Emit(natop, rs1, rs1, rs2, 0, shil_param(), mk_reg(reg_sr_T));
		break;

	case DM_MAC:
		// mac.l/mac.w @<REG_M>+,@<REG_N>+ : e is the access size (4 or 2).
		//
		// The two loads are plain shop_readm, which is the entire point — they
		// pick up fastmem and the pinned-register addressing for free instead
		// of going through the interpreter's ReadMem. shop_readm with size 2
		// already sign-extends (lha), which is exactly what mac.w wants.
		//
		// Rn is read first, then incremented, then Rm — matching the hardware,
		// and making `mac.l @Rn+,@Rn+` (m == n) read mem[a] and mem[a+4] with
		// Rn ending at a+8, which is what the manual specifies.
		//
		// DELIBERATELY does NOT set state.info.has_readm. Its only consumer is
		// the idle-loop heuristic at the bottom of dec_DecodeBlock, which
		// multiplies a block's cycles by 30 when it is a short conditional loop
		// that reads and never writes. A MAC accumulation loop —
		// `mac.l @Rm+,@Rn+ ; dt Rk ; bf loop` — matches that shape exactly and
		// is real work, not a spin. These blocks took the shop_ifb path before,
		// which never set the flag either, so leaving it alone keeps their
		// timing identical to today's.
		block.Emit(shop_readm, mk_reg(reg_temp),  rs1, shil_param(), e);
		block.Emit(shop_add,   rs1, rs1, mk_imm(e));
		block.Emit(shop_readm, mk_reg(reg_temp2), rs2, shil_param(), e);
		block.Emit(shop_add,   rs2, rs2, mk_imm(e));
		block.Emit(natop, mk_reg(reg_macl), mk_reg(reg_temp), mk_reg(reg_temp2),
		           0, mk_reg(reg_sr_status), mk_reg(reg_mach));
		break;

	case DM_XTRCT:
		// xtrct <REG_M>,<REG_N> : Rn = (Rn >> 16) | (Rm << 16).
		// Expressed with existing single-instruction shil ops rather than a
		// dedicated one -- reg_temp is needed because rd aliases rs1 here.
		block.Emit(shop_shr, mk_reg(reg_temp), rs1, mk_imm(16));
		block.Emit(shop_shl, rs1, rs2, mk_imm(16));
		block.Emit(shop_or,  rs1, rs1, mk_reg(reg_temp));
		break;

	case DM_CLRMAC:
		block.Emit(shop_mov32, mk_reg(reg_mach), mk_imm(0));
		block.Emit(shop_mov32, mk_reg(reg_macl), mk_imm(0));
		break;

	case DM_WriteTOp:
		block.Emit(natop,reg_sr_T,rs1,rs2);
		break;

	case DM_DT:
		verify(natop==shop_sub);
		block.Emit(natop,rs1,rs1,rs2);
		block.Emit(shop_seteq,mk_reg(reg_sr_T),rs1,mk_imm(0));
		break;

	case DM_Shift:
		if (natop==shop_shl && e==1)
			block.Emit(shop_shr,mk_reg(reg_sr_T),rs1,mk_imm(31));
		else if (e==1)
			block.Emit(shop_and,mk_reg(reg_sr_T),rs1,mk_imm(1));

		block.Emit(natop,rs1,rs1,mk_imm(e));
		break;

	case DM_Rot:
		if (!(((s32)e>=0?e:-e)&0x1000))
		{
			if ((s32)e<0)
			{
				//left rorate
				block.Emit(shop_shr,mk_reg(reg_sr_T),rs2,mk_imm(31));
				e=-e;
			}
			else
			{
				//right rotate
				block.Emit(shop_and,mk_reg(reg_sr_T),rs2,mk_imm(1));
			}
		}
		e&=31;

		block.Emit(natop,rs1,rs2,mk_imm(e));
		break;

	case DM_BinaryOp://d=d op s
		if (e&1)
			block.Emit(natop,rs1,rs1,rs2,0,rs3);
		else
			block.Emit(natop,shil_param(),rs1,rs2,0,rs3);
		break;

	case DM_UnaryOp: //d= op s
		if (transfer_64 && natop==shop_mov32) 
			natop=shop_mov64;

		if (natop==shop_cvt_i2f_n && state.cpu.RoundToZero)
			natop=shop_cvt_i2f_z;

		if (e&1)
			block.Emit(natop,shil_param(),rs1);
		else
			block.Emit(natop,rs1,rs2);
		break;

	case DM_WriteM:	//write(d,s)
		{
			//0 has no effect, so get rid of it
			if (rs3.is_imm() && rs3._imm==0)
				rs3=shil_param();

			state.info.has_writem=true;
			if (transfer_64) e=(s32)e*2;
			bool update_after=false;
			if ((s32)e<0)
			{
				if (rs1._reg!=rs2._reg)	//reg shoudn't be updated if its writen
				{
					block.Emit(shop_sub,rs1,rs1,mk_imm(-e));
				}
				else
				{
					verify(rs3.is_null());
					rs3=mk_imm(e);
					update_after=true;
				}
			}

			block.Emit(shop_writem,shil_param(),rs1,rs2,(s32)e<0?-e:e,rs3);

			if (update_after)
			{
				block.Emit(shop_sub,rs1,rs1,mk_imm(-e));
			}
		}
		break;

	case DM_ReadM:
		//0 has no effect, so get rid of it
		if (rs3.is_imm() && rs3._imm==0)
				rs3=shil_param();

		state.info.has_readm=true;
		if (transfer_64) e=(s32)e*2;

		block.Emit(shop_readm,rs1,rs2,shil_param(),(s32)e<0?-e:e,rs3);
		if ((s32)e<0)
		{
			if (rs1._reg!=rs2._reg)//the reg shoudn't be updated if it was just readed
				block.Emit(shop_add,rs2,rs2,mk_imm(-e));
		}
		break;

	case DM_fiprOp:
		{
			shil_param rdd=mk_regi(rs1._reg+3);
			block.Emit(natop,rdd,rs1,rs2);
		}
		break;

	case DM_EXTOP:
		{
			block.Emit(natop,rs1,rs2,mk_imm(e==1?0xFF:0xFFFF));
		}
		break;
	
	case DM_MUL:
		{
			shilop op;
			shil_param rd=mk_reg(reg_macl);
			shil_param rd2=shil_param();

			switch((s32)e)
			{
				case 16:	op=shop_mul_u16; break;
				case -16:	op=shop_mul_s16; break;

				case -32:	op=shop_mul_i32; break;

				case 64:	op=shop_mul_u64; rd2 = mk_reg(reg_mach); break;
				case -64:	op=shop_mul_s64; rd2 = mk_reg(reg_mach); break;

				default:
					die("DM_MUL: Failed to classify opcode");
			}

			block.Emit(op,rd,rs1,rs2,0,shil_param(),rd2);
		}
		break;

	case DM_DIV0:
		{
			// Try to match the full 32-step ROTCL/DIV1 division idiom and
			// replace it with one real divide. Unmatched div0u/div0s keep the
			// original native SR bit-manipulation below; only div1 itself (no
			// decoder entry) goes to the interpreter.
			//
			// On a match we must not be in a delay slot (we skip 128 bytes of
			// guest code, which is meaningless mid-delay-slot).
			bool matched = !state.cpu.is_delayslot &&
				(e==1 ? MatchDiv32u(op, state.cpu.rpc)
				      : MatchDiv32s(op, state.cpu.rpc));

			// s_div_match_len is only meaningful if the matcher actually ran
			if (!state.cpu.is_delayslot)
				dec_div0_probe(state.cpu.rpc, e==1, matched);

			if (matched && get_div32_fix_preset())
			{
				// DIV32_FIX (Flycast master decoder.cpp, flycastPS3 3.1): reg3:reg1 is
				// a 64-bit dividend. The legacy form below passed no rs3, so it divided
				// reg1 alone (32/32): every unsigned 64/32 and ~1/3 of the SH manual's
				// signed 32/32 idiom (one's complement) came out wrong -- 100% / 32.7%
				// in a 2M-case PC simulation against the div1 chain; this form: 0.
				const shil_param q = mk_reg(div_som_reg1);
				const shil_param d = mk_reg(div_som_reg2);
				const shil_param h = mk_reg(div_som_reg3);
				const shil_param T = mk_reg(reg_sr_T);
				if (e==1)
				{
					block.Emit(shop_div32u, q, q, d, 0, h, h);          // rs3 = high word
					block.Emit(shop_and, T, q, mk_imm(1));
					block.Emit(shop_shr, q, q, mk_imm(1));
					block.Emit(shop_div32p2, h, h, d, 0, T);
				}
				else
				{
					// T bit 31 carries the quotient sign (div0s's T) into div32p2.
					block.Emit(shop_xor, T, h, d);
					block.Emit(shop_and, T, T, mk_imm(0x80000000));
					block.Emit(shop_div32s, q, q, d, 0, h, h);
					block.Emit(shop_and, mk_reg(reg_temp), q, mk_imm(1));
					block.Emit(shop_sar, q, q, mk_imm(1));
					block.Emit(shop_or, T, T, mk_reg(reg_temp));
					block.Emit(shop_div32p2, h, h, d, 0, T);
					block.Emit(shop_and, T, T, mk_imm(1));              // T back to 0/1
				}

				state.cpu.rpc += 128;
				block.cycles  += CPU_RATIO*64;
				block.opcodes += 64;
			}
			else if (matched)
			{
				if (e==1)
				{
					// div0u: unsigned divide. quo -> reg1, rem -> reg3
					block.Emit(shop_div32u, mk_reg(div_som_reg1), mk_reg(div_som_reg1), mk_reg(div_som_reg2), 0, shil_param(), mk_reg(div_som_reg3));

					// The 32nd quotient bit is still "in T" after the sequence;
					// the register holds the upper 31 bits.
					block.Emit(shop_and, mk_reg(reg_sr_T), mk_reg(div_som_reg1), mk_imm(1));
					block.Emit(shop_shr, mk_reg(div_som_reg1), mk_reg(div_som_reg1), mk_imm(1));
				}
				else
				{
					// div0s: signed divide
					block.Emit(shop_div32s, mk_reg(div_som_reg1), mk_reg(div_som_reg1), mk_reg(div_som_reg2), 0, shil_param(), mk_reg(div_som_reg3));

					block.Emit(shop_and, mk_reg(reg_sr_T), mk_reg(div_som_reg1), mk_imm(1));
					block.Emit(shop_sar, mk_reg(div_som_reg1), mk_reg(div_som_reg1), mk_imm(1));
				}

				// Non-restoring remainder fixup: reg3 = T ? rem : rem - divisor
				block.Emit(shop_div32p2, mk_reg(div_som_reg3), mk_reg(div_som_reg3), mk_reg(div_som_reg2), 0, mk_reg(reg_sr_T));

				// Skip the 64 matched guest opcodes (32x rotcl + 32x div1) and
				// account for their cycles/opcode count (the decode loop won't
				// see them). Also keeps the idle-loop detector honest.
				state.cpu.rpc += 128;
				block.cycles  += CPU_RATIO*64;
				block.opcodes += 64;
			}
			else if (e==1)
			{
				//crear QM (bits 8,9)
				u32 qm=(1<<8)|(1<<9);
				block.Emit(shop_and,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_imm(~qm));
				//clear T !
				block.Emit(shop_mov32,mk_reg(reg_sr_T),mk_imm(0));
			}
			else
			{
				//sr.Q=r[n]>>31;
				//sr.M=r[m]>>31;
				//sr.T=sr.M^sr.Q;

				//This is nasty because there isn't a temp reg ..
				//VERY NASTY

				//Clear Q & M
				block.Emit(shop_and,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_imm(~((1<<8)|(1<<9))));

				//sr.Q=r[n]>>31;
				block.Emit(shop_sar,mk_reg(reg_sr_T),rs1,mk_imm(31));
				block.Emit(shop_and,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(1<<8));
				block.Emit(shop_or,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_reg(reg_sr_T));

				//sr.M=r[m]>>31;
				block.Emit(shop_sar,mk_reg(reg_sr_T),rs2,mk_imm(31));
				block.Emit(shop_and,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(1<<9));
				block.Emit(shop_or,mk_reg(reg_sr_status),mk_reg(reg_sr_status),mk_reg(reg_sr_T));

				//sr.T=sr.M^sr.Q;
				block.Emit(shop_xor,mk_reg(reg_sr_T),rs1,rs2);
				block.Emit(shop_shr,mk_reg(reg_sr_T),mk_reg(reg_sr_T),mk_imm(31));
			}
		}
		break;

	default:
		verify(false);
	}

	return true;
}

int stuffcmp(const void* p1,const void* p2)
{
	sh4_opcodelistentry* a=(sh4_opcodelistentry*)p1;
	sh4_opcodelistentry* b=(sh4_opcodelistentry*)p2;

	return b->fallbacks-a->fallbacks;
}
DecodedBlock* dec_DecodeBlock(u32 startpc,fpscr_type fpu_cfg,u32 max_cycles)
{
	block.Setup(startpc);
	state.Setup(startpc,fpu_cfg);
#ifndef HOST_NO_REC
	ngen_GetFeatures(&state.ngen);
#endif
	for(;;)
	{
		switch(state.NextOp)
		{
		case NDO_Delayslot:
			state.NextOp=state.DelayOp;
			state.cpu.is_delayslot=true;
			FALLTHROUGH;	//there is no break here by design
		case NDO_NextOp:
			{
				if (block.cycles>=max_cycles && !state.cpu.is_delayslot)
				{
					dec_End(state.cpu.rpc,BET_StaticJump,false);
				}
				else
				{
					u32 op=ReadMem16(state.cpu.rpc);
					block.opcodes++;
					if (op>=0xF000)
						block.cycles+=0;
					else
						block.cycles+=CPU_RATIO;

					RecOpCallFP* rec_oph=OpDesc[op]->rec_oph;
					if (rec_oph && dec_is_newop(op) && !get_jit_newops_preset())
						rec_oph=0;

					if (state.ngen.OnlyDynamicEnds || !rec_oph)
					{
						if (state.ngen.InterpreterFallback || !dec_generic(op))
						{
							dec_div1_probe(op,state.cpu.rpc);
							dec_fallback(op);
							if (OpDesc[op]->SetPC())
							{
								dec_DynamicSet(reg_nextpc);
								dec_End(NullAddress,BET_DynamicJump,false);
							}
							if (OpDesc[op]->SetFPSCR() && !state.cpu.is_delayslot)
							{
								dec_End(state.cpu.rpc+2,BET_StaticJump,false);
							}
						}
					}
					else
					{
						rec_oph(op);
					}

					state.cpu.rpc+=2;
				}
			}
			break;

		case NDO_Jump:
			state.NextOp=state.JumpOp;
			state.cpu.rpc=state.JumpAddr;
			break;

		case NDO_End:
			goto _end;
		}
	}

_end:
	block.sh4_code_size=state.cpu.rpc-block.start;
	block.NextBlock=state.NextAddr;
	block.BranchBlock=state.JumpAddr;
	block.BlockType=state.BlockType;

	//cycle tricks
	{
		//Small-n-simple idle loop detector :p
		if (state.info.has_readm && !state.info.has_writem && !state.info.has_fpu && block.opcodes<6)
		{
			if (block.BlockType==BET_Cond_0 || block.BlockType==BET_Cond_1)
			{
				// IDLE_X3 (per game; other_fix.md 2.3 / P71): x30 also bills
				// small work blocks (loop tails, null checks), so a game can
				// drop frames in EMULATED time while SPEED reads 100%.
				// Reicast's x3, only for bf or a backward bt, bills them closer
				// to real; it costs ~x1.5 SH4 host time, so it only pays for a
				// game below its frame rate with host time to spare.
				if (!get_idle_x3_preset())
					block.cycles*=30;
				else if (block.BlockType==BET_Cond_0 || block.BranchBlock<=block.start)
					block.cycles*=3;
			}

			if (block.BranchBlock==block.start)
			{
				block.cycles*=10;
			}
		}

		//if in syscalls area (ip.bin etc) skip fast :p
		if ((block.start&0x1FFF0000)==0x0C000000)
		{
			if (block.start&0x8000)
			{
				//ip.bin (boot loader/img etc)
				block.cycles*=15;
			}
			else
			{
				//syscalls
				block.cycles*=5;
			}
			
		}
		//make sure we don't use wayy-too-many cycles ;p
		block.cycles=min(block.cycles,max_cycles);
	}

	// IDLE_KAMUI: the tail of a known vsync wait loop ends the timeslice after
	// one turn (the backend loads r29 = 0 after this block's cycle check).
	block.idle_hash = 0;
	if (get_idle_kamui_preset())
	{
		const int form = idle_kamui_form(block.start, block.opcodes);
		if (form >= 0)
		{
			block.idle_hash = 1;
			// One line per newly compiled match, bounded: proves the rule fires
			// and gives the tail address to compare with the PS3's vectors
			// (CvS2 8C21A2EE, Ikaruga 8C0370D2, Rez 8C046052, Quake III 8C0CB69E).
			static u32 s_idle_kamui_log = 0;
			if (s_idle_kamui_log < 16)
			{
				s_idle_kamui_log++;
				printf("[IDLE] Kamui vsync wait form %d at %08X\n", form, block.start);
				fflush(stdout);
			}
		}
	}

	// IDLE_SHINOBI: the test block of the Shinobi idle fiber's task walk
	// leaves a debt of 15 slices (r29 = -15 * timeslice after its check),
	// dropped by the next interrupt (wii_driver.cpp mainloop).
	if (!block.idle_hash && get_idle_shinobi_preset() &&
	    idle_task_rule(block.start, block.opcodes))
	{
		block.idle_hash = 16;
		// Same bounded log; PS3 vectors: Garou 8C015E70, Sonic Shuffle
		// 8C0158A0, Border Down 8C04838C.
		static u32 s_idle_task_log = 0;
		if (s_idle_task_log < 16)
		{
			s_idle_task_log++;
			printf("[IDLE] Shinobi task loop at %08X\n", block.start);
			fflush(stdout);
		}
	}

	static int cc=0;
	cc++;
	if ((cc&31)==0 && HOST_OS==OS_WINDOWS)
	{
		vector<sh4_opcodelistentry> stuff;
		for (u32 i=0;opcodes[i].oph;i++)
		{
			if (opcodes[i].fallbacks)
				stuff.push_back(opcodes[i]);
		}
		if (stuff.size() && 0)
		{
			printf("Compile stats:\n");
			qsort(&stuff[0],stuff.size(),sizeof(stuff[0]),stuffcmp);
      /* Debug print, enable if you want to see the most common fallbacks
      !!! This also generates a warning at compilation for now
			for (u32 i=0;i<10 && i<stuff.size();i++)
			{
				printf("%05llu :%04llX %u\n",
       (unsigned long long)stuff[i].fallbacks,
       (unsigned long long)stuff[i].rez,
       stuff[i].diss);
			}
      */
		}
	}
	return &block;
}
void dec_Cleanup()
{
	block.oplist.clear();
}

