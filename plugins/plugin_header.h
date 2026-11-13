#pragma once
#include "config.h"
#include <cstddef>

#define _PLUGIN_HEADER_

#if HOST_OS==OS_PSP
	#define ALIGN(x) __attribute__((aligned(x)))
	#define FASTCALL
	#define fastcall
	void __debugbreak();
	#include <pspkernel.h>
	#include <psputilsforkernel.h>
	#include <pspsysmem_kernel.h>
	#include <pspnet.h>
#elif HOST_OS==OS_PS2
	#define ALIGN(x) __attribute__((aligned(x)))
	#define FASTCALL
	#define fastcall
	void __debugbreak();
	#include <tamtypes.h>
#elif HOST_OS==OS_LINUX
	#include "linux/typedefs.h"
#elif HOST_OS==OS_WII
	#include <gccore.h>
	#include <stdint.h>
	#include <cstddef>
	typedef int8_t   s8;
	typedef int16_t  s16;
	typedef int32_t  s32;
	typedef int64_t  s64;
	typedef uint8_t  u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	#define FASTCALL
	#define fastcall
	inline void __debugbreak() {}
	#define ALIGN(x) __attribute__((aligned(x)))
#elif HOST_OS==OS_WIIU
	// <gccore.h> here is NOT libogc: wiiu/Makefile puts wiiu/compat/ first on
	// the include path, and its gccore.h maps the few libogc calls the shared
	// core makes (arena, IRQ, cache, time base) onto Cafe OS / wut.
	#include <gccore.h>
	#include <stdint.h>
	#include <cstddef>
	typedef int8_t   s8;
	typedef int16_t  s16;
	typedef int32_t  s32;
	typedef int64_t  s64;
	typedef uint8_t  u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	#define FASTCALL
	#define fastcall
	inline void __debugbreak() {}
	#define ALIGN(x) __attribute__((aligned(x)))
#elif HOST_OS==OS_PS3
	#include "linux/typedefs.h"
#elif HOST_OS==OS_WINDOWS
	#include <stdint.h>
	#include <cstddef>
	#ifdef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
	#undef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
	#endif

	#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1

	#ifdef _CRT_SECURE_NO_DEPRECATE
	#undef _CRT_SECURE_NO_DEPRECATE
	#endif
	#define _CRT_SECURE_NO_DEPRECATE

	#define ALIGN(x) __declspec(align(x))
	//basic types
	typedef signed __int8  s8;
	typedef signed __int16 s16;
	typedef signed __int32 s32;
	typedef signed __int64 s64;

	typedef unsigned __int8  u8;
	typedef unsigned __int16 u16;
	typedef unsigned __int32 u32;
	typedef unsigned __int64 u64;
	#define FASTCALL __fastcall
	#define fastcall __fastcall
#else
	#error HOST_OS NOT SUPPORTED
#endif

typedef float f32;
typedef double f64;

#if HOST_ARCH==ARCH_X64
	typedef u64 unat;
	typedef s64 snat;
#else
	typedef size_t unat;
	typedef ptrdiff_t snat;
#endif

typedef char wchar;

#if HOST_OS==OS_WINDOWS
	#define EXPORT extern "C" __declspec(dllexport)
#else
	#define EXPORT extern "C"
#endif

#define EXPORT_CALL



#ifndef CDECL
#define CDECL __cdecl
#endif

#define ALIGN16  ALIGN(16)
#define ALIGN32  ALIGN(32)
#define ALIGN64  ALIGN(64)


//intc function pointer and enums
enum HollyInterruptType
{
	holly_nrm = 0x0000,
	holly_ext = 0x0100,
	holly_err = 0x0200,
};

enum HollyInterruptID
{
		// asic9a /sh4 external holly normal [internal]
		holly_RENDER_DONE_vd = holly_nrm | 0,	//bit 0 = End of Render interrupt : Video
		holly_RENDER_DONE_isp = holly_nrm | 1,	//bit 1 = End of Render interrupt : ISP
		holly_RENDER_DONE = holly_nrm | 2,		//bit 2 = End of Render interrupt : TSP

		holly_SCANINT1 = holly_nrm | 3,			//bit 3 = V Blank-in interrupt
		holly_SCANINT2 = holly_nrm | 4,			//bit 4 = V Blank-out interrupt
		holly_HBLank = holly_nrm | 5,			//bit 5 = H Blank-in interrupt

		holly_YUV_DMA = holly_nrm | 6,			//bit 6 = End of Transferring interrupt : YUV
		holly_OPAQUE = holly_nrm | 7,			//bit 7 = End of Transferring interrupt : Opaque List
		holly_OPAQUEMOD = holly_nrm | 8,		//bit 8 = End of Transferring interrupt : Opaque Modifier Volume List

		holly_TRANS = holly_nrm | 9,			//bit 9 = End of Transferring interrupt : Translucent List
		holly_TRANSMOD = holly_nrm | 10,		//bit 10 = End of Transferring interrupt : Translucent Modifier Volume List
		holly_PVR_DMA = holly_nrm | 11,			//bit 11 = End of DMA interrupt : PVR-DMA
		holly_MAPLE_DMA = holly_nrm | 12,		//bit 12 = End of DMA interrupt : Maple-DMA

