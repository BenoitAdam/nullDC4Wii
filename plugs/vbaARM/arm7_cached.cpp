// ARM7DI cached (pre-decoded / threaded) interpreter for the Dreamcast AICA core.
//
// This is an ALTERNATIVE execution path to arm_Run() in arm7.cpp. It keeps the
// proven per-opcode semantics from arm-new.h but removes the per-instruction
// fetch + condition switch + 12-bit decode switch from the hot loop. Each ARM
// instruction is decoded ONCE into one ARM7CACHEOP "uop" holding a computed-goto
// dispatch label plus pre-extracted operand fields (r0..r3 / imm / cond). The hot
// loop is then a chain of `goto *next->dispatch`.
//
// Coverage: the ARM7DI (ARMv3) instruction set ONLY, per arm7di.pdf Figure 8:
//   Data Processing / PSR Transfer (MRS/MSR), Multiply (MUL/MLA),
//   Single Data Swap (SWP/SWPB), Single Data Transfer (LDR/STR, word/byte),
//   Block Data Transfer (LDM/STM), Branch (B/BL), Software Interrupt (SWI).
// Instructions NOT in ARM7DI (halfword/signed loads, long multiply, BX, Thumb,
// coprocessor) are decoded to the undefined-instruction handler.
//
// Cache invalidation policy (per project decision): the cache is reset only when
// the core is reset (arm_Run_Cached(_, reset=true)). Self-modifying / DMA-written
// AICA code is NOT tracked. This matches arm_Run()'s assumption for AICA workloads.
//
// Requires GCC computed-goto (&&label / goto *). The active build target is
// __GNUC__ + __POWERPC__ (devkitPPC / Wii), which supports it.

#include "arm7.h"
#include "arm_mem.h"

#define C_CORE
#define DEV_VERSION

// ── Glue identical to arm7.cpp ──────────────────────────────────────────────
#define CPUReadMemoryQuick(addr) arm_ReadMem32((addr) & ARAM_MASK)
#define CPUReadByte arm_ReadMem8
#define CPUReadMemory arm_ReadMem32
#define CPUReadHalfWord arm_ReadMem16

#define CPUWriteMemory arm_WriteMem32
#define CPUWriteHalfWord arm_WriteMem16
#define CPUWriteByte arm_WriteMem8

#define reg arm_Reg
#define armNextPC arm_ArmNextPC

#define CPUUpdateTicksAccessSeq32(a) 1
#define CPUUpdateTicksAccess32(a) 1
#define CPUUpdateTicksAccess16(a) 1

// ── Shared core state (defined in arm7.cpp) ─────────────────────────────────
enum
{
	R13_IRQ=18, R14_IRQ=19, SPSR_IRQ=20,
	R13_USR=26, R14_USR=27,
	R13_SVC=28, R14_SVC=29, SPSR_SVC=30,
	R13_ABT=31, R14_ABT=32, SPSR_ABT=33,
	R13_UND=34, R14_UND=35, SPSR_UND=36,
	R8_FIQ=37, R9_FIQ=38, R10_FIQ=39, R11_FIQ=40, R12_FIQ=41,
	R13_FIQ=42, R14_FIQ=43, SPSR_FIQ=44,
};

typedef union { u32 I; } reg_pair;

extern u32 arm_ArmNextPC;
extern reg_pair arm_Reg[45];

extern bool C_OUT, N_FLAG, Z_FLAG, C_FLAG, V_FLAG;
extern bool armIrqEnable, armFiqEnable;
extern int armMode;
extern bool Arm7Enabled;
extern BYTE cpuBitsSet[256];

void CPUSwitchMode(int mode, bool saveState, bool breakLoop=true);
void CPUUpdateCPSR();
void CPUUpdateFlags(bool breakLoop);
void CPUSoftwareInterrupt(int comment);
void CPUUndefinedException();
void CPUFiq();

// CPUUpdateFlags / CPUSwitchMode in arm7.cpp default breakLoop=true. The cached
// path is driven by the SH4 timeslice the same way as arm_Run(), so keep the
// same break-loop behavior.
#define CPUUpdateFlags() CPUUpdateFlags(true)

// ── Cached uop layout ───────────────────────────────────────────────────────
// NOTE on the original spec: it sketched ARM7CACHEOP as a `union` of
// {void* dispatch; u8 r0..r3; u32 imm}. A union overlaps all members, so it
// cannot hold a dispatch pointer AND four register indices AND an immediate at
// once. The intended layout is a struct (one dispatch + the operand fields),
// which is what we use. On the 32-bit PPC target this is 12 bytes.
struct ARM7CACHEOP {
	void* dispatch;   // computed-goto target (&&label)
	u8 r0;            // primary: dest (Rd)
	u8 r1;            // primary: base / source1 (Rn)
	u8 r2;            // primary: source2 register (Rm) / Rs
	u8 cond;          // ARM condition field (opcode>>28)
	u32 imm;          // immediate operand / shift amount / raw opcode (LDM/STM)
};

#define ARM7_CACHE_SIZE   (192*1024)
#define ARM7_EP_SIZE      (2*1024*1024)
#define ARM7_EP_MASK      (ARAM_MASK)        // logical_pc & MASK -> entrypoint slot

static ARM7CACHEOP  ARM7_CACHE[ARM7_CACHE_SIZE];
static ARM7CACHEOP* ARM7_ENTRYPOINTS[ARM7_EP_SIZE];
static u32          ARM7_CACHE_LAST_OP;

