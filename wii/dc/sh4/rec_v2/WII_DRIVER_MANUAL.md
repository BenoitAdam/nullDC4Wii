# Wii Driver Manual - Dreamcast Emulator Dynamic Recompiler

## Overview

This file implements a PowerPC-based dynamic recompiler (dynarec) for a Dreamcast emulator running on Nintendo Wii. It translates SH4 (Dreamcast CPU) instructions into PowerPC instructions that can execute natively on the Wii's Broadway processor.

## Architecture

### Target Platform: Nintendo Wii (PowerPC)

The Wii uses a PowerPC-based CPU (Broadway) with the following characteristics:

**General Purpose Registers (GPRs):**
- 32 registers (r0-r31) of 32 bits each
- **r0**: Volatile temporary register
- **r1**: Stack pointer (grows downward)
- **r2**: TOC (Table of Contents) pointer
- **r3-r10**: Volatile, used for function parameters; r3 also holds return values
- **r11**: Volatile, environment pointer for indirect calls
- **r12**: Volatile, used for branching and linking
- **r13-r31**: Preserved across function calls (19 callee-saved registers)

**Floating Point Registers (FPRs):**
- 32 registers (f0-f31) of 64 bits each, supporting single, double, or vector operations
- **f0**: Volatile scratch register
- **f1-f4**: Volatile, parameters and return values
- **f5-f13**: Volatile parameters
- **f14-f31**: Preserved across function calls (18 callee-saved registers)

**Special Registers:**
- **LR (Link Register)**: Stores return addresses
- **CR (Condition Register)**: Contains condition flags (CR0-CR7)
- **XER**: Exception register
- **FPSCR**: Floating-point status and control register

**Calling Convention:**
- Return addresses stored in Link Register (LR)
- LR saved at SP+4 on function entry
- Stack grows toward zero

## Key Components

### 1. Register Allocation

The driver uses specific PowerPC registers for emulation state:

```cpp
ppc_ireg ppc_cycles = ppc_r29;    // Cycle counter
ppc_ireg ppc_contex = ppc_r30;    // SH4 context pointer
ppc_ireg ppc_djump = ppc_r31;     // Dynamic jump target
ppc_ireg ppc_next_pc = ppc_rarg0; // Next PC to execute
```

### 2. Memory Operations

#### SH4 Register Access
- **`ppc_sh_load()`**: Load 32-bit value from SH4 context
- **`ppc_sh_load_f32()`**: Load 32-bit float from SH4 context
- **`ppc_sh_load_u16()`**: Load 16-bit unsigned value from SH4 context
- **`ppc_sh_store()`**: Store 32-bit value to SH4 context
- **`ppc_sh_store_f32()`**: Store 32-bit float to SH4 context
- **`ppc_sh_addr()`**: Get address of SH4 register

#### Memory Read Operations
Supports reads of 1, 2, 4, and 8 bytes with sign/zero extension:
- **1 byte**: `ReadMem8` with sign extension (`extsbx`)
- **2 bytes**: `ReadMem16` with sign extension (`extshx`)
- **4 bytes**: `ReadMem32`
- **8 bytes**: `ReadMem64` (stores to consecutive registers)

#### Memory Write Operations
Supports writes of 1, 2, 4, and 8 bytes:
- **1 byte**: `WriteMem8` with AND masking to 0xFF
- **2 bytes**: `WriteMem16` with AND masking to 0xFFFF
- **4 bytes**: `WriteMem32`
- **8 bytes**: `WriteMem64` (reads from consecutive registers)

### 3. Code Emission

#### Immediate Loading
**`ppc_li(D, imm)`**: Load immediate value into register
- If value fits in 16 bits: uses `addi` (add immediate)
- Otherwise: uses `addis` (add immediate shifted) + `ori` (OR immediate)

#### Branching
- **`ppc_bx(dst, LK)`**: Branch to destination with optional link
- **`ppc_call(funct)`**: Call function (branch with link)
- **`ppc_jump(funct)`**: Jump to function (branch without link)
- **`ppc_call_and_jump(funct)`**: Call function, then jump to its return value

### 4. Block Compilation

#### Block Entry (`ngen_Begin`)
1. Decrements cycle counter by block's cycle count
2. Checks if cycles are exhausted (branch on negative)
3. If exhausted, saves PC and jumps to update routine
4. Otherwise, continues with block execution

#### Block Exit (`ngen_End`)

Handles different block ending types:

**Conditional Branches (`BET_Cond_0`, `BET_Cond_1`):**
- Tests condition (SR.T flag)
- Statically links both taken and not-taken branches

**Dynamic Call (`BET_DynamicCall`):**
- Saves return address to PR (Procedure Register)
- Jumps to dynamically computed target

**Dynamic Jump (`BET_DynamicJump`):**
- Jumps to dynamically computed target