		holly_MAPLE_VBOI = holly_nrm | 13,		//bit 13 = Maple V blank over interrupt
		holly_GDROM_DMA = holly_nrm | 14,		//bit 14 = End of DMA interrupt : GD-DMA
		holly_SPU_DMA = holly_nrm | 15,			//bit 15 = End of DMA interrupt : AICA-DMA

		holly_EXT_DMA1 = holly_nrm | 16,		//bit 16 = End of DMA interrupt : Ext-DMA1(External 1)
		holly_EXT_DMA2 = holly_nrm | 17,		//bit 17 = End of DMA interrupt : Ext-DMA2(External 2)
		holly_DEV_DMA = holly_nrm | 18,			//bit 18 = End of DMA interrupt : Dev-DMA(Development tool DMA)

		holly_CH2_DMA = holly_nrm | 19,			//bit 19 = End of DMA interrupt : ch2-DMA
		holly_PVR_SortDMA = holly_nrm | 20,		//bit 20 = End of DMA interrupt : Sort-DMA (Transferring for alpha sorting)
		holly_PUNCHTHRU = holly_nrm | 21,		//bit 21 = End of Transferring interrupt : Punch Through List

		// asic9c/sh4 external holly external [EXTERNAL]
		holly_GDROM_CMD = holly_ext | 0x00,	//bit 0 = GD-ROM interrupt
		holly_SPU_IRQ = holly_ext | 0x01,	//bit 1 = AICA interrupt
		holly_EXP_8BIT = holly_ext | 0x02,	//bit 2 = Modem interrupt
		holly_EXP_PCI = holly_ext | 0x03,	//bit 3 = External Device interrupt

		// asic9b/sh4 external holly err only error [error]
		//missing quite a few ehh ?
		//bit 0 = RENDER : ISP out of Cache(Buffer over flow)
		//bit 1 = RENDER : Hazard Processing of Strip Buffer
		holly_PRIM_NOMEM = holly_err | 0x02,	//bit 2 = TA : ISP/TSP Parameter Overflow
		holly_MATR_NOMEM = holly_err | 0x03,	//bit 3 = TA : Object List Pointer Overflow
		//bit 4 = TA : Illegal Parameter
		//bit 5 = TA : FIFO Overflow
		//bit 6 = PVRIF : Illegal Address set
		//bit 7 = PVRIF : DMA over run
		//bit 8 = MAPLE : Illegal Address set
		//bit 9 = MAPLE : DMA over run
		//bit 10 = MAPLE : Write FIFO over flow
		//bit 11 = MAPLE : Illegal command
		//bit 12 = G1 : Illegal Address set
		//bit 13 = G1 : GD-DMA over run
		//bit 14 = G1 : ROM/FLASH access at GD-DMA
		//bit 15 = G2 : AICA-DMA Illegal Address set
		//bit 16 = G2 : Ext-DMA1 Illegal Address set
		//bit 17 = G2 : Ext-DMA2 Illegal Address set
		//bit 18 = G2 : Dev-DMA Illegal Address set
		//bit 19 = G2 : AICA-DMA over run
		//bit 20 = G2 : Ext-DMA1 over run
		//bit 21 = G2 : Ext-DMA2 over run
		//bit 22 = G2 : Dev-DMA over run
		//bit 23 = G2 : AICA-DMA Time out
		//bit 24 = G2 : Ext-DMA1 Time out
		//bit 25 = G2 : Ext-DMA2 Time out
		//bit 26 = G2 : Dev-DMA Time out
		//bit 27 = G2 : Time out in CPU accessing
};



typedef void FASTCALL HollyRaiseInterruptFP(HollyInterruptID intr);
typedef void FASTCALL HollyCancelInterruptFP(HollyInterruptID intr);


struct vram_block
{
	u32 start;
	u32 end;
	u32 len;
	u32 type;

	void* userdata;
};

typedef void FASTCALL vramLockCBFP (vram_block* block,u32 addr);


#include "plugin_types.h"

template<typename T>
static inline T* host_ptr_xor(T* x)
{
	#if HOST_ENDIAN == ENDIAN_BIG
		unat tmp=(unat)x;
		tmp^=4-sizeof(T);
		return (T*)tmp;
	#else
		return x;
	#endif
}

#if HOST_ENDIAN == ENDIAN_BIG
	#define HOST_TO_LE64(x) ((HOST_TO_LE32((x&0xFFFFFFFF))<<32) | HOST_TO_LE32((x>>32)))
	#define HOST_TO_LE32(a) \
	((((a) & 0xff) << 24)  | (((a) & 0xff00) << 8) | \
	 (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))

	#define HOST_TO_LE16(x) ((u8)(x>>8)|((u8)x<<8))

	#define HOST_PTR_LE8_XOR(x) host_ptr_xor((u8*)(x))
	#define HOST_PTR_LE16_XOR(x) host_ptr_xor((u16*)(x))
#else
	#define HOST_TO_LE64(x) (x)
	#define HOST_TO_LE32(x) (x)
	#define HOST_TO_LE16(x) (x)
	
	#define HOST_PTR_LE8_XOR(x) (x)
	#define HOST_PTR_LE16_XOR(x) (x)
#endif

template <u32 sz,typename T>
T host_to_le(T v)
{
	if (sz==1)
		return v;
	else if (sz==2)
		return HOST_TO_LE16(v);
	else if (sz==4)
		return HOST_TO_LE32(v);
	else if (sz==8)
		return HOST_TO_LE64(v);
#ifdef die
	else
		die("i'm dead");
#endif
}
