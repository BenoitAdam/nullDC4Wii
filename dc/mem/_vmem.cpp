// _vmem.cpp – virtual memory subsystem for Dreamcast-on-Wii emulation
//
// _vmem v2 layer overview:
//   _vmem_MemInfo_ptr[256]  – top-level dispatch table (indexed by addr[31:24])
//   _vmem_RF8/16/32         – per-handler read  function tables
//   _vmem_WF8/16/32         – per-handler write function tables
//
// Each _vmem_MemInfo_ptr entry is either:
//   • A host pointer (≥256) with low bits encoding an address-wrap shift, OR
//   • A small integer (< HANDLER_COUNT*4) encoding a handler-table index.
// ---------------------------------------------------------------------------

#include "_vmem.h"
#include "dc/aica/aica_if.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_mem.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define HANDLER_MAX   0x1F
#define HANDLER_COUNT (HANDLER_MAX + 1)

// Safe sentinel returned on unmapped reads.
// NOTE: Using 0 here instead of 0xDEADC0D3 prevents the game from treating
// the error value as a valid pointer and cascading into garbage writes.
// The old value (0xDEADC0D3) was the root cause of the 0xDEADE5F3 freeze:
// the game read from an unmapped region, got 0xDEADC0D3 back, added an offset,
// and started writing to 0xDEADE5F3+ — corrupting state and hanging.
#define MEM_ERROR_RETURN_VALUE 0u

// ---------------------------------------------------------------------------
// Global tables
// ---------------------------------------------------------------------------
static _vmem_handler     _vmem_lrp;   // next free handler slot

static _vmem_ReadMem8FP*  _vmem_RF8  [HANDLER_COUNT];
// ---------------------------------------------------------------------------
// [FMOV64] 64-bit block accesses on a big-endian host.
//
// Every buffer this file hands out (main RAM, the VRAM 64-bit window at
// 0x04xxxxxx) stores each aligned 4-byte group of DC address space as one
// HOST-endian u32 at the same offset. That is the convention the whole
// emulator is built on: pvr_write_area1_32() does `*(u32*)&vram[addr] = data`,
// and the texture decoders read individual bytes back as `vram[offset ^ 3]`.
//
// The handler path below honours it for 64-bit accesses - it splits them into
// two 32-bit accesses, low half at addr and high half at addr+4, exactly as a
// little-endian DC lays them out. The direct block path did not: it issued one
// raw `*(u64*)` store, which on a big-endian host puts the HIGH 32 bits at addr
// and the LOW 32 bits at addr+4. Every 64-bit access to a mapped block
// therefore had its two 32-bit halves transposed.
//
// HOWEVER - and this matters - the legacy behaviour is most likely the CORRECT
// one, and this preset probably makes it wrong. The SH4 manual defines
// FMOV.D DRm,@Rn as TWO 32-bit transfers: FRm to (Rn) and FRm+1 to (Rn+4).
// `dr_hex` is `((u64*)fr)` (sh4_registers.h), so on a big-endian host that u64
// already holds FRm in the high half and FRm+1 in the low half - and a raw
// big-endian u64 store therefore lays down FRm at addr and FRm+1 at addr+4,
// which is exactly what the hardware does. Splitting it the other way round
// breaks that.
//
// It was written while chasing the Alone in the Dark background speckle, on the
// theory that a texture uploaded with fmov pairs would come out with each
// 8-byte group's halves swapped. That was DISPROVEN on hardware: decoding the
// same texture with the swap undone (src[offset ^ 7] instead of ^ 3) is just as
// corrupt. See [[aitd-background-speckle]].
//
// Kept only so the two behaviours can be A/B'd, default OFF. If you are reading
// this while hunting something else: turning it ON is not a fix, it is a
// deliberate deviation from what the SH4 does.
//
// The branch only exists in the u64 template instantiation, so u8/u16/u32
// accesses are untouched and cost nothing.
// ---------------------------------------------------------------------------
extern "C" int get_fmov64_fix_preset();

// ---------------------------------------------------------------------------
// [AITD] VRAM write census - TEMPORARY DIAGNOSTIC.
//
// The Alone in the Dark backdrop is already speckled in emulated VRAM before
// the renderer ever touches it, and the decode is verified byte-correct, so
// something wrote those bytes wrong. There are four ways bytes reach VRAM
// (direct block stores to the 0x04 64-bit window, the 0x05 32-bit-area
// handlers, the TA direct-texture path, and the store queue), and they do NOT
// share a layout convention - so the first thing to establish is simply WHICH
// one uploaded this texture, and at what access width.
//
// Counters only, no behaviour change. Remove with the rest of the [AITD] work.
// ---------------------------------------------------------------------------
u32 g_aitd_vw_blk[4] = {0,0,0,0};   // block path by width: [0]=8b [1]=16b [2]=32b [3]=64b
u32 g_aitd_vw_blk_lo = 0xFFFFFFFFu; // address range the block path touched
u32 g_aitd_vw_blk_hi = 0;

