#pragma once

/*
    wii_fastmem.h - PowerPC-MMU fastmem for the SH4 dynarec (FASTMEM preset)

    Claims the low 512 MB of PowerPC effective-address space
    (EA 0x00000000-0x1FFFFFFF, segment registers SR0/SR1 + a hand-built
    hashed page table) as a direct image of the Dreamcast's 29-bit external
    address space:

        EA = sh4_addr & 0x1FFFFFFF        (one rlwinm, no compares)

    Backed regions (all page-table mapped onto the existing MEM2 arrays,
    same storage convention as the _vmem direct path - BE u32 words with
    the addr^(4-sz) sub-word swizzle):

        EA 0x0C000000-0x0FFFFFFF -> mem_b     (16 MB main RAM, 4 mirrors)
        EA 0x04000000-0x04FFFFFF -> vram      (8 MB, 64-bit path, 2 mirrors)
        EA 0x06000000-0x06FFFFFF -> vram      (mirror)
        EA 0x00800000-0x00FFFFFF -> aica_ram  (2 MB wave RAM, 4 mirrors)
        EA 0x02800000-0x02FFFFFF -> aica_ram  (area-0 image mirror)

    Everything else (MMIO, BIOS/flash, TA FIFO, VRAM 32-bit path, P4/SQ
    after masking) is left unmapped: a JIT load/store there takes a DSI,
    and the handler BACK-PATCHES the faulting site with a branch to a
    generated slow-path trampoline (see rec_fastmem_* in wii_driver.cpp),
    then retries.  The exception handler itself never emulates the access,
    so MMIO side effects (GD-ROM DMA file I/O etc.) always run in normal
    context with interrupts enabled.

    Gated by the `fastmem` preset (g_fastmem_preset in main.cpp,
    default OFF).  Wholly inert when the preset is off.
*/

#ifdef __cplusplus
extern "C" {
#endif

// 1 once WiiFastmem_Init() has set up SRs/SDR1/HTAB and the DSI handler and
// the window self-test passed. The JIT emits fastmem code only when this
// is set (checked per block at compile time).
extern int g_wii_fastmem_active;

// Idempotent. Reads the `fastmem` preset; when ON, carves the page table
// from the MEM2 arena, maps the regions above, installs the DSI handler
// (tuxedo libogc PPCExcptSetHandler) and runs a read/write self-test
// through the window. On any failure it prints and leaves fastmem
// inactive (JIT falls back to the legacy inline-table paths).
// Call before the first block is compiled (done at ngen_mainloop entry).
void WiiFastmem_Init(void);

// Implemented in wii_driver.cpp: decode the faulting fastmem site at
// `pc` (one of the fixed shapes emitted by shop_readm/shop_writem),
// build a slow-path trampoline in the trampoline pool and patch the
// site with a branch to it. Returns 0 on success, nonzero if `pc` is
// not a recognizable fastmem site (the DSI handler then panics via
// libogc's default handler).
int rec_fastmem_patch(unsigned int pc);

#ifdef __cplusplus
}
#endif
