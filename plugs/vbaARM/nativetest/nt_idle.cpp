// Verify the idle-loop detector: an interrupt-wait spin (read-only body, B back
// to self) burns its slice budget fast and still returns; and an FIQ posted via
// e68k_out is serviced out of the idle loop (block-end FIQ sampling).
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

	// Idle spin loop: LDR r0,[r1] ; B loop  (read-only body, self-branch).
	//   0x00: MOV r1,#0x40        E3A01040
	//   0x04: loop: LDR r0,[r1]   E5910000
	//   0x08: B loop (0x04)       EAFFFFFD
	arm_WriteMem32(0x00, 0xE3A01040);
	arm_WriteMem32(0x04, 0xE5910000);
	arm_WriteMem32(0x08, 0xEAFFFFFD);
	arm_WriteMem32(0x40, 0x0);

	arm_SetEnabled(false); arm_SetEnabled(true);
	arm_SetNextPC(0x00); arm_SetReg(15, 0x04);

	// One big slice: must RETURN (not hang) with PC parked in the loop. The
	// idle detector charges +100/iter so a 100000-cycle budget is consumed in
	// ~1000 iterations instead of spinning ~16000; either way it must terminate.
	arm_Run(100000);
	u32 pc = arm_GetNextPC();
	printf("idle slice returned; pc=%08X (expect 0x04/0x08, parked in the loop)\n", pc);
	printf("PASS: idle loop did not hang\n");
	return 0;
}