**Dynamic Return (`BET_DynamicRet`):**
- Returns using address in PR register

**Static Call/Jump (`BET_StaticCall`, `BET_StaticJump`):**
- May save return address (for calls)
- Directly links to statically known target

**Static Intr (`BET_StaticIntr`):**
- Checks interrupt flag
- Conditionally branches to interrupt handler

#### Static Link Patch Sites (`DoStatic`)

`DoStatic()` must emit a fixed 12-byte `addis+ori+bl` sequence.
`ngen_LinkBlock_Static` computes its patch site as LR-12 and overwrites it
with a direct `b compiled_target`, so a variable-length `ppc_li` (which
collapses to one instruction when the target's low half is zero, e.g. a block
at `0x8C010000`) would make the patcher clobber the instruction *before* the
sequence — in a `BET_Cond` block end, that is the other exit path's branch.

### 5. Operation Handling

#### Binary Operations (Integer)
All follow the pattern: `binop_start()` → operation → `binop_end()`

Supported operations:
- **Arithmetic**: `add`, `sub`, `mul_i32`
- **Logical**: `and`, `or`, `xor`
- **Shifts**: `shl` (logical left), `shr` (logical right), `sar` (arithmetic right)

#### Binary Operations (Floating Point)
Follow the pattern: `binop_start_fpu()` → operation → `binop_end_fpu()`

Supported operations:
- **Arithmetic**: `fadd`, `fsub`, `fmul`, `fdiv`

#### Move Operations
- **`shop_mov32`**: Move 32-bit value (register or immediate)
- **`shop_mov64`**: Move 64-bit value (uses consecutive register pairs)

#### Control Flow
- **`shop_ifb`**: Conditional block (flushes and reloads registers)
- **`shop_jdyn`**: Dynamic jump (loads target with optional offset)
- **`shop_jcond`**: Conditional jump setup

### 6. Function Calling Convention

The driver implements a custom calling convention adapter (`ngen_CC_*` functions):

1. **`ngen_CC_Start()`**: Initializes parameter list
2. **`ngen_CC_Param()`**: Adds parameters:
   - Return values stored to context immediately
   - Regular parameters queued for later processing
3. **`ngen_CC_Call()`**: Emits actual call:
   - Allocates float and integer argument registers
   - Loads parameters from SH4 context
   - Processes parameters in reverse order
   - Emits the function call

Parameter types:
- **CPT_u32**: 32-bit unsigned integer
- **CPT_f32**: 32-bit float
- **CPT_ptr**: Pointer (computes address in context)
- **CPT_u32rv**: 32-bit return value
- **CPT_f32rv**: Float return value
- **CPT_u64rvL/H**: 64-bit return value (low/high parts)

### 7. Main Loop (`ngen_mainloop`)

The main execution loop:

1. **Stack Frame Setup:**
   - Allocates stack space (8 + 20*4 bytes)
   - Saves Link Register at SP+stack_size+4
   - Saves GPRs r13-r31

2. **Initialization:**
   - Loads SH4 context base pointer
   - Sets initial cycle count to `SH4_TIMESLICE`
   - Loads initial PC from context

3. **Execution Loop (`loop_no_update`):**
   - Calls `bm_GetCode()` to get compiled block
   - Jumps to the returned code

4. **Update Path (`loop_do_update_write`):**
   - Saves PC to context
   - Restores cycle counter
   - Calls `UpdateSystem()` for timing/interrupts
   - Continues execution

5. **Exit:**
   - Restores Link Register
   - Restores GPRs r14-r31 (note: offset by 1 from save)
   - Destroys stack frame
   - Returns via `blr`

### 8. Block Linking

**Static Linking (`ngen_LinkBlock_Static`):**
- Called when a statically-known branch target needs compilation
- Compiles target block if needed
- Patches calling site with direct jump
- Makes patched code executable

**Stub Functions:**
- `ngen_LinkBlock_Static_stub`: Entry point for static linking
- `ngen_LinkBlock_Dynamic_1st_stub`: Reserved for dynamic linking (unused)
- `ngen_LinkBlock_Dynamic_2nd_stub`: Reserved for dynamic linking (unused)
- `ngen_BlockCheckFail_stub`: Called when block validation fails

### 9. Cache Coherency

**`make_address_range_executable()`:**
- Flushes data cache (`DCFlushRange`)
- Invalidates instruction cache (`ICInvalidateRange`)
- Required on PowerPC after code generation to ensure CPU sees new instructions

## Memory Layout

The driver uses the Wii's memory model:
- **Code Buffer**: Dynamic code generation area
- **SH4 Context**: Structure containing emulated CPU state
- **Stack**: Standard PowerPC stack for function calls

## Debugging Features

