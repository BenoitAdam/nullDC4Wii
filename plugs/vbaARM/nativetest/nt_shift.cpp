// Exercise the split shift uops across all forms, immediate & register amounts,
// flag and non-flag ops, and carry-sensitive cases. Compare cached vs reference.
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

	// A straight-line program exercising shift forms; park with B self at end.
	// r0 = 0x12345678 base; r1 = shift amounts; results accumulate into r4 via EOR
	// so any wrong shift/carry diverges. Use MOVS/ANDS to exercise carry paths.
	u32 a = 0;
	auto E = [&](u32 op){ arm_WriteMem32(a, op); a += 4; };
	E(0xE59F00A0);  // LDR r0,[pc,#0xA0]  -> 0x12345678 (literal at end)
	E(0xE3A0400 + 0); // MOV r4,#0  (E3A04000)
	// LSL imm: MOVS r2, r0, LSL #4   E1B02200
	E(0xE1B02200); E(0xE0244002);   // EOR r4,r4,r2
	// LSR imm: MOVS r2, r0, LSR #5   E1B02220+? LSR#5 = (5<<7)|0x20 -> E1B022A0
	E(0xE1B022A0); E(0xE0244002);
	// ASR imm #8: E1B02240|(8<<7)=E1B02440
	E(0xE1B02440); E(0xE0244002);
	// ROR imm #3: E1B02260|(3<<7)=E1B021E0
	E(0xE1B021E0); E(0xE0244002);
	// RRX (ROR #0): E1B02060
	E(0xE1B02060); E(0xE0244002);
	// register shift: MOV r3,#3 ; MOVS r2,r0,LSL r3  E1B02310
	E(0xE3A03003); E(0xE1B02312); E(0xE0244002);   // LSL r3 form: E1B0231(2? Rm=0,Rs=3): (3<<8)|(1<<4)|0 = E1B02310 ; use Rm=0
	// ANDS with shifted operand to exercise carry-writing shift + _creg op:
	// MOV r5,#0xFF ; ANDS r6,r5,r0,LSR #1  E0156020|(1<<7)=E01560A0
	E(0xE3A050FF); E(0xE01560A0); E(0xE0244006);
	// ADD (non-flag) with shift: ADD r7,r0,r0,LSL #2  E0807100
	E(0xE0807100); E(0xE0244007);
	E(0xEAFFFFFE);  // B self (park)
	u32 park = a - 4;
	// literal pool: place 0x12345678 at the LDR target (pc of LDR=0, +8+0xA0=0xA8)
	arm_WriteMem32(0xA8, 0x12345678);

	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetReg(0,0); arm_SetNextPC(0x00); arm_SetReg(15,0x04);
	u32 pv=0xFFFFFFFF;
	for (int i=0;i<2000;i++){u32 pc=arm_GetNextPC(); if(pc==park&&pv==park)break; pv=pc; arm_Run(64);}
	printf("r2=%08X r4=%08X r6=%08X r7=%08X cpsr=%08X\n",
	       arm_GetReg(2), arm_GetReg(4), arm_GetReg(6), arm_GetReg(7), arm_GetReg(16));
	return 0;
}
