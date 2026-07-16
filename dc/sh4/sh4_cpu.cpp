/*
	SH4 CPU Interpreter - Core Operations
	Optimized for Wii/PowerPC architecture
*/

#include "types.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_interpreter.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "sh4_registers.h"
#include "ccn.h"
#include "intc.h"
#include "tmu.h"
#include "dc/gdrom/gdrom_if.h"

//=============================================================================
// OPCODE DECODE MACROS
//=============================================================================

#define GetN(str)       ((str >> 8) & 0xF)
#define GetM(str)       ((str >> 4) & 0xF)
#define GetImm4(str)    ((str >> 0) & 0xF)
#define GetImm8(str)    ((str >> 0) & 0xFF)
#define GetSImm8(str)   ((s8)((str >> 0) & 0xFF))
#define GetImm12(str)   ((str >> 0) & 0xFFF)
#define GetSImm12(str)  (((s16)((GetImm12(str)) << 4)) >> 4)

#define iNimp  cpu_iNimp
#define iWarn  cpu_iWarn

//=============================================================================
// MEMORY ACCESS MACROS
//=============================================================================

// Read operations with sign/zero extension
#define ReadMemU32(to, addr)            to = ReadMem32(addr)
#define ReadMemS32(to, addr)            to = (s32)ReadMem32(addr)
#define ReadMemS16(to, addr)            to = (u32)(s32)(s16)ReadMem16(addr)
#define ReadMemS8(to, addr)             to = (u32)(s32)(s8)ReadMem8(addr)

// Base + offset read operations
#define ReadMemBOU32(to, addr, offset)  ReadMemU32(to, addr + offset)
#define ReadMemBOS16(to, addr, offset)  ReadMemS16(to, addr + offset)
#define ReadMemBOS8(to, addr, offset)   ReadMemS8(to, addr + offset)

// Write operations
#define WriteMemU32(addr, data)         WriteMem32(addr, (u32)data)
#define WriteMemU16(addr, data)         WriteMem16(addr, (u16)data)
#define WriteMemU8(addr, data)          WriteMem8(addr, (u8)data)

// Base + offset write operations
#define WriteMemBOU32(addr, offset, data) WriteMemU32(addr + offset, data)
#define WriteMemBOU16(addr, offset, data) WriteMemU16(addr + offset, data)
#define WriteMemBOU8(addr, offset, data)  WriteMemU8(addr + offset, data)

//=============================================================================
// GLOBAL STATE
//=============================================================================

bool sh4_sleeping;

//=============================================================================
// DEBUG/ERROR REPORTING
//=============================================================================

void cpu_iNimp(u32 op, const char* info)
{
	printf("Unimplemented opcode: 0x%04X : %s\n", op, info);
}

void cpu_iWarn(u32 op, const char* info)
{
	printf("Warning opcode: 0x%04X : %s @ PC=0x%08X\n", op, info, curr_pc);
}

//=============================================================================
// INCLUDE INSTRUCTION IMPLEMENTATIONS
//=============================================================================

#include "sh4_cpu_movs.h"
#include "sh4_cpu_branch.h"
#include "sh4_cpu_arith.h"
#include "sh4_cpu_logic.h"
#include "dc/mem/mmu.h"

//=============================================================================
// TLB/CACHE CONTROL
//=============================================================================

// ldtlb - Load UTLB entry
sh4op(i0000_0000_0011_1000)
{
	printf("ldtlb %d/%d\n", CCN_MMUCR.URC, CCN_MMUCR.URB);
	UTLB[CCN_MMUCR.URC].Data = CCN_PTEL;
	UTLB[CCN_MMUCR.URC].Address = CCN_PTEH;
	UTLB_Sync(CCN_MMUCR.URC);
}

// ocbi @<REG_N> - Operand Cache Block Invalidate
sh4op(i0000_nnnn_1001_0011)
{
	// Cache operation - no-op in interpreter mode
	// const u32 n = GetN(op);
}

// ocbp @<REG_N> - Operand Cache Block Purge
sh4op(i0000_nnnn_1010_0011)
{
	// Cache operation - no-op in interpreter mode
	// const u32 n = GetN(op);
}

