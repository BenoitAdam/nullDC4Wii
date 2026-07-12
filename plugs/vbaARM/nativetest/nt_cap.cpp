// Stress the fall-through path: a long straight-line run that crosses the
// 128-uop block cap (exercises the rehome terminator), plus a branch INTO the
// middle of an already-decoded block (mid-block entrypoint dispatch, then
// fall-through from there). Run on the active engine; compare cached vs ref.
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

	// 200 sequential ADD r0,r0,#1 (E2800001) starting at 0x00, then park.
	// 200 > MAX_BLOCK_UOPS(128), so the block caps mid-run and the rehome
	// terminator must carry execution into the next decoded chunk seamlessly.
	u32 a = 0;
	for (int i = 0; i < 200; i++) { arm_WriteMem32(a, 0xE2800001); a += 4; }
	u32 park1 = a;                       // 0x320
	arm_WriteMem32(a, 0xEAFFFFFE); a += 4;   // B self (park)

	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetReg(0, 0);
	arm_SetNextPC(0x00); arm_SetReg(15, 0x04);
	{
		u32 pv = 0xFFFFFFFF;
		for (int i = 0; i < 5000; i++) {
			u32 pc = arm_GetNextPC();
			if (pc == park1 && pv == park1) break;
			pv = pc; arm_Run(64);          // big slices: many ops per call
		}
	}
	printf("run1 r0=%08X (expect 0x000000C8 = 200)\n", arm_GetReg(0));

	// Branch INTO the middle of the (now-decoded) run: jump to 0x190 (=instr 100)
	// and run to park. Should execute exactly (200-100)=100 more ADDs from r0=0.
	memset(s_aram, 0, ARAM_SIZE);
	a = 0;
	for (int i = 0; i < 200; i++) { arm_WriteMem32(a, 0xE2800001); a += 4; }
	u32 park2 = a;
	arm_WriteMem32(a, 0xEAFFFFFE);
	// Prime the cache by running from 0 once (decode the whole thing).
	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetReg(0, 0); arm_SetNextPC(0x00); arm_SetReg(15, 0x04);
	{
		u32 pv = 0xFFFFFFFF;
		for (int i = 0; i < 5000; i++) { u32 pc = arm_GetNextPC(); if (pc==park2 && pv==park2) break; pv=pc; arm_Run(64); }
	}
	// Now re-enter mid-block at instr 150 (0x258) with r0=0 (no reset -> cache warm).
	arm_SetReg(0, 0); arm_SetNextPC(0x258); arm_SetReg(15, 0x258 + 4);
	{
		u32 pv = 0xFFFFFFFF;
		for (int i = 0; i < 5000; i++) { u32 pc = arm_GetNextPC(); if (pc==park2 && pv==park2) break; pv=pc; arm_Run(64); }
	}
	printf("run2 r0=%08X (expect 0x00000032 = 50, from instr 150..199)\n", arm_GetReg(0));
	return 0;
}
