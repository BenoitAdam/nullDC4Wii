/*
Third step of optimisation

Future Optimization Opportunities:
  Paired Singles: Wii's PowerPC has paired-single instructions that could accelerate vector ops
  SIMD: Could use AltiVec for FIPR/FTRV operations
  Cache Optimization: Consider data layout for better cache usage

*/

// ============================================================================
// RUNTIME - ACCURACY SELECTION
// ============================================================================

// External function to get the user's selected accuracy mode
// This is defined in main.cpp
extern "C" int get_accuracy_preset();

// Helper macros to check current accuracy mode
#define FAST() (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

#include "types.h"
#include <math.h>
#include <float.h>

#include "sh4_interpreter.h"
#include "sh4_registers.h"
#include "dc/mem/sh4_mem.h"

// ============================================================================
// MACROS AND HELPER FUNCTIONS
// ============================================================================

#define sh4op(str) void FASTCALL str(u32 op)
#define GetN(str) ((str >> 8) & 0xf)
#define GetM(str) ((str >> 4) & 0xf)
#define GetImm4(str) ((str >> 0) & 0xf)
#define GetImm8(str) ((str >> 0) & 0xff)
#define GetImm12(str) ((str >> 0) & 0xfff)

#define GetDN(opc) ((op & 0x0F00) >> 9)
#define GetDM(opc) ((op & 0x00F0) >> 5)

void iNimp(const char *str);

#define IS_DENORMAL(f) (((*(f)) & 0x7f800000) == 0)

#define ReadMemU64(to, addr) to = ReadMem64(addr)
#define ReadMemU32(to, addr) to = ReadMem32(addr)
#define ReadMemS32(to, addr) to = (s32)ReadMem32(addr)
#define ReadMemS16(to, addr) to = (u32)(s32)(s16)ReadMem16(addr)
#define ReadMemS8(to, addr) to = (u32)(s32)(s8)ReadMem8(addr)

// Base,offset format
#define ReadMemBOU32(to, addr, offset) ReadMemU32(to, addr + offset)
#define ReadMemBOS16(to, addr, offset) ReadMemS16(to, addr + offset)
#define ReadMemBOS8(to, addr, offset) ReadMemS8(to, addr + offset)

// Write Mem Macros
#define WriteMemU64(addr, data) WriteMem64(addr, (u64)data)
#define WriteMemU32(addr, data) WriteMem32(addr, (u32)data)
#define WriteMemU16(addr, data) WriteMem16(addr, (u16)data)
#define WriteMemU8(addr, data) WriteMem8(addr, (u8)data)

// Base,offset format
#define WriteMemBOU32(addr, offset, data) WriteMemU32(addr + offset, data)
#define WriteMemBOU16(addr, offset, data) WriteMemU16(addr + offset, data)
#define WriteMemBOU8(addr, offset, data) WriteMemU8(addr + offset, data)

// ============================================================================
// NaN FIXING - Flycast improvement for better accuracy
// ============================================================================

// Fix NaN values according to SH4 spec
INLINE float fixNaN(float value)
{
  u32 *v = (u32 *)&value;
  // If NaN, return default NaN (0x7FFFFFFF for positive, 0xFFFFFFFF for negative)
  if ((*v & 0x7F800000) == 0x7F800000 && (*v & 0x007FFFFF) != 0)
  {
    *v = (*v & 0x80000000) | 0x7FFFFFFF;
  }
  return value;
}