// ocbwb @<REG_N> - Operand Cache Block Write-Back
sh4op(i0000_nnnn_1011_0011)
{
	// Cache operation - no-op in interpreter mode
	// const u32 n = GetN(op);
}

//=============================================================================
// STORE QUEUE OPERATIONS
//=============================================================================

// Store Queue Write helper - template for MMU on/off
template<bool mmu_on>
INLINE void FASTCALL do_sqw(u32 Dest)
{
	u32* sq = (u32*)&sq_both[Dest & 0x20];
	u32 Address;

	if (mmu_on)
	{
		Address = mmu_TranslateSQW(Dest & 0xFFFFFFE0);
	}
	else
	{
		const u32 QACR = ((Dest & 0x20) == 0) ? CCN_QACR0.Area : CCN_QACR1.Area;
		Address = (Dest & 0x03FFFFE0) | (QACR << 26);
	}

	if (((Address >> 26) & 0x7) == 4)  // Area 4 - Tile Accelerator
	{
		TAWriteSQ(Address, sq);
	}
	else
	{
		WriteMemBlock_nommu_ptr(Address, sq, 32);  // 8 * 4 bytes
	}
}

void FASTCALL do_sqw_mmu(u32 dst)   { do_sqw<true>(dst); }
void FASTCALL do_sqw_nommu(u32 dst) { do_sqw<false>(dst); }

// pref @<REG_N> - Prefetch / Store Queue flush
sh4op(i0000_nnnn_1000_0011)
{
	const u32 n = GetN(op);
	const u32 Dest = r[n];

	if ((Dest >> 26) == 0x38)  // Store Queue address range
	{
		if (CCN_MMUCR.AT)
			do_sqw<true>(Dest);
		else
			do_sqw<false>(Dest);
	}
}

//=============================================================================
// STATUS REGISTER FLAG OPERATIONS
//=============================================================================

// sets - Set S bit
sh4op(i0000_0000_0101_1000)
{
	sr.S = 1;
}

// clrs - Clear S bit
sh4op(i0000_0000_0100_1000)
{
	sr.S = 0;
}

// sett - Set T bit
sh4op(i0000_0000_0001_1000)
{
	sr.T = 1;
}

// clrt - Clear T bit
sh4op(i0000_0000_0000_1000)
{
	sr.T = 0;
}

// movt <REG_N> - Move T bit to register
sh4op(i0000_nnnn_0010_1001)
{
	const u32 n = GetN(op);
	r[n] = !!sr.T;
}

//=============================================================================
// COMPARISON OPERATIONS
//=============================================================================

// cmp/pz <REG_N> - Compare greater than or equal to zero
sh4op(i0100_nnnn_0001_0001)
{
	const u32 n = GetN(op);
	sr.T = (((s32)r[n]) >= 0) ? 1 : 0;
}

// cmp/pl <REG_N> - Compare greater than zero
sh4op(i0100_nnnn_0001_0101)
{
	const u32 n = GetN(op);
	sr.T = (((s32)r[n]) > 0) ? 1 : 0;
}

// cmp/eq #<imm>,R0 - Compare immediate with R0
sh4op(i1000_1000_iiii_iiii)
{
	const u32 imm = (u32)(s32)GetSImm8(op);
	sr.T = (r[0] == imm) ? 1 : 0;
}

// cmp/eq <REG_M>,<REG_N> - Compare equal
sh4op(i0011_nnnn_mmmm_0000)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = (r[m] == r[n]) ? 1 : 0;
}

// cmp/hs <REG_M>,<REG_N> - Compare unsigned higher or same
sh4op(i0011_nnnn_mmmm_0010)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = (r[n] >= r[m]) ? 1 : 0;
}

// cmp/ge <REG_M>,<REG_N> - Compare signed greater or equal
sh4op(i0011_nnnn_mmmm_0011)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = (((s32)r[n]) >= ((s32)r[m])) ? 1 : 0;
}

// cmp/hi <REG_M>,<REG_N> - Compare unsigned higher
sh4op(i0011_nnnn_mmmm_0110)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = (r[n] > r[m]) ? 1 : 0;
}