static _vmem_WriteMem8FP* _vmem_WF8  [HANDLER_COUNT];

static _vmem_ReadMem16FP*  _vmem_RF16[HANDLER_COUNT];
static _vmem_WriteMem16FP* _vmem_WF16[HANDLER_COUNT];

static _vmem_ReadMem32FP*  _vmem_RF32[HANDLER_COUNT];
static _vmem_WriteMem32FP* _vmem_WF32[HANDLER_COUNT];

// Top-level dispatch table: 256 entries, one per 16 MB page.
ALIGN(65536) void* _vmem_MemInfo_ptr[0x100];

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Return the number of low address bits to mask away for a given memory block.
// e.g. a 16 MB block (mask=0x00FFFFFF) → returns 0 (no masking needed for
// a full 16 MB page), a 2 MB VRAM block → returns the appropriate shift.
static u32 FindMask(u32 msk)
{
    u32 s  = (u32)-1;
    u32 rv = 0;
    while (msk != s >> rv)
        rv++;
    return rv;
}

// Core read dispatcher (templated on access size).
template<typename T>
static INLINE T fastcall _vmem_readt(u32 addr)
{
    const u32 sz   = sizeof(T);
    const u32 page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        // MMIO / handler path
        const u32 id = (u32)iirf;
        if (sz == 1)
            return (T)_vmem_RF8[id / 4](addr);
        else if (sz == 2)
            return (T)_vmem_RF16[id / 4](addr);
        else if (sz == 4)
            return (T)_vmem_RF32[id / 4](addr);
        else if (sz == 8)
        {
            // 64-bit: two consecutive 32-bit reads
            T rv = (T)_vmem_RF32[id / 4](addr);
            rv  |= (T)((u64)_vmem_RF32[id / 4](addr + 4) << 32);
            return rv;
        }
        die("_vmem_readt: invalid size");
    }
    else
    {
        // Direct RAM/VRAM path
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;
#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
        else if (sz == 8 && get_fmov64_fix_preset())
        {
            // Two 32-bit reads, low half first - same order the handler path
            // uses. See the [FMOV64] note above.
            const u32 lo = *((u32*)&((u8*)ptr)[addr]);
            const u32 hi = *((u32*)&((u8*)ptr)[addr + 4]);
            return (T)(((u64)hi << 32) | (u64)lo);
        }
#endif
        return *((T*)&((u8*)ptr)[addr]);
    }
}

// Core write dispatcher (templated on access size).
template<typename T>
static INLINE void fastcall _vmem_writet(u32 addr, T data)
{
    const u32 sz   = sizeof(T);
    const u32 page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        // MMIO / handler path
        const u32 id = (u32)iirf;
        if (sz == 1)
            _vmem_WF8[id / 4](addr, (u8)data);
        else if (sz == 2)
            _vmem_WF16[id / 4](addr, (u16)data);
        else if (sz == 4)
            _vmem_WF32[id / 4](addr, (u32)data);
        else if (sz == 8)
        {
            _vmem_WF32[id / 4](addr,     (u32)(u64)data);
            _vmem_WF32[id / 4](addr + 4, (u32)((u64)data >> 32));
        }
        else
            die("_vmem_writet: invalid size");
    }
    else
    {
        // Direct RAM/VRAM path
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;

        // [AITD] census: is anything writing the VRAM 64-bit window directly?
        if (1)
        {
            const u32 pg = page & 0x1F;
            if (pg == 0x04 || pg == 0x06 || pg == 0x07)
            {
                g_aitd_vw_blk[sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3]++;
                if (addr < g_aitd_vw_blk_lo) g_aitd_vw_blk_lo = addr;
                if (addr > g_aitd_vw_blk_hi) g_aitd_vw_blk_hi = addr;
            }
        }

#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
        else if (sz == 8 && get_fmov64_fix_preset())
        {
            // Two 32-bit stores, low half first - same order the handler path
            // uses. See the [FMOV64] note above.
            const u64 v = (u64)data;
            *((u32*)&((u8*)ptr)[addr])     = (u32)v;
            *((u32*)&((u8*)ptr)[addr + 4]) = (u32)(v >> 32);
            return;
        }
#endif
        *((T*)&((u8*)ptr)[addr]) = data;
    }
}

// ---------------------------------------------------------------------------
// Public read/write accessors
// ---------------------------------------------------------------------------
u8  fastcall _vmem_ReadMem8  (u32 addr) { return _vmem_readt<u8> (addr); }
u16 fastcall _vmem_ReadMem16 (u32 addr) { return _vmem_readt<u16>(addr); }
u32 fastcall _vmem_ReadMem32 (u32 addr) { return _vmem_readt<u32>(addr); }
u64 fastcall _vmem_ReadMem64 (u32 addr) { return _vmem_readt<u64>(addr); }

