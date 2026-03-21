// elf_loader.cpp – SH4 ELF loader for nullDC4Wii
//
// Based on the original Elf.cpp from the nullDC 360 port (ZeZu, 2004/2005),
// adapted for the Wii big-endian environment.
//
// Key difference from the x86/360 version: on the Wii, the _vmem layer
// serves memory as big-endian. SH4 code and data are little-endian.
// The correct approach (matching how GDI/CDI loading works) is to write
// data using WriteMem32_nommu which applies HOST_TO_LE32 internally.
// For the ELF loader we write byte-by-byte into mem_b[] directly, exactly
// like the original Elf.cpp does — this is correct because:
//   - mem_b is a raw byte array
//   - The _vmem read functions apply endian correction on the way OUT
//   - So bytes stored in file order will be read correctly by the SH4 CPU

#include "plugs/ImgReader/elf_loader.h"
#include "dc/sh4/sh4_registers.h"   // Sh4cntx, next_pc, r[], sr macros
#include "dc/mem/sh4_mem.h"          // mem_b, RAM_MASK
#include "plugs/ImgReader/Elf.h"     // Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, constants
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// elf_load  – parse ELF, copy PT_LOAD / SHT_PROGBITS sections into mem_b
// ---------------------------------------------------------------------------
bool elf_load(const char* path, u32* out_entry)
{
    printf("[ELF] Loading %s\n", path);

    FILE* f = fopen(path, "rb");
    if (!f) { printf("[ELF] Cannot open file\n"); return false; }

    // Get file size
    fseek(f, 0, SEEK_END);
    u32 flen = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (flen < sizeof(Elf32_Ehdr) || flen > 0x800000) // 8 MB max
    {
        printf("[ELF] File size %u out of range\n", flen);
        fclose(f); return false;
    }

    // Read entire file into a heap buffer (same approach as original Elf.cpp)
    u8* buf = (u8*)malloc(flen);
    if (!buf) { printf("[ELF] Out of memory\n"); fclose(f); return false; }

    if (fread(buf, 1, flen, f) != flen)
    {
        printf("[ELF] Read error\n");
        free(buf); fclose(f); return false;
    }
    fclose(f);

    Elf32_Ehdr* eh = (Elf32_Ehdr*)buf;

    // --- Validate header (byte-by-byte magic, no endian issues) ---
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F')
    {
        printf("[ELF] Bad magic\n"); free(buf); return false;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
    {
        printf("[ELF] Not 32-bit ELF\n"); free(buf); return false;
    }
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        printf("[ELF] Not little-endian ELF\n"); free(buf); return false;
    }

    // ELF fields are little-endian; swap for big-endian Wii host
    #define LE16(x) (u16)(((u16)(x) >> 8) | ((u16)(x) << 8))
    #define LE32(x) (u32)(((u32)(x) >> 24) | (((u32)(x) >> 8) & 0xFF00u) | \
                          (((u32)(x) << 8) & 0xFF0000u) | ((u32)(x) << 24))

    u16 e_machine  = LE16(eh->e_machine);
    u16 e_type     = LE16(eh->e_type);
    u32 e_entry    = LE32(eh->e_entry);
    u32 e_phoff    = LE32(eh->e_phoff);
    u16 e_phnum    = LE16(eh->e_phnum);
    u16 e_phentsize= LE16(eh->e_phentsize);
    u32 e_shoff    = LE32(eh->e_shoff);
    u16 e_shnum    = LE16(eh->e_shnum);
    u16 e_shentsize= LE16(eh->e_shentsize);

    if (e_machine != EM_SH)
    {
        printf("[ELF] Wrong arch: %u (expected %u for SH4)\n", e_machine, EM_SH);
        free(buf); return false;
    }
    if (e_type != ET_EXEC)
    {
        printf("[ELF] Not an executable (type=%u)\n", e_type);
        free(buf); return false;
    }

    printf("[ELF] Entry: 0x%08X  Segments: %u  Sections: %u\n",
           e_entry, e_phnum, e_shnum);

    // Sanity check offsets
    if (flen < e_shoff + (u32)e_shentsize * e_shnum ||
        flen < e_phoff + (u32)e_phentsize * e_phnum)
    {
        printf("[ELF] Header offsets exceed file size\n");
        free(buf); return false;
    }

    int loaded = 0;

    // -----------------------------------------------------------------------
    // Pass 1: load via section headers (SHT_PROGBITS + SHF_ALLOC)
    // This is what the original Elf.cpp does and gives the most precise
    // control — each allocatable section is placed at its exact sh_addr.
    // -----------------------------------------------------------------------
    if (e_shnum > 0 && e_shoff > 0)
    {
        for (u16 i = 0; i < e_shnum; i++)
        {
            Elf32_Shdr* sh = (Elf32_Shdr*)(buf + e_shoff + (u32)e_shentsize * i);

            u32 sh_type   = LE32(sh->sh_type);
            u32 sh_flags  = LE32(sh->sh_flags);
            u32 sh_addr   = LE32(sh->sh_addr);
            u32 sh_offset = LE32(sh->sh_offset);
            u32 sh_size   = LE32(sh->sh_size);

            if (sh_type != SHT_PROGBITS) continue;
            if (!(sh_flags & SHF_ALLOC))  continue;
            if (sh_size == 0)              continue;

            // Must land in DC RAM (physical 0x0C000000)
            if ((sh_addr & 0x0F000000) != 0x0C000000)
            {
                printf("[ELF] Section %u: addr 0x%08X not in DC RAM, skipping\n",
                       i, sh_addr);
                continue;
            }

            u32 dst = sh_addr & RAM_MASK;
            if (dst + sh_size > mem_b.size)
            {
                printf("[ELF] Section %u: overflows RAM\n", i);
                continue;
            }
            if (sh_offset + sh_size > flen)
            {
                printf("[ELF] Section %u: overflows file\n", i);
                continue;
            }

            printf("[ELF] Section %u: 0x%08X <- file+0x%X  (%u bytes)\n",
                   i, sh_addr, sh_offset, sh_size);

            // Copy byte-by-byte exactly as the original Elf.cpp does.
            // mem_b is a raw byte array; _vmem read functions handle endian
            // correction when the SH4 CPU reads instructions/data back.
            for (u32 x = 0; x < sh_size; x++)
                mem_b.data[dst + x] = buf[sh_offset + x];

            loaded++;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 2: fallback to program headers (PT_LOAD) if no sections were found
    // (stripped ELFs may have no section table)
    // -----------------------------------------------------------------------
    if (loaded == 0 && e_phnum > 0)
    {
        printf("[ELF] No sections loaded, trying program headers\n");
        for (u16 i = 0; i < e_phnum; i++)
        {
            Elf32_Phdr* ph = (Elf32_Phdr*)(buf + e_phoff + (u32)e_phentsize * i);

            u32 p_type   = LE32(ph->p_type);
            u32 p_offset = LE32(ph->p_offset);
            u32 p_vaddr  = LE32(ph->p_vaddr);
            u32 p_filesz = LE32(ph->p_filesz);
            u32 p_memsz  = LE32(ph->p_memsz);

            if (p_type != PT_LOAD) continue;
            if (p_filesz == 0 && p_memsz == 0) continue;
            if ((p_vaddr & 0x0F000000) != 0x0C000000)
            {
                printf("[ELF] Seg %u: vaddr 0x%08X not in DC RAM, skipping\n",
                       i, p_vaddr);
                continue;
            }

            u32 dst = p_vaddr & RAM_MASK;
            if (dst + p_memsz > mem_b.size) { printf("[ELF] Seg %u overflows RAM\n", i); continue; }

            printf("[ELF] Seg %u: 0x%08X  file=0x%X  mem=0x%X\n",
                   i, p_vaddr, p_filesz, p_memsz);

            // Copy file data
            for (u32 x = 0; x < p_filesz; x++)
                mem_b.data[dst + x] = buf[p_offset + x];

            // Zero BSS
            if (p_memsz > p_filesz)
                memset(mem_b.data + dst + p_filesz, 0, p_memsz - p_filesz);

            loaded++;
        }
    }

    #undef LE16
    #undef LE32

    free(buf);

    if (loaded == 0)
    {
        printf("[ELF] Nothing was loaded\n");
        return false;
    }

    *out_entry = e_entry;
    printf("[ELF] Load complete (%d section(s))  entry=0x%08X\n", loaded, e_entry);
    return true;
}

// ---------------------------------------------------------------------------
// elf_apply_entry – poke entry point into SH4 context after CPU reset
// ---------------------------------------------------------------------------
void elf_apply_entry(u32 entry)
{
    next_pc = entry;
    r[15]   = 0x8CFFF000u;  // stack pointer: top of DC RAM
    sr.SetFull(0x00000000u); // user mode, interrupts enabled

    printf("[ELF] SH4 context patched: PC=0x%08X  SP=0x%08X\n", next_pc, r[15]);
}
