#include "plugs/nullExtDev/bba.h"
#include "wii/dcnet.h"
#include "dc/asic/asic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VLANClientState
{
    net_receive* receive;
    net_can_receive* can_receive;
    void* opaque;
};

/*
 * GAPS/PCI address map used by the Dreamcast Broadband Adapter:
 *   0x1400..0x15ff  GAPS bridge registers
 *   0x1600..0x16ff  RTL8139 PCI configuration space
 *   0x1700..0x17ff  RTL8139 registers
 *   0x840000..0x847fff and 0x848000..0x84ffff  adapter RAM / DMA window
 *
 * The Ethernet chip implementation is the permissively licensed nullDC
 * RTL8139 model. This file supplies the GAPS bridge and connects packets to
 * the independent Wii DCNet transport.
 */
namespace
{
static const u32 kRamBase = 0x840000;
static const u32 kRamMask = 0x7fff;
static const u32 kRamBaseMask = 0xff8000;
static const u32 kRamSize = 0x8000;
static const u32 kGapsRegs = 0x1400;
static const u32 kGapsRegsSize = 0x200;
static const u32 kPciConfig = 0x1600;
static const u32 kPciConfigSize = 0x100;
static const u32 kRtlRegs = 0x1700;
static const u32 kRtlRegsSize = 0x100;
static const u32 kIntEnable = 0x14;
static const u32 kReset = 0x18;
static const u32 kDmaBase = 0x28;
static const u32 kDmaOffset = 0x2c;
static const u32 kMaxFrame = 1514;
static const u32 kPollIntervalCycles = 200000;

static ext_device_init_params s_params = {};
static bool s_hasParams = false;
static bool s_initialized = false;
static bool s_irqPending = false;
static u8 s_ram[kRamSize];
static u8 s_regs[kGapsRegsSize];
static u32 s_dmaBase = kRamBase;
static u32 s_dmaOffset = 0;
static u32 s_pollCycles = 0;
static PCIDevice* s_pciDevice = nullptr;
static void* s_rtlState = nullptr;
static VLANClientState s_vlanClient = {};
static bool s_clientReady = false;
static u8 s_pendingFrame[kMaxFrame];
static u16 s_pendingFrameLength = 0;

static u32 ReadLittleEndian(const u8* bytes, u32 size)
{
    u32 value = 0;
    for (u32 i = 0; i < size && i < 4; i++)
        value |= ((u32)bytes[i]) << (i * 8);
    return value;
}

static u32 ReadCircular(const u8* bytes, u32 capacity, u32 offset, u32 size)
{
    u32 value = 0;
    for (u32 i = 0; i < size && i < 4; i++)
        value |= ((u32)bytes[(offset + i) % capacity]) << (i * 8);
    return value;
}

static void WriteCircular(u8* bytes, u32 capacity, u32 offset, u32 value, u32 size)
{
    for (u32 i = 0; i < size && i < 4; i++)
        bytes[(offset + i) % capacity] = (u8)(value >> (i * 8));
}

static void UpdateInterrupt()
{
    if (!s_hasParams || s_params.RaiseInterrupt == nullptr ||
        s_params.CancelInterrupt == nullptr)
        return;

    if (s_irqPending && s_regs[kIntEnable] != 0)
        s_params.RaiseInterrupt(holly_EXP_PCI);
    else
        s_params.CancelInterrupt(holly_EXP_PCI);
}

static bool IsMappedBbaAddress(u32 address)
{
    if (address >= kGapsRegs && address < kGapsRegs + kGapsRegsSize)
        return true;
    if (address >= kPciConfig && address < kPciConfig + kPciConfigSize)
        return true;
    if (address >= kRtlRegs && address < kRtlRegs + kRtlRegsSize)
        return true;
    if ((address & kRamBaseMask) == kRamBase)
        return true;
    if (address >= 0x1800 && (address & kRamBaseMask) == 0x848000)
        return true;
    return false;
}

static void EnsureInitialized()
{
    if (s_initialized || !s_hasParams)
        return;

    memset(s_ram, 0, sizeof(s_ram));
    memset(s_regs, 0, sizeof(s_regs));
    memcpy(s_regs, "GAPSPCI_BRIDGE_2", 16);
    memcpy(&s_regs[0x1c], "SEGA", 4);
    s_dmaBase = kRamBase;
    s_dmaOffset = 0;
    s_irqPending = false;
    s_pollCycles = 0;
    s_pendingFrameLength = 0;

    NICInfo nic = {};
    /* Fixed locally-administered MAC; DCNet carries raw Ethernet frames. */
    nic.macaddr[0] = 0x02;
    nic.macaddr[1] = 0x00;
    nic.macaddr[2] = 0x4e;
    nic.macaddr[3] = 0x44;
    nic.macaddr[4] = 0x43;
    nic.macaddr[5] = 0x01;

    s_pciDevice = pci_rtl8139_init(nullptr, &nic, 0);
    if (s_pciDevice == nullptr)
    {
        printf("[BBA] Failed to create RTL8139 device\n");
        return;
    }

    s_rtlState = rtl8139_get_opaque(s_pciDevice);
    s_initialized = s_rtlState != nullptr;
    if (!s_initialized)
    {
        rtl8139_destroy_device(s_pciDevice);
        s_pciDevice = nullptr;
        printf("[BBA] RTL8139 device state unavailable\n");
        return;
    }

    printf("[BBA] Virtual Broadband Adapter initialized\n");
    if (!DCNet_Start())
        printf("[BBA] Could not start DCNet worker\n");
}

static void PollIncomingFrames()
{
    if (!s_initialized || !s_clientReady || s_vlanClient.receive == nullptr ||
        s_vlanClient.can_receive == nullptr)
        return;

    for (u32 i = 0; i < 8; i++)
    {
        if (s_pendingFrameLength == 0)
        {
            const int len = DCNet_ReceiveFrame(s_pendingFrame, sizeof(s_pendingFrame));
            if (len <= 0)
                break;
            s_pendingFrameLength = (u16)len;
        }

        if (!s_vlanClient.can_receive(s_vlanClient.opaque))
            break;

        s_vlanClient.receive(s_vlanClient.opaque, s_pendingFrame, s_pendingFrameLength);
        s_pendingFrameLength = 0;
    }
}
} // namespace

