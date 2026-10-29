#pragma once

#include "plugins/plugin_header.h"
#include "plugs/nullExtDev/rtl8139c.h"

/* Dreamcast Broadband Adapter compatibility layer. */
void BBA_Init(ext_device_init_params* params);
void BBA_Reset(bool manual);
void BBA_Term(void);
u32 BBA_ReadMem(u32 addr, u32 size);
void BBA_WriteMem(u32 addr, u32 data, u32 size);
void BBA_Update(u32 cycles);

/* Emulated millisecond timer used by the RTL8139 PHY/link model. */
void SetUpdateCallback(void (*callback)(void), u32 milliseconds);

/* Functions consumed by the RTL8139 model. */
void cpu_physical_memory_write(u32 addr, const void* buffer, u32 size);
void cpu_physical_memory_read(u32 addr, void* buffer, u32 size);
PCIDevice* pci_register_device(PCIBus* bus, const char* name, int instance_size,
                               int devfn, PCIConfigReadFunc* read_fn,
                               PCIConfigWriteFunc* write_fn);
void pci_register_io_region(PCIDevice* device, int region, uint32_t size,
                            int type, PCIMapIORegionFunc* map_fn);
uint32_t pci_default_read_config(PCIDevice* device, uint32_t address, int len);
void pci_default_write_config(PCIDevice* device, uint32_t address,
                              uint32_t value, int len);
void pci_set_irq(PCIDevice* device, u32 irq, u32 level);
VLANClientState* qemu_new_vlan_client(void* nic_info, net_receive* receive,
                                      net_can_receive* can_receive, void* opaque);
void qemu_send_packet(VLANClientState* client, const uint8_t* packet,
                      int size, u32 from_address);

/* Helpers kept inside the RTL model translation unit. */
void* rtl8139_get_opaque(PCIDevice* device);
void rtl8139_destroy_device(PCIDevice* device);
