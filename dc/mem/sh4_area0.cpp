#include "types.h"
#include "sh4_mem.h"
#include "sb.h"
#include "dc/pvr/pvr_if.h"
#include "dc/gdrom/gdrom_if.h"
#include "dc/aica/aica_if.h"

#include "plugins/plugin_manager.h"


//=============================================================================
// Area 0 Memory Map (Dreamcast SH4)
//=============================================================================
// 0x00000000 - 0x001FFFFF  : System/Boot ROM (MPX)
// 0x00200000 - 0x0021FFFF  : Flash Memory
// 0x00400000 - 0x005F67FF  : Unassigned
// 0x005F6800 - 0x005F69FF  : System Control Reg.
// 0x005F6C00 - 0x005F6CFF  : Maple i/f Control Reg.
// 0x005F7000 - 0x005F70FF  : GD-ROM / NAOMI BD Reg.
// 0x005F7400 - 0x005F74FF  : G1 i/f Control Reg.
// 0x005F7800 - 0x005F78FF  : G2 i/f Control Reg.
// 0x005F7C00 - 0x005F7CFF  : PVR i/f Control Reg.
// 0x005F8000 - 0x005F9FFF  : TA / PVR Core Reg.
// 0x00600000 - 0x006007FF  : MODEM
// 0x00600800 - 0x006FFFFF  : G2 (Reserved)
// 0x00700000 - 0x00707FFF  : AICA Sound Control Reg.
// 0x00710000 - 0x0071000B  : AICA RTC Control Reg.
// 0x00800000 - 0x00FFFFFF  : AICA Wave Memory
// 0x01000000 - 0x01FFFFFF  : External Device
// 0x02000000 - 0x03FFFFFF* : Image Area* (2MB mirror)
//=============================================================================

// Helper: read T from byte array at addr, applying LE conversion
template<u32 sz, class T>
static inline T ReadMemArr(const u8* arr, u32 offset)
{
	switch (sz)
	{
	case 1: return (T)arr[offset];
	case 2: return (T)HOST_TO_LE16(*(const u16*)&arr[offset]);
	case 4: return (T)HOST_TO_LE32(*(const u32*)&arr[offset]);
	default: return (T)0;
	}
}

// Helper: write T into byte array at addr
template<u32 sz, class T>
static inline void WriteMemArr(u8* arr, u32 offset, T data)
{
	switch (sz)
	{
	case 1: arr[offset] = (u8)data; break;
	case 2: *(u16*)&arr[offset] = HOST_TO_LE16((u16)data); break;
	case 4: *(u32*)&arr[offset] = HOST_TO_LE32((u32)data); break;
	}
}

//=============================================================================
// FLASH_CHIP preset (wii/main.cpp; Flycast DCFlashChip, hw/flashrom/flashrom.cpp)
//=============================================================================
// The flash is an AMD-style chip: the BIOS unlocks it with command bytes
// (AA @x555, 55 @x2AA/x2AAA, then A0 program / 80..10 chip erase / 80..30
// sector erase / 90 ID mode), a byte program can only clear bits, an erase
// sets 0xFF, and the factory partition (0x1A000-0x1BFFF) is write-protected.
// Legacy stored every write as plain RAM: the command bytes landed in the
// data, a program could set bits again, and a sector erase wrote 0x30 into
// the sector's first byte instead of erasing it. Harmless while the flash is
// reloaded from file at every boot; not once it is saved back. Same state
// machine as Flycast, byte writes only (Flycast drops wider ones too).
extern "C" int get_flash_chip_preset();

enum FlashState { FS_Normal, FS_ReadAMDID1, FS_ReadAMDID2, FS_ByteProgram,
                  FS_EraseAMD1, FS_EraseAMD2, FS_EraseAMD3, FS_SelectMode };
static FlashState s_flash_state = FS_Normal;

// Bounded: unexpected sequences and erases (both rare) go to /ndclog.txt.
static void flash_chip_log(const char* what, u32 addr, u32 val)
{
	static u32 s_count = 0;
	if (s_count >= 16)
		return;
	s_count++;
	printf("[FLASH] %s @ %05X: %02X\n", what, addr, val);
	fflush(stdout);
}