INLINE double fixNaN64(double value)
{
  u64 *v = (u64 *)&value;
  // If NaN, return default NaN
  if ((*v & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (*v & 0x000FFFFFFFFFFFFFULL) != 0)
  {
    *v = (*v & 0x8000000000000000ULL) | 0x7FFFFFFFFFFFFFFFULL;
  }
  return value;
}

// ============================================================================
// DENORMAL HANDLING
// ============================================================================

INLINE void Denorm32(float &value)
{
  if (fpscr.DN)
  {
    u32 *v = (u32 *)&value;
    if (IS_DENORMAL(v) && (*v & 0x7fFFFFFF) != 0)
    {
      *v &= 0x80000000;
    }
  }
}

// Use fixNaN for better accuracy (Flycast improvement)
#define CHECK_FPU_32(v) v = fixNaN(v)
#define CHECK_FPU_64(v) v = fixNaN64(v)

// Placeholder macros for potential future rounding mode control
#define START64()
#define END64()
#define STARTMODE64()
#define ENDMODE64()

// ============================================================================
// HELPER FUNCTIONS FOR DOUBLE PRECISION (Flycast style)
// ============================================================================

static INLINE double getDRn(u32 op)
{
  return GetDR((op >> 9) & 7);
}

static INLINE double getDRm(u32 op)
{
  return GetDR((op >> 5) & 7);
}

static INLINE void setDRn(u32 op, double d)
{
  SetDR((op >> 9) & 7, d);
}

// ============================================================================
// FPU OPERATIONS
// ============================================================================

// fadd <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0000)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);
    fr[n] += fr[m];
    CHECK_FPU_32(fr[n]);
  }
  else
  {
    double d = getDRn(op) + getDRm(op);
    CHECK_FPU_64(d);
    setDRn(op, d);
  }
}

// fsub <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0001)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    fr[n] -= fr[m];
    CHECK_FPU_32(fr[n]);
  }
  else
  {
    double d = getDRn(op) - getDRm(op);
    CHECK_FPU_64(d);
    setDRn(op, d);
  }
}

// fmul <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0010)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);
    fr[n] *= fr[m];
    CHECK_FPU_32(fr[n]);
  }
  else
  {
    double d = getDRn(op) * getDRm(op);
    CHECK_FPU_64(d);
    setDRn(op, d);
  }
}

// fdiv <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0011)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    fr[n] /= fr[m];

    CHECK_FPU_32(fr[n]);
  }
  else
  {
    double d = getDRn(op) / getDRm(op);
    CHECK_FPU_64(d);
    setDRn(op, d);
  }
}

// fcmp/eq <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0100)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    sr.T = (fr[m] == fr[n]) ? 1 : 0;
  }
  else
  {
    sr.T = (getDRm(op) == getDRn(op)) ? 1 : 0;
  }
}

// fcmp/gt <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_0101)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    sr.T = (fr[n] > fr[m]) ? 1 : 0;
  }
  else
  {
    sr.T = (getDRn(op) > getDRm(op)) ? 1 : 0;
  }
}

// ============================================================================
// MEMORY OPERATIONS
// ============================================================================

// fmov.s @(R0,<REG_M>),<FREG_N>
sh4op(i1111_nnnn_mmmm_0110)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    ReadMemU32(fr_hex[n], r[m] + r[0]);
  }
  else
  {
    u32 n = GetN(op) >> 1;
    u32 m = GetM(op);
    if (((op >> 8) & 0x1) == 0)
    {
      ReadMemU64(dr_hex[n], r[m] + r[0]);
    }
    else
    {
      ReadMemU64(xd_hex[n], r[m] + r[0]);
    }
  }
}

// fmov.s <FREG_M>,@(R0,<REG_N>)
sh4op(i1111_nnnn_mmmm_0111)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    WriteMem32(r[0] + r[n], fr_hex[m]);
  }
  else
  {
    u32 n = GetN(op);
    u32 m = GetM(op) >> 1;
    if (((op >> 4) & 0x1) == 0)
    {
      WriteMemU64(r[n] + r[0], dr_hex[m]);
    }
    else
    {
      WriteMemU64(r[n] + r[0], xd_hex[m]);
    }
  }
}

// fmov.s @<REG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_1000)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);
    ReadMemU32(fr_hex[n], r[m]);
  }
  else
  {
    u32 n = GetN(op) >> 1;
    u32 m = GetM(op);
    if (((op >> 8) & 0x1) == 0)
    {
      ReadMemU64(dr_hex[n], r[m]);
    }
    else
    {
      ReadMemU64(xd_hex[n], r[m]);
    }
  }
}

// fmov.s @<REG_M>+,<FREG_N>
sh4op(i1111_nnnn_mmmm_1001)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    ReadMemU32(fr_hex[n], r[m]);
    r[m] += 4;
  }
  else
  {
    u32 n = GetN(op) >> 1;
    u32 m = GetM(op);
    if (((op >> 8) & 0x1) == 0)
    {
      ReadMemU64(dr_hex[n], r[m]);
    }
    else
    {
      ReadMemU64(xd_hex[n], r[m]);
    }
    r[m] += 8;
  }
}

