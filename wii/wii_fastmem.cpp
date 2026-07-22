/*
    wii_fastmem.cpp - PowerPC-MMU fastmem for the SH4 dynarec (FASTMEM preset)

    See wii_fastmem.h for the overall design. This file owns the hardware
    side only:

      * a hand-built classic-PPC hashed page table (HTAB) in MEM2 that maps
        EA 0x00000000-0x1FFFFFFF (SR0/SR1) onto the existing guest-memory
        arrays (mem_b / vram / aica_ram, all physically contiguous in MEM2),
      * SDR1 / segment-register setup + TLB invalidation,
      * the DSI exception handler (new "tuxedo" libogc convention) that
        forwards JIT faults to the back-patcher in wii_driver.cpp,
      * a boot-time window self-test.

    The JIT side (emission of the branchless load/store shapes, fault-site
    decoding, trampoline generation) lives in wii_driver.cpp
    (rec_fastmem_*), so this file needs no knowledge of the emitter.

    Why this is safe alongside libogc (per the libogc maintainer):
    libogc only uses BATs; SRs/SDR1 are left to the application, and the
    whole EA range 0x00000000-0x7FFFFFFF is untranslated/free. Data cache
    aliasing between the window and the normal 0x9xxxxxxx views is
    coherent on Broadway: the 32KB 8-way L1 has 4KB per way, so cache
    index bits never exceed the page offset (VIPT with no aliasing), and
    L2 is physical.

    Exception-handler contract (from disassembly of the tuxedo libogc
    PPCExcptEntryGeneral stub, installed at each vector by PPCExcptInit):
      - the stub runs in real mode, saves r11/r12 to SPRG0/SPRG1, saves
        CR to __ppc_excpt_buf[2], SRR0 (fault PC) to buf[0], SRR1 (fault
        MSR) to buf[1], then rfi's into the registered handler with
        translation RE-ENABLED (MSR.IR/DR=1) and EE=0, RI=0, FP=0.
      - r11 = exception id at entry; all other GPRs still hold the
        interrupted values.
      - DAR/DSISR are still live in their SPRs.
      - if RI was already 0 (fault inside a handler) the stub panics via
        PPCExcptDefaultHandler before ever reaching us.
    So the handler below runs at its normal virtual address and may call
    plain (FPU-free!) C code, but must restore SRR0/SRR1/CR/r11/r12 from
    buf/SPRGs itself before rfi.
*/

#include "types.h"

#if HOST_OS == OS_WII

#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <tuxedo/ppc/exception.h>

#include "dc/mem/sh4_mem.h"      // mem_b
#include "dc/pvr/pvr_if.h"       // vram
#include "dc/aica/aica_if.h"     // aica_ram
#include "wii_fastmem.h"

// Preset accessor (main.cpp). 0=off (default), 1=on.
extern "C" int get_fastmem_preset();

int g_wii_fastmem_active = 0;

// ============================================================================
// Page table
//
// 512 KB HTAB = 8192 PTEGs of 8 PTEs. We map ~28.7K pages (RAM 4 mirrors +
// VRAM 2x2 mirrors + AICA 4+4 mirrors) into 65536 slots (~44% load), with
// the secondary hash as backstop, so insertion can't realistically fail.
// The hardware walks the table PHYSICALLY (uncached), hence the explicit
// DCFlushRange after building it.
// ============================================================================

#define FASTMEM_HTAB_SIZE  (512 * 1024)
#define FASTMEM_HTAB_MASK  0x07                      // HTABMASK for 512 KB
#define FASTMEM_HASH_MASK  ((FASTMEM_HTAB_MASK << 10) | 0x3FF)

// Arbitrary VSID base; SR0/SR1 carry the two live segments, SR2-SR7 get
// distinct VSIDs with no PTEs so any stray access below 0x80000000 outside
// the window still takes a clean DSI instead of falsely hash-matching.
#define FASTMEM_VSID_BASE  0x00DC0

static u8* s_htab      = 0;   // virtual (cached MEM2) address of the HTAB
static u32 s_htab_phys = 0;