PCIDevice* pci_register_device(PCIBus* bus, const char* name, int instanceSize,
                               int devfn, PCIConfigReadFunc* readFn,
                               PCIConfigWriteFunc* writeFn)
{
    (void)readFn;
    (void)writeFn;
    if (instanceSize < (int)sizeof(PCIDevice))
        return nullptr;

    PCIDevice* device = (PCIDevice*)malloc((size_t)instanceSize);
    if (device == nullptr)
        return nullptr;

    memset(device, 0, (size_t)instanceSize);
    device->bus = bus;
    device->devfn = devfn;
    if (name != nullptr)
    {
        strncpy(device->name, name, sizeof(device->name) - 1);
        device->name[sizeof(device->name) - 1] = '\0';
    }
    return device;
}

void pci_register_io_region(PCIDevice* device, int region, uint32_t size,
                            int type, PCIMapIORegionFunc* mapFn)
{
    if (device == nullptr || region < 0 || region >= PCI_NUM_REGIONS)
        return;

    PCIIORegion* io = &device->io_regions[region];
    io->addr = 0xffffffffU;
    io->size = size;
    io->type = (uint8_t)type;
    io->map_func = mapFn;

    const u32 configOffset = region == PCI_ROM_SLOT ? 0x30 : 0x10 + (u32)region * 4;
    if (configOffset + 4 <= sizeof(device->config))
        WriteCircular(device->config, sizeof(device->config), configOffset, (u32)type, 4);
}

uint32_t pci_default_read_config(PCIDevice* device, uint32_t address, int len)
{
    if (device == nullptr || len < 1 || len > 4 || address + (u32)len > 256)
        return 0xffffffffU;
    return ReadLittleEndian(&device->config[address], (u32)len);
}