// cmp/gt <REG_M>,<REG_N> - Compare signed greater than
sh4op(i0011_nnnn_mmmm_0111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = (((s32)r[n]) > ((s32)r[m])) ? 1 : 0;
}

// cmp/str <REG_M>,<REG_N> - Compare string (any bytes equal)
sh4op(i0010_nnnn_mmmm_1100)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 temp = r[n] ^ r[m];
	
	const u32 HH = (temp >> 24) & 0xFF;
	const u32 HL = (temp >> 16) & 0xFF;
	const u32 LH = (temp >> 8) & 0xFF;
	const u32 LL = temp & 0xFF;
	
	// T=1 if any byte is equal (any of HH,HL,LH,LL is zero)
	sr.T = (HH && HL && LH && LL) ? 0 : 1;
}

// tst #<imm>,R0 - Test immediate with R0
sh4op(i1100_1000_iiii_iiii)
{
	const u32 result = r[0] & GetImm8(op);
	sr.T = (result == 0) ? 1 : 0;
}

// tst <REG_M>,<REG_N> - Test logical AND
sh4op(i0010_nnnn_mmmm_1000)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	sr.T = ((r[n] & r[m]) == 0) ? 1 : 0;
}

//=============================================================================
// MULTIPLICATION OPERATIONS
//=============================================================================

// mulu.w <REG_M>,<REG_N> - Multiply unsigned word
sh4op(i0010_nnnn_mmmm_1110)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	macl = ((u16)r[n]) * ((u16)r[m]);
}

// muls.w <REG_M>,<REG_N> - Multiply signed word
sh4op(i0010_nnnn_mmmm_1111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	macl = (u32)(((s16)(u16)r[n]) * ((s16)(u16)r[m]));
}

// dmulu.l <REG_M>,<REG_N> - Double-length multiply unsigned
sh4op(i0011_nnnn_mmmm_0101)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u64 result = (u64)r[n] * (u64)r[m];
	
	macl = (u32)(result & 0xFFFFFFFF);
	mach = (u32)(result >> 32);
}

// dmuls.l <REG_M>,<REG_N> - Double-length multiply signed
sh4op(i0011_nnnn_mmmm_1101)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const s64 result = (s64)(s32)r[n] * (s64)(s32)r[m];
	
	macl = (u32)(result & 0xFFFFFFFF);
	mach = (u32)(result >> 32);
}

// mul.l <REG_M>,<REG_N> - Multiply long
sh4op(i0000_nnnn_mmmm_0111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	macl = (u32)(((s32)r[n]) * ((s32)r[m]));
}

// mac.w @<REG_M>+,@<REG_N>+ - Multiply-accumulate word
sh4op(i0100_nnnn_mmmm_1111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	
	if (sr.S != 0)
	{
		printf("mac.w @<REG_M>+,@<REG_N>+ : S=%d (saturation mode)\n", !!sr.S);
	}
	else
	{
		const s32 rn = (s32)(s16)ReadMem16(r[n]);
		r[n] += 2;
		
		const s32 rm = (s32)(s16)ReadMem16(r[m]);
		r[m] += 2;
		
		const s32 mul = rm * rn;
		s64 mac = macl | ((u64)mach << 32);
		mac += mul;
		
		macl = (u32)mac;
		mach = (u32)(mac >> 32);
	}
}

// mac.l @<REG_M>+,@<REG_N>+ - Multiply-accumulate long
sh4op(i0000_nnnn_mmmm_1111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	
	verify(sr.S == 0);  // Saturation not supported
	
	s32 rm, rn;
	ReadMemS32(rm, r[m]);
	r[m] += 4;
	ReadMemS32(rn, r[n]);
	r[n] += 4;
	
	const s64 mul = (s64)rm * (s64)rn;
	s64 mac = macl | ((u64)mach << 32);
	const s64 result = mac + mul;
	
	macl = (u32)result;
	mach = (u32)(result >> 32);
}

//=============================================================================
// DIVISION OPERATIONS
//=============================================================================

// div0u - Initialize division (unsigned)
sh4op(i0000_0000_0001_1001)
{
	sr.Q = 0;
	sr.M = 0;
	sr.T = 0;
}