void fastcall _vmem_WriteMem8  (u32 addr, u8  data) { _vmem_writet<u8> (addr, data); }
void fastcall _vmem_WriteMem16 (u32 addr, u16 data) { _vmem_writet<u16>(addr, data); }
void fastcall _vmem_WriteMem32 (u32 addr, u32 data) { _vmem_writet<u32>(addr, data); }
void fastcall _vmem_WriteMem64 (u32 addr, u64 data) { _vmem_writet<u64>(addr, data); }

// ---------------------------------------------------------------------------
// Default "not mapped" handlers
// These log once per unique address (rate-limited) to avoid log spam during
// level transitions, then return the safe sentinel value.
// ---------------------------------------------------------------------------

// Simple rate-limiter: skip logging if we've already seen this page recently.
static u32 s_last_unmapped_page = 0xFFFFFFFFu;
static u32 s_unmapped_log_count = 0;
static const u32 MAX_LOG_PER_PAGE = 8;

static bool should_log_unmapped(u32 addr)
{
    u32 page = addr >> 12; // 4 KB granularity for rate limiting
    if (page != s_last_unmapped_page)
    {
        s_last_unmapped_page  = page;
        s_unmapped_log_count  = 0;
    }
    if (s_unmapped_log_count < MAX_LOG_PER_PAGE)
    {
        s_unmapped_log_count++;
        return true;
    }
    return false;
}

u8 fastcall _vmem_ReadMem8_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read8  from unmapped 0x%08X\n", addr);
    return (u8)MEM_ERROR_RETURN_VALUE;
}
u16 fastcall _vmem_ReadMem16_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read16 from unmapped 0x%08X\n", addr);
    return (u16)MEM_ERROR_RETURN_VALUE;
}
u32 fastcall _vmem_ReadMem32_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read32 from unmapped 0x%08X\n", addr);
    return (u32)MEM_ERROR_RETURN_VALUE;
}

void fastcall _vmem_WriteMem8_not_mapped(u32 addr, u8 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write8  to unmapped 0x%08X = 0x%02X\n", addr, data);
}
void fastcall _vmem_WriteMem16_not_mapped(u32 addr, u16 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write16 to unmapped 0x%08X = 0x%04X\n", addr, data);
}
void fastcall _vmem_WriteMem32_not_mapped(u32 addr, u32 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write32 to unmapped 0x%08X = 0x%08X\n", addr, data);
}

// ---------------------------------------------------------------------------
// Handler registration
// ---------------------------------------------------------------------------
_vmem_handler _vmem_register_handler(
    _vmem_ReadMem8FP*   read8,
    _vmem_ReadMem16FP*  read16,
    _vmem_ReadMem32FP*  read32,
    _vmem_WriteMem8FP*  write8,
    _vmem_WriteMem16FP* write16,
    _vmem_WriteMem32FP* write32)
{
    _vmem_handler rv = _vmem_lrp++;
    verify(rv < HANDLER_COUNT);

    _vmem_RF8 [rv] = read8   ? read8   : _vmem_ReadMem8_not_mapped;
    _vmem_RF16[rv] = read16  ? read16  : _vmem_ReadMem16_not_mapped;
    _vmem_RF32[rv] = read32  ? read32  : _vmem_ReadMem32_not_mapped;

    _vmem_WF8 [rv] = write8  ? write8  : _vmem_WriteMem8_not_mapped;
    _vmem_WF16[rv] = write16 ? write16 : _vmem_WriteMem16_not_mapped;
    _vmem_WF32[rv] = write32 ? write32 : _vmem_WriteMem32_not_mapped;

    return rv;
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------
void _vmem_map_handler(_vmem_handler Handler, u32 start, u32 end)
{
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    for (u32 i = start; i <= end; i++)
        _vmem_MemInfo_ptr[i] = (u8*)0 + (Handler * 4);
}

void _vmem_map_block(void* base, u32 start, u32 end, u32 mask)
{
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    verify(((unat)base & 0xFF) == 0);
    verify(base != 0);

    const u32 shift = FindMask(mask);
    u32 j = 0;
    for (u32 i = start; i <= end; i++)
    {
        _vmem_MemInfo_ptr[i] = &((u8*)base)[j] + shift;
        j += 0x1000000; // advance one 16 MB page
    }
}

void _vmem_mirror_mapping(u32 new_region, u32 start, u32 size)
{
    u32 end = start + size - 1;
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    // Destination region must not overlap source range
    verify(!((new_region >= start) && (new_region <= end)));

    for (u32 i = 0; i < size; i++)
        _vmem_MemInfo_ptr[(new_region + i) & 0xFF] = _vmem_MemInfo_ptr[(start + i) & 0xFF];
}

// ---------------------------------------------------------------------------
// Dynarec helpers
// ---------------------------------------------------------------------------
void _vmem_get_ptrs(u32 sz, bool write, void*** vmap, void*** func)
{
    *vmap = _vmem_MemInfo_ptr;
    switch (sz)
    {
    case 1: *func = write ? (void**)_vmem_WF8  : (void**)_vmem_RF8;  return;
    case 2: *func = write ? (void**)_vmem_WF16 : (void**)_vmem_RF16; return;
    case 4:
    case 8: *func = write ? (void**)_vmem_WF32 : (void**)_vmem_RF32; return;
    default: die("_vmem_get_ptrs: invalid size");
    }
}

void* _vmem_read_const(u32 addr, bool& ismem, u32 sz)
{
    const u32  page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr  = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        ismem = false;
        const u32 id = (u32)iirf;
        if (sz == 1) return (void*)_vmem_RF8 [id / 4];
        if (sz == 2) return (void*)_vmem_RF16[id / 4];
        if (sz == 4) return (void*)_vmem_RF32[id / 4];
        die("_vmem_read_const: invalid size");
    }
    else
    {
        ismem = true;
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;
#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
#endif
        return &((u8*)ptr)[addr];
    }
}