// Insert one 4 KB translation. ea is the window EA, pa the physical target.
// PTE word0 = V | VSID | H | API ; word1 = RPN | R | C | WIMG=0 | PP=10.
// R/C are pre-set so hardware never needs to write the table back.
static bool htab_insert(u32 ea, u32 pa)
{
    const u32 vsid = FASTMEM_VSID_BASE + (ea >> 28);
    const u32 pi   = (ea >> 12) & 0xFFFF;            // page index in segment
    const u32 hash = (vsid & 0x7FFFF) ^ pi;
    const u32 api  = (ea >> 22) & 0x3F;
    const u32 w0   = 0x80000000 | (vsid << 7) | api;
    const u32 w1   = (pa & 0xFFFFF000) | 0x180 | 0x2;

    for (u32 h = 0; h < 2; h++)
    {
        const u32 idx  = (h ? ~hash : hash) & FASTMEM_HASH_MASK;
        u32*      pteg = (u32*)(s_htab + (idx << 6));
        for (u32 slot = 0; slot < 8; slot++)
        {
            u32* pte = pteg + slot * 2;
            if (!(pte[0] & 0x80000000))
            {
                pte[0] = w0 | (h << 6);
                pte[1] = w1;
                return true;
            }
        }
    }
    return false;
}

// Map [ea_base, ea_base+ea_size) onto host block `host` mirrored every
// (off_mask+1) bytes. ea_base must be a multiple of (off_mask+1) so that
// (ea & off_mask) is the offset into the block (which holds for every
// region below). Host block must be 4 KB-aligned physically.
static bool map_region(u32 ea_base, u32 ea_size, void* host, u32 off_mask)
{
    const u32 host_phys = MEM_VIRTUAL_TO_PHYSICAL(host);
    if (host_phys & 0xFFF)
    {
        printf("[fastmem] region %08X: host %p not page-aligned\n", ea_base, host);
        return false;
    }
    for (u32 ea = ea_base; ea < ea_base + ea_size; ea += 0x1000)
    {
        if (!htab_insert(ea, host_phys + (ea & off_mask)))
        {
            printf("[fastmem] HTAB overflow at EA %08X\n", ea);
            return false;
        }
    }
    return true;
}

// ============================================================================
// DSI handler (assembly glue)
//
// Entered per the tuxedo contract described at the top of this file:
// translation ON, EE=0, RI=0, FP=0, r11/r12 free (originals in SPRG0/1),
// CR free (original in __ppc_excpt_buf[2]), everything else live.
//
// It builds an EABI frame on the interrupted (JIT) stack, saves the
// C-volatile integer state, and calls WiiFastmem_HandleDSI(fault_pc).
// On success the faulting site has been back-patched, so we restore the
// full entry state and rfi to RETRY the same PC (which now branches to
// the generated trampoline). On failure we chain to libogc's default
// handler (panic screen) exactly as if it had been dispatched directly:
// r11 = exception id, CR0.EQ clear (= "RI was set, use the dedicated
// exception stack").
//
// FPU is disabled here (exceptions clear MSR.FP) - neither this glue nor
// anything it calls may touch floating point. The r2/r13 SDA bases are
// intact (the JIT never modifies them), so C code works directly.
// ============================================================================

extern "C" void __wii_fastmem_dsi_handler(void);
extern "C" int  WiiFastmem_HandleDSI(u32 pc);

asm(R"(
    .text
    .align  2
    .globl  __wii_fastmem_dsi_handler
    .type   __wii_fastmem_dsi_handler, %function
__wii_fastmem_dsi_handler:
    stwu    1, -96(1)
    stw     0,  16(1)
    stw     3,  20(1)
    stw     4,  24(1)
    stw     5,  28(1)
    stw     6,  32(1)
    stw     7,  36(1)
    stw     8,  40(1)
    stw     9,  44(1)
    stw     10, 48(1)
    mflr    0
    stw     0,  52(1)
    mfctr   0
    stw     0,  56(1)
    mfxer   0
    stw     0,  60(1)

    lis     3, __ppc_excpt_buf@ha
    lwz     3, __ppc_excpt_buf@l(3)         # r3 = fault PC (saved SRR0)
    bl      WiiFastmem_HandleDSI            # 0 = site patched
    cmpwi   3, 0

    lwz     0,  60(1)
    mtxer   0
    lwz     0,  56(1)
    mtctr   0
    lwz     0,  52(1)
    mtlr    0
    lwz     10, 48(1)
    lwz     9,  44(1)
    lwz     8,  40(1)
    lwz     7,  36(1)
    lwz     6,  32(1)
    lwz     5,  28(1)
    lwz     4,  24(1)
    lwz     3,  20(1)
    lwz     0,  16(1)
    addi    1, 1, 96
    bne     1f                              # not a fastmem site -> panic

    lis     12, __ppc_excpt_buf@ha
    la      12, __ppc_excpt_buf@l(12)
    lwz     11, 0(12)
    mtsrr0  11                              # retry the (now patched) PC
    lwz     11, 4(12)
    mtsrr1  11                              # restore interrupted MSR
    lwz     11, 8(12)
    mtcr    11
    mfsprg  11, 0
    mfsprg  12, 1
    rfi

1:  # Chain to the libogc panic path with the state it expects.
    li      11, 3                           # PPC_EXCPT_DSI
    cmpwi   1, 0                            # CR0.EQ = 0 (normal dispatch)
    b       PPCExcptDefaultHandler
    .size   __wii_fastmem_dsi_handler, . - __wii_fastmem_dsi_handler
    .previous
)");