static void flash_chip_write(u32 addr, u32 val, u32 sz)
{
	addr &= FLASH_SIZE - 1;
	if (sz != 1)
	{
		flash_chip_log("non-byte write dropped", addr, val);
		return;
	}
	val &= 0xFF;

	switch (s_flash_state)
	{
	case FS_Normal:
		if (val == 0xAA && ((addr & 0xFFF) == 0x555 || (addr & 0xFFF) == 0xAAA))
			s_flash_state = FS_ReadAMDID1;
		else if (val != 0xF0 && val != 0xFF)   // F0/FF = reset, already normal
			flash_chip_log("write outside a command dropped", addr, val);
		break;

	case FS_ReadAMDID1:
		if (val == 0x55 && ((addr & 0xFFFF) == 0x02AA || (addr & 0xFFFF) == 0x2AAA || (addr & 0xFFF) == 0x555))
			s_flash_state = FS_ReadAMDID2;
		else
		{
			if (val != 0xF0)
				flash_chip_log("unexpected unlock 2", addr, val);
			s_flash_state = FS_Normal;
		}
		break;

	case FS_ReadAMDID2:
		if (val == 0x80 && ((addr & 0xFFFF) == 0x0555 || (addr & 0xFFFF) == 0x5555 || (addr & 0xFFF) == 0xAAA))
			s_flash_state = FS_EraseAMD1;
		else if (val == 0xA0 && ((addr & 0xFFFF) == 0x0555 || (addr & 0xFFFF) == 0x5555 || (addr & 0xFFF) == 0xAAA))
			s_flash_state = FS_ByteProgram;
		else if (val == 0x90 && (addr & 0xFFFF) == 0x5555)
			s_flash_state = FS_SelectMode;
		else
		{
			if (val != 0xF0)
				flash_chip_log("unexpected command", addr, val);
			s_flash_state = FS_Normal;
		}
		break;

	case FS_ByteProgram:
		if ((addr & 0x1E000) != 0x1A000)       // factory partition is protected
		{
			const u8 nv = flash_b[addr] & (u8)val;
			if (nv != flash_b[addr])
			{
				flash_b[addr] = nv;
				flash_b_dirty = true;          // FLASH_SAVE
			}
		}
		s_flash_state = FS_Normal;
		break;

	case FS_EraseAMD1:
		if (val == 0xAA && ((addr & 0xFFF) == 0x555 || (addr & 0xFFF) == 0xAAA))
			s_flash_state = FS_EraseAMD2;
		else
		{
			if (val != 0xF0)
				flash_chip_log("unexpected erase unlock 1", addr, val);
			s_flash_state = FS_Normal;
		}
		break;

	case FS_EraseAMD2:
		if (val == 0x55 && ((addr & 0xFFFF) == 0x02AA || (addr & 0xFFFF) == 0x2AAA || (addr & 0xFFF) == 0x555))
			s_flash_state = FS_EraseAMD3;
		else
		{
			if (val != 0xF0)
				flash_chip_log("unexpected erase unlock 2", addr, val);
			s_flash_state = FS_Normal;
		}
		break;

	case FS_EraseAMD3:
		if (val == 0x10 && ((addr & 0xFFF) == 0x555 || (addr & 0xFFF) == 0xAAA))
		{
			// chip erase: everything but the factory partition
			u8 save[0x2000];
			memcpy(save, &flash_b[0x1A000], 0x2000);
			memset(&flash_b[0], 0xFF, FLASH_SIZE);
			memcpy(&flash_b[0x1A000], save, 0x2000);
			flash_b_dirty = true;              // FLASH_SAVE
			flash_chip_log("chip erase", addr, val);
		}
		else if (val == 0x30)
		{
			// sector erase; SA3 (factory) is protected
			u32 start = 0, len = 0;
			switch (addr & ~0x1FFF)
			{
			case 0x00000: start = 0x00000; len = 0x10000; break;   // SA0
			case 0x10000: start = 0x10000; len = 0x8000;  break;   // SA1
			case 0x18000: start = 0x18000; len = 0x2000;  break;   // SA2
			case 0x1C000: start = 0x1C000; len = 0x4000;  break;   // SA4
			default: break;                                         // SA3 / mid-sector
			}
			if (len)
			{
				memset(&flash_b[start], 0xFF, len);
				flash_b_dirty = true;          // FLASH_SAVE
			}
			flash_chip_log(len ? "sector erase" : "sector erase ignored", addr, val);
		}
		else if (val != 0xF0)
			flash_chip_log("unexpected erase command", addr, val);
		s_flash_state = FS_Normal;
		break;

	default:
		s_flash_state = FS_Normal;
		break;
	}
}

// ID mode: the first byte read returns the chip's ID, then the chip is back to
// normal (Flycast DCFlashChip::Read8). A wider read gets data above it.
static u8 flash_chip_read_id(u32 addr)
{
	s_flash_state = FS_Normal;
	switch (addr & 0x43)
	{
	case 0:  return 4;                                     // manufacturer
	case 1:  return 0xB0;                                  // device
	case 2:  return (addr & 0x1E000) == 0x1A000;           // sector protected?
	default: return 0;
	}
}