// fmov.s <FREG_M>,@<REG_N>
sh4op(i1111_nnnn_mmmm_1010)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);
    WriteMemU32(r[n], fr_hex[m]);
  }
  else
  {
    u32 n = GetN(op);
    u32 m = GetM(op) >> 1;

    if (((op >> 4) & 0x1) == 0)
    {
      WriteMemU64(r[n], dr_hex[m]);
    }
    else
    {
      WriteMemU64(r[n], xd_hex[m]);
    }
  }
}

// fmov.s <FREG_M>,@-<REG_N>
sh4op(i1111_nnnn_mmmm_1011)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

    u32 addr = r[n] - 4;
    WriteMemU32(addr, fr_hex[m]);
    r[n] = addr;
  }
  else
  {
    u32 n = GetN(op);
    u32 m = GetM(op) >> 1;

    u32 addr = r[n] - 8;
    if (((op >> 4) & 0x1) == 0)
    {
      WriteMemU64(addr, dr_hex[m]);
    }
    else
    {
      WriteMemU64(addr, xd_hex[m]);
    }
    r[n] = addr;
  }
}

// fmov <FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_1100)
{
  if (fpscr.SZ == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);
    fr[n] = fr[m];
  }
  else
  {
    u32 n = GetN(op) >> 1;
    u32 m = GetM(op) >> 1;
    switch ((op >> 4) & 0x11)
    {
    case 0x00:
      dr_hex[n] = dr_hex[m];
      break;
    case 0x01:
      dr_hex[n] = xd_hex[m];
      break;
    case 0x10:
      xd_hex[n] = dr_hex[m];
      break;
    case 0x11:
      xd_hex[n] = xd_hex[m];
      break;
    }
  }
}

// ============================================================================
// SINGLE OPERAND OPERATIONS
// ============================================================================

// fabs <FREG_N>
sh4op(i1111_nnnn_0101_1101)
{
  int n = GetN(op);

  if (fpscr.PR == 0)
    fr_hex[n] &= 0x7FFFFFFF;
  else
    fr_hex[n & 0xE] &= 0x7FFFFFFF;
}

// FSCA FPUL, DRn//F0FD//1111_nnn0_1111_1101
sh4op(i1111_nnn0_1111_1101)
{
  int n = GetN(op) & 0xE;

  if (fpscr.PR == 0)
  {
    u32 pi_index = fpul & 0xFFFF;

    // Use lookup table (assumes sin_table is defined elsewhere)
    fr[n + 0] = sin_table[pi_index];
    fr[n + 1] = sin_table[pi_index + 0x4000];
  }
  else
    iNimp("FSCA : Double precision mode");
}

// FSRRA //1111_nnnn_0111_1101
sh4op(i1111_nnnn_0111_1101)
{
  u32 n = GetN(op);
  if (fpscr.PR == 0)
  {
    fr[n] = 1.0f / sqrtf(fr[n]);
    CHECK_FPU_32(fr[n]);
  }
  else
    iNimp("FSRRA : Double precision mode");
}

// fcnvds <DR_N>,FPUL
sh4op(i1111_nnnn_1011_1101)
{
  if (fpscr.PR == 1)
  {
    u32 *p = &fpul;
    *((float *)p) = (float)getDRn(op);
  }
  else
  {
    iNimp("FCNVDS: Single precision mode");
  }
}

// fcnvsd FPUL,<DR_N>
sh4op(i1111_nnnn_1010_1101)
{
  if (fpscr.PR == 1)
  {
    u32 *p = &fpul;
    setDRn(op, (double)*((float *)p));
  }
  else
  {
    iNimp("FCNVSD: Single precision mode");
  }
}