// C side of the handler. INTEGER CODE ONLY (MSR.FP is off; a float touch
// here would nest an FP-unavailable exception into the RI=0 panic path),
// and NO printf/stdio anywhere in this path (newlib's stdout lock can
// invoke the scheduler with EE=0 and deadlock — hardware lesson from the
// 2026-07-19 MMU experiment).
//
// Self-test probe mode: while the boot self-test runs, a DSI means one of
// the hardware assumptions (SDR1 walker, BATs, hash function) is wrong.
// Instead of panicking, record it and SKIP the faulting instruction (bump
// the saved SRR0 in the libogc lowmem buffer) so the self-test finishes
// and fastmem is left gracefully inactive.
extern "C" u32 __ppc_excpt_buf[];     // libogc lowmem save: [0]=SRR0 [1]=SRR1 [2]=CR

static volatile int s_selftest_probe   = 0;
static volatile int s_selftest_faulted = 0;

extern "C" int WiiFastmem_HandleDSI(u32 pc)
{
    if (s_selftest_probe)
    {
        s_selftest_faulted = 1;
        __ppc_excpt_buf[0] = pc + 4;  // resume past the probing access
        return 0;
    }
    return rec_fastmem_patch(pc);
}

// ============================================================================
// MMU enable + self-test
// ============================================================================

static void fastmem_enable_mmu(void)
{
    const u32 level = IRQ_Disable();

    // The hardware table walk reads PHYSICAL memory - push the freshly
    // built HTAB out of the data cache first.
    DCFlushRange(s_htab, FASTMEM_HTAB_SIZE);
    asm volatile("sync");

    asm volatile("mtspr 25, %0" :: "r"(s_htab_phys | FASTMEM_HTAB_MASK)); // SDR1

    // SR0-SR7: T=0, Ks=0, Kp=0, distinct VSIDs (only SR0/SR1 have PTEs).
    for (u32 i = 0; i < 8; i++)
        asm volatile("mtsrin %0, %1" :: "r"(FASTMEM_VSID_BASE + i), "r"(i << 28));
    asm volatile("sync; isync");

    // Invalidate every dTLB/iTLB congruence class (Broadway: 64 sets x 4 KB).
    for (u32 i = 0; i < 64; i++)
        asm volatile("tlbie %0" :: "r"(i << 12));
    asm volatile("sync; isync");

    IRQ_Restore(level);
}

// One aliased location: store via the window, read back via the host
// pointer, and the reverse. Also proves d-cache coherency of the alias
// pair on real hardware (VIPT-no-alias assumption).
static bool test_alias(u32 ea, void* host_ptr)
{
    volatile u32* win  = (volatile u32*)ea;
    volatile u32* host = (volatile u32*)host_ptr;
    const u32 save = *host;
    bool ok = true;

    *win = 0xA55A0000 | (ea >> 16);
    if (*host != (0xA55A0000 | (ea >> 16))) ok = false;
    *host = 0x5AA50000 | (ea & 0xFFFF);
    if (*win != (0x5AA50000 | (ea & 0xFFFF))) ok = false;

    *host = save;
    if (!ok)
        printf("[fastmem] self-test FAILED at EA %08X (host %p)\n", ea, host_ptr);
    return ok;
}

