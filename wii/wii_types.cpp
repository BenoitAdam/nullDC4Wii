#include <stdio.h>

// Every verify()/die() in the codebase (see types.h's dbgbreak macro) calls
// this right before spinning forever. The old implementation wrote to
// address 0 to trigger a debugger trap — but on Wii, address 0 is valid,
// mapped MEM1 RAM (not a guard page), so instead of trapping it silently
// corrupted whatever low-memory structure lives there and left the process
// in an undefined, uninvestigable state on top of the original assertion.
void __debugbreak() { printf("__debugbreak() hit\n"); }

