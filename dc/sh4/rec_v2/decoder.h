#pragma once
#include "shil.h"
#include "../sh4_if.h"

#define mkbet(c,s,v) ((c<<3)|(s<<1)|v)
enum BlockEndType
{
	BET_CLS_Static=0,
	BET_CLS_Dynamic=1,
	BET_CLS_COND=2,

	BET_SCL_Jump=0,
	BET_SCL_Call=1,
	BET_SCL_Ret=2,
	BET_SCL_Intr=3,
	

	BET_StaticJump=mkbet(BET_CLS_Static,BET_SCL_Jump,0),	//BranchBlock is jump target
	BET_StaticCall=mkbet(BET_CLS_Static,BET_SCL_Call,0),	//BranchBlock is jump target, NextBlock is ret hint
	BET_StaticIntr=mkbet(BET_CLS_Static,BET_SCL_Intr,0),	//(pending inttr!=0) -> Intr else NextBlock

	BET_DynamicJump=mkbet(BET_CLS_Dynamic,BET_SCL_Jump,0),	//pc+2 is jump target
	BET_DynamicCall=mkbet(BET_CLS_Dynamic,BET_SCL_Call,0),	//pc+2 is jump target, NextBlock is ret hint
	BET_DynamicRet=mkbet(BET_CLS_Dynamic,BET_SCL_Ret,0),	//pr is jump target
	BET_DynamicIntr=mkbet(BET_CLS_Dynamic,BET_SCL_Intr,0),	//(pending inttr!=0) -> Intr else Dynamic

	BET_Cond_0=mkbet(BET_CLS_COND,BET_SCL_Jump,0),			//sr.T==0 -> BranchBlock else NextBlock
	BET_Cond_1=mkbet(BET_CLS_COND,BET_SCL_Jump,1),			//sr.T==1 -> BranchBlock else NextBlock
};

class DecodedBlock
{

public:
	void Setup(u32 rpc);

	u32 start;	//entry point, the block may be non-linear in memory
	u32 cycles;
	u32 opcodes;
	u32 sh4_code_size;	//bytes of SH4 source consumed, for the block-check guard

	u32 BranchBlock;	//STATIC_*,COND_*: jump target
	u32 NextBlock;		//*_CALL,COND_*: next block (by position)

	BlockEndType BlockType;
	vector<shil_opcode> oplist;

	void Emit(shilop op,shil_param rd=shil_param(),shil_param rs1=shil_param(),shil_param rs2=shil_param(),u32 flags=0,shil_param rs3=shil_param(),shil_param rd2=shil_param())
	{
		shil_opcode sp;
		
		sp.flags=flags;
		sp.op=op;
		sp.rd=(rd);
		sp.rd2=(rd2);
		sp.rs1=(rs1);
		sp.rs2=(rs2);
		sp.rs3=(rs3);

		oplist.push_back(sp);
	}
};

DecodedBlock* dec_DecodeBlock(u32 rpc,fpscr_type fpu_cfg,u32 max_cycles);

// shop_ifb execution profiler (see IFB_PROFILE in decoder.cpp). Distinct
// interpreter-fallback opcodes get a slot on first sight; the JIT emits an
// increment of that slot next to the interpreter call, and dec_ifb_report()
// prints the busiest as a rate once a second. IFB_SLOTS lives here so the
// backend can size nothing and just call dec_ifb_slot().
// Master switch: 0 compiles out the counters, the [IFB] boot list, and the
// increment the JIT emits. Left OFF: measured on ChuChu 2026-08-23 at only
// 17-25 K fallbacks/s during mouse mania (~0.5% of the frame), i.e. the
// interpreter path is NOT a bottleneck there. Transient bursts do reach
// ~330 K/s during CD seek/loading (stc SR / ldc SR / rotcl), but those phases
// already run at 100% speed. Flip to 1 if a game behaves as though it is
// interpreting a hot loop.
#define IFB_PROFILE 0
#define IFB_SLOTS 64
extern u32 g_ifb_count[IFB_SLOTS];
extern u16 g_ifb_op[IFB_SLOTS];
extern u32 g_ifb_slots;
u32  dec_ifb_slot(u32 op);   // compile-time only
void dec_ifb_report(double tdiff);
void dec_Cleanup();