//=============================================================================
// Unified read handler (templated on access size)
//=============================================================================
template<u32 sz, class T, u32 b_start, u32 b_end>
T FASTCALL ReadMem_area0(u32 addr)
{
	addr &= 0x01FFFFFF;  // Strip unused upper bits

	// base is the 64KB page index (addr >> 16)
	const u32 base = addr >> 16;

	// --- Boot ROM: 0x000000 - 0x1FFFFF (pages 0x00-0x1F) ---
	if (base <= 0x1F)
	{
		return ReadMemArr<sz, T>(bios_b.data, addr);
	}

	// --- Flash Memory: 0x200000 - 0x21FFFF (pages 0x20-0x21) ---
	if (base >= 0x20 && base <= 0x21)
	{
		// FLASH_CHIP: only its command sequence can enter ID mode.
		if (s_flash_state == FS_SelectMode)
		{
			const u32 off = (addr - 0x00200000) & (FLASH_SIZE - 1);
			const T rv = ReadMemArr<sz, T>(flash_b.data, addr - 0x00200000);
			return (T)((rv & ~(T)0xFF) | flash_chip_read_id(off));
		}
		return ReadMemArr<sz, T>(flash_b.data, addr - 0x00200000);
	}

	// --- 0x5F page ---
	if (base == 0x5F)
	{
		// Unassigned: 0x400000 - 0x5F67FF
		if (addr <= 0x005F67FF)
		{
			// EMUERROR2("Read from unassigned area0 region, addr=%x", addr);
      // printf instead of EMUERROR because tiny freeze otherwise    
      printf("Read from unassigned area0 region, addr=%x", addr);
			return (T)0;
		}

		// GD-ROM: 0x5F7000 - 0x5F70FF  (checked before the wider SB block)
		if (addr >= 0x005F7000 && addr <= 0x005F70FF)
		{
			return (T)ReadMem_gdrom(addr, sz);
		}

		// All SB registers: 0x5F6800 - 0x5F7CFF
		if (addr >= 0x005F6800 && addr <= 0x005F7CFF)
		{
			return (T)sb_ReadMem(addr, sz);
		}

		// TA / PVR Core: 0x5F8000 - 0x5F9FFF
		if (addr >= 0x005F8000 && addr <= 0x005F9FFF)
		{
			return (T)pvr_readreg_TA(addr, sz);
		}

		// EMUERROR2("Read from unmapped 0x5F area, addr=%x", addr);
    // printf instead of EMUERROR because tiny freeze otherwise    
    printf("Read from unmapped 0x5F area, addr=%x", addr);
		return (T)0;
	}

	// --- MODEM: 0x600000 - 0x6007FF (page 0x60, low range) ---
	if (base == 0x60 && addr <= 0x006007FF)
	{
		return (T)libExtDevice_ReadMem_A0_006(addr, sz);
	}

	// --- G2 Reserved: 0x600800 - 0x6FFFFF (pages 0x60-0x6F, high range) ---
	if (base >= 0x60 && base <= 0x6F && addr >= 0x00600800)
{
    // G2 devices (Modem, BBA, etc.) – not emulated; return 0
    return (T)0;
}

	// --- AICA Sound Control: 0x700000 - 0x707FFF (page 0x70) ---
	if (base == 0x70 && addr <= 0x00707FFF)
	{
		return (T)ReadMem_aica_reg(addr, sz);
	}

	// --- AICA RTC: 0x710000 - 0x71000B (page 0x71) ---
	if (base == 0x71 && addr <= 0x0071000B)
	{
		return (T)ReadMem_aica_rtc(addr, sz);
	}

	// --- AICA Wave Memory: 0x800000 - 0xFFFFFF (pages 0x80-0xFF) ---
	if (base >= 0x80 && base <= 0xFF)
	{
		return (T)libAICA_ReadMem_aica_ram(addr, sz);
	}

	// --- External Device: 0x1000000 - 0x1FFFFFF (pages 0x100-0x1FF) ---
	if (base >= 0x100 && base <= 0x1FF)
	{
		return (T)libExtDevice_ReadMem_A0_010(addr, sz);
	}

	// Unmapped
	return (T)0;
}