// Set on reset(); the next non-reset entry (which has label addresses in scope)
// initializes the dispatch sentinel and clears every entrypoint to it.
static bool         s_cache_dirty = true;

// ── Condition evaluation (mirrors arm-new.h) ────────────────────────────────
static inline bool cond_check(u32 cond)
{
	switch(cond) {
	case 0x0: return Z_FLAG;
	case 0x1: return !Z_FLAG;
	case 0x2: return C_FLAG;
	case 0x3: return !C_FLAG;
	case 0x4: return N_FLAG;
	case 0x5: return !N_FLAG;
	case 0x6: return V_FLAG;
	case 0x7: return !V_FLAG;
	case 0x8: return C_FLAG && !Z_FLAG;
	case 0x9: return !C_FLAG || Z_FLAG;
	case 0xA: return N_FLAG == V_FLAG;
	case 0xB: return N_FLAG != V_FLAG;
	case 0xC: return !Z_FLAG && (N_FLAG == V_FLAG);
	case 0xD: return Z_FLAG || (N_FLAG != V_FLAG);
	case 0xE: return true;
	default:  return false; // 0xF: never (NV)
	}
}

// ── Block-data-transfer execution helpers ───────────────────────────────────
// LDM/STM are data-dependent (the register list is decoded at runtime), so per
// the project decision they use runtime-decoded handlers rather than a label
// per list. These free functions implement the canonical ARM LDM/STM address
// algorithm, which is exactly what arm-new.h's per-variant code computes:
//   n        = popcount(reg list)
//   IA P=0U=1 first=Rn         wb=Rn+4n
//   IB P=1U=1 first=Rn+4       wb=Rn+4n
//   DA P=0U=0 first=Rn-4n+4    wb=Rn-4n
//   DB P=1U=0 first=Rn-4n      wb=Rn-4n
// Registers transfer in ascending register order at ascending addresses.
// Writeback (W) lands the computed final base; matching arm-new.h, when the
// base is also in the list it is written back as part of the transfer order
// (the early-writeback `temp` trick), which the self-test exercises.

static inline u32 arm_blockBase(u32 opcode, u32* outFirst, u32* outWb)
{
	int base = (opcode >> 16) & 0xF;
	u32 rn   = reg[base].I & 0xFFFFFFFC;
	u32 n    = cpuBitsSet[opcode & 0xFF] + cpuBitsSet[(opcode >> 8) & 0xFF];
	bool P   = (opcode >> 24) & 1;
	bool U   = (opcode >> 23) & 1;
	u32 first, wb;
	if (U) {                 // increment
		first = P ? rn + 4 : rn;
		wb    = rn + 4 * n;
	} else {                 // decrement
		first = P ? rn - 4 * n : rn - 4 * n + 4;
		wb    = rn - 4 * n;
	}
	*outFirst = first & 0xFFFFFFFC;
	*outWb    = wb;
	return base;
}

static void arm_cached_stm(u32 opcode, int& clockTicks)
{
	u32 first, wb;
	int base = arm_blockBase(opcode, &first, &wb);
	bool W = (opcode >> 21) & 1;
	bool S = (opcode >> 22) & 1;
	u32 address = first;
	clockTicks += 2;

	// S-bit STM transfers the USER-bank registers. We snapshot which bank to
	// read each register from; on this core the user-bank copies live at the
	// R*_USR / fiq indices when not in user/system mode.
	bool userBank = S && (armMode != 0x10 && armMode != 0x1f);

	bool wroteBase = false;
	for (int i = 0; i <= 15; i++) {
		if (!(opcode & (1 << i)))
			continue;
		u32 v;
		if (i == 15) {
			v = reg[15].I + 4;              // matches arm-new.h (PC+12)
		} else if (i == base && W && !wroteBase) {
			// base is the lowest set reg with writeback: arm-new.h stores the
			// ORIGINAL base then writes back; canonical ARM also stores original.
			v = reg[base].I;
		} else if (userBank) {
			v = (i >= 8 && i <= 12 && armMode == 0x11)
			      ? reg[(R8_FIQ - 8) + i].I
			      : (i == 13 ? reg[R13_USR].I : (i == 14 ? reg[R14_USR].I : reg[i].I));
		} else {
			v = reg[i].I;
		}
		CPUWriteMemory(address, v);
		clockTicks += 1 + CPUUpdateTicksAccess32(address);
		address += 4;
		if (W && !wroteBase) {            // writeback at first transfer
			reg[base].I = wb;
			wroteBase = true;
		}
	}
}

