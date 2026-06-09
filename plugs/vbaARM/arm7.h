#pragma once
#include "arm_aica.h"

void arm_Init();
void arm_Reset();
void arm_Run(u32 uNumCycles);
void arm_SetEnabled(bool enabled);

// Self-test accessors (see arm7_selftest.cpp)
u32  arm_GetReg(int n);
void arm_SetReg(int n, u32 value);
u32  arm_GetNextPC();
void arm_SetNextPC(u32 value);

// Runs the embedded ARM7DI conformance tests.
// Returns the number of FAILED tests (0 == all passed).
// Prints a per-test pass/fail summary via printf.
int  arm_RunSelfTests();

#define arm_sh4_bias (20) // 2 = fps drop / 20 = Nice / 25 = ok


