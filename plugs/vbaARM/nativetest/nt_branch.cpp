// Focused branch / return test: exercises B (fwd+back), BL + MOV pc,lr return,
// and LDR pc. Runs the SAME program on the active interpreter (cached if
// ARM7_USE_CACHED) and prints the register trail so cached vs ref can be diffed.
//
// Build (from nativetest/):
//   PATH=/c/msys64/usr/bin:$PATH C:/msys64/usr/bin/g++.exe -std=c++17 -O1 \
//      -fno-strict-aliasing -iquote . -I .. -DARM7_USE_CACHED \
//      ../arm7.cpp ../arm7_cached.cpp nt_branch.cpp -o nt_branch_c.exe
//   (drop -DARM7_USE_CACHED for the reference build)
#include "arm7.h"
#include "arm_mem.h"

static u8 s_aram[ARAM_SIZE];
u8*  arm_aica_ram = s_aram;
bool e68k_out = false;

template<int sz, typename T> T fastcall ReadMemArm(u32 addr) {
	addr &= 0x00FFFFFF;
	if (addr & (sz - 1)) { u32 a = addr & ~3u, w = *(u32*)&s_aram[a & ARAM_MASK], r = (addr & 3) * 8;
		return (T)((w >> r) | (w << (32 - r))); }
	if (sz == 1) addr ^= 3; else if (sz == 2) addr ^= 2;
	return *(T*)&s_aram[addr & ARAM_MASK];
}
template<int sz, typename T> void fastcall WriteMemArm(u32 addr, T data) {
	addr &= 0x00FFFFFF;
	if (addr & (sz - 1)) { u8 t[4]; t[3]=(u8)(data>>24);t[2]=(u8)(data>>16);t[1]=(u8)(data>>8);t[0]=(u8)data;
		for(int i=0;i<4;++i) s_aram[((addr+i)^3)&ARAM_MASK]=t[i]; return; }
	if (sz == 1) addr ^= 3; else if (sz == 2) addr ^= 2;
	*(T*)&s_aram[addr & ARAM_MASK] = (T)data;
}
template u8  fastcall ReadMemArm<1,u8>(u32);   template u16 fastcall ReadMemArm<2,u16>(u32);
template u32 fastcall ReadMemArm<4,u32>(u32);  template void fastcall WriteMemArm<1,u8>(u32,u8);
template void fastcall WriteMemArm<2,u16>(u32,u16); template void fastcall WriteMemArm<4,u32>(u32,u32);

static void emit(u32 addr, u32 op) { arm_WriteMem32(addr, op); }

