#pragma once
// elf_loader.h – bare-metal SH4 ELF loader for nullDC4Wii
//
// Usage:
//   u32 entry = 0;
//   if (elf_load("sd:/discs/mygame.elf", &entry))
//       elf_apply_entry(entry);
//
// elf_load()        – parses the ELF, copies PT_LOAD segments into mem_b,
//                     returns the ELF entry point in *out_entry.
// elf_apply_entry() – writes the entry point into Sh4cntx (reg_nextpc / reg_pc)
//                     and sets up a sane initial stack pointer (R15).
//                     Call this AFTER sh4_cpu.Reset() so our value wins.

#include "types.h"

// Returns true on success.  *out_entry receives the ELF e_entry value.
bool elf_load(const char* path, u32* out_entry);

// Pokes the entry point (and a default SP) directly into the SH4 context.
void elf_apply_entry(u32 entry);