static void arm_cached_ldm(u32 opcode, int& clockTicks, bool hasPC)
{
	u32 first, wb;
	int base = arm_blockBase(opcode, &first, &wb);
	bool W = (opcode >> 21) & 1;
	bool S = (opcode >> 22) & 1;
	u32 address = first;
	clockTicks += 2;

	// S without PC -> load into USER bank; S with PC -> restore SPSR after.
	bool userBank = S && !hasPC && (armMode != 0x10 && armMode != 0x1f);

	for (int i = 0; i <= 14; i++) {
		if (!(opcode & (1 << i)))
			continue;
		u32 v = CPUReadMemory(address);
		clockTicks += 1 + CPUUpdateTicksAccess32(address);
		address += 4;
		if (userBank) {
			if (i >= 8 && i <= 12 && armMode == 0x11) reg[(R8_FIQ - 8) + i].I = v;
			else if (i == 13) reg[R13_USR].I = v;
			else if (i == 14) reg[R14_USR].I = v;
			else reg[i].I = v;
		} else {
			reg[i].I = v;
		}
	}

	if (hasPC) {
		reg[15].I = CPUReadMemory(address);
		clockTicks += 2 + CPUUpdateTicksAccess32(address);
		address += 4;

		// Writeback MUST happen BEFORE the S-bit CPUSwitchMode: arm-new.h's
		// LDM{cond}<a>S {..,pc} writes back reg[base]=temp while still in the
		// CURRENT mode, then restores CPSR (which re-banks r13/r14). Doing the
		// switch first would land the base writeback in the NEW mode's banked
		// register, corrupting the returned-to code's stack pointer (observed:
		// sp clobbered to the literal-pool address after an LDMFD sp!,{pc}^).
		if (W && !(opcode & (1 << base)))
			reg[base].I = wb;

		if (S)
			CPUSwitchMode(reg[17].I & 0x1f, false);
		armNextPC = reg[15].I & 0xFFFFFFFC;
		reg[15].I = armNextPC + 4;
		return;            // base writeback already handled above
	}

	// Non-PC LDM: writeback after the transfers (no mode switch involved).
	if (W && !(opcode & (1 << base)))
		reg[base].I = wb;
}

// ── Operation macros (verbatim semantics from arm-new.h, C_CORE path) ────────
#define NEG(i) ((i) >> 31)
#define POS(i) ((~(i)) >> 31)
#define ADDCARRY(a, b, c) \
  C_FLAG = ((NEG(a) & NEG(b)) | (NEG(a) & POS(c)) | (NEG(b) & POS(c))) ? true : false;
#define ADDOVERFLOW(a, b, c) \
  V_FLAG = ((NEG(a) & NEG(b) & POS(c)) | (POS(a) & POS(b) & NEG(c))) ? true : false;
#define SUBCARRY(a, b, c) \
  C_FLAG = ((NEG(a) & POS(b)) | (NEG(a) & POS(c)) | (POS(b) & POS(c))) ? true : false;
#define SUBOVERFLOW(a, b, c) \
  V_FLAG = ((NEG(a) & POS(b) & POS(c)) | (POS(a) & NEG(b) & NEG(c))) ? true : false;

// dest = rd index, base = rn index, value = operand2, C_OUT = shifter carry.
#define DO_AND   reg[dest].I = reg[base].I & value;
#define DO_ANDS  reg[dest].I = reg[base].I & value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_EOR   reg[dest].I = reg[base].I ^ value;
#define DO_EORS  reg[dest].I = reg[base].I ^ value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_SUB   reg[dest].I = reg[base].I - value;
#define DO_SUBS  { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_RSB   reg[dest].I = value - reg[base].I;
#define DO_RSBS  { u32 lhs=reg[base].I, rhs=value, res=rhs-lhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(rhs,lhs,res); SUBOVERFLOW(rhs,lhs,res); }
#define DO_ADD   reg[dest].I = reg[base].I + value;
#define DO_ADDS  { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_ADC   reg[dest].I = reg[base].I + value + (u32)C_FLAG;
#define DO_ADCS  { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs+(u32)C_FLAG; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_SBC   reg[dest].I = reg[base].I - value - !((u32)C_FLAG);
#define DO_SBCS  { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs-!((u32)C_FLAG); reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_RSC   reg[dest].I = value - reg[base].I - !((u32)C_FLAG);
#define DO_RSCS  { u32 lhs=reg[base].I, rhs=value, res=rhs-lhs-!((u32)C_FLAG); reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(rhs,lhs,res); SUBOVERFLOW(rhs,lhs,res); }
#define DO_TST   { u32 res=reg[base].I & value; N_FLAG=(res&0x80000000)?true:false; Z_FLAG=(res)?false:true; C_FLAG=C_OUT; }
#define DO_TEQ   { u32 res=reg[base].I ^ value; N_FLAG=(res&0x80000000)?true:false; Z_FLAG=(res)?false:true; C_FLAG=C_OUT; }
#define DO_CMP   { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_CMN   { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_ORR   reg[dest].I = reg[base].I | value;
#define DO_ORRS  reg[dest].I = reg[base].I | value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_MOV   reg[dest].I = value;
#define DO_MOVS  reg[dest].I = value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_BIC   reg[dest].I = reg[base].I & (~value);
#define DO_BICS  reg[dest].I = reg[base].I & (~value); \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_MVN   reg[dest].I = ~value;
#define DO_MVNS  reg[dest].I = ~value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;

// Forward declaration so arm7.cpp's switchable entry can call us.
void arm_Run(u32 CycleCount);

// ── Shifter-operand value computation (operand2) ────────────────────────────
// Each data-processing label uses one FORM_* macro to produce `value` and (for
// the flag-setting / logical labels) `C_OUT`, exactly as arm-new.h does. The
// decoded operand fields are:
//   op->r2  = Rm (operand register)
//   op->imm = shift amount (imm-shift forms) | Rs index (reg-shift forms)
//             | pre-rotated 32-bit immediate (immediate operand2 form)
// C_OUT defaults to C_FLAG at the top of every data-proc label.

// Register, shift by immediate amount (amount already masked into op->imm).
#define FORM_LSL_IMM  { u32 v=reg[op->r2].I; u32 s=op->imm; \
    if(s){ C_OUT=(v>>(32-s))&1; value=v<<s; } else { value=v; } }
