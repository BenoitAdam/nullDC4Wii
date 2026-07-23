#pragma once
#include "types.h"
#include "sh4_if.h"

struct Sh4Context
{
	f32 xf[16];
	f32 fr[16];

	// Scratch bounce buffer used by the native recompiler for int<->float
	// conversions (fctiwz / integer-to-double magic-constant trick). Not an
	// SH4 architectural register.
	//
	// Placed HERE (right after the FP register files, offset 128 — 8-byte
	// aligned so lfd/stfd are well-formed) rather than at the tail of the
	// struct on purpose: the cvt_f2i_t / cvt_i2f_* JIT sequences bounce a
	// value between an FP register (xf/fr, offsets 0..127) and this slot on
	// every conversion. Keeping it inside the hot, perpetually-resident
	// register-file region means the bounce hits an already-cached L1 line
	// instead of faulting in an otherwise-cold line ~160 bytes away. All
	// accesses are via offsetof()/offset(), so the field may live anywhere;
	// only the 8-byte alignment matters. Move it back below old_fpscr to A/B.
	u64 jit_scratch;

	u32 r[16];

	u32 r_bank[8];

	u32 gbr,ssr,spc,sgr,dbr,vbr;
	u32 mach,macl,pr,fpul;
	u32 pc;
	//u16* pc_ptr;

	sr_type sr;
	fpscr_type fpscr;
	sr_type old_sr;
	fpscr_type old_fpscr;

	u32 offset(u32 sh4_reg);
	u32 offset(Sh4RegType sh4_reg) { return offset((u32)sh4_reg); }
};



extern ALIGN(64) Sh4Context Sh4cntx;
#define r Sh4cntx.r
#define r_bank Sh4cntx.r_bank
#define gbr Sh4cntx.gbr
#define ssr Sh4cntx.ssr
#define spc Sh4cntx.spc
#define sgr Sh4cntx.sgr
#define dbr Sh4cntx.dbr
#define vbr Sh4cntx.vbr
#define mach Sh4cntx.mach
#define macl Sh4cntx.macl
#define pr Sh4cntx.pr
#define fpul Sh4cntx.fpul
#define next_pc Sh4cntx.pc
#define curr_pc (next_pc-2)
#define sr Sh4cntx.sr
#define fpscr Sh4cntx.fpscr
#define old_sr Sh4cntx.old_sr
#define old_fpscr Sh4cntx.old_fpscr
#define fr Sh4cntx.fr
#define xf Sh4cntx.xf
#define fr_hex ((u32*)(fr))
#define xf_hex ((u32*)(xf))
#define dr_hex ((u64*)(fr))
#define xd_hex ((u64*)(xf))



void UpdateFPSCR();
bool UpdateSR();

union DoubleReg
{
	f64 dbl;
	f32 sgl[2];
};

INLINE f64 GetDR(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("DR_r INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;

	#if HOST_ENDIAN==ENDIAN_BIG
		t.sgl[0]=fr[(n<<1) + 0];
		t.sgl[1]=fr[(n<<1) + 1];
	#else
		t.sgl[1]=fr[(n<<1) + 0];
		t.sgl[0]=fr[(n<<1) + 1];
	#endif

	return t.dbl;
}

INLINE f64 GetXD(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("XD_r INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;
	#if HOST_ENDIAN==ENDIAN_BIG
		t.sgl[0]=xf[(n<<1) + 0];
		t.sgl[1]=xf[(n<<1) + 1];
	#else
		t.sgl[1]=xf[(n<<1) + 0];
		t.sgl[0]=xf[(n<<1) + 1];
	#endif

	return t.dbl;
}

INLINE void SetDR(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("DR_w INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;
	t.dbl=val;

	#if HOST_ENDIAN==ENDIAN_BIG
		fr[(n<<1) | 0]=t.sgl[0];
		fr[(n<<1) | 1]=t.sgl[1];
	#else
		fr[(n<<1) | 1]=t.sgl[0];
		fr[(n<<1) | 0]=t.sgl[1];
	#endif
}

INLINE void SetXD(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("XD_w INDEX OVERRUN %d >7\n",n);
#endif

	DoubleReg t;
	t.dbl=val;
	#if HOST_ENDIAN==ENDIAN_BIG
		xf[(n<<1) | 0]=t.sgl[0];
		xf[(n<<1) | 1]=t.sgl[1];
	#else
		xf[(n<<1) | 1]=t.sgl[0];
		xf[(n<<1) | 0]=t.sgl[1];
	#endif
}
//needs to be removed
u32* Sh4_int_GetRegisterPtr(Sh4RegType reg);
//needs to be made portable
void SetFloatStatusReg();
extern f32 sin_table[0x10000+0x4000];