#pragma once
#include "types.h"

// ---------------------------------------------------------------------------
// _vmem – virtual memory subsystem for Dreamcast-on-Wii emulation
// ---------------------------------------------------------------------------
// Design: a 256-entry top-level table (_vmem_MemInfo_ptr) is indexed by the
// upper 8 bits of a 32-bit SH-4 address.  Each entry either:
//   • points directly into a host memory block (fast RAM/VRAM path), or
//   • encodes a handler-table index for MMIO regions.
// ---------------------------------------------------------------------------

// ---- Function-pointer typedefs --------------------------------------------
typedef u8   fastcall _vmem_ReadMem8FP  (u32 Address);
typedef u16  fastcall _vmem_ReadMem16FP (u32 Address);
typedef u32  fastcall _vmem_ReadMem32FP (u32 Address);

typedef void fastcall _vmem_WriteMem8FP (u32 Address, u8  data);
typedef void fastcall _vmem_WriteMem16FP(u32 Address, u16 data);
typedef void fastcall _vmem_WriteMem32FP(u32 Address, u32 data);

// Opaque handle returned by _vmem_register_handler()
typedef u32 _vmem_handler;

// ---- Lifecycle ------------------------------------------------------------
void _vmem_init  ();
void _vmem_reset ();
void _vmem_term  ();

// Reserve / release the host memory backing store.
// Call _vmem_reserve() once at startup before any mapping calls.
bool _vmem_reserve ();
void _vmem_release ();

// ---- Handler registration -------------------------------------------------
// Pass NULL for any function pointer to get a "not-mapped" default that logs
// and returns 0 (reads) / is a no-op (writes).
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
// Map a registered MMIO handler over [start..end] (inclusive, in 16MB pages).
void _vmem_map_handler(_vmem_handler Handler, u32 start, u32 end);

// Map a host memory block over [start..end] with the given address mask.
// 'base' must be 256-byte aligned and non-NULL.
void _vmem_map_block(void* base, u32 start, u32 end, u32 mask);

// Copy mappings from [start .. start+size-1] to [new_region .. new_region+size-1].
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
void  _vmem_get_ptrs(u32 sz, bool write, void*** vmap, void*** func);

// Returns a host pointer for a compile-time-constant SH-4 address.
// Sets ismem=true and returns a pointer into RAM/VRAM, or
// sets ismem=false and returns the handler function pointer.
void* _vmem_read_const(u32 addr, bool& ismem, u32 sz);

// ---- Diagnostic -----------------------------------------------------------
// Returns true if 'addr' falls inside a mapped memory region (not a handler).
bool _vmem_is_mapped(u32 addr);
