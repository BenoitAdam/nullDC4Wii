#pragma once

#include "types.h"
#include "plugins/plugin_manager.h"

//==============================================================================
// PowerVR Interface Header
// Dreamcast PVR (PowerVR) hardware interface and abstraction layer
//==============================================================================

//------------------------------------------------------------------------------
// VRAM Access
//------------------------------------------------------------------------------
extern VArray2 vram;

//------------------------------------------------------------------------------
// PVR Register Interface
//------------------------------------------------------------------------------

/**
 * Read from PVR Tile Accelerator register
 * @param addr Register address (should be in 0x5F8000 range)
 * @param sz Size of read operation in bytes (1, 2, or 4)
 * @return Register value
 */
u32 pvr_readreg_TA(u32 addr, u32 sz);

/**
 * Write to PVR Tile Accelerator register
 * @param addr Register address (should be in 0x5F8000 range)
 * @param data Value to write
 * @param sz Size of write operation in bytes (1, 2, or 4)
 */
void pvr_writereg_TA(u32 addr, u32 data, u32 sz);

//------------------------------------------------------------------------------
// VRAM Access Functions (32-bit to 64-bit address conversion)
//------------------------------------------------------------------------------

/**
 * Read 8-bit value from VRAM Area 1
 * Note: 8-bit VRAM reads are not natively supported by hardware
 * @param addr Physical address in Area 1
 * @return 8-bit value (always returns 0 - not supported)
 */
u8 FASTCALL pvr_read_area1_8(u32 addr);

/**
 * Read 16-bit value from VRAM Area 1
 * @param addr Physical address in Area 1
 * @return 16-bit value from VRAM
 */
u16 FASTCALL pvr_read_area1_16(u32 addr);

/**
 * Read 32-bit value from VRAM Area 1
 * @param addr Physical address in Area 1
 * @return 32-bit value from VRAM
 */
u32 FASTCALL pvr_read_area1_32(u32 addr);

/**
 * Write 8-bit value to VRAM Area 1
 * Note: 8-bit VRAM writes are not natively supported by hardware
 * @param addr Physical address in Area 1
 * @param data Value to write (ignored - not supported)
 */
void FASTCALL pvr_write_area1_8(u32 addr, u8 data);

/**
 * Write 16-bit value to VRAM Area 1
 * @param addr Physical address in Area 1
 * @param data 16-bit value to write
 */
void FASTCALL pvr_write_area1_16(u32 addr, u16 data);

/**
 * Write 32-bit value to VRAM Area 1
 * @param addr Physical address in Area 1
 * @param data 32-bit value to write
 */
void FASTCALL pvr_write_area1_32(u32 addr, u32 data);

//------------------------------------------------------------------------------
// YUV Converter Output Regions
//------------------------------------------------------------------------------

/**
 * Look a VRAM address up against the regions the YUV converter has written.
 * Lets the renderer tell an FMV frame (rows packed at the video's native
 * width) apart from a YUV422 texture the game uploaded at its declared
 * power-of-two width - the two need different source pitches.
 * @param addr  VRAM address (VRAM_MASK'd) of a YUV422 texture
 * @param out_w Receives the real source pitch in pixels
 * @param out_h Receives the real source row count
 * @return 1 if addr falls inside converter output, 0 otherwise
 */
extern "C" int yuv_conv_lookup(u32 addr, u32 *out_w, u32 *out_h);

//------------------------------------------------------------------------------
// Tile Accelerator DMA Interface
//------------------------------------------------------------------------------

/**
 * Write data to Tile Accelerator via DMA
 * Handles polygon data, YUV conversion, and direct VRAM writes
 * @param address Target address (determines operation type)
 * @param data Pointer to source data
 * @param count Number of 32-byte blocks to transfer
 */
void FASTCALL TAWrite(u32 address, u32* data, u32 count);

/**
 * Write data to Tile Accelerator via Store Queue
 * Optimized single-block transfer (32 bytes)
 * @param address Target address (determines operation type)
 * @param data Pointer to 32-byte source data block
 */
void FASTCALL TAWriteSQ(u32 address, u32* data);

//------------------------------------------------------------------------------
// Module Lifecycle
//------------------------------------------------------------------------------

/**
 * Initialize PVR subsystem
 * Called once at emulator startup
 */
void pvr_Init();

/**
 * Terminate PVR subsystem
 * Called once at emulator shutdown
 */
void pvr_Term();

/**
 * Reset PVR to default state
 * @param Manual True if user-initiated reset, false for automatic reset
 */
void pvr_Reset(bool Manual);

/**
 * Update PVR state for given number of cycles
 * @param cycles Number of CPU cycles to advance
 */
void pvr_Update(u32 cycles);

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------

// Delegate PVR update to plugin implementation
#define UpdatePvr(clc) libPvr_UpdatePvr(clc)

// PVR register base address
#define PVR_BASE 0x005F8000

//------------------------------------------------------------------------------
// Address Space Layout (for reference)
//------------------------------------------------------------------------------
// 0x00000000 - 0x007FFFFF : TA Polygon Data (8MB)
// 0x00800000 - 0x00FFFFFF : YUV Converter (8MB)
// 0x01000000 - 0x01FFFFFF : Direct VRAM Write (16MB)