#define FORM_LSR_IMM  { u32 v=reg[op->r2].I; u32 s=op->imm; \
    if(s){ C_OUT=(v>>(s-1))&1; value=v>>s; } else { value=0; C_OUT=(v&0x80000000)?true:false; } }
#define FORM_ASR_IMM  { u32 v=reg[op->r2].I; u32 s=op->imm; \
    if(s){ C_OUT=((s32)v>>(int)(s-1))&1; value=(s32)v>>(int)s; } \
    else { if(v&0x80000000){ value=0xFFFFFFFF; C_OUT=true; } else { value=0; C_OUT=false; } } }
#define FORM_ROR_IMM  { u32 v=reg[op->r2].I; u32 s=op->imm; \
    if(s){ C_OUT=(v>>(s-1))&1; value=((v<<(32-s))|(v>>s)); } \
    else { /* RRX */ u32 cc=(u32)C_FLAG; C_OUT=(v&1)?true:false; value=((v>>1)|(cc<<31)); } }

// Register, shift by register amount (Rs = op->imm), full ARM7 width handling.
#define FORM_LSL_REG  { u32 v=reg[op->r2].I; u32 s=reg[op->imm].I & 0xFF; \
    if(s){ if(s==32){ value=0; C_OUT=(v&1)?true:false; } \
           else if(s<32){ C_OUT=(v>>(32-s))&1; value=v<<s; } \
           else { value=0; C_OUT=false; } } else { value=v; } }
#define FORM_LSR_REG  { u32 v=reg[op->r2].I; u32 s=reg[op->imm].I & 0xFF; \
    if(s){ if(s==32){ value=0; C_OUT=(v&0x80000000)?true:false; } \
           else if(s<32){ C_OUT=(v>>(s-1))&1; value=v>>s; } \
           else { value=0; C_OUT=false; } } else { value=v; } }
#define FORM_ASR_REG  { u32 v=reg[op->r2].I; u32 s=reg[op->imm].I & 0xFF; \
    if(s<32){ if(s){ C_OUT=((s32)v>>(int)(s-1))&1; value=(s32)v>>(int)s; } else { value=v; } } \
    else { if(v&0x80000000){ value=0xFFFFFFFF; C_OUT=true; } else { value=0; C_OUT=false; } } }
#define FORM_ROR_REG  { u32 v=reg[op->r2].I; u32 s=reg[op->imm].I & 0xFF; \
    if(s){ s&=0x1f; if(s){ C_OUT=(v>>(s-1))&1; value=((v<<(32-s))|(v>>s)); } \
           else { value=v; C_OUT=(v&0x80000000)?true:false; } } \
    else { value=v; C_OUT=(v&0x80000000)?true:false; } }

// Immediate operand2: op->imm already holds the rotated 32-bit constant.
// For rotate==0 the shifter carry is unaffected (stays C_FLAG); for rotate!=0
// it is bit 31 of the rotated value. We split these at decode time into two
// labels so neither path needs a runtime branch on the rotate amount.
#define FORM_IMM_ROT0 { value=op->imm; }                       // C_OUT stays C_FLAG
#define FORM_IMM_ROTN { value=op->imm; C_OUT=(value&0x80000000)?true:false; }

// (The data-processing and LDR/STR label bodies + their decode tables are
//  generated mechanically from these same FORM_*/DO_* macros by
//  gen_arm7_cached.py and pulled in via arm7_cached_gen.inc below.)

#if !defined(__GNUC__)
#  error "arm7_cached.cpp requires GCC computed-goto (&&label / goto *)."
#endif

// ════════════════════════════════════════════════════════════════════════════
//  The cached interpreter.
//
//  Pipeline / PC model (identical to arm_Run() in arm7.cpp):
//    Before executing the instruction at address A:  armNextPC == A,
//    reg[15] == A+4. The per-instruction prologue then does
//    `armNextPC = reg[15]; reg[15] += 4;`, so during execution reg[15] == A+8
//    (the ARM prefetch value) and armNextPC == A+4 (the next instruction).
//
//  The threaded loop folds that prologue into NEXT(): every label ends with
//  NEXT, which (1) honors the cycle budget, (2) looks up the uop for the
//  instruction at the current armNextPC, (3) runs the prologue, (4) dispatches.
//  Sequential ops leave armNextPC/reg[15] to the prologue; branch / PC-write
//  ops overwrite armNextPC and reg[15] themselves (exactly as arm-new.h) before
//  reaching NEXT, which then re-homes on the branch target.
// ════════════════════════════════════════════════════════════════════════════

