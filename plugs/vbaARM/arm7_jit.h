#pragma once
#include "types.h"

// ARM7DI -> PowerPC recompiler for the AICA sound CPU (arm7_jit.cpp).
// Selected by the arm7_jit preset, latched at every arm_Reset().

void arm_jit_after_init();       // end of arm_Init(): one-time conformance self-test
void arm_jit_on_reset();         // end of arm_Reset(): latch engine, flush the cache
bool arm_jit_active();           // true -> arm_Run() goes through arm_jit_run()
void arm_jit_run(u32 CycleCount);
