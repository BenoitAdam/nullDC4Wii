// Native ARM7DI self-test harness. Mirrors arm7_selftest.cpp but with no Wii
// (ogc) dependency, so the cached interpreter can be validated on the host.
// Uses the REAL ../arm7.h chain (via -I..) plus the leaf stubs in this dir.
#include "arm7.h"
#include "arm_mem.h"
#include "arm7_selftest_data.h"

// Backing store for AICA RAM and the FIQ pin (declared extern by the real
// arm_aica.h / arm_mem chain).
static u8 s_aram[ARAM_SIZE];
u8*  arm_aica_ram = s_aram;
bool e68k_out = false;

// The real arm_mem.h only DECLARES these templates (defined in arm_mem.cpp,
// which we do not build). We must reproduce arm_mem.cpp's behavior EXACTLY, not
// a flat model: the core applies a big-endian byte/halfword swap (addr^3 / ^2)
// and an unaligned-LDR word rotate.
//
// IMPORTANT endianness note: arm_mem.cpp's byte/halfword XOR swap (addr^3 /
// addr^2) only round-trips correctly on a BIG-ENDIAN host (the Wii). The data
// is loaded byte-by-byte via WriteMem8 and fetched as words via ReadMem32; the
// ^3 swap + a native word read reconstructs the original word ONLY because the
// Wii is big-endian. This native harness runs on a LITTLE-ENDIAN x86 host, so
// we must NOT apply the swap (it would byte-reverse every word and corrupt the
// instruction stream). We keep a flat layout — which round-trips on LE — and
// reproduce only the host-endian-independent behavior that the tests actually
// exercise: the unaligned-LDR "aligned read + rotate-right" (arm_mem.cpp lines
// 92-101), which a naive byte-gather would get wrong.
template<int sz, typename T>
T fastcall ReadMemArm(u32 addr)
{
	addr &= 0x00FFFFFF;
	if (sz == 4 && (addr & 3)) {
		// Unaligned LDR: read the aligned word and rotate right by (addr&3)*8.
		u32 aligned = addr & ~3u;
		u32 word = *(u32*)&s_aram[aligned & ARAM_MASK];
		u32 rot  = (addr & 3) * 8;
		return (T)((word >> rot) | (word << (32 - rot)));
	}
	return *(T*)&s_aram[addr & ARAM_MASK];
}
template<int sz, typename T>
void fastcall WriteMemArm(u32 addr, T data)
{
	addr &= 0x00FFFFFF;
	*(T*)&s_aram[addr & ARAM_MASK] = (T)data;
}
// Explicit instantiations for the widths the core uses.
template u8  fastcall ReadMemArm<1,u8>(u32);
template u16 fastcall ReadMemArm<2,u16>(u32);
template u32 fastcall ReadMemArm<4,u32>(u32);
template void fastcall WriteMemArm<1,u8>(u32,u8);
template void fastcall WriteMemArm<2,u16>(u32,u16);
template void fastcall WriteMemArm<4,u32>(u32,u32);

static const u32 END_MARKER = 0xDEADBEEFu;
static const u32 MAX_INSNS  = 100000;

static bool run_one(const Arm7SelfTest& t, bool& timedOut)
{
	arm_aica_ram = s_aram;
	memset(s_aram, 0, ARAM_SIZE);

	for (u32 i = 0; i < t.size; i++)
		arm_WriteMem8(i, t.data[i]);

	arm_SetEnabled(false);
	arm_SetEnabled(true);
	arm_SetNextPC(0);
	arm_SetReg(15, 4);

	u32 insns = 0;
	timedOut = false;
	while (true) {
		u32 next_pc   = arm_GetNextPC();
		u32 next_insn = arm_ReadMem32(next_pc & ARAM_MASK);
		if (next_insn == END_MARKER) break;
		if (insns >= MAX_INSNS) { timedOut = true; return false; }
		arm_Run(1);
		insns++;
	}

	u32 r1 = arm_GetReg(1);
	return (r1 & 0x3FFFFFFFu) == 0;
}

int main()
{
	arm_Init();
	int failed = 0, timeouts = 0;
	for (int i = 0; i < arm7_selftest_count; i++) {
		bool to = false;
		bool ok = run_one(arm7_selftests[i], to);
		if (!ok) {
			failed++;
			if (to) timeouts++;
			u32 r1 = arm_GetReg(1);
			printf("FAIL %-12s r1=0x%08X%s\n", arm7_selftests[i].name, r1,
			       to ? "  (TIMEOUT)" : "");
		}
	}
	printf("\n%d / %d passed (%d failed, %d timeouts)\n",
	       arm7_selftest_count - failed, arm7_selftest_count, failed, timeouts);
	return failed ? 1 : 0;
}