void arm_Run_Cached(u32 CycleCount, bool reset)
{
	static int clockTicks;            // matches arm-new.h / LDM-STM helper signature
	static ARM7CACHEOP* pc_op;

	if (reset) {
		// Point every entrypoint at the decode trampoline and reset the pool.
		// ARM7_CACHE[0] is the reserved "decode this block" sentinel.
		ARM7_CACHE[0].dispatch = NULL;          // patched below to &&lbl_unknown_block
		ARM7_CACHE_LAST_OP = 1;
		// We cannot take &&label outside the function body before first run, so
		// the actual fill happens on the first non-reset call (s_inited path).
		s_cache_dirty = true;
		return;
	}

	if (!Arm7Enabled)
		return;

	// One-time / post-reset initialization that needs label addresses.
	if (s_cache_dirty) {
		ARM7_CACHE[0].dispatch = &&lbl_unknown_block;
		for (u32 i = 0; i < ARM7_EP_SIZE; i++)
			ARM7_ENTRYPOINTS[i] = &ARM7_CACHE[0];
		ARM7_CACHE_LAST_OP = 1;
		s_cache_dirty = false;
	}

	clockTicks = 0;

	// FIQ check at slice entry, mirroring arm_Run(). CPUFiq() reads reg[15] for
	// lr_fiq and sets armNextPC to the FIQ vector; the kickoff below picks the
	// new PC up via dispatch_pc = armNextPC.
	if (armFiqEnable && e68k_out) {
		CPUFiq();
	}

	// ── dispatch_pc execution model ────────────────────────────────────────────
	// A single running program counter, dispatch_pc, drives dispatch. The common
	// uops (no r15) never touch reg[15] / armNextPC: they do their work, advance
	// dispatch_pc by 4, and re-dispatch. Only the special PC-read uops materialize
	// reg[15] (= dispatch_pc + 8, the ARM prefetch value), and only the PC-write
	// uops change the control flow (they set dispatch_pc to the new target).
	//
	// reg[15] / armNextPC are reconciled with dispatch_pc only at slice exit, so
	// the next slice's FIQ/exception entry (which read reg[15]/armNextPC) and the
	// kickoff see exactly what the old per-prologue engine left behind.
	u32 dispatch_pc;

	// Re-home: charge cycles + budget check + dispatch the uop registered for the
	// current dispatch_pc via the entrypoint table. Used at block entry and after
	// any control-flow change (branch / PC write).
	#define DISPATCH() do {                              \
			if ((u32)clockTicks >= CycleCount) goto lbl_exit; \
			clockTicks += 6;                              \
			pc_op = ARM7_ENTRYPOINTS[dispatch_pc & ARM7_EP_MASK]; \
			goto *pc_op->dispatch;                        \
		} while(0)

	// Sequential advance (non-PC-writing ops): the next instruction's uop is the
	// next CONTIGUOUS cache slot (the decoder lays a block out sequentially), so
	// fall straight through with pc_op++ and SKIP the entrypoint-table lookup.
	// The last uop of a capped block is a synthetic rehome terminator (lbl_rehome)
	// that re-homes via DISPATCH(), so pc_op++ never walks past a block boundary.
	#define ADVANCE() do {                               \
			dispatch_pc += 4;                             \
			if ((u32)clockTicks >= CycleCount) goto lbl_exit; \
			clockTicks += 6;                              \
			pc_op++;                                      \
			goto *pc_op->dispatch;                        \
		} while(0)

	// Re-dispatch after a PC write: the op has already stored the new PC into
	// dispatch_pc. (Kept as a named macro for the PC-write / branch labels.)
	#define DISPATCH_PC() DISPATCH()

	// Materialize the ARM prefetch value for the few uops that read r15.
	#define SET_PC_READ() do { reg[15].I = dispatch_pc + 8; } while(0)

	// Per-uop condition guard. A failed condition skips the instruction's effects
	// (including any branch / PC write) and advances to the next instruction.
	//
	// The fail path RE-HOMES via the entrypoint table rather than pc_op++ : a
	// block-ending conditional op (conditional B/BL/LDM{pc}/MOV pc/SWI) is the
	// last uop of its block with NO contiguous successor, so pc_op++ would walk
	// into an unrelated pool slot. Re-homing on dispatch_pc+4 is always correct
	// regardless of the uop's position in the block. (Failed conditions are the
	// uncommon case, so the extra table lookup here is not on the hot path.)
	#define COND_GUARD() do {                            \
			if (!cond_check(pc_op->cond)) {              \
				dispatch_pc += 4;                         \
				DISPATCH();                               \
			}                                             \
		} while(0)

	// Kick off: start dispatching from the incoming PC (armNextPC), exactly the
	// instruction the previous slice / reset / FIQ left pending.
	dispatch_pc = armNextPC;
	DISPATCH();

	// ── Generated threaded label bodies (data-proc + LDR/STR variants) ──────
	#define GEN_LABELS
	#include "arm7_cached_gen.inc"
	#undef GEN_LABELS

	// ── Hand-written labels for the remaining ARM7DI instructions ───────────
	// All operate on pc_op; the raw opcode is carried in pc_op->imm where the
	// handler needs full field access (LDM/STM, MSR field masks, etc).

lbl_mul:
	// MUL/MLA field convention (set by decode):
	//   r0 = Rd (19:16)   r1 = Rs (11:8)   r2 = Rm (3:0)
	//   imm bit 8 = S flag;  imm[3:0] = Rn (accumulate reg, 15:12) for MLA.
	COND_GUARD();
	{
		ARM7CACHEOP* op = pc_op;
		int dest = op->r0;
		u32 rs = reg[op->r1].I;
		reg[dest].I = reg[op->r2].I * rs;
		if (op->imm & 0x100) {
			N_FLAG = (reg[dest].I & 0x80000000) ? true : false;
			Z_FLAG = (reg[dest].I) ? false : true;
		}
		if (((s32)rs) < 0) rs = ~rs;
		if ((rs & 0xFFFFFF00) == 0) clockTicks += 2;
		else if ((rs & 0xFFFF0000) == 0) clockTicks += 3;
		else if ((rs & 0xFF000000) == 0) clockTicks += 4;
		else clockTicks += 5;
	}
	ADVANCE();