// div0s <REG_M>,<REG_N> - Initialize division (signed)
sh4op(i0010_nnnn_mmmm_0111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	
	sr.Q = (r[n] >> 31) & 1;
	sr.M = (r[m] >> 31) & 1;
	sr.T = sr.Q ^ sr.M;
}

// div1 <REG_M>,<REG_N> - 1-step division
sh4op(i0011_nnnn_mmmm_0100)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	
	const u32 old_Q = sr.Q;

	r[n] = (r[n] << 1) | sr.T;
	
	u32 tmp1;
	if (old_Q == 0)
	{
		tmp1 = r[n];
		r[n] -= r[m];
	}
	else
	{
		tmp1 = r[n];
		r[n] += r[m];
	}
	
	sr.Q = (r[n] > tmp1) ? 1 : 0;
	
	if (sr.Q == sr.M)
		sr.T = 1;
	else
		sr.T = 0;
}

//=============================================================================
// ARITHMETIC WITH CARRY/OVERFLOW
//=============================================================================

// addc <REG_M>,<REG_N> - Add with carry
sh4op(i0011_nnnn_mmmm_1110)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 tmp0 = r[n];
	const u32 tmp1 = r[n] + r[m];
	
	r[n] = tmp1 + !!sr.T;
	
	sr.T = ((tmp0 > tmp1) || (tmp1 > r[n])) ? 1 : 0;
}

// addv <REG_M>,<REG_N> - Add with overflow check
sh4op(i0011_nnnn_mmmm_1111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const s32 dest = (s32)r[n];
	const s32 src = (s32)r[m];
	const s32 ans = dest + src;
	
	r[n] = (u32)ans;
	
	// Overflow if sign of operands same but result differs
	if ((dest >= 0 && src >= 0 && ans < 0) || (dest < 0 && src < 0 && ans >= 0))
		sr.T = 1;
	else
		sr.T = 0;
}

// subc <REG_M>,<REG_N> - Subtract with carry
sh4op(i0011_nnnn_mmmm_1010)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 tmp0 = r[n];
	const u32 tmp1 = r[n] - r[m];
	
	r[n] = tmp1 - !!sr.T;
	
	sr.T = ((tmp0 < tmp1) || (tmp1 < r[n])) ? 1 : 0;
}

// subv <REG_M>,<REG_N> - Subtract with overflow check
sh4op(i0011_nnnn_mmmm_1011)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const s32 dest = (s32)r[n];
	const s32 src = (s32)r[m];
	const s32 ans = dest - src;
	
	r[n] = (u32)ans;
	
	// Overflow if signs differ and result sign matches source
	if ((dest >= 0 && src < 0 && ans < 0) || (dest < 0 && src >= 0 && ans >= 0))
		sr.T = 1;
	else
		sr.T = 0;
}

// dt <REG_N> - Decrement and test
sh4op(i0100_nnnn_0001_0000)
{
	const u32 n = GetN(op);
	r[n] -= 1;
	sr.T = (r[n] == 0) ? 1 : 0;
}

// negc <REG_M>,<REG_N> - Negate with carry
sh4op(i0110_nnnn_mmmm_1010)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 tmp = 0 - r[m];
	
	r[n] = tmp - !!sr.T;
	
	sr.T = ((0 < tmp) || (tmp < r[n])) ? 1 : 0;
}

// neg <REG_M>,<REG_N> - Negate
sh4op(i0110_nnnn_mmmm_1011)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = -r[m];
}

// not <REG_M>,<REG_N> - Bitwise NOT
sh4op(i0110_nnnn_mmmm_0111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = ~r[m];
}

//=============================================================================
// SHIFT OPERATIONS (SINGLE BIT)
//=============================================================================

// shll <REG_N> - Shift left logical 1 bit
sh4op(i0100_nnnn_0000_0000)
{
	const u32 n = GetN(op);
	sr.T = r[n] >> 31;
	r[n] <<= 1;
}

// shal <REG_N> - Shift left arithmetic 1 bit
sh4op(i0100_nnnn_0010_0000)
{
	const u32 n = GetN(op);
	sr.T = r[n] >> 31;
	r[n] = ((s32)r[n]) << 1;
}

