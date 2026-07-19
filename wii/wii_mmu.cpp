// ---------------------------------------------------------------------------
// wii_mmu.cpp – Broadway paged-MMU driver for the Dreamcast guest window.
// See wii_mmu.h for the big-picture design notes.
//
// Hardware pieces used (PPC750CL / Broadway):
//   • SR0–SR15  segment registers: EA[31:28] selects one; supplies the VSID.
//               libogc never touches them, they contain whatever the loader
//               left behind — we program all 16 deliberately.
//   • SDR1      physical base + mask of the hashed page table (HTAB).
//   • HTAB      N PTEGs of 8 PTEs (8 bytes each). A page's PTE may live in
//               its primary or secondary (H=1) PTEG. The 750 family has a
//               HARDWARE table-walker: on TLB miss it searches the HTAB
//               itself; only a complete miss raises DSI/ISI.
//   • BATs      libogc's 1:1 mappings of MEM1/MEM2 at 0x8/0x9/0xC/0xD.
//               BAT translation has priority over segment/page translation,
//               so our SR programming cannot disturb libogc's world.
//
// The guest window lives at EA 0x00000000–0x1FFFFFFF (SR0/SR1), identity-
// mapped to SH4 physical addresses. RAM and VRAM get real PTEs; everything
// else DSIs into WMMU_HandleExcpt which emulates the access via _vmem.
// ---------------------------------------------------------------------------

#include "wii/wii_mmu.h"

#if WMMU_ENABLE && (HOST_OS == OS_WII)

#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <ogc/context.h>      // frame_context, EX_DSI/EX_ISI
#include <ogc/cache.h>        // DCFlushRange

#include "dc/mem/sh4_mem.h"   // mem_b, _vmem accessors
#include "dc/aica/aica_if.h"  // aica_ram
#include "dc/pvr/pvr_if.h"    // vram

// Classic libogc internals (symbol lives in exception.o, no public header).
extern "C" void __exception_sethandler(u32 nExcept, void (*pHndl)());
extern "C" void wmmu_excpt_stub();

// Runtime jit_mmu preset (wii/main.cpp; menu page 3 / game_presets.cfg).
extern "C" int get_jit_mmu_preset();

// SH4 recompiler code cache (dc/sh4/rec_v2/driver.cpp) — the back-patcher
// only ever touches faulting sites inside this range.
#include "dc/sh4/rec_v2/ngen.h"   // CODE_SIZE
extern u8 CodeCache[];
#define WMMU_CODECACHE_SIZE CODE_SIZE

WMMU_Stats wmmu_stats;

// ---------------------------------------------------------------------------
// PTE / SR encoding (32-bit PowerPC MMU)
// ---------------------------------------------------------------------------
#define PTE_V          0x80000000u              // word0: valid
#define PTE_H          0x00000040u              // word0: secondary hash
#define PTE_R          0x00000100u              // word1: referenced
#define PTE_C          0x00000080u              // word1: changed
#define PTE_PP_RW      0x00000002u              // word1: PP=10 (sup RW)
#define SR_N           0x10000000u              // SR: no-execute
#define WMMU_VSID(sr)  (0x000D0C00u + (sr))     // arbitrary, distinct per SR

#define WMMU_NPTEGS    (WMMU_HTAB_SIZE / 64)

// The guest window: SH4 physical space, 512 MB at EA 0.
#define WMMU_WINDOW_END 0x20000000u

static vu32* htab_uc;         // uncached alias — all PTE writes go here so
                              // the hardware walker always sees them
static u32   htab_phys;
static bool  wmmu_up;         // hw initialized
static bool  wmmu_dispatch;   // DSI→_vmem emulation armed (self-test, and
                              // permanently once the JIT uses the window)
static bool  wmmu_jit_active; // rec_v2 may emit window accesses
static bool  wmmu_text_ok;    // allow dispatch for PCs in main .text (only
                              // during the self-test; afterwards a window
                              // fault from C code is a wild pointer → dump)