lbl_mla:
	COND_GUARD();
	{
		ARM7CACHEOP* op = pc_op;
		int dest = op->r0;
		u32 rs = reg[op->r1].I;
		reg[dest].I = reg[op->r2].I * rs + reg[op->imm & 0xF].I;
		if (op->imm & 0x100) {
			N_FLAG = (reg[dest].I & 0x80000000) ? true : false;
			Z_FLAG = (reg[dest].I) ? false : true;
		}
		if (((s32)rs) < 0) rs = ~rs;
		if ((rs & 0xFFFFFF00) == 0) clockTicks += 3;
		else if ((rs & 0xFFFF0000) == 0) clockTicks += 4;
		else if ((rs & 0xFF000000) == 0) clockTicks += 5;
		else clockTicks += 6;
	}
	ADVANCE();

lbl_swp:
	COND_GUARD();
	{
		ARM7CACHEOP* op = pc_op;
		u32 address = reg[op->r1].I;          // Rn
		u32 temp = CPUReadMemory(address);
		CPUWriteMemory(address, reg[op->r2].I);   // Rm
		reg[op->r0].I = temp;                 // Rd
		clockTicks += 4;
	}
	ADVANCE();

lbl_swpb:
	COND_GUARD();
	{
		ARM7CACHEOP* op = pc_op;
		u32 address = reg[op->r1].I;
		u32 temp = CPUReadByte(address);
		CPUWriteByte(address, reg[op->r2].I & 0xFF);
		reg[op->r0].I = temp & 0xFF;
		clockTicks += 4;
	}
	ADVANCE();

lbl_mrs_cpsr:
	COND_GUARD();
	CPUUpdateCPSR();
	reg[pc_op->r0].I = reg[16].I;
	clockTicks += 1;
	ADVANCE();

lbl_mrs_spsr:
	COND_GUARD();
	reg[pc_op->r0].I = reg[17].I;
	clockTicks += 1;
	ADVANCE();

lbl_msr_cpsr:
	COND_GUARD();
	{
		// imm holds the raw opcode; works for both register and immediate forms.
		u32 opcode = pc_op->imm;
		CPUUpdateCPSR();
		u32 value;
		if (opcode & 0x02000000) {            // immediate form
			value = opcode & 0xFF;
			int shift = (opcode & 0xF00) >> 7;
			if (shift) value = ((value << (32 - shift)) | (value >> shift));
		} else {
			value = reg[opcode & 15].I;
		}
		u32 newValue = reg[16].I;
		if (armMode > 0x10) {
			if (opcode & 0x00010000) newValue = (newValue & 0xFFFFFF00) | (value & 0x000000FF);
			if (opcode & 0x00020000) newValue = (newValue & 0xFFFF00FF) | (value & 0x0000FF00);
			if (opcode & 0x00040000) newValue = (newValue & 0xFF00FFFF) | (value & 0x00FF0000);
		}
		if (opcode & 0x00080000) newValue = (newValue & 0x00FFFFFF) | (value & 0xFF000000);
		newValue |= 0x10;
		CPUSwitchMode(newValue & 0x1f, false);
		reg[16].I = newValue;
		CPUUpdateFlags();
		clockTicks += 1;
	}
	ADVANCE();

lbl_msr_spsr:
	COND_GUARD();
	{
		u32 opcode = pc_op->imm;
		if (armMode > 0x10 && armMode < 0x1f) {
			u32 value;
			if (opcode & 0x02000000) {
				value = opcode & 0xFF;
				int shift = (opcode & 0xF00) >> 7;
				if (shift) value = ((value << (32 - shift)) | (value >> shift));
			} else {
				value = reg[opcode & 15].I;
			}
			if (opcode & 0x00010000) reg[17].I = (reg[17].I & 0xFFFFFF00) | (value & 0x000000FF);
			if (opcode & 0x00020000) reg[17].I = (reg[17].I & 0xFFFF00FF) | (value & 0x0000FF00);
			if (opcode & 0x00040000) reg[17].I = (reg[17].I & 0xFF00FFFF) | (value & 0x00FF0000);
			if (opcode & 0x00080000) reg[17].I = (reg[17].I & 0x00FFFFFF) | (value & 0xFF000000);
		}
		clockTicks += 1;
	}
	ADVANCE();

	// Block data transfer. Per project decision: a single handler split only by
	// "register list includes PC" (lbl_ldm_pc) vs not (lbl_ldm_nopc), with the
	// register-list / P,U,S,W decode done at runtime from the raw opcode.
lbl_stm:
	COND_GUARD();
	// STM may store r15 (as PC+12 = reg[15]+4) and reads reg[base]; materialize
	// the prefetch PC so any r15 reference matches arm-new.h.
	SET_PC_READ();
	arm_cached_stm(pc_op->imm, clockTicks);
	ADVANCE();

lbl_ldm_nopc:
	COND_GUARD();
	SET_PC_READ();
	arm_cached_ldm(pc_op->imm, clockTicks, /*hasPC=*/false);
	ADVANCE();

lbl_ldm_pc:
	COND_GUARD();
	SET_PC_READ();
	arm_cached_ldm(pc_op->imm, clockTicks, /*hasPC=*/true);
	// arm_cached_ldm stored the loaded PC into armNextPC; follow it.
	dispatch_pc = armNextPC;
	DISPATCH_PC();

