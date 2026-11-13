// wiiu/wiiu_nojit.cpp
//
// Link stubs for the Wii U build, which has no SH4/ARM7 recompiler (config.h:
// HOST_CPU = CPU_PPC_ESPRESSO -> USE_INTERP -> HOST_NO_REC).
//
// HOST_NO_REC already compiles the recompiler bodies out of dc/sh4/rec_v2/
// (driver.cpp, blockmanager.cpp, the ngen_GetFeatures call in decoder.cpp), and
// plugs/vbaARM/arm7_jit.cpp has its own non-Wii stubs. What is left are the few
// shared files that still NAME a recompiler-side symbol unconditionally. On the
// Wii those come from wii/dc/sh4/rec_v2/wii_driver.cpp (the PPC backend), which
// the Wii U does not build. None of them can run here: the interpreter never
// asks for a block, and every census below is gated on a JIT preset that
// wiiu/wiiu_presets.cpp forces off. They exist only so the link resolves.

#include "types.h"
#include "dc/sh4/rec_v2/blockmanager.h"   // bm_Reset: C linkage, as on the Wii
#include "dc/sh4/rec_v2/shil.h"
#include "dc/sh4/rec_v2/ngen.h"

#ifndef HOST_NO_REC
#error "wiiu/wiiu_nojit.cpp is for the interpreter-only Wii U build (HOST_NO_REC)"
#endif

// nullDC.cpp dc_elf_pre_run_hook(): flushes the block table before an ELF boot.
void bm_Reset() {}

// dc/sh4/rec_v2/shil.cpp: canonical-call emitters, only reached while compiling
// a block, which never happens without the recompiler.
void ngen_CC_Start(shil_opcode *op) {}
void ngen_CC_Param(shil_opcode *op, shil_param *par, CanonicalParamType tp) {}
void ngen_CC_Call(shil_opcode *op, void *function) {}
void ngen_CC_Finish(shil_opcode *op) {}

// plugs/drkPvr/SPG.cpp, once a second: JIT censuses (IFB PROBE / HOTBLOCKS).
extern "C" void ifb_probe_dump(double seconds) {}
extern "C" void hotblocks_dump(double seconds) {}

// plugs/drkPvr/ta.cpp: store-queue PC census, reads the recompiler's last
// store-queue site. The interpreter has none to report.
u32  sqpc_current()   { return 0; }
void sqpc_dump_once() {}
