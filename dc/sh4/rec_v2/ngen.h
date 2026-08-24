/*
	Header file for native generator interface
	Needs some cleanup
*/

#pragma once
#include "rec_config.h"
#include "decoder.h"
#include "blockmanager.h"


#define CODE_SIZE   (6291456) // =(6*1024*1024) // 4 or 6 MB for Wii ?


#if HOST_OS==OS_LINUX
extern "C" {
#endif
//alternative emit ptr, set to 0 to use the main buffer
extern u32* emit_ptr;

void emit_Write8(u8 data);
void emit_Write16(u16 data);
void emit_Write32(u32 data);
void emit_Skip(u32 sz);
u32 emit_FreeSpace();
void* emit_GetCCPtr();
void emit_SetBaseAddr();

//Called from ngen_FailedToFindBlock
DynarecCodeEntry* rdv_FailedToFindBlock();
//Called when a block check failed, and the block needs to be invalidated
DynarecCodeEntry* FASTCALL rdv_BlockCheckFail(u32 pc);
//Called to compile code @pc
DynarecCodeEntry* rdv_CompilePC();
//Returns 0 if there is no code @pc, code ptr otherwise
DynarecCodeEntry* rdv_FindCode();
//Finds or compiles code @pc
DynarecCodeEntry* rdv_FindOrCompile();


extern volatile bool  sh4_int_bCpuRun;
//Stuff to be implemented per dynarec core

//Called to compile a block
DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks);
//Called when blocks are reseted
void ngen_ResetBlocks();
//Value to be returned when the block manager failed to find a block,
//should call rdv_FailedToFindBlock and then jump to the return value
extern void (*ngen_FailedToFindBlock)();
//the dynarec mainloop
void ngen_mainloop();
//Drop the emitted mainloop so the next ngen_mainloop() regenerates it.
//Needed when a second game is launched from the file browser without a full
//Term/Init cycle: the code cache is wiped under it, and the accuracy preset
//(which is baked into the mainloop's timeslice at emit time) may have changed.
void ngen_ResetMainloop();
//ngen features
struct ngen_features
{
	bool OnlyDynamicEnds;		//if set the block endings aren't handled natively and only Dynamic block end type is used
	bool InterpreterFallback;	//if set all the non-branch opcodes are hanlded with the ifb opcode
};

void ngen_GetFeatures(ngen_features* dst);

//Canonical callback interface
enum CanonicalParamType
{
	CPT_u32,
	CPT_u32rv,
	CPT_u64rvL,
	CPT_u64rvH,
	CPT_f32,
	CPT_f32rv,
	CPT_ptr,
};

void ngen_CC_Start(shil_opcode* op);
void ngen_CC_Param(shil_opcode* op,shil_param* par,CanonicalParamType tp);
void ngen_CC_Call(shil_opcode*op,void* function);
void ngen_CC_Finish(shil_opcode* op);

#if HOST_OS==OS_LINUX
}
#endif
