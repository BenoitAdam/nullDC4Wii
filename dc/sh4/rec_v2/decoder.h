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
void dec_Cleanup();

