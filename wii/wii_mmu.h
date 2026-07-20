#pragma once
// ---------------------------------------------------------------------------
// wii_mmu – drive the Broadway (PPC750CL) paged MMU from the emulator.
//
// Goal: map the Dreamcast guest address space 1:1 into the Wii's unused
// effective-address range 0x00000000–0x7FFFFFFF (segment registers SR0–SR7),
// so the SH4 recompiler can eventually replace the _vmem table-lookup
// load/store path with plain PPC lwz/stw instructions:
//
//     guest 0x0C000000 (DC main RAM)  ==  PPC effective addr 0x0C000000
//
// RAM/VRAM pages are backed by real PTEs (hashed page table, 4 KB pages).
// Everything else (MMIO: PVR regs, AICA regs, GD-ROM, SH4 area0 …) is left
// unmapped: an access raises a DSI exception, our handler decodes the
// faulting instruction and dispatches it through the existing _vmem
// handlers, then resumes — the classic "fastmem" trick.
//
// libogc only uses BATs (0x8000_0000+ regions); SRs and SDR1 are ours to
// program (confirmed by libogc dev). This build links the "tuxedo" libogc:
// we install our handler with PPCExcptSetHandler(), and wii_mmu_excpt.S
// builds the frame_context itself from the SPRG0/1 + __ppc_excpt_buf
// protocol (verified by disassembling libogc.a's exception_asm.o /
// exception.o — no public header documents it).
//
// Byte-order convention: identical to _vmem's big-endian trick — 32-bit
// accesses are direct, 8/16-bit accesses must XOR the low address bits
// (ea ^ (4-size)). The window aliases the very same host buffers, so data
// written through the window is immediately visible via mem_b/vram and
// vice versa (same physical cache lines — the 750 dcache is physically
// tagged, and 4 KB way size means no aliasing issues).
//
// Phase 1 (this file): infrastructure + boot-time self-test + benchmark.
//   The recompiler does NOT use the window yet; enabling this must not
//   change emulation behaviour, it only claims ~512 KB of MEM2 for the
//   page table and takes over the DSI/ISI exception vectors.
// Phase 2 (later): point rec_v2 codegen at the window (mask top address
//   bit for P1/P2/P3 mirrors, keep the existing P4 special path).
//
// This header is included by wii_mmu_excpt.S — keep everything outside the
// __ASSEMBLER__ guard preprocessor-only.
// ---------------------------------------------------------------------------

// ---- Compile-time switches ------------------------------------------------
// Master switch. 0 = everything (C + asm stub) compiles to nothing and the
// exception vectors are left untouched.
#define WMMU_ENABLE       1

// Run the boot-time self-test + benchmark (prints PASS/FAIL lines).
#define WMMU_SELFTEST     1

// JIT guest-memory codegen mode — RUNTIME preset since 2026-07-20:
// `jit_mmu` in game_presets.cfg / options menu page 3 (g_jit_mmu_preset,
// get_jit_mmu_preset() in wii/main.cpp). Fixed at game launch; the
// recompiler bakes the mode into every emitted block.
//
//   0 = legacy codegen (default): reads fast only for 0x8C P1 RAM, writes
//       inline the _vmem table walk; everything else cold C calls.
//
//   1 = MMU window + DSI backpatch (phase 2 experiment). Correct on real
//       hardware, but MEASURED ~10% SLOWER (Castlevania): Broadway's DTLB
//       is 128 entries x 4 KB (512 KB reach) and every miss costs a
//       hardware HTAB walk to MEM2 — a per-access tax the BAT-based paths
//       never pay. Kept for A/B and as research infrastructure. Requires
//       a fully PASSing boot self-test, else silently falls back to legacy.
//
//   2 = BAT-masked RAM READ path (phase 2b). No MMU at all. READS ONLY:
//       any RAM mirror is masked to its 16 MB offset and added to
//       mem_b.data (a BAT-mapped pointer — zero TLB involvement):
//         rlwinm r0,addr,6,29,31 ; cmpwi 3 ; bne cold   <- one guard:
//              all RAM mirrors 0x0C/0x2C/../0xCC have addr[28:26]==0b011,
//              which also auto-excludes OC-RAM (0x7C) and area7 (0x1F).
//              (Quirk: reserved P4 RAM-lookalikes 0xEC-0xEF are treated as
//              RAM; DC software never touches them.)
//         rlwinm rarg0,addr,0,8,31 ; addis rarg0,mem_b_hi ; [xori] ; load
//       Covers ALL RAM mirrors (legacy reads: 0x8C only) for +1 insn.
//       WRITES stay on the legacy inline table walk on purpose: it already
//       covers every direct-mapped region — RAM, VRAM and the HOT store
//       queues 0xE0-0xE3 (TA geometry submission) — and stores don't care
//       about the table load's latency. Narrowing writes to RAM (the first
//       mode-1/2 attempt) sent SQ/VRAM stores to cold C calls: -2 FPS.
//
// Verdict of the A/B on Castlevania (real Wii): 0 = 32 FPS, 1 = 30, 2 = 31.
// The legacy BAT-based inline paths were already at the hardware optimum —
// Broadway's BATs are fastmem, and its 128-entry DTLB makes real page
// tables a net loss. 0 stays the default; 1/2 selectable per game.

