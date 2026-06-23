/*
	Config header file to properly generate all the #defines from simple _SYSTEM defines on makefiles :)
*/

#pragma once

//Basic config values

//OSes
#define OS_PSP      1
#define OS_WINDOWS  3
#define OS_LINUX    4
#define OS_WII      5
#define OS_XBOX     6
#define OS_PS2      7
#define OS_PS3      8

//Architectures (generic)
#define ARCH_ARM 1
#define ARCH_X86 2
#define ARCH_X64 3
#define ARCH_MIPS 4
#define ARCH_PPC 5

//Cpu model (specific)
#define CPU_X86_GENERIC 1	//specialised runtime checks for sse support
#define CPU_MIPS_ALLEGREX 2	//psp
#define CPU_ARM_CORTEX_A8 3	//Beagleboard/Pandora/Omap3*
#define CPU_MIPS_5900 4		//ps2
#define CPU_PPC_BROADWAY 5	//wii
#define CPU_PPC_CELL 6		//ps3
#define CPU_ARMV5	7	//For android testing, for now

//host hardware
#define SYS_PC			1	//PC (win/linux)
#define SYS_PSP			2
#define SYS_PANDORA		3
#define SYS_BEAGLE		4
#define SYS_WII			5
#define SYS_PS3			6
#define SYS_PS2			7

//host endian
#define ENDIAN_LITTLE	1
#define ENDIAN_BIG	2

//these MUST NOT be used.
#define BIG_ENDIAN "wrong endian" qwerpsdoija dfa sd fas;df asd;f
#define LITTLE_ENDIAN "wrong endian" qwerpsdoija dfa sd fas;df asd;f

//compiler used
#define COMPILER_GCC 1
#define COMPILER_VC 2

//Renderer used
#define REND_PSP    1
#define REND_GLES2  2
#define REND_SOFT   3   // Or .. something [ null atm ]
#define REND_WII	4
#define REND_PS2	5


// Add defs to IDE/makefile whatever as _cpuname, _osname
// at the very end if they still aren't defined it will build for x86/windows

#ifdef _PANDORA
	#define _ARM
	#define _LINUX
	#define HOST_CPU CPU_ARM_CORTEX_A8
	#define HOST_SYS SYS_PANDORA
	#define REND_API REND_GLES2
	#define VER_TARGET "pandora"
#endif

#ifdef _BEAGLE
	#define _ARM
	#define _LINUX
	#define HOST_CPU CPU_ARM_CORTEX_A8
	#define HOST_SYS SYS_BEAGLE
	#define REND_API REND_GLES2
	#define VER_TARGET "beagly"
#endif

#ifdef _ANDROID
	#define _ARM
	#define _LINUX
	#define HOST_CPU CPU_ARM_CORTEX_A8
	#define HOST_SYS SYS_BEAGLE
	#define REND_API REND_GLES2
	#define VER_TARGET "android/arm"
	//#define USE_INTERP 1
#endif

#ifdef _WIN86
	#define _X86
	#define _WINDOWS
	#define HOST_CPU CPU_X86_GENERIC
	#define HOST_SYS SYS_PC
	#ifdef _PSPEMU
		#define REND_API REND_PSP
	#else
		#define REND_API REND_GLES2
	#endif
	#define VER_TARGET "win86"
#endif

#ifdef _LIN86
	#define _X86
	#define _WINDOWS
	#define HOST_CPU CPU_X86_GENERIC
	#define HOST_SYS SYS_PC
	#define REND_API REND_GLES2
	#define VER_TARGET "linux86"
#endif

#ifdef _WII
	#define _PPC
	#define HOST_CPU CPU_PPC_BROADWAY
	#define HOST_SYS SYS_WII
	#define REND_API REND_WII
	#define VER_TARGET "wii"
#endif

#ifdef _PSP
	#define _MIPS
	#define HOST_CPU CPU_MIPS_ALLEGREX
	#define HOST_SYS SYS_PSP
	#define REND_API REND_PSP
	#define VER_TARGET "psp"
#endif

#ifdef _PS2
	#define _MIPS
	#define HOST_CPU CPU_MIPS_5900
	#define HOST_SYS SYS_PS2
	#define REND_API REND_PS2
	#define VER_TARGET "ps2"
#endif