// DSI dispatch log — filled from EXCEPTION CONTEXT, so no printf/stdio here:
// newlib stdio takes LWP locks and the unlock path can invoke the thread
// scheduler, which deadlocks when called with EE=0 from an exception (this
// exact failure froze the first self-test right after printing "DSI #1").
// Normal-context code drains it via wmmu_drain_log().
struct WmmuLogEnt { u32 pc, ea; u8 store, size; };
static WmmuLogEnt wmmu_log[WMMU_LOG_DISPATCH];

static void wmmu_drain_log()   // call from NORMAL context only
{
    static u32 drained;
    while (drained < wmmu_stats.dsi_dispatched && drained < WMMU_LOG_DISPATCH)
    {
        const WmmuLogEnt& e = wmmu_log[drained++];
        printf("[WMMU]   DSI #%u: pc=%08x %s%u ea=%08x\n",
               drained, e.pc, e.store ? "w" : "r", e.size * 8, e.ea);
    }
}

static inline u32 mftb_lo()
{
    u32 v;
    asm volatile("mftb %0" : "=r"(v));
    return v;
}

// ---------------------------------------------------------------------------
// Page table
// ---------------------------------------------------------------------------

// Insert (or refresh) the PTE mapping 4 KB page 'ea' → physical 'pa'.
// Cached, write-back, R/C pre-set so the walker never needs to update them.
static bool wmmu_insert_pte(u32 ea, u32 pa)
{
    const u32 vsid = WMMU_VSID(ea >> 28);
    const u32 pidx = (ea >> 12) & 0xFFFF;
    const u32 hash = (vsid & 0x7FFFF) ^ pidx;
    const u32 w0   = PTE_V | (vsid << 7) | (pidx >> 10);   // V|VSID|API
    const u32 w1   = (pa & 0xFFFFF000) | PTE_R | PTE_C | PTE_PP_RW;

    for (u32 h = 0; h < 2; h++)
    {
        const u32  idx   = (h ? ~hash : hash) & (WMMU_NPTEGS - 1);
        vu32*      pteg  = &htab_uc[idx * 16];
        const u32  want0 = w0 | (h ? PTE_H : 0);

        // Refresh an existing mapping of this page, if any.
        for (u32 s = 0; s < 8; s++)
        {
            if (pteg[s * 2] == want0)
            {
                pteg[s * 2 + 1] = w1;
                asm volatile("sync");
                asm volatile("tlbie %0" :: "r"(ea));
                asm volatile("sync");
                return true;
            }
        }
    }

    for (u32 h = 0; h < 2; h++)
    {
        const u32 idx  = (h ? ~hash : hash) & (WMMU_NPTEGS - 1);
        vu32*     pteg = &htab_uc[idx * 16];

        for (u32 s = 0; s < 8; s++)
        {
            if (!(pteg[s * 2] & PTE_V))
            {
                // Data word first, then the valid bit — the walker can race
                // us at any time, it must never see V=1 with a stale RPN.
                pteg[s * 2 + 1] = w1;
                asm volatile("eieio");
                pteg[s * 2] = w0 | (h ? PTE_H : 0);
                asm volatile("sync");
                wmmu_stats.ptes_inserted++;
                return true;
            }
        }
    }
    return false;   // both PTEGs full → enlarge WMMU_HTAB_SIZE
}

