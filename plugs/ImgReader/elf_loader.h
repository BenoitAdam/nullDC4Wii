#pragma once
// elf_loader.h – SH4 ELF loader for nullDC4Wii
//
// Drop elf_loader.cpp + elf_loader.h alongside Elf.cpp/Elf.h in plugs/ImgReader/
//
// elf_load()        – reads the ELF, copies all SHT_PROGBITS/PT_LOAD sections
//                     into mem_b (emulated DC RAM), returns entry point.
// elf_apply_entry() – writes the entry point into Sh4cntx (next_pc / PC)
//                     and sets up SP (R15) and SR. Call AFTER sh4_cpu.Reset().

#include "types.h"

bool elf_load(const char* path, u32* out_entry);
void elf_apply_entry(u32 entry);
