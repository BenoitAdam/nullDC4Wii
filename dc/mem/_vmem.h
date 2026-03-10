#pragma once
#include "types.h"

// ---------------------------------------------------------------------------
// _vmem – virtual memory subsystem for Dreamcast-on-Wii emulation
// ---------------------------------------------------------------------------
// Design: a 256-entry top-level table (_vmem_MemInfo_ptr) is indexed by the
// upper 8 bits of a 32-bit SH-4 address.  Each entry either:
//   • points directly into a host memory block (fast RAM/VRAM path), or
//   • encodes a handler-table index for MMIO regions.
//
// Improvements over v2:
//   • HANDLER_MAX raised from 31 → 63 (more MMIO regions supported)
//   • Pointer/handler discrimination uses a dedicated tag bit (bit 0 of a
//     separate _vmem_MemInfo_is_handler[256] flags array) — no more fragile
//     "is the masked pointer zero?" heuristic.
//   • _vmem_map_block accepts a read-only flag; writes to ROM regions are
//     logged and silently dropped instead of corrupting state.
//   • _vmem_mirror_mapping checks the full destination range for overlap.
//   • _vmem_reserve() validates Arena2 free space before committing (Wii).
//   • All public functions are documented.
// ---------------------------------------------------------------------------

// ---- Function-pointer typedefs --------------------------------------------
typedef u8   fastcall _vmem_ReadMem8FP  (u32 Address);
typedef u16  fastcall _vmem_ReadMem16FP (u32 Address);
typedef u32  fastcall _vmem_ReadMem32FP (u32 Address);

typedef void fastcall _vmem_WriteMem8FP (u32 Address, u8  data);
typedef void fastcall _vmem_WriteMem16FP(u32 Address, u16 data);
typedef void fastcall _vmem_WriteMem32FP(u32 Address, u32 data);

// Opaque handle returned by _vmem_register_handler().
// Valid range: [0, HANDLER_MAX].
typedef u32 _vmem_handler;

// Returned by _vmem_register_handler() on failure (table full).
#define VMEM_INVALID_HANDLER ((u32)~0u)

// ---- Lifecycle ------------------------------------------------------------

// Initialise all mapping tables and register the default "not-mapped" handler
// in slot 0.  Must be called before any other _vmem function.
void _vmem_init  ();

// Reset all mapping tables to the power-on state (same as _vmem_init but
// callable at any time, e.g. on soft-reset).
void _vmem_reset ();

// Tear down the subsystem.  Currently a no-op for software-only tables.
void _vmem_term  ();

// Reserve / release the host memory backing store.
// _vmem_reserve() must succeed before any mapping calls.
// Returns false and prints a diagnostic if Arena2 has insufficient space (Wii).
bool _vmem_reserve ();
void _vmem_release ();

// ---- Handler registration -------------------------------------------------
// Register a set of MMIO read/write callbacks and return the handler index.
// Pass NULL for any pointer to use the safe "not-mapped" default which logs
// accesses (rate-limited) and returns 0 / is a no-op.
// Returns VMEM_INVALID_HANDLER if the handler table is full.
_vmem_handler _vmem_register_handler(
    _vmem_ReadMem8FP*   read8,
    _vmem_ReadMem16FP*  read16,
    _vmem_ReadMem32FP*  read32,
    _vmem_WriteMem8FP*  write8,
    _vmem_WriteMem16FP* write16,
    _vmem_WriteMem32FP* write32);

// Convenience macros for registering templated read/write pairs.
#define _vmem_register_handler_Template(read, write) \
    _vmem_register_handler( \
        read<1,u8>,  read<2,u16>,  read<4,u32>, \
        write<1,u8>, write<2,u16>, write<4,u32>)

#define _vmem_register_handler_Template1(read, write, etp) \
    _vmem_register_handler( \
        read<1,u8,etp>,  read<2,u16,etp>,  read<4,u32,etp>, \
        write<1,u8,etp>, write<2,u16,etp>, write<4,u32,etp>)

#define _vmem_register_handler_Template2(read, write, etp1, etp2) \
    _vmem_register_handler( \
        read<1,u8,etp1,etp2>,  read<2,u16,etp1,etp2>,  read<4,u32,etp1,etp2>, \
        write<1,u8,etp1,etp2>, write<2,u16,etp1,etp2>, write<4,u32,etp1,etp2>)

// ---- Mapping --------------------------------------------------------------

// Map a registered MMIO handler over SH-4 pages [start..end] (inclusive).
// 'start' and 'end' are the upper 8 bits of the SH-4 address (i.e. 16 MB
// page indices, 0x00–0xFF).
void _vmem_map_handler(_vmem_handler Handler, u32 start, u32 end);

// Map a host memory block over SH-4 pages [start..end] (inclusive).
//   base   – host pointer; must be 256-byte aligned and non-NULL.
//   mask   – address wrap mask (e.g. 0x001FFFFF for a 2 MB VRAM block).
//   rdonly – if true, writes to this region are silently dropped and logged.
void _vmem_map_block(void* base, u32 start, u32 end, u32 mask, bool rdonly = false);

// Copy mappings from [start .. start+size-1] to [new_region .. new_region+size-1].
// Asserts that the source and destination ranges do not overlap.
void _vmem_mirror_mapping(u32 new_region, u32 start, u32 size);

// Helper: map a block that repeats (mirrors) every blck_size bytes.
#define _vmem_map_block_mirror(base, start, end, blck_size) \
    do { \
        u32 _bs = (blck_size) >> 24; \
        for (u32 _p = (start); _p <= (u32)(end); _p += _bs) \
            _vmem_map_block((base), _p, _p + _bs - 1, (blck_size) - 1); \
    } while(0)

// ---- Public read/write accessors ------------------------------------------
u8   fastcall _vmem_ReadMem8  (u32 Address);
u16  fastcall _vmem_ReadMem16 (u32 Address);
u32  fastcall _vmem_ReadMem32 (u32 Address);
u64  fastcall _vmem_ReadMem64 (u32 Address);

void fastcall _vmem_WriteMem8  (u32 Address, u8  data);
void fastcall _vmem_WriteMem16 (u32 Address, u16 data);
void fastcall _vmem_WriteMem32 (u32 Address, u32 data);
void fastcall _vmem_WriteMem64 (u32 Address, u64 data);

// ---- Dynarec helpers ------------------------------------------------------

// Returns the base vmap table and the appropriate function-pointer table for
// the given access size and direction.
void _vmem_get_ptrs(u32 sz, bool write, void*** vmap, void*** func);

// Returns a host pointer for a compile-time-constant SH-4 address.
// Sets ismem=true  → returns a pointer directly into RAM/VRAM.
// Sets ismem=false → returns the handler function pointer.
void* _vmem_read_const(u32 addr, bool& ismem, u32 sz);

// ---- Diagnostic -----------------------------------------------------------

// Returns true if 'addr' falls inside a directly-mapped memory region (not a
// handler).
bool _vmem_is_mapped(u32 addr);

// Returns true if the page containing 'addr' is marked read-only.
bool _vmem_is_readonly(u32 addr);

// Print a human-readable summary of the full page map to stdout.
void _vmem_dump_map();