// fipr <FV_M>,<FV_N>
sh4op(i1111_nnmm_1110_1101)
{
  int n = GetN(op) & 0xC;
  int m = (GetN(op) & 0x3) << 2;

  if (fpscr.PR == 0)
  {
    if (ACCURATE())
    {
      // ACCURATE mode: Use double precision for intermediate calculations
      double idp = (double)fr[n + 0] * fr[m + 0];
      idp += (double)fr[n + 1] * fr[m + 1];
      idp += (double)fr[n + 2] * fr[m + 2];
      idp += (double)fr[n + 3] * fr[m + 3];

      fr[n + 3] = fixNaN((float)idp);
    }
    else if (BALANCED())
    {
      // BALANCED mode: Use double precision with NaN fixing
      double idp = (double)fr[n + 0] * fr[m + 0];
      idp += (double)fr[n + 1] * fr[m + 1];
      idp += (double)fr[n + 2] * fr[m + 2];
      idp += (double)fr[n + 3] * fr[m + 3];

      fr[n + 3] = fixNaN((float)idp);
    }
    else // FAST mode
    {
      // FAST mode: Use float only for maximum speed
      float idp = fr[n + 0] * fr[m + 0];
      idp += fr[n + 1] * fr[m + 1];
      idp += fr[n + 2] * fr[m + 2];
      idp += fr[n + 3] * fr[m + 3];

      fr[n + 3] = idp;
    }
  }
  else
    iNimp("FIPR Precision=1");
}

// fldi0 <FREG_N>
sh4op(i1111_nnnn_1000_1101)
{
  if (fpscr.PR != 0)
    return; // Flycast improvement: just return instead of iNimp

  u32 n = GetN(op);
  fr[n] = 0.0f;
}

// fldi1 <FREG_N>
sh4op(i1111_nnnn_1001_1101)
{
  if (fpscr.PR != 0)
    return; // Flycast improvement: just return instead of iNimp

  u32 n = GetN(op);
  fr[n] = 1.0f;
}

// flds <FREG_N>,FPUL
sh4op(i1111_nnnn_0001_1101)
{
  u32 n = GetN(op);
  fpul = fr_hex[n];
}

// fsts FPUL,<FREG_N>
sh4op(i1111_nnnn_0000_1101)
{
  u32 n = GetN(op);
  fr_hex[n] = fpul;
}

// float FPUL,<FREG_N>
sh4op(i1111_nnnn_0010_1101)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    fr[n] = (float)(int)fpul;
  }
  else
  {
    setDRn(op, (double)(int)fpul);
  }
}

// fneg <FREG_N>
sh4op(i1111_nnnn_0100_1101)
{
  u32 n = GetN(op);

  if (fpscr.PR == 0)
    fr_hex[n] ^= 0x80000000;
  else
    fr_hex[n & 0xE] ^= 0x80000000;
}

// frchg
sh4op(i1111_1011_1111_1101)
{
  fpscr.FR = 1 - fpscr.FR;
  UpdateFPSCR();
}

// fschg
sh4op(i1111_0011_1111_1101)
{
  fpscr.SZ = 1 - fpscr.SZ;
  UpdateFPSCR();
}

// fsqrt <FREG_N>
sh4op(i1111_nnnn_0110_1101)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    fr[n] = sqrtf(fr[n]);
    CHECK_FPU_32(fr[n]);
  }
  else
  {
    setDRn(op, fixNaN64(sqrt(getDRn(op))));
  }
}

// ftrc <FREG_N>, FPUL
sh4op(i1111_nnnn_0011_1101)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);

    // Flycast improvement: Better NaN handling
    if (isnan(fr[n]))
    {
      fpul = 0x80000000;
    }
    else
    {
      fpul = (u32)(s32)fr[n];

      // Handle overflow to max positive value
      if ((s32)fpul > 0x7fffff80)
        fpul = 0x7fffffff;
#if HOST_CPU == CPU_X86 || HOST_CPU == CPU_X64
      // Intel CPUs convert out of range float numbers to 0x80000000
      // Manually set the correct sign
      else if (fpul == 0x80000000 && fr[n] > 0)
        fpul--;
#else
      // Generic handling for other architectures
      else if (fpul == 0x80000000 && fr[n] > 0)
        fpul--;
#endif
    }
  }
  else
  {
    double d = getDRn(op);

    // Flycast improvement: Better NaN handling for double precision
    if (isnan(d))
    {
      fpul = 0x80000000;
    }
    else
    {
      fpul = (u32)(s32)d;
#if HOST_CPU == CPU_X86 || HOST_CPU == CPU_X64
      // Intel CPUs convert out of range float numbers to 0x80000000
      if (fpul == 0x80000000 && d > 0)
        fpul--;
#else
      // Generic handling for other architectures
      if (fpul == 0x80000000 && d > 0)
        fpul--;
#endif
    }
  }
}