static bool fastmem_self_test(void)
{
    bool ok = true;
    // Main RAM: all four mirrors + a high offset.
    ok &= test_alias(0x0C000010, mem_b.data + 0x10);
    ok &= test_alias(0x0D000010, mem_b.data + 0x10);
    ok &= test_alias(0x0E000010, mem_b.data + 0x10);
    ok &= test_alias(0x0F000010, mem_b.data + 0x10);
    ok &= test_alias(0x0CFFF00C, mem_b.data + 0xFFF00C);
    // VRAM: base page, in-page 8 MB mirror, 0x06 mirror page.
    ok &= test_alias(0x04000020, vram.data + 0x20);
    ok &= test_alias(0x04800020, vram.data + 0x20);
    ok &= test_alias(0x06000020, vram.data + 0x20);
    // AICA wave RAM: base + 2 MB mirror + area-0 image mirror.
    ok &= test_alias(0x00800040, aica_ram.data + 0x40);
    ok &= test_alias(0x00A00040, aica_ram.data + 0x40);
    ok &= test_alias(0x02800040, aica_ram.data + 0x40);
    return ok;
}

void WiiFastmem_Init(void)
{
    static int s_tried = 0;
    if (s_tried)
        return;
    s_tried = 1;

    if (!get_fastmem_preset())
    {
        printf("[fastmem] preset off\n");
        return;
    }

    if (!mem_b.data || !vram.data || !aica_ram.data)
    {
        printf("[fastmem] guest memory not reserved yet - inactive\n");
        return;
    }

    // Carve the (size-aligned) HTAB from the MEM2 arena, like _vmem_reserve.
    {
        const u32 level = IRQ_Disable();
        u8* lo = (u8*)SYS_GetArena2Lo();
        u8* htab = (u8*)(((u32)lo + FASTMEM_HTAB_SIZE - 1) & ~(u32)(FASTMEM_HTAB_SIZE - 1));
        SYS_SetArena2Lo(htab + FASTMEM_HTAB_SIZE);
        IRQ_Restore(level);
        s_htab      = htab;
        s_htab_phys = MEM_VIRTUAL_TO_PHYSICAL(htab);
    }
    memset(s_htab, 0, FASTMEM_HTAB_SIZE);

    // Window layout (see header). Order does not matter.
    bool ok = true;
    ok &= map_region(0x0C000000, 0x04000000, mem_b.data,    RAM_SIZE  - 1); // RAM x4
    ok &= map_region(0x04000000, 0x01000000, vram.data,     VRAM_SIZE - 1); // VRAM 64-bit
    ok &= map_region(0x06000000, 0x01000000, vram.data,     VRAM_SIZE - 1); // VRAM mirror
    ok &= map_region(0x00800000, 0x00800000, aica_ram.data, ARAM_SIZE - 1); // wave RAM
    ok &= map_region(0x02800000, 0x00800000, aica_ram.data, ARAM_SIZE - 1); // area-0 mirror
    if (!ok)
    {
        printf("[fastmem] mapping failed - inactive\n");
        return;
    }

    fastmem_enable_mmu();

    // Handler goes in BEFORE the self-test so a wrong hardware assumption
    // degrades to "fastmem inactive" instead of a boot panic (probe mode
    // skips faulting test accesses). It stays installed either way: when
    // inactive no fastmem code is ever emitted, and a genuine stray DSI
    // still ends on the libogc panic screen via the handler's fail path.
    PPCExcptSetHandler(PPC_EXCPT_DSI, __wii_fastmem_dsi_handler);

    s_selftest_probe = 1;
    const bool st_ok = fastmem_self_test();
    s_selftest_probe = 0;

    if (!st_ok || s_selftest_faulted)
    {
        printf("[fastmem] window self-test %s - inactive (mappings left dormant)\n",
               s_selftest_faulted ? "FAULTED" : "failed");
        return;
    }

    g_wii_fastmem_active = 1;
    printf("[fastmem] active: HTAB %p (phys %08X, %u KB), window 0x00000000-0x1FFFFFFF\n",
           s_htab, s_htab_phys, FASTMEM_HTAB_SIZE / 1024);
}

#else // !OS_WII

int g_wii_fastmem_active = 0;
void WiiFastmem_Init(void) {}

#endif
