// Dreamcast external-device interface: virtual BBA plus DCNet transport.
#include "plugins/plugin_header.h"
#include "plugs/nullExtDev/bba.h"

// 0x00600000 is the Dreamcast modem area. Modem emulation is not implemented
// yet; keep the previous safe stub here while BBA/DCNet is developed.
u32 FASTCALL libExtDevice_ReadMem_A0_006(u32 addr, u32 size)
{
    (void)addr;
    (void)size;
    return 0;
}

void FASTCALL libExtDevice_WriteMem_A0_006(u32 addr, u32 data, u32 size)
{
    (void)addr;
    (void)data;
    (void)size;
}

// 0x01000000 is the external-device aperture used by the Dreamcast BBA.
u32 FASTCALL libExtDevice_ReadMem_A0_010(u32 addr, u32 size)
{
    return BBA_ReadMem(addr, size);
}

void FASTCALL libExtDevice_WriteMem_A0_010(u32 addr, u32 data, u32 size)
{
    BBA_WriteMem(addr, data, size);
}

// Area 5 remains unimplemented; the BBA uses the Area-0 external-device map.
u32 FASTCALL libExtDevice_ReadMem_A5(u32 addr, u32 size)
{
    (void)addr;
    (void)size;
    return 0;
}

void FASTCALL libExtDevice_WriteMem_A5(u32 addr, u32 data, u32 size)
{
    (void)addr;
    (void)data;
    (void)size;
}

void FASTCALL libExtDevice_Update(u32 cycles)
{
    BBA_Update(cycles);
}

s32 FASTCALL libExtDevice_Load(void)
{
    return rv_ok;
}

void FASTCALL libExtDevice_Unload(void)
{
}

void FASTCALL libExtDevice_Reset(bool manual)
{
    BBA_Reset(manual);
}

s32 FASTCALL libExtDevice_Init(ext_device_init_params* params)
{
    BBA_Init(params);
    return rv_ok;
}

void FASTCALL libExtDevice_Term(void)
{
    BBA_Term();
}
