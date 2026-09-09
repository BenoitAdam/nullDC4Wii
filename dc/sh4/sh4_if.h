/**
 * @file sh4_if.h
 * @brief SH4 CPU Interface for Dreamcast Emulator (Wii Port)
 * @author Original code with Wii optimizations
 * @date 2025
 * 
 * This header defines the interface for the SH4 CPU emulation, including
 * register definitions, control structures, and function pointers for
 * both interpreter and dynarec implementations.
 */

#pragma once

#include "types.h"
#include "intc_types.h"

// Breakpoint opcode definition
#define BPT_OPCODE 0x8A00

/**
 * @enum Sh4RegType
 * @brief Enumeration of all SH4 registers (physical and virtual)
 * 
 * This enum defines all register types used by the SH4 CPU, including:
 * - General Purpose Registers (GPRs)
 * - Floating Point Unit registers (FPU)
 * - System/Control registers
 * - Virtual registers for dynarec optimization
 */
enum Sh4RegType
{
	// General Purpose Registers (r0-r15)
	reg_r0 = 0,
	reg_r1,
	reg_r2,
	reg_r3,
	reg_r4,
	reg_r5,
	reg_r6,
	reg_r7,
	reg_r8,
	reg_r9,
	reg_r10,
	reg_r11,
	reg_r12,
	reg_r13,
	reg_r14,
	reg_r15,

	// FPU Bank 0 (fr0-fr15)
	reg_fr_0,
	reg_fr_1,
	reg_fr_2,
	reg_fr_3,
	reg_fr_4,
	reg_fr_5,
	reg_fr_6,
	reg_fr_7,
	reg_fr_8,
	reg_fr_9,
	reg_fr_10,
	reg_fr_11,
	reg_fr_12,
	reg_fr_13,
	reg_fr_14,
	reg_fr_15,

	// FPU Bank 1 (xf0-xf15)
	reg_xf_0,
	reg_xf_1,
	reg_xf_2,
	reg_xf_3,
	reg_xf_4,
	reg_xf_5,
	reg_xf_6,
	reg_xf_7,
	reg_xf_8,
	reg_xf_9,
	reg_xf_10,
	reg_xf_11,
	reg_xf_12,
	reg_xf_13,
	reg_xf_14,
	reg_xf_15,

	// GPR Interrupt Bank (r0_Bank-r7_Bank)
	reg_r0_Bank,
	reg_r1_Bank,
	reg_r2_Bank,
	reg_r3_Bank,
	reg_r4_Bank,
	reg_r5_Bank,
	reg_r6_Bank,
	reg_r7_Bank,

	// System/Control Registers
	reg_gbr,        // Global Base Register
	reg_ssr,        // Saved Status Register
	reg_spc,        // Saved Program Counter
	reg_sgr,        // Saved General Register
	reg_dbr,        // Debug Base Register
	reg_vbr,        // Vector Base Register
	reg_mach,       // Multiply-Accumulate High
	reg_macl,       // Multiply-Accumulate Low
	reg_pr,         // Procedure Register
	reg_fpul,       // Floating Point Communication Register
	reg_nextpc,     // Next Program Counter
	reg_sr,         // Status Register (includes T bit)
	reg_sr_status,  // Status Register (status bits only)
	reg_sr_T,       // Status Register (T bit only)
	reg_fpscr,      // Floating Point Status/Control Register
	
	reg_pc_dyn,     // Dynamic PC (write-only, dynarec use only)

  reg_temp,      // Temporary register for internal use (not a real SH4 register)
  reg_temp2,     // Second decoder scratch (mac.l/mac.w need two loaded values live at once)

	sh4_reg_count,  // Total count of physical registers

	// Virtual Registers (dynarec optimization)
	// Double-precision floating point (dr0-dr14)
	regv_dr_0,
	regv_dr_2,
	regv_dr_4,
	regv_dr_6,
	regv_dr_8,
	regv_dr_10,
	regv_dr_12,
	regv_dr_14,

