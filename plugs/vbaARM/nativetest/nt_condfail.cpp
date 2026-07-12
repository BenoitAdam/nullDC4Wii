// Regression test for the fall-through bug: a block-ending CONDITIONAL op whose
// condition FAILS must advance to the next sequential instruction, not pc_op++
// into an adjacent pool slot. Exercises conditional B (not taken), conditional
// MOV pc (not taken), conditional LDM{pc} (not taken).
#include "arm7.h"
#include "arm_mem.h"

static u8 s_aram[ARAM_SIZE];
u8*  arm_aica_ram = s_aram;
bool e68k_out = false;

template<int sz,typename T> T fastcall ReadMemArm(u32 a){a&=0xFFFFFF;if(sz==4&&(a&3)){u32 al=a&~3u,w=*(u32*)&s_aram[al&ARAM_MASK],r=(a&3)*8;return (T)((w>>r)|(w<<(32-r)));}return *(T*)&s_aram[a&ARAM_MASK];}
template<int sz,typename T> void fastcall WriteMemArm(u32 a,T d){a&=0xFFFFFF;*(T*)&s_aram[a&ARAM_MASK]=(T)d;}
template u8 fastcall ReadMemArm<1,u8>(u32);template u16 fastcall ReadMemArm<2,u16>(u32);template u32 fastcall ReadMemArm<4,u32>(u32);
template void fastcall WriteMemArm<1,u8>(u32,u8);template void fastcall WriteMemArm<2,u16>(u32,u16);template void fastcall WriteMemArm<4,u32>(u32,u32);

int main()
{
	arm_Init();
	memset(s_aram, 0, ARAM_SIZE);

	// 0x00: MOV r0,#0           E3A00000
	// 0x04: MOVS r1,#1          E3B01001   (sets Z=0, N=0)
	// 0x08: BEQ 0x100           0A...      (NOT taken since Z=0) -> must fall to 0x0C
	// 0x0C: ADD r0,r0,#1        E2800001   (r0=1)
	// 0x10: CMP r1,#1           E3510001   (Z=1)
	// 0x14: BNE 0x100           1A...      (NOT taken since Z=1) -> fall to 0x18
	// 0x18: ADD r0,r0,#0x10     E2800010   (r0=0x11)
	// 0x1C: B 0x1C (park)       EAFFFFFE
	// 0x100: ADD r0,r0,#0x100   E2800C01   (the "taken" target; should NOT run)
	arm_WriteMem32(0x00, 0xE3A00000);
	arm_WriteMem32(0x04, 0xE3B01001);
	arm_WriteMem32(0x08, 0x0A00003C);   // BEQ ->0x08+8+0x3C*4=0x100
	arm_WriteMem32(0x0C, 0xE2800001);
	arm_WriteMem32(0x10, 0xE3510001);
	arm_WriteMem32(0x14, 0x1A000039);   // BNE ->0x14+8+0x39*4=0x100
	arm_WriteMem32(0x18, 0xE2800010);
	arm_WriteMem32(0x1C, 0xEAFFFFFE);
	arm_WriteMem32(0x100, 0xE2800C01);

	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetReg(0,0); arm_SetNextPC(0x00); arm_SetReg(15,0x04);
	u32 pv=0xFFFFFFFF;
	for (int i=0;i<500;i++){u32 pc=arm_GetNextPC(); if(pc==0x1C&&pv==0x1C)break; pv=pc; arm_Run(64);}
	printf("r0=%08X pc=%08X (expect r0=0x11, pc=0x1C; NOT 0x1xx)\n", arm_GetReg(0), arm_GetNextPC());
	return 0;
}
