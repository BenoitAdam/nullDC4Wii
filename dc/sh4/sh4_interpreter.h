#pragma once
#include "types.h"
#include "sh4_if.h"
#include "intc.h"

#undef sh4op
#define sh4op(str) void FASTCALL str(u32 op)
typedef void (FASTCALL OpCallFP)(u32 op);

// -------------------------------------------------------------------------
// Runtime accuracy preset (defined in main.cpp)
//   0 = Fast     — largest timeslice, loosest peripheral timing
//   1 = Balanced — moderate timeslice, good compatibility
//   2 = Accurate — original timeslice, tightest peripheral timing
// -------------------------------------------------------------------------
extern "C" int get_accuracy_preset();

#define FAST()     (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

// -------------------------------------------------------------------------
// Per-preset SH4 execution constants
//
//  TIMESLICE     — SH4 cycles dispatched between UpdateSystem() calls
//  CPU_RATIO     — opcode cost in host cycles (drains the timeslice counter)
//
//  Cascade thresholds (in UpdateSystem() call counts):
//  MEDIUM_PERIOD — AICA / DMA
//  SLOW_PERIOD   — GDRom        (2× medium)
//  VSLOW_PERIOD  — Maple / RTC  (4× medium)
//
//  Fast stretches the timeslice so peripherals are polled less often,
//  saving CPU at the cost of timing accuracy.
// -------------------------------------------------------------------------

//                             TIMESLICE   CPU_RATIO  MEDIUM  SLOW   VSLOW
// Fast     (1792 cycles)         ×4          8          4      8      16
// Balanced  (896 cycles)         ×2          8          8     16      32
// Accurate  (448 cycles)    original         8          8     16      32

static const s32 SH4_TIMESLICE_FAST      = 1792;
static const s32 SH4_CPU_RATIO_FAST      = 8;
static const u32 SH4_MEDIUM_FAST         = 4;    // every  7 168 cycles
static const u32 SH4_SLOW_FAST           = 8;    // every 14 336 cycles
static const u32 SH4_VSLOW_FAST          = 16;   // every 28 672 cycles

static const s32 SH4_TIMESLICE_BALANCED  = 896;
static const s32 SH4_CPU_RATIO_BALANCED  = 8;
static const u32 SH4_MEDIUM_BALANCED     = 8;    // every  7 168 cycles
static const u32 SH4_SLOW_BALANCED       = 16;   // every 14 336 cycles
static const u32 SH4_VSLOW_BALANCED      = 32;   // every 28 672 cycles

static const s32 SH4_TIMESLICE_ACCURATE  = 448;
static const s32 SH4_CPU_RATIO_ACCURATE  = 8;
static const u32 SH4_MEDIUM_ACCURATE     = 8;    // every  3 584 cycles // very accurate 1
static const u32 SH4_SLOW_ACCURATE       = 16;   // every  7 168 cycles // very accurate 2
static const u32 SH4_VSLOW_ACCURATE      = 32;   // every 14 336 cycles // very accurate 4

// -------------------------------------------------------------------------
// Opcode classification flags
// -------------------------------------------------------------------------
enum OpcodeType
{
	Normal       = 0,    // No special handling required
	ReadsPC      = 1,    // PC must be valid/set before calling
	WritesPC     = 2,    // Opcode writes PC (branch)
	Delayslot    = 4,    // Has a delay-slot opcode (only valid with WritesPC)
	WritesSR     = 8,    // Writes SR  -> UpdateSR() must be called after
	WritesFPSCR  = 16,   // Writes FPSCR -> UpdateFPSCR() must be called after
	Invalid      = 128,  // Undefined/illegal opcode

	ReadWritePC       = ReadsPC | WritesPC,
	WritesSRRWPC      = WritesSR | ReadsPC | WritesPC,

	Branch_dir        = ReadWritePC,
	Branch_rel        = ReadWritePC,
	Branch_dir_d      = Delayslot | Branch_dir,
	Branch_rel_d      = Delayslot | Branch_rel,
};

// -------------------------------------------------------------------------
// Interpreter interface
// -------------------------------------------------------------------------
void Sh4_int_Run();
void Sh4_int_Stop();
void Sh4_int_Step();
void Sh4_int_Skip();
void Sh4_int_Reset(bool Manual);
void Sh4_int_Init();
void Sh4_int_Term();
bool Sh4_int_IsCpuRunning();

// Exception helpers (legacy misspelling kept as inline alias for ABI compat)
void FASTCALL sh4_int_RaiseException(u32 ExceptionCode, u32 VectorAddr);
static inline void FASTCALL sh4_int_RaiseExeption(u32 code, u32 vec)
{
	sh4_int_RaiseException(code, vec);
}

u32  Sh4_int_GetRegister(Sh4RegType reg);
void Sh4_int_SetRegister(Sh4RegType reg, u32 regdata);

void ExecuteDelayslot();
void ExecuteDelayslot_RTE();

int FASTCALL UpdateSystem();
// Split form for the recompiler: _no_event() runs the GPR-free peripheral
// cascade + interrupt-pending check (returns nonzero if an interrupt must be
// dispatched); _handle_event() runs the GPR-touching dispatch. See
// sh4_interpreter.cpp for the equivalence invariant.
int FASTCALL UpdateSystem_no_event();
int FASTCALL UpdateSystem_handle_event();

// Coarse timer (~13,216 kHz) used for block lifecycle tracking
extern u32 gcp_timer;