void pci_default_write_config(PCIDevice* device, uint32_t address,
                              uint32_t value, int len)
{
    if (device == nullptr || len < 1 || len > 4 || address + (u32)len > 256)
        return;

    for (int i = 0; i < len; i++)
    {
        const u32 offset = address + (u32)i;
        /* Vendor/device ID, revision/class, header type, and interrupt pin are read-only. */
        if (offset <= 3 || (offset >= 8 && offset <= 0x0b) ||
            offset == 0x0e || offset == 0x3d)
            continue;
        device->config[offset] = (u8)(value >> (i * 8));
    }
}

VLANClientState* qemu_new_vlan_client(void* nicInfo, net_receive* receive,
                                      net_can_receive* canReceive, void* opaque)
{
    (void)nicInfo;
    s_vlanClient.receive = receive;
    s_vlanClient.can_receive = canReceive;
    s_vlanClient.opaque = opaque;
    s_clientReady = receive != nullptr && canReceive != nullptr;
    return &s_vlanClient;
}

void qemu_send_packet(VLANClientState* client, const uint8_t* packet,
                      int size, u32 fromAddress)
{
    (void)client;
    (void)fromAddress;
    if (packet == nullptr || size < 14 || size > (int)kMaxFrame)
        return;
    /* The transport queues frames while DCNet is still connecting. */
    DCNet_SendFrame(packet, (u16)size);
}

void pci_set_irq(PCIDevice* device, u32 irq, u32 level)
{
    (void)device;
    (void)irq;
    s_irqPending = level != 0;
    UpdateInterrupt();
}

void cpu_physical_memory_write(u32 address, const void* buffer, u32 size)
{
    if (buffer == nullptr || size == 0)
        return;
    const u32 offset = address & kRamMask;
    const u32 first = size > kRamSize - offset ? kRamSize - offset : size;
    memcpy(&s_ram[offset], buffer, first);
    if (first < size)
        memcpy(s_ram, (const u8*)buffer + first, size - first);
}

void cpu_physical_memory_read(u32 address, void* buffer, u32 size)
{
    if (buffer == nullptr || size == 0)
        return;
    const u32 offset = address & kRamMask;
    const u32 first = size > kRamSize - offset ? kRamSize - offset : size;
    memcpy(buffer, &s_ram[offset], first);
    if (first < size)
        memcpy((u8*)buffer + first, s_ram, size - first);
}

void BBA_Init(ext_device_init_params* params)
{
    if (params != nullptr)
    {
        s_params = *params;
        s_hasParams = true;
    }
}

void BBA_Reset(bool manual)
{
    (void)manual;
    if (s_initialized && s_rtlState != nullptr)
        rtl8139_resete(s_rtlState);
    s_dmaBase = kRamBase;
    s_dmaOffset = 0;
    s_pollCycles = 0;
    s_pendingFrameLength = 0;
    s_irqPending = false;
    memset(s_regs, 0, sizeof(s_regs));
    memcpy(s_regs, "GAPSPCI_BRIDGE_2", 16);
    memcpy(&s_regs[0x1c], "SEGA", 4);
    UpdateInterrupt();
}

void BBA_Term(void)
{
    DCNet_Stop();
    if (s_pciDevice != nullptr)
        rtl8139_destroy_device(s_pciDevice);

    s_pciDevice = nullptr;
    s_rtlState = nullptr;
    s_initialized = false;
    s_clientReady = false;
    memset(&s_vlanClient, 0, sizeof(s_vlanClient));
    s_pendingFrameLength = 0;
    s_irqPending = false;
    s_pollCycles = 0;
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_regs, 0, sizeof(s_regs));
}

