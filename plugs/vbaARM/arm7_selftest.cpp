// ARM7DI conformance self-test runner.
//
// Executes the vendored arm7di-tests-dreamcast binaries against this project's
// vbaARM core to verify the ARM7DI interpreter before the emulator boots.
// Ported from the Rust harness (crates/arm7di-core/tests/arm7di_bin_tests.rs)
// in the nullDC rust edition.
//
// Test protocol (per vendor/arm7di-tests-dreamcast/test.py):
//   * Each binary is linked at 0x0; execution starts at offset 0.
//   * The test ends when the next instruction word equals 0xDEADBEEF.
//   * Success: the low 30 bits of r1 are zero. Non-zero bits are error flags.
//
// The binaries are little-endian ARM. This core reads AICA RAM with a
// byte-swap (host is big-endian Wii), so we load each test byte through
// arm_WriteMem8() and read back through arm_ReadMem32(), which keeps the
// swap symmetric and reproduces the Rust harness's flat-LE behavior.

#include "arm7.h"
#include "arm_mem.h"
#include "arm7_selftest_data.h"

#include <ogc/system.h>
#define printf(...) printf(__VA_ARGS__)

// Core internals we drive directly.
extern u8* arm_aica_ram;

// Register file indices (match arm7.cpp).
#define RN_R1   1
#define RN_R15  15
#define RN_CPSR 16

static const u32 END_MARKER = 0xDEADBEEFu;
static const u32 MAX_INSNS  = 100000;   // safety limit, matches Rust MAX_CYCLES

// 2 MB scratch ARAM (matches ARAM_SIZE so the core's ARAM_MASK behaves exactly
// as in real operation). Static so it lives for the whole run with no heap use.
static u8 s_test_aram[ARAM_SIZE];

// Decode r1 error flags into a short human-readable string (matches Rust).
static void decode_r1_errors(u32 r1, char* out, int out_sz)
{
    out[0] = 0;
    int used = 0;
    #define APPEND(s) do { \
        int _l = 0; const char* _p = (s); while (_p[_l]) _l++; \
        if (used + _l + 1 < out_sz) { for (int _i=0;_i<_l;_i++) out[used+_i]=_p[_i]; \
            used += _l; out[used]=0; } } while(0)

    if (r1 & 0x10) APPEND("BAD_Rd ");
    if (r1 & 0x20) APPEND("BAD_Rn ");
    if (r1 & 0xFF00) APPEND("OTHER ");
    u32 remaining = r1 & ~0x30u & 0xFF;
    if (remaining) APPEND("FLAGS ");
    #undef APPEND
}

// Run a single embedded test binary. Returns true on pass.
static bool run_one(const Arm7SelfTest& t)
{
    // Point the core at our scratch ARAM and clear it.
    arm_aica_ram = s_test_aram;
    for (u32 i = 0; i < ARAM_SIZE; i++)
        s_test_aram[i] = 0;

    // Load the binary one byte at a time so the BE byte-swap is applied.
    for (u32 i = 0; i < t.size; i++)
        arm_WriteMem8(i, t.data[i]);

    // Reset + enable the core, then place the PC at the test entry (offset 0).
    // arm_SetEnabled(true) calls arm_Reset() internally on the off->on edge.
    arm_SetEnabled(false);
    arm_SetEnabled(true);

    // Pipeline seeding (this core's convention, mirroring arm_Reset()):
    //   before each fetch  reg[15] == armNextPC + 4
    //   the fetch loop does armNextPC = reg[15]; reg[15] += 4
    // so the instruction at address A executes with reg[15] == A + 8.
    // To start at address 0 we therefore seed armNextPC=0, reg[15]=4 — NOT
    // reg[15]=8 (that would make the first instruction see reg[15]=12, off by
    // one pipeline stage; that bug made nearly every test's PC-relative LDR /
    // literal-pool load read the wrong word).
    arm_SetNextPC(0);
    arm_SetReg(RN_R15, 4);

    u32 insns = 0;
    while (true)
    {
        u32 next_pc   = arm_GetNextPC();
        u32 next_insn = arm_ReadMem32(next_pc & ARAM_MASK);

        if (next_insn == END_MARKER)
            break;

        if (insns >= MAX_INSNS)
        {
            printf("[ARM7TEST]   %-8s TIMEOUT after %u insns (no END_MARKER)\n",
                   t.name, insns);
            return false;
        }

        arm_Run(1);   // execute exactly one instruction (>=6 ticks -> 1 step)
        insns++;
    }

    u32 r1 = arm_GetReg(RN_R1);
    u32 error_bits = r1 & 0x3FFFFFFFu;   // only low 30 bits are error flags

    if (error_bits == 0)
        return true;

    char errs[64];
    decode_r1_errors(r1, errs, sizeof(errs));
    printf("[ARM7TEST]   %-8s FAIL r1=0x%08X (%s)\n", t.name, r1, errs);
    return false;
}

int arm_RunSelfTests()
{
    printf("\n==================== ARM7DI SELF-TEST ====================\n");
    printf("[ARM7TEST] Running %d ARM7DI conformance tests...\n", arm7_selftest_count);

    // Fully initialize the core. arm_Init() builds the cpuBitsSet[] population
    // table that LDM/STM use to compute base writeback offsets; without it the
    // writeback address is wrong (showed up as BAD_Rn in the LDM/STM tests).
    arm_Init();

    // Preserve the caller's ARAM pointer; the emulator wires this up later via
    // arm_init_mem(), but be safe in case the order ever changes.
    u8* saved_aram = arm_aica_ram;

    int failed = 0;
    for (int i = 0; i < arm7_selftest_count; i++)
    {
        if (!run_one(arm7_selftests[i]))
            failed++;
    }

    arm_aica_ram = saved_aram;
    arm_SetEnabled(false);   // leave the core disabled; emulator re-enables later

    if (failed == 0)
        printf("[ARM7TEST] PASS: all %d tests passed.\n", arm7_selftest_count);
    else
        printf("[ARM7TEST] FAIL: %d / %d tests failed.\n", failed, arm7_selftest_count);
    printf("==========================================================\n\n");

    return failed;
}