lbl_b:
	COND_GUARD();
	{
		// During execution reg[15] reads as A+8 = dispatch_pc+8; target =
		// (A+8) + offset. Update dispatch_pc directly; no reg[15] maintenance.
		s32 offset = pc_op->imm;     // pre-sign-extended, pre-shifted offset
		dispatch_pc = dispatch_pc + 8 + (u32)offset;
		clockTicks += 3;
	}
	DISPATCH_PC();

lbl_bl:
	COND_GUARD();
	{
		// lr = address of next instruction = A+4 = dispatch_pc+4.
		s32 offset = pc_op->imm;
		reg[14].I = dispatch_pc + 4;
		dispatch_pc = dispatch_pc + 8 + (u32)offset;
		clockTicks += 3;
	}
	DISPATCH_PC();

lbl_swi:
	COND_GUARD();
	clockTicks += 3;
	// CPUSoftwareInterrupt reads reg[15] (-> lr_svc = PC-4) and sets armNextPC=0x08.
	SET_PC_READ();
	CPUSoftwareInterrupt(pc_op->imm & 0x00FFFFFF);
	dispatch_pc = armNextPC;
	DISPATCH_PC();

lbl_undefined:
	COND_GUARD();
#ifdef DEV_VERSION
	printf("Undefined ARM instruction (cached) %08x at %08x\n", pc_op->imm, dispatch_pc);
#endif
	// CPUUndefinedException sets armNextPC to the vector; pick it up via dispatch_pc.
	reg[15].I = dispatch_pc + 8;
	CPUUndefinedException();
	dispatch_pc = armNextPC;
	DISPATCH_PC();

	// Block-cap terminator: the synthetic uop appended when a block hit the
	// MAX_BLOCK_UOPS cap. Sequential ADVANCE() (pc_op++) lands here after the
	// last real uop; dispatch_pc already points at the next instruction, so we
	// re-home through the entrypoint table to its (separately decoded) block.
lbl_rehome:
	DISPATCH();

	// ── Block decode: fill cache slots for the block starting at dispatch_pc ──
	// Reached when an entrypoint still points at the sentinel ARM7_CACHE[0].