// shlr <REG_N> - Shift right logical 1 bit
sh4op(i0100_nnnn_0000_0001)
{
	const u32 n = GetN(op);
	sr.T = r[n] & 0x1;
	r[n] >>= 1;
}

// shar <REG_N> - Shift right arithmetic 1 bit
sh4op(i0100_nnnn_0010_0001)
{
	const u32 n = GetN(op);
	sr.T = r[n] & 1;
	r[n] = ((s32)r[n]) >> 1;
}

//=============================================================================
// DYNAMIC SHIFT OPERATIONS
//=============================================================================

// shad <REG_M>,<REG_N> - Shift arithmetic dynamic
sh4op(i0100_nnnn_mmmm_1100)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 sgn = r[m] & 0x80000000;
	
	if (sgn == 0)
	{
		r[n] <<= (r[m] & 0x1F);
	}
	else if ((r[m] & 0x1F) == 0)
	{
		r[n] = ((s32)r[n] < 0) ? 0xFFFFFFFF : 0;
	}
	else
	{
		r[n] = ((s32)r[n]) >> ((~r[m] & 0x1F) + 1);
	}
}

// shld <REG_M>,<REG_N> - Shift logical dynamic
sh4op(i0100_nnnn_mmmm_1101)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u32 sgn = r[m] & 0x80000000;
	
	if (sgn == 0)
	{
		r[n] <<= (r[m] & 0x1F);
	}
	else if ((r[m] & 0x1F) == 0)
	{
		r[n] = 0;
	}
	else
	{
		r[n] >>= ((~r[m] & 0x1F) + 1);
	}
}

//=============================================================================
// ROTATE OPERATIONS
//=============================================================================

// rotcl <REG_N> - Rotate left through carry
sh4op(i0100_nnnn_0010_0100)
{
	const u32 n = GetN(op);
	const u32 t = !!sr.T;
	
	sr.T = r[n] >> 31;
	r[n] = (r[n] << 1) | t;
}

// rotl <REG_N> - Rotate left
sh4op(i0100_nnnn_0000_0100)
{
	const u32 n = GetN(op);
	sr.T = r[n] >> 31;
	r[n] = (r[n] << 1) | (!!sr.T);
}

// rotcr <REG_N> - Rotate right through carry
sh4op(i0100_nnnn_0010_0101)
{
	const u32 n = GetN(op);
	const u32 t = r[n] & 0x1;
	
	r[n] = (r[n] >> 1) | ((!!sr.T) << 31);
	sr.T = t;
}

// rotr <REG_N> - Rotate right
sh4op(i0100_nnnn_0000_0101)
{
	const u32 n = GetN(op);
	sr.T = r[n] & 0x1;
	r[n] = (r[n] >> 1) | ((!!sr.T) << 31);
}

//=============================================================================
// BYTE/WORD MANIPULATION
//=============================================================================

// swap.b <REG_M>,<REG_N> - Swap lower 2 bytes
sh4op(i0110_nnnn_mmmm_1000)
{
	const u32 m = GetM(op);
	const u32 n = GetN(op);
	r[n] = (r[m] & 0xFFFF0000) | ((r[m] & 0xFF) << 8) | ((r[m] >> 8) & 0xFF);
}

// swap.w <REG_M>,<REG_N> - Swap words
sh4op(i0110_nnnn_mmmm_1001)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	const u16 t = (u16)(r[m] >> 16);
	r[n] = (r[m] << 16) | t;
}

// extu.b <REG_M>,<REG_N> - Zero-extend byte
sh4op(i0110_nnnn_mmmm_1100)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = (u32)(u8)r[m];
}

// extu.w <REG_M>,<REG_N> - Zero-extend word
sh4op(i0110_nnnn_mmmm_1101)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = (u32)(u16)r[m];
}

// exts.b <REG_M>,<REG_N> - Sign-extend byte
sh4op(i0110_nnnn_mmmm_1110)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = (u32)(s32)(s8)(u8)r[m];
}

// exts.w <REG_M>,<REG_N> - Sign-extend word
sh4op(i0110_nnnn_mmmm_1111)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = (u32)(s32)(s16)(u16)r[m];
}