// ---------------------------------------------------------------------------
// Diagnostic helper
// ---------------------------------------------------------------------------
bool _vmem_is_mapped(u32 addr)
{
    const u32  page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    return (void*)(iirf & ~(unat)HANDLER_MAX) != nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void _vmem_init()
{
    _vmem_reset();
}

void _vmem_reset()
{
    memset(_vmem_RF8,  0, sizeof(_vmem_RF8));
    memset(_vmem_RF16, 0, sizeof(_vmem_RF16));
    memset(_vmem_RF32, 0, sizeof(_vmem_RF32));
    memset(_vmem_WF8,  0, sizeof(_vmem_WF8));
    memset(_vmem_WF16, 0, sizeof(_vmem_WF16));
    memset(_vmem_WF32, 0, sizeof(_vmem_WF32));

    memset(_vmem_MemInfo_ptr, 0, sizeof(_vmem_MemInfo_ptr));

    _vmem_lrp = 0;

    // Slot 0 = the "not-mapped" default; must be registered first.
    verify(_vmem_register_handler(0, 0, 0, 0, 0, 0) == 0);

    // Reset log-rate-limiter state.
    s_last_unmapped_page = 0xFFFFFFFFu;
    s_unmapped_log_count = 0;
}

void _vmem_term()
{
    // Nothing to do for the software-only mapping tables.
}

// ---------------------------------------------------------------------------
// Memory reservation
// ---------------------------------------------------------------------------
#if HOST_OS == OS_PSP
#define SLIM_RAM ((u8*)0x0A000000)
#elif HOST_OS != OS_WII
ALIGN(256) u8 SLIM_RAM[ARAM_SIZE + VRAM_SIZE + RAM_SIZE];
#endif

bool _vmem_reserve()
{
#if HOST_OS == OS_WII
    u32 level = IRQ_Disable();

    u8* ram_alloc = (u8*)SYS_GetArena2Lo();
    // Align to 65536 bytes (required by _vmem_map_block).
    ram_alloc = (u8*)(((unat)ram_alloc + 65535) & ~(unat)65535);
    SYS_SetArena2Lo(ram_alloc + ARAM_SIZE + VRAM_SIZE + RAM_SIZE);

    extern u8* vram_buffer;
    vram_buffer = (u8*)SYS_GetArena2Lo();
    vram_buffer = (u8*)(((unat)vram_buffer + 63) & ~(unat)63); // 64-byte align
    SYS_SetArena2Lo(vram_buffer + VRAM_SIZE * 2);

    IRQ_Restore(level);

    printf("[vmem] Wii RAM: %p  VRAM buffer: %p  GDDR3 free: %.2f MB\n",
           ram_alloc, vram_buffer,
           ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / (1024.f * 1024.f));
#else
    u8* ram_alloc = SLIM_RAM;

#endif

    aica_ram.size = ARAM_SIZE;
    aica_ram.data = ram_alloc;
    ram_alloc += ARAM_SIZE;

    vram.size = VRAM_SIZE;
    vram.data = ram_alloc;
    ram_alloc += VRAM_SIZE;

    mem_b.size = RAM_SIZE;
    mem_b.data = ram_alloc;
    ram_alloc += RAM_SIZE;

    return true;
}

void _vmem_release()
{
    // Memory is either statically allocated (non-Wii) or managed by the Wii
    // arena allocator; no explicit free is needed here.
}
