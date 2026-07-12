// Native-test leaf stand-in for the project's root types.h. Provides only the
// base types / macros the vbaARM header chain needs, so the REAL arm7.h /
// arm_aica.h / vbaARM.h / arm_mem.h (from ../) compile and link on the host.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#define FASTCALL
#define fastcall
#define naked

#include "plugin_types.h"
