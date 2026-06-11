// PIC/veneer test: data-processing instructions that WRITE PC (Rd==15) using a
// rotated immediate or pc as operand. This is the pattern at 0x0C54 in the
// failing title:  SUB lr,pc,#4 ; ADD pc,pc,#(imm8 ROR rot).
// Verifies the cached path computes the PC-relative target and the rotated
// immediate exactly like the reference.
#include "arm7.h"
#include "arm_mem.h"

static u8 s_aram[ARAM_SIZE];
u8*  arm_aica_ram = s_aram;
bool e68k_out = false;

template<int sz,typename T> T fastcall ReadMemArm(u32 a){a&=0xFFFFFF;if(sz==4&&(a&3)){u32 al=a&~3u,w=*(u32*)&s_aram[al&ARAM_MASK],r=(a&3)*8;return (T)((w>>r)|(w<<(32-r)));}return *(T*)&s_aram[a&ARAM_MASK];}
template<int sz,typename T> void fastcall WriteMemArm(u32 a,T d){a&=0xFFFFFF;*(T*)&s_aram[a&ARAM_MASK]=(T)d;}
template u8 fastcall ReadMemArm<1,u8>(u32);template u16 fastcall ReadMemArm<2,u16>(u32);template u32 fastcall ReadMemArm<4,u32>(u32);
template void fastcall WriteMemArm<1,u8>(u32,u8);template void fastcall WriteMemArm<2,u16>(u32,u16);template void fastcall WriteMemArm<4,u32>(u32,u32);

static void run_from(u32 entry, u32 park, const char* tag)
{
	arm_SetNextPC(entry); arm_SetReg(15, entry + 4);
	u32 pv = 0xFFFFFFFF;
	for (int i = 0; i < 100; i++) {
		u32 pc = arm_GetNextPC();
		if (pc == park && pv == park) break;
		pv = pc; arm_Run(1);
	}
	printf("%-8s end pc=%08X r0=%08X lr=%08X\n", tag, arm_GetNextPC(),
	       arm_GetReg(0), arm_GetReg(14));
}

int main()
{
	arm_Init();
	memset(s_aram, 0, ARAM_SIZE);

	// Case A: ADD pc, pc, #(rotated imm). At 0x00, pc reads as 0x08.
	//   ADD pc,pc,#0x15C should jump to 0x08 + 0x15C = 0x164.
	//   E28FDF57 = ADD pc,pc,#(0x57 ROR 30) = 0x57<<2 = 0x15C.
	//   0x00: MOV r0,#0xAA       E3A000AA
	//   0x04: E28FDF57           ADD pc,pc,#0x15C
	//   ... gap ...
	//   0x164: MOV r0,#0xBB      E3A000BB
	//   0x168: B 0x168 (park)    EAFFFFFE
	arm_WriteMem32(0x00, 0xE3A000AA);
	arm_WriteMem32(0x04, 0xE28FDF57);
	arm_WriteMem32(0x164, 0xE3A000BB);
	arm_WriteMem32(0x168, 0xEAFFFFFE);

	// Case B: SUB lr,pc,#4 ; then MOV pc,lr returns to the SUB+? (veneer LR set).
	//   0x200: SUB lr,pc,#4      E24FE004   -> lr = (0x200+8) - 4 = 0x204
	//   0x204: ADD pc,pc,#0      E28FF000   -> pc = (0x204+8)+0 = 0x20C
	//   0x208: (skipped)
	//   0x20C: MOV pc,lr         E1A0F00E   -> back to lr=0x204 ... but 0x204 is ADD pc
	//          to avoid a loop, instead park: put B park at 0x20C.
	//   redo: 0x20C: B 0x20C     EAFFFFFE
	arm_WriteMem32(0x200, 0xE24FE004);
	arm_WriteMem32(0x204, 0xE28FF000);
	arm_WriteMem32(0x20C, 0xEAFFFFFE);

	arm_SetEnabled(false); arm_SetEnabled(true);

	run_from(0x00, 0x168, "caseA");
	printf("EXPECT   caseA end pc=00000168 r0=000000BB (ADD pc,pc,#0x15C -> 0x164)\n");

	arm_SetEnabled(false); arm_SetEnabled(true);
	run_from(0x200, 0x20C, "caseB");
	printf("EXPECT   caseB end pc=0000020C lr=00000204 (SUB lr,pc,#4; ADD pc,pc,#0->0x20C)\n");
	return 0;
}