// xtrct <REG_M>,<REG_N> - Extract middle 32 bits
sh4op(i0010_nnnn_mmmm_1101)
{
	const u32 n = GetN(op);
	const u32 m = GetM(op);
	r[n] = ((r[n] >> 16) & 0xFFFF) | ((r[m] << 16) & 0xFFFF0000);
}

//=============================================================================
// MEMORY-BASED LOGICAL OPERATIONS (GBR relative)
//=============================================================================

// tst.b #<imm>,@(R0,GBR)
sh4op(i1100_1100_iiii_iiii)
{
	iNimp(op, "tst.b #<imm>,@(R0,GBR)");
}

// and.b #<imm>,@(R0,GBR)
sh4op(i1100_1101_iiii_iiii)
{
	iNimp(op, "and.b #<imm>,@(R0,GBR)");
}

// xor.b #<imm>,@(R0,GBR)
sh4op(i1100_1110_iiii_iiii)
{
	iNimp(op, "xor.b #<imm>,@(R0,GBR)");
}

// or.b #<imm>,@(R0,GBR)
sh4op(i1100_1111_iiii_iiii)
{
	const u32 addr = gbr + r[0];
	u8 temp = (u8)ReadMem8(addr);
	temp |= GetImm8(op);
	WriteMem8(addr, temp);
}

// tas.b @<REG_N> - Test and set
sh4op(i0100_nnnn_0001_1011)
{
	const u32 n = GetN(op);
	const u8 val = (u8)ReadMem8(r[n]);
	
	sr.T = (val == 0) ? 1 : 0;
	WriteMem8(r[n], val | 0x80);
}

//=============================================================================
// STATUS REGISTER OPERATIONS
//=============================================================================

// stc SR,<REG_N>
sh4op(i0000_nnnn_0000_0010)
{
	const u32 n = GetN(op);
	r[n] = sr.GetFull();
}

// sts FPSCR,<REG_N>
sh4op(i0000_nnnn_0110_1010)
{
	const u32 n = GetN(op);
	r[n] = fpscr.full;
	UpdateFPSCR();
}

// sts.l FPSCR,@-<REG_N>
sh4op(i0100_nnnn_0110_0010)
{
	const u32 n = GetN(op);
	r[n] -= 4;
	WriteMemU32(r[n], fpscr.full);
}

// stc.l SR,@-<REG_N>
sh4op(i0100_nnnn_0000_0011)
{
	const u32 n = GetN(op);
	r[n] -= 4;
	WriteMemU32(r[n], sr.GetFull());
}

// lds.l @<REG_N>+,FPSCR
sh4op(i0100_nnnn_0110_0110)
{
	const u32 n = GetN(op);
	ReadMemU32(fpscr.full, r[n]);
	UpdateFPSCR();
	r[n] += 4;
}

// ldc.l @<REG_N>+,SR
sh4op(i0100_nnnn_0000_0111)
{
	const u32 n = GetN(op);
	u32 sr_t;
	ReadMemU32(sr_t, r[n]);
	sr.SetFull(sr_t);
	r[n] += 4;
	
	if (UpdateSR())
	{
		UpdateINTC();
	}
}

// lds <REG_N>,FPSCR
sh4op(i0100_nnnn_0110_1010)
{
	const u32 n = GetN(op);
	fpscr.full = r[n];
	UpdateFPSCR();
}

// ldc <REG_N>,SR
sh4op(i0100_nnnn_0000_1110)
{
	const u32 n = GetN(op);
	sr.SetFull(r[n]);
	
	if (UpdateSR())
	{
		UpdateINTC();
	}
}

//=============================================================================
// UNIMPLEMENTED / ERROR HANDLERS
//=============================================================================

// Unknown/unimplemented opcode
sh4op(iNotImplemented)
{
	cpu_iNimp(op, "Unknown opcode");
}

// GDROM HLE operation (not supported)
sh4op(gdrom_hle_op)
{
	// EMUERROR("GDROM HLE NOT SUPPORTED");
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("GDROM HLE NOT SUPPORTED\n");
}