// Hashed page table size. Must be 64 KB * power of two, >= 64 KB.
// 512 KB = 8192 PTEGs = 64k PTE slots; we insert ~24k PTEs (RAM x4 mirrors
// + VRAM x4 wraps) so collisions stay rare. Allocated from the MEM2 arena.
#define WMMU_HTAB_SIZE    (512 * 1024)

// Log the first N DSI-dispatched MMIO accesses (then go quiet).
#define WMMU_LOG_DISPATCH 16

// ---- frame_context layout (shared by wii_mmu.cpp / wii_mmu_excpt.S) -------
// Built by wmmu_excpt_stub on the interrupted stack from the new "tuxedo"
// libogc exception ABI (PPCExcptSetHandler). See wii_mmu_excpt.S for the
// entry protocol (SPRG0/1 + __ppc_excpt_buf — reverse-engineered from
// libogc.a's exception_asm.o/exception.o, no public header exposes it).
#if WMMU_ENABLE
#define WMMU_CTX_EXCPT   0x00
#define WMMU_CTX_SRR0    0x04
#define WMMU_CTX_SRR1    0x08
#define WMMU_CTX_MSR     0x0C
#define WMMU_CTX_DAR     0x10
#define WMMU_CTX_LR      0x14
#define WMMU_CTX_CTR     0x18
#define WMMU_CTX_CR      0x1C
#define WMMU_CTX_XER     0x20
#define WMMU_CTX_GPR     0x24
#define WMMU_CTX_SIZE    0xB0   // 0xA4 rounded up to 16 for stack alignment
#endif

#ifndef __ASSEMBLER__

#include "types.h"

#if WMMU_ENABLE && (HOST_OS == OS_WII)

// Exception-time register snapshot handed to WMMU_HandleExcpt(). Field order
// matches WMMU_CTX_* above exactly (all u32, no padding) — keep both in sync.
// Field names avoid SRR0/SRR1/DAR: tuxedo/ppc/spr.h #defines those to raw SPR
// numbers, and since it's dragged in transitively ahead of this header (via
// types.h -> gccore.h), "ctx->SRR0" would preprocess into "ctx->26".
typedef struct frame_context
{
    u32 EXCPT_Number;
    u32 pc;     // SRR0
    u32 srr1;   // SRR1 (original MSR)
    u32 MSR;    // duplicate of srr1; kept for the wmmu_dump() printf
    u32 dar;    // DAR
    u32 LR;
    u32 CTR;
    u32 CR;
    u32 XER;
    u32 GPR[32];
} frame_context;

// One-shot hook called at the end of mem_map_defualt(): installs handlers,
// builds the page table, maps RAM/VRAM, runs the self-test. Idempotent.
void WMMU_OnMemMapped();

// Map [ea .. ea+size) 1:1 onto host buffer 'host' (both 4 KB aligned).
// Returns false if a PTE could not be inserted (both PTEGs full).
bool WMMU_MapRange(u32 ea, void* host, u32 size);

// True once init + mapping + self-test all succeeded AND WMMU_JIT is on:
// the recompiler may emit window accesses. Constant after WMMU_OnMemMapped.
bool WMMU_JitActive();

// Print/refresh the DSI counters (normal context; called from the SPG
// 2-second stats tick). Prints only when the counters changed.
void WMMU_PrintStats();

// Stats.
struct WMMU_Stats
{
    u32 ptes_inserted;
    u32 dsi_dispatched;   // MMIO accesses emulated via DSI
    u32 dsi_patched;      // JIT sites back-patched to their cold path
    u32 isi_seen;
};
extern WMMU_Stats wmmu_stats;

#else

inline void WMMU_OnMemMapped() {}
inline bool WMMU_JitActive()   { return false; }
inline void WMMU_PrintStats()  {}

#endif // WMMU_ENABLE && OS_WII

#endif // __ASSEMBLER__