// fmac <FREG_0>,<FREG_M>,<FREG_N>
sh4op(i1111_nnnn_mmmm_1110)
{
  if (fpscr.PR == 0)
  {
    u32 n = GetN(op);
    u32 m = GetM(op);

#ifdef __cpp_lib_math_special_functions
    // Flycast improvement: Use FMA if available for better accuracy
    fr[n] = fmaf(fr[0], fr[m], fr[n]);
#else
    // Fallback: Use double precision for intermediate calculation
    fr[n] = (f32)((f64)fr[n] + (f64)fr[0] * (f64)fr[m]);
#endif
    CHECK_FPU_32(fr[n]);
  }
  else
  {
    iNimp("fmac <DREG_0>,<DREG_M>,<DREG_N>");
  }
}

// ftrv xmtrx,<FV_N>
sh4op(i1111_nn01_1111_1101)
{
  /*
  XF[0] XF[4] XF[8] XF[12]    FR[n]      FR[n]
  XF[1] XF[5] XF[9] XF[13]  * FR[n+1] -> FR[n+1]
  XF[2] XF[6] XF[10] XF[14]   FR[n+2]    FR[n+2]
  XF[3] XF[7] XF[11] XF[15]   FR[n+3]    FR[n+3]
  */

  u32 n = GetN(op) & 0xC;

  if (fpscr.PR == 0)
  {
    if (ACCURATE() || BALANCED())
    {
      // ACCURATE/BALANCED mode: Use double precision for intermediate calculations
      double v1 = (double)xf[0] * fr[n + 0] +
                  (double)xf[4] * fr[n + 1] +
                  (double)xf[8] * fr[n + 2] +
                  (double)xf[12] * fr[n + 3];

      double v2 = (double)xf[1] * fr[n + 0] +
                  (double)xf[5] * fr[n + 1] +
                  (double)xf[9] * fr[n + 2] +
                  (double)xf[13] * fr[n + 3];

      double v3 = (double)xf[2] * fr[n + 0] +
                  (double)xf[6] * fr[n + 1] +
                  (double)xf[10] * fr[n + 2] +
                  (double)xf[14] * fr[n + 3];

      double v4 = (double)xf[3] * fr[n + 0] +
                  (double)xf[7] * fr[n + 1] +
                  (double)xf[11] * fr[n + 2] +
                  (double)xf[15] * fr[n + 3];

      fr[n + 0] = fixNaN((float)v1);
      fr[n + 1] = fixNaN((float)v2);
      fr[n + 2] = fixNaN((float)v3);
      fr[n + 3] = fixNaN((float)v4);
    }
    else // FAST mode
    {
      // FAST mode: Use float only
      float v1 = xf[0] * fr[n + 0] +
                 xf[4] * fr[n + 1] +
                 xf[8] * fr[n + 2] +
                 xf[12] * fr[n + 3];

      float v2 = xf[1] * fr[n + 0] +
                 xf[5] * fr[n + 1] +
                 xf[9] * fr[n + 2] +
                 xf[13] * fr[n + 3];

      float v3 = xf[2] * fr[n + 0] +
                 xf[6] * fr[n + 1] +
                 xf[10] * fr[n + 2] +
                 xf[14] * fr[n + 3];

      float v4 = xf[3] * fr[n + 0] +
                 xf[7] * fr[n + 1] +
                 xf[11] * fr[n + 2] +
                 xf[15] * fr[n + 3];

      fr[n + 0] = v1;
      fr[n + 1] = v2;
      fr[n + 2] = v3;
      fr[n + 3] = v4;
    }
  }
  else
    iNimp("FTRV in dp mode");
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

void iNimp(const char *str)
{
  printf("Unimplemented sh4 fpu instruction: %s\n", str);
  // Optional: Add more robust error handling here
  // For example: log to file, trigger breakpoint, etc.
}