int main()
{
	arm_Init();
	memset(s_aram, 0, ARAM_SIZE);

	// Program:
	//   0x00: MOV r0, #0            E3A00000
	//   0x04: BL  func (0x20)       EB000005   (off = (0x20-(0x04+8))/4 = 5)
	//   0x08: ADD r0, r0, #1        E2800001   (after return)
	//   0x0c: B   end (0x18)        EA000001   (off = (0x18-(0x0c+8))/4 = 1)
	//   0x10: (skipped)             E2800040
	//   0x14: (skipped)             E2800040
	//   0x18: end: B end (self)     EAFFFFFE   (infinite — stop marker)
	//   0x1c: nop padding           E1A00000
	//   0x20: func: ADD r0,r0,#0x10 E280000F? -> use #0x10 = E2800010
	//   0x24: MOV pc, lr            E1A0F00E   (return)
	emit(0x00, 0xE3A00000);
	emit(0x04, 0xEB000005);
	emit(0x08, 0xE2800001);
	emit(0x0c, 0xEA000001);
	emit(0x10, 0xE2800040);
	emit(0x14, 0xE2800040);
	emit(0x18, 0xEAFFFFFE);
	emit(0x1c, 0xE1A00000);
	emit(0x20, 0xE2800010);
	emit(0x24, 0xE1A0F00E);

	// ── Second program region: LDR pc and LDM {pc} returns. ──
	// Reuse a fresh layout at 0x40.
	//   0x40: MOV r0,#0x100        E3A00C01  (#0x100)
	//   0x44: LDR pc,[pc,#0x14]    E59FF014  -> loads word at (0x44+8)+0x14=0x60 = 0x50
	//   0x48..: skipped
	//   0x50: target1: ADD r0,r0,#1 E2800001
	//   0x54: LDMIA sp!,{pc}       E8BD8000  (sp=r13) -> pop pc
	//   0x58: B 0x58 (park)        EAFFFFFE
	//   0x5c: nop
	//   0x60: literal = 0x50       (LDR pc target)
	emit(0x40, 0xE3A00C01);
	emit(0x44, 0xE59FF014);
	emit(0x50, 0xE2800001);
	emit(0x54, 0xE8BD8000);
	emit(0x58, 0xEAFFFFFE);
	emit(0x5c, 0xE1A00000);
	emit(0x60, 0x00000050);

	arm_SetEnabled(false);
	arm_SetEnabled(true);
	arm_SetNextPC(0);
	arm_SetReg(15, 4);

	// Run until PC parks at the self-branch at 0x18 (or a step cap).
	u32 prevPC = 0xFFFFFFFF;
	for (int i = 0; i < 200; i++) {
		u32 pc = arm_GetNextPC();
		printf("step %3d: pc=%08X r0=%08X r14=%08X\n", i, pc, arm_GetReg(0), arm_GetReg(14));
		if (pc == 0x18 && prevPC == 0x18) break;  // parked at end self-branch
		prevPC = pc;
		arm_Run(1);
	}
	printf("FINAL r0=%08X (expect 0x11 = 0x10 from func + 1 after return)\n", arm_GetReg(0));

	// ── Run 2: LDR pc + LDM {pc}. Park target for the LDM-popped pc is 0x58. ──
	printf("\n-- run 2: LDR pc / LDM {pc} --\n");
	// Set up a stack with a return address (0x58) for the LDMIA sp!,{pc}.
	arm_SetReg(13, 0x200);                 // sp
	arm_WriteMem32(0x200, 0x58);           // pop value -> pc = 0x58
	arm_SetNextPC(0x40);
	arm_SetReg(15, 0x44);
	u32 prev2 = 0xFFFFFFFF;
	for (int i = 0; i < 200; i++) {
		u32 pc = arm_GetNextPC();
		printf("step %3d: pc=%08X r0=%08X sp=%08X\n", i, pc, arm_GetReg(0), arm_GetReg(13));
		if (pc == 0x58 && prev2 == 0x58) break;
		prev2 = pc;
		arm_Run(1);
	}
	printf("FINAL r0=%08X (expect 0x101: 0x100 then +1 at target1)\n", arm_GetReg(0));

	// ── Run 3: realistic prologue/epilogue. ──
	//   STMFD sp!,{r4,lr}  ;  ...  ;  LDMFD sp!,{r4,pc}
	// STMFD = STMDB (P=1,U=0,W=1). LDMFD = LDMIA (P=0,U=1,W=1).
	//   0x80: MOV r4,#0x55          E3A04055
	//   0x84: BL 0x90               EB000001  (off=(0x90-(0x84+8))/4=1)
	//   0x88: B 0x88 (park)         EAFFFFFE
	//   0x8c: nop
	//   0x90: func: STMFD sp!,{r4,lr}  E92D4010  (regs r4|lr, P=1U=0W=1)
	//   0x94:       MOV r4,#0xAA        E3A040AA
	//   0x98:       LDMFD sp!,{r4,pc}   E8BD8010  (regs r4|pc, P=0U=1W=1)
	printf("\n-- run 3: STMFD/LDMFD {r4,pc} epilogue --\n");
	emit(0x80, 0xE3A04055);
	emit(0x84, 0xEB000001);
	emit(0x88, 0xEAFFFFFE);
	emit(0x8c, 0xE1A00000);
	emit(0x90, 0xE92D4010);
	emit(0x94, 0xE3A040AA);
	emit(0x98, 0xE8BD8010);
	arm_SetReg(13, 0x300);   // sp
	arm_SetReg(4, 0);
	arm_SetNextPC(0x80);
	arm_SetReg(15, 0x84);
	u32 prev3 = 0xFFFFFFFF;
	for (int i = 0; i < 200; i++) {
		u32 pc = arm_GetNextPC();
		printf("step %3d: pc=%08X r4=%08X sp=%08X lr=%08X\n", i, pc,
		       arm_GetReg(4), arm_GetReg(13), arm_GetReg(14));
		if (pc == 0x88 && prev3 == 0x88) break;
		prev3 = pc;
		arm_Run(1);
	}
	printf("FINAL r4=%08X sp=%08X (expect r4=0x55 restored, sp=0x300 balanced)\n",
	       arm_GetReg(4), arm_GetReg(13));

	// ── Run 4: backward-branch loop driven by a LARGE arm_Run(N). ──
	// This is the real-world pattern: one arm_Run call threads through many
	// instructions and re-takes a backward branch repeatedly, WITHOUT returning
	// to the harness between instructions (unlike arm_Run(1)).
	//   0xC0: MOV r0,#0            E3A00000
	//   0xC4: MOV r1,#10           E3A0100A
	//   0xC8: loop: ADD r0,r0,#1   E2800001
	//   0xCC:       SUBS r1,r1,#1  E2511001
	//   0xD0:       BNE loop       1AFFFFFC  (off=(0xC8-(0xD0+8))/4=-4)
	//   0xD4:       B 0xD4 (park)  EAFFFFFE
	printf("\n-- run 4: backward-branch loop via large arm_Run(N) --\n");
	emit(0xC0, 0xE3A00000);
	emit(0xC4, 0xE3A0100A);
	emit(0xC8, 0xE2800001);
	emit(0xCC, 0xE2511001);
	emit(0xD0, 0x1AFFFFFC);
	emit(0xD4, 0xEAFFFFFE);
	arm_SetNextPC(0xC0);
	arm_SetReg(15, 0xC4);
	// Run a big slice so the whole loop runs inside ONE arm_Run call.
	for (int call = 0; call < 20; call++) {
		arm_Run(500);
		u32 pc = arm_GetNextPC();
		printf("after arm_Run #%d: pc=%08X r0=%08X r1=%08X\n", call, pc,
		       arm_GetReg(0), arm_GetReg(1));
		if (pc == 0xD4) break;
	}
	printf("FINAL r0=%08X (expect 0x0A = 10 loop iterations)\n", arm_GetReg(0));

	// ── Run 5: self-modifying / DMA-rewritten code (the suspected real cause). ──
	// Decode+run a block once, then OVERWRITE the code under it (as the SH4/DMA
	// does when streaming a new driver/command) and run again WITHOUT a reset.
	// The cached path serves stale uops; the reference re-fetches. Divergence
	// here == the real-hardware bug.
	//   0xE0: MOV r0,#1   E3A00001   ; v1: sets r0=1
	//   0xE4: B 0xE4      EAFFFFFE
	printf("\n-- run 5: code rewrite without reset --\n");
	emit(0xE0, 0xE3A00001);
	emit(0xE4, 0xEAFFFFFE);
	arm_SetNextPC(0xE0);
	arm_SetReg(15, 0xE4);
	arm_Run(50);
	printf("v1: pc=%08X r0=%08X (expect r0=1)\n", arm_GetNextPC(), arm_GetReg(0));

	// Now rewrite 0xE0 to MOV r0,#2 and re-run from 0xE0 (no reset/flush).
	emit(0xE0, 0xE3A00002);   // MOV r0,#2
	printf("   (sanity: word@0xE0 now reads %08X)\n", arm_ReadMem32(0xE0));
	arm_SetReg(0, 0);
	arm_SetNextPC(0xE0);
	arm_SetReg(15, 0xE4);
	arm_Run(50);
	printf("v2: pc=%08X r0=%08X (reference=2, stale cache=1)\n",
	       arm_GetNextPC(), arm_GetReg(0));

	// ── Run 6: the EXACT failing opcode from hardware: E8FD8000. ──
	// E8FD8000 = LDM r13!, {pc} with P=1,U=1,W=1 (pre-increment, writeback).
	// The hardware log showed this loading a garbage PC. Reproduce + diff.
	//   0x110: MOV r13,#0x180      E3A0D60... use MOV r13,#0x180: E3A0DC06? -> set via reg
	//   place return addr so {pc} pops a known value.
	printf("\n-- run 6: E8FD8000 (LDM P=1 U=1 W=1 {pc}) --\n");
	emit(0x110, 0xE8FD8000);   // LDM r13!,{pc}
	emit(0x114, 0xEAFFFFFE);   // (unused)
	// For P=1,U=1: PC loaded from [r13+4]. Put target 0x114-park? Use 0x120 park.
	emit(0x120, 0xEAFFFFFE);   // park: B self
	arm_SetReg(13, 0x500);
	arm_WriteMem32(0x500, 0xAAAAAAAA);  // [r13+0] (not used by P=1)
	arm_WriteMem32(0x504, 0x120);       // [r13+4] -> pc (P=1,U=1 loads here)
	arm_SetReg(15, 0); arm_SetReg(14, 0);
	arm_SetNextPC(0x110);
	arm_SetReg(15, 0x114);
	arm_Run(50);
	printf("E8FD8000: pc=%08X r13=%08X (P=1U=1: pc<-[0x504]=0x120, wb r13=0x504)\n",
	       arm_GetNextPC(), arm_GetReg(13));
	return 0;
}
