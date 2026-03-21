// elf_loader.cpp – bare-metal SH4 ELF loader for nullDC4Wii
//
// Loads a statically linked SH4 ELF directly into the emulated Dreamcast RAM
// (mem_b) and forces the SH4 PC to the ELF entry point so the emulator boots
// into the homebrew instead of running the BIOS.
//
// Only PT_LOAD segments are processed (the only ones needed for execution).
// BSS regions (p_memsz > p_filesz) are zeroed as required by the SH4 ABI.
//
// Limitations / assumptions:
//   - The ELF must be statically linked (no dynamic linker on bare metal DC).
//   - Virtual addresses must fall in Dreamcast RAM: 0x8C000000–0x8CFFFFFF.
//     Some homebrew also uses the P1 mirror (0xAC000000) – both are handled.
//   - The ELF machine type must be EM_SH (42).

#include "elf_loader.h"
#include "dc/sh4/sh4_registers.h"   // Sh4cntx, next_pc, r[], sr macros
#include "dc/mem/sh4_mem.h"          // mem_b  (the 16 MB DC RAM buffer)
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Minimal ELF32 definitions (we don't want a full libelf dependency)
// ---------------------------------------------------------------------------
#define ELF_MAGIC_LE  0x464C457Fu   // "\x7FELF" in little-endian u32
#define EM_SH         42u           // SH (SuperH) architecture
#define PT_LOAD       1u            // Loadable segment type
#define ET_EXEC       2u            // Executable file type

#pragma pack(push, 1)
struct Elf32_Ehdr
{
    u8  e_ident[16];    // Magic, class, data, version, OS/ABI …
    u16 e_type;         // ET_EXEC = 2
    u16 e_machine;      // EM_SH  = 42
    u32 e_version;
    u32 e_entry;        // Virtual entry point
    u32 e_phoff;        // Program-header table offset
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;    // Size of one program-header entry
    u16 e_phnum;        // Number of program-header entries
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct Elf32_Phdr
{
    u32 p_type;     // PT_LOAD = 1
    u32 p_offset;   // Offset in file
    u32 p_vaddr;    // Virtual load address
    u32 p_paddr;    // Physical load address (ignored on DC)
    u32 p_filesz;   // Bytes in file
    u32 p_memsz;    // Bytes in memory (>= p_filesz; excess is BSS)
    u32 p_flags;
    u32 p_align;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Address translation: DC virtual → offset into mem_b.data
//
// Dreamcast RAM mirrors:
//   P1 cached   0x8C000000 – 0x8CFFFFFF
//   P2 uncached 0xAC000000 – 0xACFFFFFF
// Both map to the same 16 MB physical RAM (mem_b).
// ---------------------------------------------------------------------------
static bool vaddr_to_phys(u32 vaddr, u32 filesz, u32& out_offset)
{
    // Strip the top nibble to get the physical address within a 256 MB window,
    // then subtract the RAM base (0x0C000000 physical).
    const u32 phys = vaddr & 0x0FFFFFFFu;

    if (phys < 0x0C000000u || phys > 0x0FFFFFFFu)
    {
        printf("[ELF] vaddr 0x%08X is outside DC RAM range\n", vaddr);
        return false;
    }

    const u32 offset = phys - 0x0C000000u;

    if (offset + filesz > mem_b.size)
    {
        printf("[ELF] Segment vaddr=0x%08X size=0x%X overflows RAM (size=0x%X)\n",
               vaddr, filesz, mem_b.size);
        return false;
    }

    out_offset = offset;
    return true;
}

// ---------------------------------------------------------------------------
// Public API: elf_load
// ---------------------------------------------------------------------------
bool elf_load(const char* path, u32* out_entry)
{
    printf("[ELF] Loading %s\n", path);

    FILE* f = fopen(path, "rb");
    if (!f)
    {
        printf("[ELF] Cannot open file\n");
        return false;
    }

    // --- Read and validate the ELF header ---
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
    {
        printf("[ELF] Short read on header\n");
        fclose(f); return false;
    }

    // Magic
    if (*reinterpret_cast<const u32*>(ehdr.e_ident) != ELF_MAGIC_LE)
    {
        printf("[ELF] Bad magic (not an ELF file?)\n");
        fclose(f); return false;
    }

    // ELF class must be 32-bit (ELFCLASS32 = 1)
    if (ehdr.e_ident[4] != 1)
    {
        printf("[ELF] Not a 32-bit ELF (class=%u)\n", ehdr.e_ident[4]);
        fclose(f); return false;
    }

    // Machine type
    if (ehdr.e_machine != EM_SH)
    {
        printf("[ELF] Wrong architecture: %u (expected %u for SH)\n",
               ehdr.e_machine, EM_SH);
        fclose(f); return false;
    }

    // Must be an executable
    if (ehdr.e_type != ET_EXEC)
    {
        printf("[ELF] Not an executable (type=%u) – is this a relocatable object?\n",
               ehdr.e_type);
        fclose(f); return false;
    }

    if (ehdr.e_phnum == 0)
    {
        printf("[ELF] No program headers – nothing to load\n");
        fclose(f); return false;
    }

    printf("[ELF] Entry: 0x%08X  Segments: %u\n", ehdr.e_entry, ehdr.e_phnum);

    // --- Process PT_LOAD segments ---
    int loaded = 0;
    for (u16 i = 0; i < ehdr.e_phnum; i++)
    {
        Elf32_Phdr phdr;
        fseek(f, (long)(ehdr.e_phoff + (u32)i * ehdr.e_phentsize), SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, f) != 1)
        {
            printf("[ELF] Short read on program header %u\n", i);
            fclose(f); return false;
        }

        if (phdr.p_type != PT_LOAD)
            continue;
        if (phdr.p_filesz == 0 && phdr.p_memsz == 0)
            continue;

        u32 offset;
        if (!vaddr_to_phys(phdr.p_vaddr, phdr.p_filesz, offset))
        {
            // Non-fatal for BSS-only segments that fall inside RAM but have
            // filesz==0; skip gracefully.
            if (phdr.p_filesz == 0)
                continue;
            fclose(f); return false;
        }

        printf("[ELF] Seg %u: vaddr=0x%08X  file=0x%X  mem=0x%X\n",
               i, phdr.p_vaddr, phdr.p_filesz, phdr.p_memsz);

        // Copy data from file into emulated RAM
        if (phdr.p_filesz > 0)
        {
            fseek(f, (long)phdr.p_offset, SEEK_SET);
            if (fread(mem_b.data + offset, 1, phdr.p_filesz, f) != phdr.p_filesz)
            {
                printf("[ELF] Short read loading segment %u\n", i);
                fclose(f); return false;
            }
        }

        // Zero BSS (the part of the segment not covered by file data)
        if (phdr.p_memsz > phdr.p_filesz)
        {
            u32 bss_offset = offset + phdr.p_filesz;
            u32 bss_size   = phdr.p_memsz - phdr.p_filesz;

            // Clamp in case the ELF is oddly linked
            if (bss_offset + bss_size <= mem_b.size)
            {
                memset(mem_b.data + bss_offset, 0, bss_size);
                printf("[ELF]   BSS zeroed: 0x%X bytes at ram+0x%X\n",
                       bss_size, bss_offset);
            }
        }

        loaded++;
    }

    fclose(f);

    if (loaded == 0)
    {
        printf("[ELF] No PT_LOAD segments were loaded – ELF may be stripped\n");
        return false;
    }

    *out_entry = ehdr.e_entry;
    printf("[ELF] Load complete (%d segment(s))  entry=0x%08X\n", loaded, *out_entry);
    return true;
}

// ---------------------------------------------------------------------------
// Public API: elf_apply_entry
//
// Writes the entry point into Sh4cntx so the CPU starts there instead of
// at the BIOS reset vector (0xA0000000).
//
// Must be called AFTER sh4_cpu.Reset() since Reset() sets nextpc to the BIOS
// vector and would overwrite us if called afterwards.
//
// Also sets:
//   R15 (stack pointer) – top of usable RAM minus a small guard zone
//   SR  – user-mode, interrupts enabled (matches what IP.BIN leaves behind
//          when launching normal software from the BIOS)
// ---------------------------------------------------------------------------
void elf_apply_entry(u32 entry)
{
    // Sh4Context fields (from sh4_registers.h):
    //   next_pc  → macro for Sh4cntx.pc          (the Program Counter)
    //   r[15]    → macro for Sh4cntx.r[15]       (R15 = stack pointer)
    //   sr       → macro for Sh4cntx.sr           (Status Register)
    // All macros are defined in sh4_registers.h and expand to the real fields.

    // Set the Program Counter
    next_pc = entry;

    // Set Stack Pointer to top of DC RAM (16 MB, base 0x8C000000).
    // Leave a 4 KB guard zone against the first PUSH before the ELF
    // sets up its own stack frame.
    r[15] = 0x8CFFF000u;

    // Put the CPU into a clean user-mode state (no BL, no MD, IMASK=0).
    // SR 0x00000000 = user mode, all interrupts enabled, T=0.
    // Most homebrew ELFs expect this on entry (matches post-IP.BIN state).
    sr.SetFull(0x00000000u);

    printf("[ELF] SH4 context patched: PC=0x%08X  SP=0x%08X\n",
           next_pc, r[15]);
}