u32 BBA_ReadMem(u32 address, u32 size)
{
    if (size != 1 && size != 2 && size != 4)
        return 0xffffffffU;

    address &= 0x00ffffffU;
    if (!IsMappedBbaAddress(address))
        return 0;

    EnsureInitialized();
    if (!s_initialized)
        return 0xffffffffU;

    if (address >= 0x1400 && address < 0x1410)
    {
        static const char chipId[] = "GAPSPCI_BRIDGE_2";
        return ReadLittleEndian((const u8*)chipId + (address & 0x0f), size);
    }

    if ((address & kRamBaseMask) == kRamBase)
    {
        u32 offset = address & kRamMask;
        if (address & 0x8000)
            offset = (offset + s_dmaOffset) & kRamMask;
        return ReadCircular(s_ram, kRamSize, offset, size);
    }

    if (address >= kGapsRegs && address < kGapsRegs + kGapsRegsSize)
    {
        const u32 offset = address & (kGapsRegsSize - 1);
        u32 value = ReadCircular(s_regs, kGapsRegsSize, offset, size);
        if (offset == kReset)
            value &= 0xff;
        return value;
    }

    if (address >= kPciConfig && address < kPciConfig + kPciConfigSize)
        return pci_default_read_config(s_pciDevice, address & 0xff, (int)size);

    if (address >= kRtlRegs && address < kRtlRegs + kRtlRegsSize)
    {
        const u32 offset = address - kRtlRegs;
        if (size == 1) return rtl8139_mmio_readb(s_rtlState, offset);
        if (size == 2) return rtl8139_mmio_readw(s_rtlState, offset);
        return rtl8139_mmio_readl(s_rtlState, offset);
    }

    if (address >= 0x1800 && (address & kRamBaseMask) == 0x848000)
    {
        const u32 dmaStart = s_dmaBase >= kRamBase ? s_dmaBase - kRamBase : 0;
        const u32 offset = (dmaStart + (address & kRamMask)) & kRamMask;
        return ReadCircular(s_ram, kRamSize, offset, size);
    }

    return 0;
}

void BBA_WriteMem(u32 address, u32 data, u32 size)
{
    if (size != 1 && size != 2 && size != 4)
        return;

    address &= 0x00ffffffU;
    if (!IsMappedBbaAddress(address))
        return;

    EnsureInitialized();
    if (!s_initialized)
        return;

    if ((address & kRamBaseMask) == kRamBase)
    {
        u32 offset = address & kRamMask;
        if (address & 0x8000)
            offset = (offset + s_dmaOffset) & kRamMask;
        WriteCircular(s_ram, kRamSize, offset, data, size);
        return;
    }

    if (address >= kGapsRegs && address < kGapsRegs + kGapsRegsSize)
    {
        const u32 offset = address & (kGapsRegsSize - 1);
        WriteCircular(s_regs, kGapsRegsSize, offset, data, size);
        if (offset == kIntEnable)
            UpdateInterrupt();
        else if (offset == kReset && (data & 1))
        {
            printf("[BBA] GAPS reset\n");
            rtl8139_resete(s_rtlState);
        }
        else if (offset == kDmaBase)
            s_dmaBase = data & 0x00ffffffU;
        else if (offset == kDmaOffset)
            s_dmaOffset = data & kRamMask;
        return;
    }

    if (address >= kPciConfig && address < kPciConfig + kPciConfigSize)
    {
        pci_default_write_config(s_pciDevice, address & 0xff, data, (int)size);
        return;
    }

    if (address >= kRtlRegs && address < kRtlRegs + kRtlRegsSize)
    {
        const u32 offset = address - kRtlRegs;
        if (size == 1) rtl8139_mmio_writeb(s_rtlState, offset, data);
        else if (size == 2) rtl8139_mmio_writew(s_rtlState, offset, data);
        else rtl8139_mmio_writel(s_rtlState, offset, data);
        return;
    }

    if (address >= 0x1800 && (address & kRamBaseMask) == 0x848000)
    {
        const u32 dmaStart = s_dmaBase >= kRamBase ? s_dmaBase - kRamBase : 0;
        const u32 offset = (dmaStart + (address & kRamMask)) & kRamMask;
        WriteCircular(s_ram, kRamSize, offset, data, size);
    }
}

void BBA_Update(u32 cycles)
{
    if (!s_initialized)
        return;

    s_pollCycles += cycles;
    if (s_pollCycles < kPollIntervalCycles)
        return;
    s_pollCycles %= kPollIntervalCycles;
    PollIncomingFrames();
}