//=============================================================================
// Unified write handler (templated on access size)
//=============================================================================
template<u32 sz, class T, u32 b_start, u32 b_end>
void FASTCALL WriteMem_area0(u32 addr, T data)
{
	addr &= 0x01FFFFFF;  // Strip unused upper bits

	const u32 base = addr >> 16;

	// --- Boot ROM: read-only, writes are an error ---	
	if (base <= 0x1F)
	{
    // EMUERROR4("Write to Boot ROM (read-only), addr=%x, data=%x, sz=%d", addr, data, sz);
    // printf instead of EMUERROR because tiny freeze otherwise
		printf("Write to Boot ROM (read-only), addr=%x, data=%x, sz=%d\n", addr, data, sz);
		return;
	}

	// --- Flash Memory: 0x200000 - 0x21FFFF ---
	if (base >= 0x20 && base <= 0x21)
	{
		// FLASH_CHIP on: the chip's command state machine (above). Off
		// (legacy): every write is stored as plain RAM.
		if (get_flash_chip_preset())
		{
			flash_chip_write(addr - 0x00200000, (u32)data, sz);
			return;
		}
		WriteMemArr<sz, T>(flash_b.data, addr - 0x00200000, data);
		flash_b_dirty = true;   // FLASH_SAVE (it saves only with FLASH_CHIP on)
		return;
	}

	// --- 0x5F page ---
	if (base == 0x5F)
	{
		// Unassigned: 0x400000 - 0x5F67FF
		if (addr <= 0x005F67FF)
		{
			// EMUERROR4("Write to unassigned area0 region, addr=%x, data=%x, sz=%d", addr, data, sz);
      // printf instead of EMUERROR because tiny freeze otherwise
      printf("Write to unassigned area0 region, addr=%x, data=%x, sz=%d", addr, data, sz);
			return;
		}

		// GD-ROM: 0x5F7000 - 0x5F70FF  (checked before the wider SB block)
		if (addr >= 0x005F7000 && addr <= 0x005F70FF)
		{
			WriteMem_gdrom(addr, data, sz);
			return;
		}

		// All SB registers: 0x5F6800 - 0x5F7CFF
		if (addr >= 0x005F6800 && addr <= 0x005F7CFF)
		{
			sb_WriteMem(addr, data, sz);
			return;
		}

		// TA / PVR Core: 0x5F8000 - 0x5F9FFF
		if (addr >= 0x005F8000 && addr <= 0x005F9FFF)
		{
			pvr_writereg_TA(addr, data, sz);
			return;
		}

		// EMUERROR4("Write to unmapped 0x5F area, addr=%x, data=%x, sz=%d", addr, data, sz);
    // printf instead of EMUERROR because tiny freeze otherwise
    printf("Write to unmapped 0x5F area, addr=%x, data=%x, sz=%d", addr, data, sz);
		return;
	}

	// --- MODEM: 0x600000 - 0x6007FF ---
	if (base == 0x60 && addr <= 0x006007FF)
	{
		libExtDevice_WriteMem_A0_006(addr, data, sz);
		return;
	}

	// --- G2 Reserved: 0x600800 - 0x6FFFFF ---
	if (base >= 0x60 && base <= 0x6F && addr >= 0x00600800)
{
    // Ignore writes to G2 devices
    return;
}

	// --- AICA Sound Control: 0x700000 - 0x707FFF ---
	if (base == 0x70 && addr <= 0x00707FFF)
	{
		WriteMem_aica_reg(addr, data, sz);
		return;
	}

	// --- AICA RTC: 0x710000 - 0x71000B ---
	if (base == 0x71 && addr <= 0x0071000B)
	{
		WriteMem_aica_rtc(addr, data, sz);
		return;
	}

	// --- AICA Wave Memory: 0x800000 - 0xFFFFFF ---
	if (base >= 0x80 && base <= 0xFF)
	{
		libAICA_WriteMem_aica_ram(addr, data, sz);
		return;
	}

	// --- External Device: 0x1000000 - 0x1FFFFFF ---
	if (base >= 0x100 && base <= 0x1FF)
	{
		libExtDevice_WriteMem_A0_010(addr, data, sz);
		return;
	}

	// Unmapped — silently ignore (matches original behaviour)
}

//=============================================================================
// Init / Reset / Term — delegate to System Bus
//=============================================================================
void sh4_area0_Init()
{
	sb_Init();
}

void sh4_area0_Reset(bool Manual)
{
	s_flash_state = FS_Normal;   // FLASH_CHIP: a reset leaves any command
	sb_Reset(Manual);
}

void sh4_area0_Term()
{
	sb_Term();
}

//=============================================================================
// vmem handler registration and mapping
//=============================================================================
_vmem_handler area0_handler_0000_00FF;
_vmem_handler area0_handler_0100_01FF;

void map_area0_init()
{
	area0_handler_0000_00FF = _vmem_register_handler_Template2(
		ReadMem_area0, WriteMem_area0, 0x0000, 0x00FF);

	area0_handler_0100_01FF = _vmem_register_handler_Template2(
		ReadMem_area0, WriteMem_area0, 0x0100, 0x01FF);
}

void map_area0(u32 base)
{
	verify(base < 0xE000);

	// Pages 0x00-0xFF (Boot ROM, Flash, hardware registers, AICA)
	_vmem_map_handler(area0_handler_0000_00FF, 0x00 | base, 0x00 | base);

	// Pages 0x100-0x1FF (External Device)
	_vmem_map_handler(area0_handler_0100_01FF, 0x01 | base, 0x01 | base);

	// 0x02|base mirrors 0x00|base (excludes Flash and Boot ROM per HW spec)
	_vmem_mirror_mapping(0x02 | base, 0x00 | base, 0x02);
}