The driver includes debug output capability:
- Saves generated code to file: `dynarec_[address].bin`
- Useful for disassembly and analysis of generated code

## Performance Considerations

1. **Register Allocation**: Uses callee-saved registers (r29-r31) for frequently accessed state
2. **Cycle Counting**: Tracks instruction cycles for accurate timing
3. **Direct Linking**: Statically links blocks when possible to avoid lookup overhead
4. **Inline Operations**: Binary operations inlined directly rather than using function calls
5. **Batch Updates**: Only updates system state when cycles exhausted

## Limitations and Notes

- Dynamic block linking stubs are defined but not implemented
- Some SH4 operations fall back to canonical (interpreted) handlers
- The comment mentions "based on the mips one!" suggesting this was ported from a MIPS backend

## Features

**`ngen_GetFeatures()`** reports:
- **InterpreterFallback**: `false` - No automatic interpreter fallback
- **OnlyDynamicEnds**: `false` - Supports static block endings

## Code Quality Notes

Several typos exist in comments:
- "voalitle" should be "volatile"
- "sed" should be "used"
- "ming" unclear, possibly "mingw"

The comment asks "what gives?" regarding `make_address_range_executable`, suggesting some uncertainty about cache management requirements on PowerPC.

## FASTMEM (PPC-MMU) — 2026-07 rewrite

Gated by the `fastmem` preset (default OFF, options page 3, cfg key
`fastmem=on/off`). Two halves:

* **wii/wii_fastmem.cpp** — hardware side. Claims EA 0x00000000-0x1FFFFFFF
  (SR0/SR1 + a 256 KB hashed page table, placed in MEM1 when the heap can
  spare it — the hardware walker's random PTEG reads are latency-bound,
  1T-SRAM territory; MEM2 fallback otherwise) as a direct image of the
  DC's 29-bit space: RAM ×4 mirrors, VRAM (64-bit path, both pages). The
  mappings fill each PTEG with exactly 6 of 8 slots (deterministic, no
  overflow). EA
  0x00000000-0x03FFFFFF (area-0 image, incl. AICA wave RAM) must stay
  UNMAPPED forever: SQ addresses 0xE0-0xE3 mask onto it, and mapping any
  of it (v1 mapped wave RAM) makes SQ stores with matching offsets silently
  write host memory instead of the SQ buffer — that broke SQ memcpy to
  upper RAM, the TA YUV FIFO and AICA SQ uploads across half the library.
  Installs a DSI handler using the tuxedo-libogc convention
  (`PPCExcptSetHandler`, `__ppc_excpt_buf` protocol) and runs a
  fault-tolerant boot self-test (a failed assumption leaves fastmem
  inactive instead of panicking). Testing on Dolphin requires the
  **Enable MMU** accuracy option.

* **wii_driver.cpp `rec_fastmem_*`** — JIT side. When active,
  `shop_readm`/`shop_writem` emit fixed branchless shapes
  (`rlwinm rEA,rADDR,0,3,31` + `[xori swizzle]` + one load/store — no
  compares, no `_vmem` table walk; this is the skmp "no conditionals"
  design). Unmapped accesses (MMIO, masked SQ, BIOS, VRAM 32-bit path,
  OC-RAM) DSI-fault ONCE: the handler decodes the site (the EA scratch
  register — r4 vs r5 — is the shape discriminator, and the preceding
  `rlwinm` gives the original-address register), builds a trampoline in a
  128 KB pool and patches the site with `b trampoline`, then RETRIES the
  PC. Slow paths therefore always run in normal context (interrupts on) —
  never inside the exception. Write sites whose first fault lands in the
  area-0 image get an inlined `SQ? → direct sq_both store` fast path,
  keeping store-queue geometry bursts cheaper than the legacy table walk.
  Trampolines dispatch on the ORIGINAL address register, which keeps
  P4/SQ (0xE0 masks to 0x00) and OC-RAM (0x7C is NOT a 0x1C mirror)
  correct by construction.

  Iron rules learned on hardware: no printf/stdio and no FPU anywhere in
  the DSI path; the emission shapes and the fault-time decoder MUST stay
  in sync; the pool resets with the block cache (`ngen_ResetBlocks`).

  Known perf risk (measured in the 2026-07-19 precursor experiment): the
  Broadway DTLB is 128×4 KB and misses pay a hardware HTAB walk,
  while the legacy BAT paths pay zero translation cost. The rewrite
  removes 3-8 instructions + a branch per access (the precursor removed
  ~1), and the HTAB now sits in MEM1 to cut the walk latency itself. If
  the A/B still shows a tax, the next lever is Broadway's spare DBAT4-7
  (HID4.SBE): map the primary RAM window with one BAT (zero-cost
  translation) — needs mem_b at a 16 MB-aligned physical address, with
  mirrors/VRAM staying on the page table.