#ifdef _PS3
	#define _PPC
	#define HOST_CPU CPU_PPC_CELL
	#define HOST_SYS SYS_PS3
	#define HOST_OS OS_PS3
#ifndef CELL_SDK
	#define REND_API REND_SOFT
#else
	#define REND_API REND_GLES2
#endif
	#define VER_TARGET "ps3"
#endif

#ifndef HOST_ARCH

#ifdef _ARM
	#define HOST_ARCH ARCH_ARM
	#define HOST_ENDIAN ENDIAN_LITTLE
#endif

#ifdef _PPC
	#define HOST_ARCH ARCH_PPC
	#define HOST_ENDIAN ENDIAN_BIG
#endif

#ifdef _MIPS
	#define HOST_ARCH ARCH_MIPS
	#define HOST_ENDIAN ENDIAN_LITTLE
#endif

#ifdef _X64
	#define HOST_ARCH ARCH_X64
	#define HOST_ENDIAN ENDIAN_LITTLE
#endif

#ifdef _X86
	#define HOST_ARCH ARCH_X86
	#define HOST_ENDIAN ENDIAN_LITTLE
#endif

#endif // HOST_CPU




#ifndef HOST_OS

#ifdef _LINUX
#define HOST_OS OS_LINUX
#endif

#ifdef _WINDOWS
#define HOST_OS OS_WINDOWS
#endif

#ifdef _PSP
#define HOST_OS OS_PSP
#endif

#ifdef _WII
#define HOST_OS OS_WII
#endif

#ifdef _XBOX
#define HOST_OS OS_XBOX
#endif

#ifdef _PS2
#define HOST_OS OS_PS2
#define USE_INTERP
#endif

#endif // HOST_OS


//defaults
#ifndef HOST_ENDIAN
#define HOST_ENDIAN ENDIAN_LITTLE
#endif

#ifndef HOST_OS
#define HOST_OS OS_WINDOWS
#endif

#ifndef HOST_CPU
#define HOST_CPU CPU_X86
#endif

// ---- Sanity check: does HOST_ENDIAN actually match the real compiler/CPU? ----
// See wii/config.h for the full explanation. Mirrored here because this is a
// SEPARATE file from wii/config.h (same name, different folder) — anything
// outside wii/ that does #include "config.h" falls through to this copy via
// -I../, so it needs its own copy of the check, not just the wii one.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__)
	#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		#if HOST_ENDIAN != ENDIAN_BIG
			#error "Compiler reports BIG-ENDIAN target, but HOST_ENDIAN resolved to ENDIAN_LITTLE. Check that _WII (or your platform's macro) is actually defined in the Makefile/build flags."
		#endif
	#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		#if HOST_ENDIAN != ENDIAN_LITTLE
			#error "Compiler reports LITTLE-ENDIAN target, but HOST_ENDIAN resolved to ENDIAN_BIG. Check your platform defines."
		#endif
	#endif
#elif defined(__BIG_ENDIAN__)
	#if HOST_ENDIAN != ENDIAN_BIG
		#error "Compiler defines __BIG_ENDIAN__, but HOST_ENDIAN resolved to ENDIAN_LITTLE. Check that _WII (or your platform's macro) is actually defined in the Makefile/build flags."
	#endif
#elif defined(__LITTLE_ENDIAN__)
	#if HOST_ENDIAN != ENDIAN_LITTLE
		#error "Compiler defines __LITTLE_ENDIAN__, but HOST_ENDIAN resolved to ENDIAN_BIG. Check your platform defines."
	#endif
#endif
// -------------------------------------------------------------------------------


#if HOST_ARCH==ARCH_PPC && HOST_CPU!=CPU_PPC_BROADWAY
#define USE_INTERP
#endif

#ifdef USE_INTERP
#define HOST_NO_REC 1
#endif


#if HOST_CPU==CPU_X64
#define X64
#endif


#ifndef BUILD_COMPILER
	#if HOST_OS==OS_WINDOWS
		#define BUILD_COMPILER COMPILER_VC
	#else
		#define BUILD_COMPILER COMPILER_GCC
	#endif
#endif

#ifndef VER_TARGET
#define VER_TARGET "?"
#endif


#ifndef REND_API
    #define REND_API REND_SOFT
#endif