	// Extended double-precision (xd0-xd14)
	regv_xd_0,
	regv_xd_2,
	regv_xd_4,
	regv_xd_6,
	regv_xd_8,
	regv_xd_10,
	regv_xd_12,
	regv_xd_14,

	// Float vectors (fv0, fv4, fv8, fv12)
	regv_fv_0,
	regv_fv_4,
	regv_fv_8,
	regv_fv_12,

	// Extended matrix
	regv_xmtrx,

	NoReg = -1      // Invalid/No register
};

/**
 * @struct sr_type
 * @brief Status Register structure
 * 
 * The SR register contains various CPU status flags and control bits.
 * Bitfield layout varies by host endianness for optimal access.
 */
struct sr_type
{
	union
	{
		struct
		{
#if HOST_ENDIAN == ENDIAN_LITTLE
			u32 T_h     : 1;  // Bit 0:  True/False condition flag
			u32 S       : 1;  // Bit 1:  Saturation flag
			u32 rsvd0   : 2;  // Bits 2-3: Reserved
			u32 IMASK   : 4;  // Bits 4-7: Interrupt mask level
			u32 Q       : 1;  // Bit 8:  Q flag (div0u/s)
			u32 M       : 1;  // Bit 9:  M flag (div0u/s)
			u32 rsvd1   : 5;  // Bits 10-14: Reserved
			u32 FD      : 1;  // Bit 15: FPU disable
			u32 rsvd2   : 12; // Bits 16-27: Reserved
			u32 BL      : 1;  // Bit 28: Block interrupts
			u32 RB      : 1;  // Bit 29: Register bank select
			u32 MD      : 1;  // Bit 30: Privileged mode
			u32 rsvd3   : 1;  // Bit 31: Reserved
#else
			u32 rsvd3   : 1;
			u32 MD      : 1;
			u32 RB      : 1;
			u32 BL      : 1;
			u32 rsvd2   : 12;
			u32 FD      : 1;
			u32 rsvd1   : 5;
			u32 M       : 1;
			u32 Q       : 1;
			u32 IMASK   : 4;
			u32 rsvd0   : 2;
			u32 S       : 1;
			u32 T_h     : 1;
#endif
		};
		u32 status;
	};
	u32 T;  // T bit stored separately for fast access

	/**
	 * @brief Get complete SR value (status + T bit)
	 * @return Full 32-bit SR value
	 */
	INLINE u32 GetFull() const
	{
		return (status & 0x700083F2) | T;
	}

	/**
	 * @brief Set complete SR value
	 * @param value New SR value to set
	 */
	INLINE void SetFull(u32 value)
	{
		status = value & 0x700083F2;
		T = value & 1;
	}
};

/**
 * @struct fpscr_type
 * @brief Floating Point Status/Control Register structure
 * 
 * Controls FPU operation modes and flags exception conditions.
 * PR and SZ bits control precision and data size modes.
 */
