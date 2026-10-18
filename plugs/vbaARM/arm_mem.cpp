#include "arm_mem.h"
#include "arm7.h"

u8 *arm_aica_ram;
//Set to true when aica interrupt is pending
bool aica_interr=false;
u32 aica_reg_L=0;
//Set to true when the out of the intc is 1
bool e68k_out = false;
u32 e68k_reg_L;
u32 e68k_reg_M=0;	//constant ?

void update_e68k()
{
	if (!e68k_out && aica_interr)
	{
		//Set the pending signal
		//Is L register holded here too ?
		e68k_out=1;
		e68k_reg_L=aica_reg_L;
	}
}

void FASTCALL ArmInterruptChange(u32 bits,u32 L)
{
	aica_interr=bits!=0;
	if (aica_interr)
		aica_reg_L=L;
	update_e68k();
}

void e68k_AcceptInterrupt()
{
	e68k_out=false;
	update_e68k();
}

// ARM7_RESET_FIX (arm_Reset): Flycast reset() clears the e68k FIQ latch and the
// pending interrupt level. Without it a latch left set before the game's own
// ARM7 reset fires a FIQ into the freshly loaded driver (seen on the PS3 in
// Crazy Taxi). The next ArmInterruptChange() rebuilds it from the AICA state.
void arm_reset_latch()
{
	aica_interr = false;
	aica_reg_L = 0;
	e68k_out = false;
	e68k_reg_L = 0;
	e68k_reg_M = 0;
}

// AICA_RACE (Flycast a920ae590, avoidRaceCondition): when an AICA register write
// raises the SH4 interrupt (nullAICA MCIEB / MCIPD.SCPU), the ARM7 that did the
// write must keep running at least 50 more cycles before its slice ends, so it
// finishes its side of the handshake before the SH4 services the IRQ. Only acts
// inside an ARM7 slice (arm_Run sets arm_in_slice): an SH4-side write has no
// ARM slice to extend. The cached engine is clamped here; the JIT reads
// arm_race_pending back in its slow-write stub (arm7_jit.cpp jit_race_budget).
bool arm_in_slice = false;
bool arm_race_pending = false;
void arm_cached_avoid_race();   // arm7_cached.cpp

void FASTCALL ArmAvoidRaceCondition()
{
	if (!arm_in_slice)
		return;
	arm_race_pending = true;
	arm_cached_avoid_race();
}

//Reg reads from arm side ..
template <u32 sz,class T>
inline T fastcall arm_ReadReg(u32 addr)
{
	addr&=0x7FFF;
	if (addr==REG_L)
		return e68k_reg_L;
	else if(addr==REG_M)
		return e68k_reg_M;	//shoudn't really happen
	else
		return arm_params.ReadMem_aica_reg(addr,sz);
}		
template <u32 sz,class T>
inline void fastcall arm_WriteReg(u32 addr,T data)
{
	addr&=0x7FFF;
	if (addr==REG_L)
		return;				//shoudn't really happen (read only)
	else if(addr==REG_M)
	{
		//accept interrupts
		if (data&1)
			e68k_AcceptInterrupt();
	}
	else
		return arm_params.WriteMem_aica_reg(addr,data,sz);
}
//Map using _vmem .. yay
void arm_init_mem()
{
	arm_aica_ram=arm_params.aica_ram;
}
//kill mem map & free used mem ;)
void arm_term_mem()
{
	
}

//00000000~007FFFFF @DRAM_AREA* 
//00800000~008027FF @CHANNEL_DATA 
//00802800~00802FFF @COMMON_DATA 
//00803000~00807FFF @DSP_DATA 