bool WMMU_MapRange(u32 ea, void* host, u32 size)
{
    const u32 pa = (u32)host & 0x3FFFFFFF;   // virtual→physical (BAT regions)

    if (((u32)host & 0xFFF) || (ea & 0xFFF) || (size & 0xFFF))
    {
        printf("[WMMU] MapRange: unaligned ea=%08x host=%p size=%08x\n",
               ea, host, size);
        return false;
    }

    for (u32 off = 0; off < size; off += 0x1000)
    {
        if (!wmmu_insert_pte(ea + off, pa + off))
        {
            printf("[WMMU] HTAB full at ea=%08x (%u PTEs) — increase "
                   "WMMU_HTAB_SIZE\n", ea + off, wmmu_stats.ptes_inserted);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hardware bring-up
// ---------------------------------------------------------------------------
static void wmmu_hw_init()
{
    // Claim a size-aligned HTAB from the MEM2 arena (same allocator the
    // Dreamcast RAM/VRAM buffers came from in _vmem_reserve).
    u32 level = IRQ_Disable();

    u32 base = (u32)SYS_GetArena2Lo();
    base = (base + WMMU_HTAB_SIZE - 1) & ~(u32)(WMMU_HTAB_SIZE - 1);
    SYS_SetArena2Lo((void*)(base + WMMU_HTAB_SIZE));

    htab_phys = base & 0x3FFFFFFF;
    htab_uc   = (vu32*)(htab_phys | 0xC0000000);   // uncached BAT alias

    memset((void*)base, 0, WMMU_HTAB_SIZE);        // fast cached zero…
    DCFlushRange((void*)base, WMMU_HTAB_SIZE);     // …pushed out for the walker

    // Take over the DSI/ISI vectors before any translation can fault.
    __exception_sethandler(EX_DSI, wmmu_excpt_stub);
    __exception_sethandler(EX_ISI, wmmu_excpt_stub);

    // SDR1 = HTABORG | HTABMASK
    asm volatile("sync");
    asm volatile("mtspr 25,%0" :: "r"(htab_phys | ((WMMU_HTAB_SIZE >> 16) - 1)));
    asm volatile("isync");

    // Program all 16 segment registers with distinct VSIDs (no-execute).
    // SR0–SR7 form the guest window; SR8–SR15 sit behind the BATs and are
    // parked on VSIDs that map nothing, so a BAT-miss up there faults
    // cleanly instead of hitting leftover loader state.
    for (u32 sr = 0; sr < 16; sr++)
    {
        asm volatile("mtsrin %0,%1" :: "r"(SR_N | WMMU_VSID(sr)),
                                       "r"(sr << 28));
        asm volatile("isync");
    }

    // Invalidate any stale TLB entries (750: 2-way × 64 sets, EA-indexed).
    for (u32 i = 0; i < 64; i++)
        asm volatile("tlbie %0" :: "r"(i << 12));
    asm volatile("sync");

    IRQ_Restore(level);
    wmmu_up = true;
}

// ---------------------------------------------------------------------------
// DSI/ISI handling
// ---------------------------------------------------------------------------

// Terminal register dump. printf here writes straight into the console XFB
// (same mechanism as libogc's own crash screen), then we freeze so the text
// stays readable / lands in the log capture.
static void wmmu_dump(frame_context* ctx, const char* why)
{
    u32 dsisr;
    asm volatile("mfspr %0,18" : "=r"(dsisr));

    printf("\n[WMMU] FATAL %s: %s\n",
           ctx->EXCPT_Number == EX_DSI ? "DSI" : "ISI", why);
    printf("[WMMU] PC=%08x DAR=%08x DSISR=%08x MSR=%08x\n",
           ctx->SRR0, ctx->DAR, dsisr, ctx->MSR);
    printf("[WMMU] LR=%08x CTR=%08x CR=%08x XER=%08x\n",
           ctx->LR, ctx->CTR, ctx->CR, ctx->XER);
    for (int i = 0; i < 32; i += 4)
        printf("[WMMU] r%02d %08x %08x %08x %08x\n",
               i, ctx->GPR[i], ctx->GPR[i+1], ctx->GPR[i+2], ctx->GPR[i+3]);

    // Unrecoverable — freeze with the dump on screen/log. The asm keeps the
    // loop side-effectful so GCC cannot delete it (empty infinite loops are
    // UB and -O3 is allowed to assume they terminate, i.e. fall through).
    for (;;)
        asm volatile("");
}

static inline bool wmmu_pc_valid(u32 pc)
{
    if (pc & 3) return false;
    return (pc >= 0x80003000 && pc < 0x81800000) ||   // MEM1 (code, JIT cache)
           (pc >= 0x90000000 && pc < 0x94000000);     // MEM2
}

static inline bool wmmu_pc_in_cache(u32 pc)
{
    return pc >= (u32)CodeCache && pc < (u32)CodeCache + WMMU_CODECACHE_SIZE;
}

// Re-enable EE (+RI) around the _vmem dispatch so MMIO handlers run in a
// normal context — their printf/logging takes newlib/LWP locks whose unlock
// path may enter the thread scheduler, which is only safe with interrupts
// enabled. Gated on the interrupted context itself having EE on (it always
// does for JIT/self-test code; never force interrupts into a critical
// section). RI makes a nested fault recoverable enough to reach our
// recursion guard instead of an instant panic.
static inline u32 wmmu_msr_open(frame_context* ctx)
{
    u32 m;
    asm volatile("mfmsr %0" : "=r"(m));
    if (ctx->SRR1 & 0x8000)
    {
        u32 e = m | 0x8002;   // MSR_EE | MSR_RI
        asm volatile("mtmsr %0; isync" :: "r"(e));
    }
    return m;
}

static inline void wmmu_msr_close(u32 m)
{
    asm volatile("mtmsr %0; isync" :: "r"(m));
}

// One-shot self-heal for JIT MMIO sites. The emitted window fast path is
//     rlwinm r0,areg,3,29,31 ; cmpwi r0,7 ; beq cold      (P4 guard)
//     rlwinm r3,areg,0,3,31  ; [xori r3,r3,4-sz] ; load/store   <- faults
// A fault here means the address is handler-backed (MMIO/BIOS/ARAM) — and
// such sites essentially never see RAM addresses too, so flip the beq into
// an unconditional `b` to the same cold fragment: the site permanently
// takes the classic C-call path and never faults again. Register state at
// the beq is exactly the P4-taken case (areg still holds the raw guest
// address), so the cold fragment works unchanged. Patches die naturally
// with each code-cache clear and re-apply on the next fault.
static void wmmu_backpatch(u32 pc, u32 size, bool dcbz)
{
    if (dcbz)
        return;                                   // not a JIT fast-path shape

    u32*      beq_p = (u32*)(pc - (size < 4 ? 12 : 8));
    const u32 prev  = *(u32*)(pc - 4);

    // The word before the access must be our swizzle xori r3,r3,(4-size)
    // or the clrlwi rlwinm r3,rX,0,3,31 (source register field masked out).
    const u32 want_prev = (size < 4)
        ? (0x68630000u | (4 - size))
        : (0x54030000u | (3 << 6) | (31 << 1));
    const u32 prev_mask = (size < 4) ? 0xFFFFFFFFu : 0xFC1FFFFFu;
    if ((prev & prev_mask) != want_prev)
        return;

    // And the guard slot must hold `beq cr0,+forward` (AA=LK=0).
    const u32 insn = *beq_p;
    if ((insn & 0xFFFF0003u) != 0x41820000u)
        return;
    const s32 bd = (s32)(s16)(insn & 0xFFFC);
    if (bd <= 0)
        return;

    *beq_p = 0x48000000u | ((u32)bd & 0x03FFFFFC);   // b cold
    asm volatile("dcbst 0,%0; sync; icbi 0,%0; sync; isync" :: "r"(beq_p));
    wmmu_stats.dsi_patched++;
}

// Decoded memory operation
struct WmmuOp
{
    u32  size;      // 1/2/4
    bool store;
    bool update;    // rA ← EA
    bool sign;      // lha/lhax/lhaux
    bool brx;       // byte-reversed (lwbrx family)
    bool fp;        // float op (unsupported, phase 2)
    bool dcbz;
    bool valid;
};

static WmmuOp wmmu_decode(u32 op)
{
    WmmuOp m = {};
    const u32 pri = op >> 26;

    if (pri >= 32 && pri <= 45)         // D-form integer load/store
    {
        static const u8 sz[14] = { 4,4, 1,1, 4,4, 1,1, 2,2, 2,2, 2,2 };
        const u32 i = pri - 32;
        m.size   = sz[i];
        m.store  = (pri >= 36 && pri <= 39) || pri == 44 || pri == 45;
        m.update = (pri & 1) != 0;
        m.sign   = (pri == 42 || pri == 43);
        m.valid  = true;
    }
    else if (pri >= 48 && pri <= 55)    // D-form float
    {
        m.fp = true;
    }
    else if (pri == 31)                 // X-form
    {
        switch ((op >> 1) & 0x3FF)
        {
        case  23: m.size = 4;                 m.valid = true; break; // lwzx
        case  55: m.size = 4; m.update = 1;   m.valid = true; break; // lwzux
        case  87: m.size = 1;                 m.valid = true; break; // lbzx
        case 119: m.size = 1; m.update = 1;   m.valid = true; break; // lbzux
        case 279: m.size = 2;                 m.valid = true; break; // lhzx
        case 311: m.size = 2; m.update = 1;   m.valid = true; break; // lhzux
        case 343: m.size = 2; m.sign = 1;     m.valid = true; break; // lhax
        case 375: m.size = 2; m.sign = 1; m.update = 1; m.valid = true; break; // lhaux
        case 151: m.size = 4; m.store = 1;    m.valid = true; break; // stwx
        case 183: m.size = 4; m.store = 1; m.update = 1; m.valid = true; break; // stwux
        case 215: m.size = 1; m.store = 1;    m.valid = true; break; // stbx
        case 247: m.size = 1; m.store = 1; m.update = 1; m.valid = true; break; // stbux
        case 407: m.size = 2; m.store = 1;    m.valid = true; break; // sthx
        case 439: m.size = 2; m.store = 1; m.update = 1; m.valid = true; break; // sthux
        case 534: m.size = 4; m.brx = 1;      m.valid = true; break; // lwbrx
        case 662: m.size = 4; m.brx = 1; m.store = 1; m.valid = true; break; // stwbrx
        case 790: m.size = 2; m.brx = 1;      m.valid = true; break; // lhbrx
        case 918: m.size = 2; m.brx = 1; m.store = 1; m.valid = true; break; // sthbrx
        case 1014: m.dcbz = 1;                m.valid = true; break; // dcbz
        case 535: case 599: case 663: case 727:                      // lfsx…stfdx
            m.fp = true; break;
        default: break;
        }
    }
    return m;
}

static volatile u32 wmmu_depth;   // nested-DSI guard (dispatch runs with RI=1)

static void wmmu_handle_dsi(frame_context* ctx)
{
    if (++wmmu_depth > 1)
        wmmu_dump(ctx, "recursive DSI inside dispatch");
    if (!wmmu_dispatch)
        wmmu_dump(ctx, "DSI while guest dispatch is disabled");
    if (!wmmu_pc_valid(ctx->SRR0))
        wmmu_dump(ctx, "faulting PC outside code regions");

    // Only JIT-emitted window accesses (and, during the self-test, probes
    // from main .text) are legitimate — a window fault from ordinary C code
    // is a wild/null pointer and must crash loudly, not read guest memory.
    const bool from_jit = wmmu_pc_in_cache(ctx->SRR0);
    if (!from_jit && !wmmu_text_ok)
        wmmu_dump(ctx, "window fault from C code (wild pointer?)");

    const u32 op = *(u32*)ctx->SRR0;
    const u32 ra = (op >> 16) & 31;
    const u32 rd = (op >> 21) & 31;   // rD (load) / rS (store)

    WmmuOp m = wmmu_decode(op);
    if (m.fp)
        wmmu_dump(ctx, "FP load/store in window (unsupported until phase 2)");
    if (!m.valid)
        wmmu_dump(ctx, "unhandled opcode form");
    if (m.update && ra == 1)
        wmmu_dump(ctx, "update-form on r1 (stub restores r1 by offset)");

    // EA from the register file (authoritative; DAR only cross-checked).
    u32 ea = ra ? ctx->GPR[ra] : 0;
    if ((op >> 26) == 31)
        ea += ctx->GPR[(op >> 11) & 31];       // X-form: + rB
    else
        ea += (s32)(s16)(op & 0xFFFF);         // D-form: + signed displacement

    // Dispatch below runs with interrupts re-enabled (see wmmu_msr_open) so
    // the _vmem handlers behave exactly as on the classic cold-call path.
    const u32 saved_msr = wmmu_msr_open(ctx);

    if (m.dcbz)
    {
        ea &= ~31u;
        if (ea >= WMMU_WINDOW_END)
            wmmu_dump(ctx, "dcbz outside guest window");
        for (u32 i = 0; i < 32; i += 4)
            _vmem_WriteMem32(ea + i, 0);
    }
    else
    {
        if (ea >= WMMU_WINDOW_END)
            wmmu_dump(ctx, "data access outside guest window (wild pointer?)");
        if ((ea ^ ctx->DAR) & 0xFFFFF000)
            wmmu_dump(ctx, "EA/DAR mismatch (page-crossing or decode bug)");

        // Undo the big-endian sub-word address swizzle the accessor applied
        // (window convention == _vmem convention: ea = guest ^ (4-size)).
        const u32 guest = (m.size == 4) ? ea : ea ^ (4 - m.size);

        if (m.store)
        {
            u32 v = ctx->GPR[rd];
            if (m.brx)
                v = (m.size == 4) ? __builtin_bswap32(v)
                                  : __builtin_bswap16((u16)v);
            switch (m.size)
            {
            case 1: _vmem_WriteMem8 (guest, (u8)v);  break;
            case 2: _vmem_WriteMem16(guest, (u16)v); break;
            case 4: _vmem_WriteMem32(guest, v);      break;
            }
        }
        else
        {
            u32 v = 0;
            switch (m.size)
            {
            case 1: v = _vmem_ReadMem8 (guest); break;
            case 2: v = _vmem_ReadMem16(guest); break;
            case 4: v = _vmem_ReadMem32(guest); break;
            }
            if (m.brx)
                v = (m.size == 4) ? __builtin_bswap32(v)
                                  : __builtin_bswap16((u16)v);
            if (m.sign)
                v = (u32)(s32)(s16)v;
            ctx->GPR[rd] = v;
        }

        if (m.update)
            ctx->GPR[ra] = ea;
    }

    wmmu_msr_close(saved_msr);

    // JIT sites: retarget this site's P4 beq to its cold fragment so future
    // executions call the C path directly instead of faulting again.
    if (from_jit)
        wmmu_backpatch(ctx->SRR0, m.size, m.dcbz);

    // NO printf here — we are in exception context and must resume; stdio
    // locking can bounce through the thread scheduler and deadlock (even
    // with EE reopened above, the frame restore must not be preempted-
    // dependent). Log to the ring buffer instead (drained from normal
    // context by the self-test / WMMU_PrintStats).
    const u32 n = wmmu_stats.dsi_dispatched++;
    if (n < WMMU_LOG_DISPATCH)
    {
        WmmuLogEnt& e = wmmu_log[n];
        e.pc    = ctx->SRR0;
        e.ea    = ea;
        e.store = m.store || m.dcbz;
        e.size  = m.dcbz ? 4 : m.size;   // dcbz logs as a 32-bit store
    }

    ctx->SRR0 += 4;   // resume past the emulated instruction
    wmmu_depth--;
}

// Called from wmmu_excpt_stub with the completed exception frame.
// Returns to the stub, which restores the frame and rfi's to SRR0.
extern "C" int WMMU_HandleExcpt(frame_context* ctx)
{
    if (ctx->EXCPT_Number == EX_DSI)
        wmmu_handle_dsi(ctx);
    else
    {
        wmmu_stats.isi_seen++;
        wmmu_dump(ctx, "instruction fetch fault (jump into guest window?)");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Dreamcast mapping
// ---------------------------------------------------------------------------
static bool wmmu_map_dreamcast()
{
    bool ok = true;

    // Area 3: main RAM, 16 MB × 4 mirrors (0x0C–0x0F), same as _vmem map.
    for (u32 m = 0; m < 4; m++)
        ok &= WMMU_MapRange(0x0C000000 + m * 0x01000000, mem_b.data, RAM_SIZE);

    // Area 1: VRAM 64-bit interface (8 MB, wraps twice per 16 MB page) at
    // 0x04, mirrored at 0x06. 0x05/0x07 (32-bit interface) stay handler-
    // backed → DSI. AICA RAM / BIOS / flash also stay handler-backed: their
    // handlers may have side effects or different storage conventions, and
    // correctness beats speed for phase 1.
    ok &= WMMU_MapRange(0x04000000, vram.data, VRAM_SIZE);
    ok &= WMMU_MapRange(0x04800000, vram.data, VRAM_SIZE);
    ok &= WMMU_MapRange(0x06000000, vram.data, VRAM_SIZE);
    ok &= WMMU_MapRange(0x06800000, vram.data, VRAM_SIZE);

    return ok;
}

// ---------------------------------------------------------------------------
// Self-test + benchmark
// ---------------------------------------------------------------------------
#if WMMU_SELFTEST

#define WMMU_CHECK(name, cond)                                   \
    do {                                                         \
        bool _ok = (cond);                                       \
        printf("[WMMU]   %-28s %s\n", name, _ok ? "PASS" : "FAIL"); \
        if (_ok) pass++; else fail++;                            \
    } while (0)

static bool wmmu_selftest()
{
    int pass = 0, fail = 0;

    printf("[WMMU] self-test:\n");

    // --- direct-mapped RAM, both directions, u32 -------------------------
    vu32* w    = (vu32*)0x0C000000;
    u32*  host = (u32*)mem_b.data;
    u32   save = host[0];

    *w = 0xCAFEBABE;
    asm volatile("sync");
    WMMU_CHECK("RAM window->host u32", host[0] == 0xCAFEBABE);

    host[0] = 0x11223344;
    WMMU_CHECK("RAM host->window u32", *w == 0x11223344);
    WMMU_CHECK("RAM _vmem agreement", _vmem_ReadMem32(0x0C000000) == 0x11223344);
    host[0] = save;

    // --- sub-word swizzle (window uses the same ^(4-size) trick) ---------
    u32 save10 = _vmem_ReadMem32(0x0C000010);
    _vmem_WriteMem8(0x0C000013, 0xAB);
    WMMU_CHECK("RAM u8 swizzle", *(vu8*)(0x0C000013 ^ 3) == 0xAB);
    _vmem_WriteMem16(0x0C000010, 0xBEEF);
    WMMU_CHECK("RAM u16 swizzle", *(vu16*)(0x0C000010 ^ 2) == 0xBEEF);
    _vmem_WriteMem32(0x0C000010, save10);

    // --- 16 MB mirrors ---------------------------------------------------
    u32 save40 = host[0x40 / 4];
    *(vu32*)0x0C000040 = 0x600DBEEF;
    asm volatile("sync");
    WMMU_CHECK("RAM mirror 0x0F", *(vu32*)0x0F000040 == 0x600DBEEF);
    host[0x40 / 4] = save40;

    // --- VRAM window -----------------------------------------------------
    u32* vhost = (u32*)vram.data;
    u32  vsave = vhost[0];
    *(vu32*)0x04000000 = 0x7E57DA7A;
    asm volatile("sync");
    WMMU_CHECK("VRAM window->host u32", vhost[0] == 0x7E57DA7A);
    WMMU_CHECK("VRAM mirror 0x06+8MB", *(vu32*)0x06800000 == 0x7E57DA7A);
    vhost[0] = vsave;

    // --- DSI → _vmem dispatch: read BIOS region (handler-backed) ---------
    // Address laundered through a volatile so GCC can't see the near-zero
    // constant (-Warray-bounds "source object is likely at address zero").
    volatile u32 bios_addr = 0x00000100;
    u32 before = wmmu_stats.dsi_dispatched;
    u32 got    = *(vu32*)bios_addr;               // BIOS @ 0x100, unmapped → DSI
    wmmu_drain_log();
    WMMU_CHECK("DSI read dispatch (BIOS)",
               got == _vmem_ReadMem32(0x00000100) &&
               wmmu_stats.dsi_dispatched == before + 1);

    // --- DSI → _vmem dispatch: write sound RAM (handler-backed) ----------
    u32 asave = _vmem_ReadMem32(0x00800040);
    *(vu32*)0x00800040 = 0x50BEEF01;
    asm volatile("sync");
    wmmu_drain_log();
    WMMU_CHECK("DSI write dispatch (ARAM)",
               _vmem_ReadMem32(0x00800040) == 0x50BEEF01);
    _vmem_WriteMem32(0x00800040, asave);

    // --- benchmark: window load vs _vmem dispatch ------------------------
    {
        const u32 N = 1000000;
        vu32* bp  = (vu32*)0x0C001000;
        u32   acc = 0;

        u32 t0 = mftb_lo();
        for (u32 i = 0; i < N; i++)
            acc += *bp;
        u32 t1 = mftb_lo();
        for (u32 i = 0; i < N; i++)
            acc += _vmem_ReadMem32(0x0C001000);
        u32 t2 = mftb_lo();

        u32 tw = t1 - t0, tv = t2 - t1;
        printf("[WMMU]   bench %u reads: window=%u ticks, _vmem=%u ticks "
               "(x%u.%u faster) [acc=%08x]\n",
               N, tw, tv, tv / tw, (tv * 10 / tw) % 10, acc);
    }

    printf("[WMMU] self-test: %d passed, %d FAILED\n", pass, fail);
    return fail == 0;
}
#endif // WMMU_SELFTEST

// ---------------------------------------------------------------------------
// Entry point (from mem_map_defualt)
// ---------------------------------------------------------------------------
void WMMU_OnMemMapped()
{
    static bool done = false;
    if (done)
        return;   // mem map rebuilt on reset — our PTEs are already correct
    done = true;

    printf("[WMMU] Broadway MMU init: HTAB %u KB, window 0x00000000-0x1FFFFFFF\n",
           WMMU_HTAB_SIZE / 1024);

    if (((u32)mem_b.data & 0xFFF) || ((u32)vram.data & 0xFFF))
    {
        printf("[WMMU] ABORT: RAM/VRAM buffers not 4 KB aligned (%p / %p)\n",
               mem_b.data, vram.data);
        return;
    }

    wmmu_hw_init();

    if (!wmmu_map_dreamcast())
    {
        printf("[WMMU] mapping FAILED — window unusable\n");
        return;
    }

    printf("[WMMU] HTAB @ phys %08x, %u PTEs inserted, MEM2 free %.2f MB\n",
           htab_phys, wmmu_stats.ptes_inserted,
           ((u32)SYS_GetArena2Hi() - (u32)SYS_GetArena2Lo()) / (1024.f * 1024.f));

    bool healthy = true;
#if WMMU_SELFTEST
    wmmu_dispatch = true;
    wmmu_text_ok  = true;    // self-test probes fault from main .text
    healthy = wmmu_selftest();
    wmmu_text_ok  = false;
    wmmu_dispatch = false;
#endif

    // Runtime jit_mmu preset (menu page 3 / game_presets.cfg): decided
    // before the dynarec compiles its first block, constant afterwards.
    {
        const int mode = get_jit_mmu_preset();

        if (mode == 1)
        {
            if (healthy)
            {
                // Fastmem live: rec_v2 emits window accesses; MMIO faults
                // once per site (emulated + back-patched to the cold path),
                // RAM/VRAM never.
                wmmu_dispatch   = true;
                wmmu_jit_active = true;
                printf("[WMMU] JIT fastmem ACTIVE (window codegen + DSI backpatch)\n");
            }
            else
                printf("[WMMU] jit_mmu=1 refused (self-test failed) — legacy codegen\n");
        }
        else if (mode == 2)
        {
            // Mode 2 needs no MMU machinery at runtime — pure codegen
            // (BAT-masked RAM reads). Window stays validated but dormant.
            printf("[WMMU] jit_mmu=2: BAT-masked RAM read codegen (no TLB, no DSI)\n");
        }
        (void)healthy;
    }
}

bool WMMU_JitActive()
{
    return wmmu_jit_active;
}

void WMMU_PrintStats()
{
    static u32 last_d, last_p;
    if (wmmu_stats.dsi_dispatched == last_d && wmmu_stats.dsi_patched == last_p)
        return;
    last_d = wmmu_stats.dsi_dispatched;
    last_p = wmmu_stats.dsi_patched;
    wmmu_drain_log();
    printf("[WMMU] dsi=%u patched=%u\n", last_d, last_p);
}

#endif // WMMU_ENABLE && OS_WII