lbl_unknown_block:
	{
		// The decode trampoline is not a real instruction: refund the base 6
		// ticks DISPATCH() charged for it, so the re-dispatch charges exactly one
		// +6 for the real first instruction (and a CycleCount=1 slice still runs
		// one instruction rather than being exhausted by the trampoline).
		clockTicks -= 6;
		// dispatch_pc is the address we must decode (DISPATCH() does not advance it).
		u32 blockStart = dispatch_pc;
		u32 decode_pc = blockStart;

		// (Each uop registers its own entrypoint inside the loop below, so no
		// separate blockStart assignment is needed here.)

		// Cap block length. A long straight-line run with no branch would
		// otherwise grow one huge block; bound it so the next chunk gets its own
		// block. At the cap we append a rehome terminator uop (see below) that the
		// last real uop's pc_op++ ADVANCE() lands on, which re-homes via the table.
		const u32 MAX_BLOCK_UOPS = 128;
		u32 blockUops = 0;

		bool last = false;
		do {
			if (ARM7_CACHE_LAST_OP >= ARM7_CACHE_SIZE - 2) {
				// Pool exhausted: flush and restart decoding this block.
				for (u32 i = 0; i < ARM7_EP_SIZE; i++)
					ARM7_ENTRYPOINTS[i] = &ARM7_CACHE[0];
				ARM7_CACHE_LAST_OP = 1;
				decode_pc = blockStart;
				blockUops = 0;          // restart count for the re-decoded block
				// (the loop body re-registers entrypoints as it re-decodes)
			}

			u32 opcode = CPUReadMemoryQuick(decode_pc);
			ARM7CACHEOP* cop = &ARM7_CACHE[ARM7_CACHE_LAST_OP];
			// Register an entrypoint for EVERY decoded instruction, not just the
			// block start. Otherwise sequential flow (ADVANCE -> NEXT ->
			// ENTRYPOINTS[next]) finds the sentinel at every non-start address and
			// re-decodes a fresh, overlapping block per instruction, exhausting
			// the pool and eventually dispatching a stale/garbage uop.
			ARM7_ENTRYPOINTS[decode_pc & ARM7_EP_MASK] = cop;
			cop->cond = opcode >> 28;
			cop->imm = opcode;          // default: keep raw opcode
			last = false;

			// ── Inline decode: opcode -> dispatch label + operand fields ────
			// Decode MUST live here so &&label addresses are in scope. The 12-bit
			// primary classification matches arm-new.h.
			u32 fam = (opcode >> 26) & 3;   // bits[27:26]
			if (fam == 0 || fam == 1) {
				// 00: data-proc / multiply / swp / psr-transfer
				// 01: single data transfer (LDR/STR)
				if (fam == 1) {
					// LDR/STR. The generated decode selects the per-variant label.
					#define GEN_DECODE_MEM
					#include "arm7_cached_gen.inc"
					#undef GEN_DECODE_MEM
					// LDR to PC ends the block (it re-dispatches via dp_pc_done).
					if (((opcode >> 20) & 1) && (((opcode >> 12) & 0xF) == 15))
						last = true;
				} else {
					// fam == 0: data-proc register/immediate domain.
					bool isMulSpace = ((opcode & 0x0FC000F0) == 0x00000090); // 000000xx ...1001
					bool isSwp      = ((opcode & 0x0FB00FF0) == 0x01000090); // 00010B00 ...00001001
					// PSR transfer (MRS/MSR) occupies the TST/TEQ/CMP/CMN encodings
					// with the S bit clear: opcode[24:23]==0b10 && opcode[20]==0.
					bool isMrsMsr   = (((opcode >> 23) & 3) == 2) && (((opcode >> 20) & 1) == 0)
					                  && !isMulSpace && !isSwp;
					if (isMulSpace) {
						// MUL (A=0) / MLA (A=1). r0=Rd(19:16) r1=Rs(11:8) r2=Rm(3:0)
						cop->r0 = (opcode >> 16) & 0xF;
						cop->r1 = (opcode >> 8) & 0xF;
						cop->r2 = opcode & 0xF;
						u32 s = (opcode >> 20) & 1;
						u32 rn = (opcode >> 12) & 0xF;  // accumulate reg (MLA)
						cop->imm = (s << 8) | rn;
						cop->dispatch = (opcode & 0x00200000) ? &&lbl_mla : &&lbl_mul;
					} else if (isSwp) {
						cop->r0 = (opcode >> 12) & 0xF;   // Rd
						cop->r1 = (opcode >> 16) & 0xF;   // Rn
						cop->r2 = opcode & 0xF;           // Rm
						cop->dispatch = (opcode & 0x00400000) ? &&lbl_swpb : &&lbl_swp;
					} else if (isMrsMsr) {
						// Distinguish MRS vs MSR and CPSR vs SPSR.
						bool toPSR = (opcode >> 21) & 1;  // 0 = MRS, 1 = MSR
						bool spsr  = (opcode >> 22) & 1;
						if (!toPSR) {
							cop->r0 = (opcode >> 12) & 0xF;
							cop->dispatch = spsr ? &&lbl_mrs_spsr : &&lbl_mrs_cpsr;
						} else {
							cop->dispatch = spsr ? &&lbl_msr_spsr : &&lbl_msr_cpsr;
						}
					} else {
						// Data processing. Generated decode picks the (op,form) label.
						#define GEN_DECODE_DP
						#include "arm7_cached_gen.inc"
						#undef GEN_DECODE_DP
						// A data-proc write to PC (Rd==15) ends the block, except
						// the compare-only ops (TST/TEQ/CMP/CMN, op4 0x8..0xB) which
						// never write Rd.
						{
							u32 op4 = (opcode >> 21) & 0xF;
							if (((opcode >> 12) & 0xF) == 15 && !(op4 >= 0x8 && op4 <= 0xB))
								last = true;
						}
					}
				}
			} else if (fam == 2) {
				// bits[27:26]==10: block data transfer (bit25==0) or branch (bit25==1)
				if (!((opcode >> 25) & 1)) {
					// 100: block data transfer (LDM/STM)
					bool load = (opcode >> 20) & 1;
					bool hasPC = (opcode & 0x8000) != 0;
					if (!load)         cop->dispatch = &&lbl_stm;
					else if (hasPC)  { cop->dispatch = &&lbl_ldm_pc;   last = true; }
					else               cop->dispatch = &&lbl_ldm_nopc;
				} else {
					// 101: branch. Pre-extract the sign-extended, shifted offset.
					s32 offset = opcode & 0x00FFFFFF;
					if (offset & 0x00800000) offset |= 0xFF000000;
					offset <<= 2;
					cop->imm = (u32)offset;
					cop->dispatch = (opcode & 0x01000000) ? &&lbl_bl : &&lbl_b;
					last = true;        // branch ends the block
				}
			} else {
				// bits[27:26]==11: coprocessor (bit25 group) or SWI (bits[27:24]==1111).
				if (((opcode >> 24) & 0xF) == 0xF) {
					cop->dispatch = &&lbl_swi;
					last = true;        // SWI ends the block (mode/PC change)
				} else {
					// coprocessor: not part of the AICA ARM7DI workload.
					cop->dispatch = &&lbl_undefined;
					last = true;
				}
			}

			ARM7_CACHE_LAST_OP++;
			decode_pc += 4;

			// `last` set above means a control-flow instruction (branch / PC
			// write / SWI / undefined) ended the block: it re-homes via its own
			// dispatch, so no terminator is needed and pc_op++ never runs past it.
			// The cap instead ends a block on an ORDINARY sequential instruction,
			// whose ADVANCE() does pc_op++ -- so append a rehome terminator uop in
			// the next contiguous slot for that pc_op++ to land on.
			if (!last && ++blockUops >= MAX_BLOCK_UOPS) {
				ARM7_CACHE[ARM7_CACHE_LAST_OP].dispatch = &&lbl_rehome;
				ARM7_CACHE_LAST_OP++;
				last = true;
			}
		} while (!last);

		// Re-dispatch: run the freshly decoded block from its first uop.
		// dispatch_pc is still blockStart, whose entrypoint now points at the
		// first decoded uop.
	}
	DISPATCH();

lbl_exit:
	// Reconcile the architectural PC with dispatch_pc so the next slice's FIQ /
	// exception entry (which read reg[15] / armNextPC) and the kickoff resume
	// exactly where this slice left off -- matching the old per-prologue engine
	// (armNextPC = next instr, reg[15] = next instr + 4).
	armNextPC = dispatch_pc;
	reg[15].I = dispatch_pc + 4;
	return;

	#undef DISPATCH
	#undef ADVANCE
	#undef DISPATCH_PC
	#undef SET_PC_READ
	#undef COND_GUARD
}
