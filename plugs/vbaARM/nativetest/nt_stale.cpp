// Cache-staleness test: the cached interpreter only invalidates on reset. If
// ARM code is overwritten in place (by the ARM itself via STR, or by SH4/DMA on
// hardware) WITHOUT a reset, the cached path runs the STALE uop while the
// reference re-fetches current memory. This reproduces that divergence.
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

	// Program: the ARM overwrites its OWN later instruction, then falls into it.
	//   0x00: MOV r0,#1            E3A00001
	//   0x04: MOV r1,#2            E3A01002      (will be overwritten -> MOV r1,#0x99)
	//   0x08: STR r2,[r3]          E5832000      (r3=0x04, r2=new opcode) self-modify
	//   0x0c: B 0x0c (park)        EAFFFFFE
	// But we must run 0x00..0x08 FIRST (caches the block incl. 0x04), then loop
	// back so 0x04 executes from cache. Use a backward branch after the store:
	//   0x00: MOV r0,#0            E3A00000   (counter base)
	//   0x04: MOV r1,#2            E3A01002   <- target of self-modify
	//   0x08: ADD r0,r0,r1         E0800001
	//   0x0c: STR r4,[r5]          E5854000   (r5=0x04, r4 = "MOV r1,#0x40" once)
	//   0x10: SUBS r6,r6,#1        E2566001
	//   0x14: BNE 0x04             1AFFFFFA   (loop back to the (now patched) 0x04)
	//   0x18: B 0x18 (park)        EAFFFFFE
	arm_WriteMem32(0x00, 0xE3A00000);
	arm_WriteMem32(0x04, 0xE3A01002);   // MOV r1,#2
	arm_WriteMem32(0x08, 0xE0800001);
	arm_WriteMem32(0x0c, 0xE5854000);   // STR r4,[r5]
	arm_WriteMem32(0x10, 0xE2566001);
	arm_WriteMem32(0x14, 0x1AFFFFFA);
	arm_WriteMem32(0x18, 0xEAFFFFFE);

	arm_SetEnabled(false); arm_SetEnabled(true);
	// r4 = patched opcode "MOV r1,#0x40" (E3A01040); r5 = 0x04 (patch target);
	// r6 = 3 loop iterations.
	arm_SetReg(4, 0xE3A01040);
	arm_SetReg(5, 0x04);
	arm_SetReg(6, 3);
	arm_SetNextPC(0x00); arm_SetReg(15, 0x04);

	u32 pv = 0xFFFFFFFF;
	for (int i = 0; i < 200; i++) {
		u32 pc = arm_GetNextPC();
		if (pc == 0x18 && pv == 0x18) break;
		pv = pc; arm_Run(1);
	}
	printf("r0=%08X  (reference re-fetches patched MOV r1,#0x40 -> r0 grows by 0x40;\n", arm_GetReg(0));
	printf("          stale cache keeps MOV r1,#2 -> r0 grows by 2)\n");
	printf("word@0x04 now = %08X\n", arm_ReadMem32(0x04));
	return 0;
}
