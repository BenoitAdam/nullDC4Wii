// Focused test: S-bit LDM with PC in the list (LDM ...,{regs,pc}^), which must
// restore CPSR from SPSR (mode switch) in addition to loading PC. Runs the SAME
// program on the active interpreter and dumps PC / CPSR / regs so the cached and
// reference builds can be diffed.
//
// Build (from nativetest/):
//   g++ -std=c++17 -O1 -fno-strict-aliasing -iquote . -I .. -DARM7_USE_CACHED \
//       ../arm7.cpp ../arm7_cached.cpp nt_ldm_spsr.cpp -o nt_ldm_c.exe
//   (drop -DARM7_USE_CACHED for the reference build)
#include "arm7.h"
#include "arm_mem.h"

static u8 s_aram[ARAM_SIZE];
u8*  arm_aica_ram = s_aram;
bool e68k_out = false;

template<int sz,typename T> T fastcall ReadMemArm(u32 a){a&=0xFFFFFF;if(a&(sz-1)){u32 al=a&~3u,w=*(u32*)&s_aram[al&ARAM_MASK],r=(a&3)*8;return (T)((w>>r)|(w<<(32-r)));}if(sz==1)a^=3;else if(sz==2)a^=2;return *(T*)&s_aram[a&ARAM_MASK];}
template<int sz,typename T> void fastcall WriteMemArm(u32 a,T d){a&=0xFFFFFF;if(a&(sz-1)){u8 t[4];t[3]=(u8)(d>>24);t[2]=(u8)(d>>16);t[1]=(u8)(d>>8);t[0]=(u8)d;for(int i=0;i<4;++i)s_aram[((a+i)^3)&ARAM_MASK]=t[i];return;}if(sz==1)a^=3;else if(sz==2)a^=2;*(T*)&s_aram[a&ARAM_MASK]=(T)d;}
template u8 fastcall ReadMemArm<1,u8>(u32);template u16 fastcall ReadMemArm<2,u16>(u32);template u32 fastcall ReadMemArm<4,u32>(u32);
template void fastcall WriteMemArm<1,u8>(u32,u8);template void fastcall WriteMemArm<2,u16>(u32,u16);template void fastcall WriteMemArm<4,u32>(u32,u32);

// Reg indices for CPSR/SPSR (see arm7.cpp).
enum { CPSR = 16, SPSR = 17 };

static void dump(const char* tag)
{
	printf("%-10s pc=%08X cpsr=%08X(mode=%02X) r0=%08X r1=%08X sp=%08X\n",
	       tag, arm_GetNextPC(), arm_GetReg(CPSR), arm_GetReg(CPSR) & 0x1F,
	       arm_GetReg(0), arm_GetReg(1), arm_GetReg(13));
}

int main()
{
	arm_Init();
	memset(s_aram, 0, ARAM_SIZE);

	// Program at 0x00 (a single S-bit LDMFD that pops r0, r1, pc and restores CPSR):
	//   LDMFD sp!, {r0,r1,pc}^   = E8FD8003  (P=0,U=1,S=1,W=1,L=1, Rn=13, regs r0|r1|pc)
	//   B self (park)            = EAFFFFFE
	arm_WriteMem32(0x00, 0xE8FD8003);
	arm_WriteMem32(0x04, 0xEAFFFFFE);

	// Park target the popped PC will land on:
	//   0x40: B self
	arm_WriteMem32(0x40, 0xEAFFFFFE);

	arm_SetEnabled(false);
	arm_SetEnabled(true);

	// Force the core into IRQ mode (0x12) with a valid SPSR that names USER mode
	// (0x10) plus N+C flags set, so the LDM ^ must switch IRQ -> USER and restore
	// those flags. We poke the register file directly (the self-test accessors
	// expose reg[]); CPUSwitchMode wiring is exercised by the LDM itself.
	//   CPSR: IRQ mode 0x12, all flags clear.
	arm_SetReg(CPSR, 0x00000012);
	//   SPSR: USER mode 0x10 + N(0x80000000) + C(0x20000000).
	arm_SetReg(SPSR, 0xA0000010);

	// Stack: sp=0x100, popping {r0=0x11111111, r1=0x22222222, pc=0x40}.
	arm_SetReg(13, 0x100);
	arm_WriteMem32(0x100, 0x11111111);  // -> r0
	arm_WriteMem32(0x104, 0x22222222);  // -> r1
	arm_WriteMem32(0x108, 0x00000040);  // -> pc

	arm_SetNextPC(0x00);
	arm_SetReg(15, 0x04);

	dump("before");
	// Run until parked at 0x40 (PC restored) or a step cap.
	u32 prev = 0xFFFFFFFF;
	for (int i = 0; i < 50; i++) {
		u32 pc = arm_GetNextPC();
		if (pc == 0x40 && prev == 0x40) break;
		prev = pc;
		arm_Run(1);
	}
	dump("after");
	printf("EXPECT     pc=00000040 cpsr=A0000010(mode=10) r0=11111111 r1=22222222 sp=0000010C\n");

	// ── Case 2: the EXACT hardware opcode E8FD8000 (P=1,U=1,S=1,W=1,{pc}). ──
	// This is LDMIB (pre-increment) with PC + CPSR restore. P=1/U=1 means PC is
	// loaded from [r13+4], and writeback lands r13+4*1.
	printf("\n-- case 2: E8FD8000 (LDMIB ^ {pc}, P=1 U=1) --\n");
	memset(s_aram, 0, ARAM_SIZE);
	arm_WriteMem32(0x00, 0xE8FD8000);   // LDM r13!,{pc}^  (P=1,U=1,S=1,W=1)
	arm_WriteMem32(0x04, 0xEAFFFFFE);
	arm_WriteMem32(0x40, 0xEAFFFFFE);   // park target
	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetReg(CPSR, 0x00000012);       // IRQ mode
	arm_SetReg(SPSR, 0xA0000010);       // SPSR -> USER + N + C
	arm_SetReg(13, 0x100);              // sp
	arm_WriteMem32(0x100, 0xDEAD0000);  // [r13+0]  (P=1 should NOT read this)
	arm_WriteMem32(0x104, 0x00000040);  // [r13+4]  (P=1,U=1 reads PC from here)
	arm_SetNextPC(0x00); arm_SetReg(15, 0x04);
	{
		u32 pv = 0xFFFFFFFF;
		for (int i = 0; i < 50; i++) {
			u32 pc = arm_GetNextPC();
			if (pc == 0x40 && pv == 0x40) break;
			pv = pc; arm_Run(1);
		}
	}
	dump("after2");
	printf("EXPECT2    pc=00000040 (mode=10) sp=00000104   [P=1,U=1: pc<-[0x104]=0x40, wb r13=0x104]\n");
	return 0;
}