struct fpscr_type
{
	union
	{
		u32 full;
		struct
		{
#if HOST_ENDIAN == ENDIAN_LITTLE
			u32 RM          : 2;  // Rounding mode
			u32 finexact    : 1;  // Inexact exception flag
			u32 funderflow  : 1;  // Underflow exception flag
			u32 foverflow   : 1;  // Overflow exception flag
			u32 fdivbyzero  : 1;  // Divide by zero flag
			u32 finvalidop  : 1;  // Invalid operation flag
			u32 einexact    : 1;  // Enable inexact
			u32 eunderflow  : 1;  // Enable underflow
			u32 eoverflow   : 1;  // Enable overflow
			u32 edivbyzero  : 1;  // Enable divide by zero
			u32 einvalidop  : 1;  // Enable invalid operation
			u32 cinexact    : 1;  // Cause inexact
			u32 cunderflow  : 1;  // Cause underflow
			u32 coverflow   : 1;  // Cause overflow
			u32 cdivbyzero  : 1;  // Cause divide by zero
			u32 cinvalid    : 1;  // Cause invalid
			u32 cfpuerr     : 1;  // Cause FPU error
			u32 DN          : 1;  // Denormalization mode
			u32 PR          : 1;  // Precision mode (0=single, 1=double)
			u32 SZ          : 1;  // Transfer size mode
			u32 FR          : 1;  // FPU register bank
			u32 pad         : 10; // Padding
#else
			u32 pad         : 10;
			u32 FR          : 1;
			u32 SZ          : 1;
			u32 PR          : 1;
			u32 DN          : 1;
			u32 cfpuerr     : 1;
			u32 cinvalid    : 1;
			u32 cdivbyzero  : 1;
			u32 coverflow   : 1;
			u32 cunderflow  : 1;
			u32 cinexact    : 1;
			u32 einvalidop  : 1;
			u32 edivbyzero  : 1;
			u32 eoverflow   : 1;
			u32 eunderflow  : 1;
			u32 einexact    : 1;
			u32 finvalidop  : 1;
			u32 fdivbyzero  : 1;
			u32 foverflow   : 1;
			u32 funderflow  : 1;
			u32 finexact    : 1;
			u32 RM          : 2;
#endif
		};
		// Alternate view for PR+SZ access
		struct
		{
#if HOST_ENDIAN == ENDIAN_LITTLE
			u32 nil     : 19;
			u32 PR_SZ   : 2;
			u32 nilz    : 11;
#else
			u32 nilz    : 11;
			u32 PR_SZ   : 2;
			u32 nil     : 19;
#endif
		};
	};
};

// Function pointer typedefs for CPU control
typedef void RunFP();
typedef void StopFP();
typedef void StepFP();
typedef void SkipFP();
typedef void ResetFP(bool Manual);
typedef void InitFP();
typedef void TermFP();
typedef bool IsCpuRunningFP();

/**
 * @brief Raise SH4 exception
 * @param ExceptionCode Exception code to raise
 * @param VectorAddress Vector table address for exception handler
 */
typedef void FASTCALL sh4_int_RaiseExceptionFP(u32 ExceptionCode, u32 VectorAddress);

/**
 * @struct sh4_if
 * @brief SH4 CPU interface structure
 * 
 * Provides function pointers for controlling the SH4 CPU emulation.
 * Can be initialized with either interpreter or dynarec backend.
 */
struct sh4_if
{
	RunFP* Run;                  // Execute CPU until stopped
	StopFP* Stop;                // Stop CPU execution
	StepFP* Step;                // Execute single instruction
	SkipFP* Skip;                // Skip current instruction
	ResetFP* Reset;              // Reset CPU state
	InitFP* Init;                // Initialize CPU subsystem
	TermFP* Term;                // Terminate CPU subsystem
	TermFP* ResetCache;          // Reset code cache (dynarec)
	IsCpuRunningFP* IsCpuRunning; // Check if CPU is running
};

// Interface acquisition functions

/**
 * @brief Get SH4 interpreter interface
 * @param cpu Pointer to sh4_if structure to populate
 * 
 * Initializes the CPU interface with interpreter backend.
 * Slower but more compatible and easier to debug.
 */
void Get_Sh4Interpreter(sh4_if* cpu);

/**
 * @brief Get SH4 recompiler (dynarec) interface
 * @param cpu Pointer to sh4_if structure to populate
 * 
 * Initializes the CPU interface with dynamic recompiler backend.
 * Faster but more complex. Optimized for Wii PPC architecture.
 */
void Get_Sh4Recompiler(sh4_if* cpu);

/**
 * @brief Release SH4 interface resources
 * @param cpu Pointer to sh4_if structure to clean up
 * 
 * Frees any resources associated with the CPU interface.
 */
void Release_Sh4If(sh4_if* cpu);

// #endif // SH4_IF_H