template<int sz,typename T>
T fastcall ReadMemArm(u32 addr)
{
	// printf("ReadMemArm %08x\n",addr);
	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
        if (addr&(sz-1)){
            //printf("ARM unal rd %d %08x\n",sz,addr);
            switch (sz)
            {
                case 4:
                {
                    // ARM7DI unaligned LDR: read the *aligned* word containing
                    // the address, then rotate right by (addr&3)*8. This is the
                    // hardware behavior; a plain byte-gather from the unaligned
                    // offset gives the wrong result (and failed the LDR_* tests).
                    u32 aligned = addr & ~3u;
                    u32 word = *(u32*)&arm_aica_ram[aligned & ARAM_MASK];
                    u32 rot  = (addr & 3) * 8;
                    return (word >> rot) | (word << (32 - rot));
                }
                default:
                    dbgbreak;
            }
        }
        if (sz==1)
            addr^=3;
        else if (sz==2)
            addr^=2;
        
		// printf("ReadMemArm rv %08x\n",*(T*)&arm_aica_ram[addr&ARAM_MASK]);

        return *(T*)&arm_aica_ram[addr&ARAM_MASK];
	}
	else
	{
		return arm_ReadReg<sz,T>(addr);
	}
}

// ARM7_STR_ALIGN preset (wii/main.cpp): how an unaligned word STR lands.
extern "C" int get_arm7_str_align_preset();

// Unaligned word stores seen. Logs the first 4 addresses, then the running
// total at each power of two, so a game that does this ~2000 times a second
// (ChuChu Rocket on the PS3, first address 0x1103A) still writes only ~20
// lines an hour. Only reached on the unaligned path, never on a normal store.
static u32 s_arm_unal_str=0;
static void arm_unal_store(u32 addr, int aligned)
{
	s_arm_unal_str++;
	if (s_arm_unal_str<=4 || (s_arm_unal_str & (s_arm_unal_str-1))==0)
	{
		printf("[ARM7] unaligned STR #%u at %06X (%s)\n", s_arm_unal_str, addr,
		       aligned ? "aligned word" : "legacy scatter");
		fflush(stdout);
	}
}

template<int sz,typename T>
void fastcall WriteMemArm(u32 addr,T data)
{
//	printf("WriteMemArm %08x %08x\n",addr,data);
	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
        if (addr&(sz-1)){
            //printf("ARM unal wr %d %08x\n",sz,addr);
            switch (sz)
            {
                case 4:
                {
                    // Every unaligned word store lands here, from all three ARM7
                    // engines (interpreter, cached, and arm7_jit's CK_WR cold
                    // stub / jit_swp); the JIT's inline STM masks the base & ~3.
                    const int aligned = get_arm7_str_align_preset();
                    arm_unal_store(addr, aligned);
                    if (aligned)
                    {
                        // ARM7DI: address bits 1-0 are ignored, the full register
                        // is written, not rotated, to the aligned word (Flycast
                        // arm_mem.h writeMem). No byte swap: arm_aica_ram holds
                        // each word in host order, as the aligned path below.
                        // The read side (ReadMemArm) already does the ARM7DI rule.
                        *(u32*)&arm_aica_ram[addr&ARAM_MASK&~3u]=(u32)data;
                        break;
                    }
                    // Legacy: scatters the 4 bytes from the unaligned address,
                    // corrupting half of this word and half of the next one --
                    // ChuChu Rocket's sound driver state, hence its late key-offs
                    // and slow, echoing effects (flycastPS3 chuchu_fix.md).
                    u8 t[4];
                    t[3]=data>>24; t[2]=data>>16; t[1]=data>>8; t[0]=data;

                    int i;
                    for(i=0;i<4;++i)
                        arm_aica_ram[((addr+i)^3)&ARAM_MASK]=t[i];
                    break;
                }
                default:
                    dbgbreak;
            }
            return;
        }
        if (sz==1)
            addr^=3;
        else if (sz==2)
            addr^=2;
        
		*(T*)&arm_aica_ram[addr&ARAM_MASK]=data;
	}
	else
	{
		arm_WriteReg<sz,T>(addr,data);
	}
}

template u8 ReadMemArm<1,u8>(u32 adr);
template u16 ReadMemArm<2,u16>(u32 adr);
template u32 ReadMemArm<4,u32>(u32 adr);

template void WriteMemArm<1,u8>(u32 adr,u8 data);
template void WriteMemArm<2,u16>(u32 adr,u16 data);
template void WriteMemArm<4,u32>(u32 adr,u32 data);

