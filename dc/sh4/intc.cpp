//Sh4 interrupt controller
//needs a rewrite and check varius things all around

#include "types.h"
#include "intc.h"
#include "tmu.h"
#include "ccn.h"
#include "sh4_registers.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/asic/asic.h"
#include "dc/maple/maple_if.h"

/*
	Now .. here is some text to keep you busy :D

	On sh4
	an Interrupt request is created , by setting the Interrupt request input to hi
	while it is hi , when this Interrupt can be processed , it is
	the Interrupt code , must set the input to low or after exiting the Interrupt handler the
	Interrupt may be re accepted
	on dc (extra )
	Dc has 2 Interrupt controlers, one on the Holly block (asic) that handles all Interrupts releated to external h/w
	and one on the sh4 that handles all Interrupts that hapen iside the sh4 (from the modules) and , has 3 line input
	from the asic.
	the asic has many Interrupts , 21 normal , 4 external (eg , modem) and 32 error Interrupts.Asic works like a mixer
	and it outputs 3 Interrupt signals from all that.if many Interrupts(asic) occur at once, they can share the same Interrupt(intc)

	intc sorts the Interrupts based on pririty and trys to accept the one w/ the hiest priority first

	how all that crap is implemented :
	The new code is much more accurate, emulating Interrupt request lines (InterruptSourceList).
	This means thast the RaiseInterrupt can't be the olny thing needed for Interrupts but we
	also need callbacks to get the Interrupt reguest line status.

	On Asic , things are simple , RaiseInterrupt sets the asic Interrupt bits
	and according to the settings on asic , it may signal pedning Interrupts
	withc are checked within CheckInterrupts.For implementing new asic insterupts
	adding em on the Interrupts list is all that is needed

	For internal sh4 Interrupts , exept form adding em on the Interrupts list ,
	you must write seperate Priority and Pending callbacks

	//PERFORMANCE NOTICE/
	This intc is much more accurate when compared to the old one and much slower.
	Thanks to the detection of any pending Interrupts tho (Flags set at RaiseInterrupt)
	it is much faster when interrupts are not hapening (eg , 99% o the cpu operation).
	Also , the structure of this intc allows us to know much earlier if Interrupts will
	not be accepted (eg , bl bit ect) so , it is much faster that the previus one when
	Interrupts are disabled using sr.bl or sr.imask

	//conlusion/
	We have an almost 100% acurate intc while it is faster that the previus one :)
	i'd say that this is a success meh

	missing things :
	More internal Interrupts [olny timers are emulated]...
	Exeptions [both here and on cpu code ...]

	now , let's get to the code
*/
//#define COUNT_INTERRUPT_UPDATES

//these are fixed
u16 IRLPriority=0x0246;
#define IRLP9 &IRLPriority,0
#define IRLP11 &IRLPriority,4
#define IRLP13 &IRLPriority,8

#define GIPA(p) &INTC_IPRA.reg_data,4*p
#define GIPB(p) &INTC_IPRB.reg_data,4*p
#define GIPC(p) &INTC_IPRC.reg_data,4*p

struct InterptSourceList_Entry
{
	u16* PrioReg;
	u32 Shift;
	u32 IntEvnCode;

	u32 GetPrLvl() const { return ((*PrioReg)>>Shift)&0xF; }
};

const InterptSourceList_Entry InterruptSourceList[]=
{
	//IRL
	{IRLP9,0x320},//sh4_IRL_9			= KMIID(sh4_int,0x320,0),
	{IRLP11,0x360},//sh4_IRL_11			= KMIID(sh4_int,0x360,1),
	{IRLP13,0x3A0},//sh4_IRL_13			= KMIID(sh4_int,0x3A0,2),

	//HUDI
	{GIPC(0),0x600},//sh4_HUDI_HUDI		= KMIID(sh4_int,0x600,3),  /* H-UDI underflow */

	//GPIO (missing on dc ?)
	{GIPC(3),0x620},//sh4_GPIO_GPIOI		= KMIID(sh4_int,0x620,4),

	//DMAC
	{GIPC(2),0x640},//sh4_DMAC_DMTE0		= KMIID(sh4_int,0x640,5),
	{GIPC(2),0x660},//sh4_DMAC_DMTE1		= KMIID(sh4_int,0x660,6),
	{GIPC(2),0x680},//sh4_DMAC_DMTE2		= KMIID(sh4_int,0x680,7),
	{GIPC(2),0x6A0},//sh4_DMAC_DMTE3		= KMIID(sh4_int,0x6A0,8),
	{GIPC(2),0x6C0},//sh4_DMAC_DMAE		= KMIID(sh4_int,0x6C0,9),

	//TMU
	{GIPA(3),0x400},//sh4_TMU0_TUNI0		=  KMIID(sh4_int,0x400,10), /* TMU0 underflow */
	{GIPA(2),0x420},//sh4_TMU1_TUNI1		=  KMIID(sh4_int,0x420,11), /* TMU1 underflow */
	{GIPA(1),0x440},//sh4_TMU2_TUNI2		=  KMIID(sh4_int,0x440,12), /* TMU2 underflow */
	{GIPA(1),0x460},//sh4_TMU2_TICPI2		=  KMIID(sh4_int,0x460,13),

	//RTC
	{GIPA(0),0x480},//sh4_RTC_ATI			= KMIID(sh4_int,0x480,14),
	{GIPA(0),0x4A0},//sh4_RTC_PRI			= KMIID(sh4_int,0x4A0,15),
	{GIPA(0),0x4C0},//sh4_RTC_CUI			= KMIID(sh4_int,0x4C0,16),

	//SCI
	{GIPB(1),0x4E0},//sh4_SCI1_ERI		= KMIID(sh4_int,0x4E0,17),
	{GIPB(1),0x500},//sh4_SCI1_RXI		= KMIID(sh4_int,0x500,18),
	{GIPB(1),0x520},//sh4_SCI1_TXI		= KMIID(sh4_int,0x520,19),
	{GIPB(1),0x540},//sh4_SCI1_TEI		= KMIID(sh4_int,0x540,29),

	//SCIF
	{GIPC(1),0x700},//sh4_SCIF_ERI		= KMIID(sh4_int,0x700,21),
	{GIPC(1),0x720},//sh4_SCIF_RXI		= KMIID(sh4_int,0x720,22),
	{GIPC(1),0x740},//sh4_SCIF_BRI		= KMIID(sh4_int,0x740,23),
	{GIPC(1),0x760},//sh4_SCIF_TXI		= KMIID(sh4_int,0x760,24),

	//WDT
	{GIPB(3),0x560},//sh4_WDT_ITI			= KMIID(sh4_int,0x560,25),

	//REF
	{GIPB(2),0x580},//sh4_REF_RCMI		= KMIID(sh4_int,0x580,26),
	{GIPA(2),0x5A0},//sh4_REF_ROVI		= KMIID(sh4_int,0x5A0,27),
};

//dynamicaly built
//Maps siid -> EventID
ALIGN64 u16 InterruptEnvId[32]=
{
	0
};
//dynamicaly built
//Maps piid -> 1<<siid
ALIGN64 u32 InterruptBit[32] =
{
	0
};
ALIGN64 u32 InterruptLevelBit[16]=
{
	0
};
void FASTCALL RaiseInterrupt_(InterruptID intr);
bool fastcall Do_Interrupt(u32 intEvn);
bool Do_Exeption(u32 epc, u32 expEvn, u32 CallVect);

#define IPr_LVL6  0x6
#define IPr_LVL4  0x4
#define IPr_LVL2  0x2

extern bool sh4_sleeping;

//bool InterruptsArePending=true;

INTC_ICR_type  INTC_ICR;
INTC_IPRA_type INTC_IPRA;
INTC_IPRB_type INTC_IPRB;
INTC_IPRC_type INTC_IPRC;

//InterruptID intr_l;

u32 interrupt_vpend;
u32 interrupt_vmask;	//-1 -> Ignore that kthx.0x0FFFffff allows all interrupts ( virtual interrupts are allways masked
u32 decoded_srimask;	//-1 kills all interrupts,including rebuild ones.

//bit 0 ~ 27 : interrupt source 27:0. 0 = lowest level, 27 = highest level.
//bit 28~31  : virtual interrupt sources.These have to do with the emu

//28:31 = 0 -> nothing (disabled)
//28:31 = 1 -> rebuild interrupt list		, retest interrupts
//28:31 = * -> undefined

//Rebuild sorted interrupt id table (priorities where updated)
void RequestSIIDRebuild()
{
	interrupt_vpend|=2<<28;
}
bool SRdecode()
{
	u32 imsk=sr.IMASK;
	decoded_srimask=InterruptLevelBit[imsk];

	if (sr.BL)
		decoded_srimask=0x0FFFFFFF;

	return (interrupt_vpend&interrupt_vmask)>decoded_srimask;
}

void fastcall VirtualInterrupt(u32 id)
{
	if (id)
	{
		u32 cnt=0;
		u32 vpend=interrupt_vpend;
		u32 vmask=interrupt_vmask;
		interrupt_vpend=0;
		interrupt_vmask=0xF0000000;
		//rebuild interrupt table
		for (u32 ilevel=0;ilevel<16;ilevel++)
		{
			for (u32 isrc=0;isrc<28;isrc++)
			{
				if (InterruptSourceList[isrc].GetPrLvl()==ilevel)
				{
					InterruptEnvId[cnt]=InterruptSourceList[isrc].IntEvnCode;
					bool p=InterruptBit[isrc]&vpend;
					bool m=InterruptBit[isrc]&vmask;
					InterruptBit[isrc]=1<<cnt;
					if (p)
						interrupt_vpend|=InterruptBit[isrc];
					if (m)
						interrupt_vmask|=InterruptBit[isrc];
					cnt++;
				}
			}
			InterruptLevelBit[ilevel]=(1<<cnt)-1;
		}

		SRdecode();
	}
}
//#ifdef COUNT_INTERRUPT_UPDATES
u32 no_interrupts,yes_interrupts;
//#endif
#if HOST_OS==OS_WINDOWS
#include <intrin.h>
#endif
u32 INLINE BSR(u32 v)
{

#if (HOST_ARCH==ARCH_ARM && HOST_CPU!=CPU_ARMV5) || (HOST_CPU==CPU_MIPS_ALLEGREX)

	u32 rv;
	 asm ("clz %0,%1"
             :"=r"(rv)        /* output */
             :"r"(v)         /* input */
			 );
	 return 31-rv;
#elif (HOST_CPU==CPU_MIPS_5900) && 0
#error TODO thiss
	u32 rv;
	 asm ("PLZCW %0,%1"
             :"=r"(rv)        /* output */
             :"r"(v)         /* input */
			 );
	 return 31-rv;
#elif (HOST_ARCH==ARCH_X86)
    #if HOST_OS == OS_LINUX
        u32 rv;
        asm ("bsr %0,%1"
             :"=r"(rv)        // output //
             :"r"(v)         // input //
             );
        return rv;
    #else
        unsigned long rv;
        _BitScanReverse(&rv,v);
        return rv;
	#endif

#else

	// Generic, *FIXME* ADD PPC BIT OP //
	for (int i=31;i>=0;i--)
	{
		if ((1<<i)&v)
			return i;
	}
#endif

	return 0;
}
// Cheap, side-effect-free predicate: does an interrupt currently need handling?
// Touches NO SH4 GPRs (does not run Do_Interrupt). Used by the JIT to decide
// whether it must flush its pinned GPRs before dispatching the event handler.
int UpdateINTC_pending()
{
	u32 intped=interrupt_vpend&interrupt_vmask;
	return intped>decoded_srimask;
}

int UpdateINTC()
{
	u32 intped=interrupt_vpend&interrupt_vmask;
	if (intped<=decoded_srimask)
		return 0;
	if (intped&0xF0000000)
	{
		VirtualInterrupt(intped>>28);
		return UpdateINTC();
	}

	return Do_Interrupt(InterruptEnvId[BSR(intped)]);
}

void SetInterruptPend(InterruptID intr)
{
	u32 piid= intr & InterruptPIIDMask;
	interrupt_vpend|=InterruptBit[piid];
}
void ResetInterruptPend(InterruptID intr)
{
	u32 piid= intr & InterruptPIIDMask;
	interrupt_vpend&=~InterruptBit[piid];
}

void SetInterruptMask(InterruptID intr)
{
	u32 piid= intr & InterruptPIIDMask;
	interrupt_vmask|=InterruptBit[piid];
}
void ResetInterruptMask(InterruptID intr)
{
	u32 piid= intr & InterruptPIIDMask;
	interrupt_vmask&=~InterruptBit[piid];
}

bool fastcall Do_Interrupt(u32 intEvn)
{
	CCN_INTEVT = intEvn;

	ssr = sr.GetFull();
	spc = next_pc;
	sgr = r[15];
	sr.BL = 1;
	sr.MD = 1;
	sr.RB = 1;
	UpdateSR();
	next_pc = vbr + 0x600;

	return true;
}

bool Do_Exeption(u32 epc, u32 expEvn, u32 CallVect)
{
	CCN_EXPEVT = expEvn;

	ssr = sr.GetFull();
	spc = epc;
	sgr = r[15];
	sr.BL = 1;
	sr.MD = 1;
	sr.RB = 1;
	UpdateSR();

	next_pc = vbr + CallVect;

	return true;
}

//Register writes need interrupt re-testing !

void write_INTC_IPRA(u32 data)
{
	if (INTC_IPRA.reg_data!=(u16)data)
	{
		INTC_IPRA.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
void write_INTC_IPRB(u32 data)
{
	if (INTC_IPRB.reg_data!=(u16)data)
	{
		INTC_IPRB.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
void write_INTC_IPRC(u32 data)
{
	if (INTC_IPRC.reg_data!=(u16)data)
	{
		INTC_IPRC.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
//Init/Res/Term
void intc_Init()
{
	//INTC ICR 0xFFD00000 0x1FD00000 16 0x0000 0x0000 Held Held Pclk
	INTC[(INTC_ICR_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	INTC[(INTC_ICR_addr&0xFF)>>2].readFunction=0;
	INTC[(INTC_ICR_addr&0xFF)>>2].writeFunction=0;
	INTC[(INTC_ICR_addr&0xFF)>>2].data16=&INTC_ICR.reg_data;

	//INTC IPRA 0xFFD00004 0x1FD00004 16 0x0000 0x0000 Held Held Pclk
	INTC[(INTC_IPRA_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(INTC_IPRA_addr&0xFF)>>2].readFunction=0;
	INTC[(INTC_IPRA_addr&0xFF)>>2].writeFunction=write_INTC_IPRA;
	INTC[(INTC_IPRA_addr&0xFF)>>2].data16=&INTC_IPRA.reg_data;

	//INTC IPRB 0xFFD00008 0x1FD00008 16 0x0000 0x0000 Held Held Pclk
	INTC[(INTC_IPRB_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(INTC_IPRB_addr&0xFF)>>2].readFunction=0;
	INTC[(INTC_IPRB_addr&0xFF)>>2].writeFunction=write_INTC_IPRB;
	INTC[(INTC_IPRB_addr&0xFF)>>2].data16=&INTC_IPRB.reg_data;

	//INTC IPRC 0xFFD0000C 0x1FD0000C 16 0x0000 0x0000 Held Held Pclk
	INTC[(INTC_IPRC_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(INTC_IPRC_addr&0xFF)>>2].readFunction=0;
	INTC[(INTC_IPRC_addr&0xFF)>>2].writeFunction=write_INTC_IPRC;
	INTC[(INTC_IPRC_addr&0xFF)>>2].data16=&INTC_IPRC.reg_data;
}

void intc_Reset(bool Manual)
{
	INTC_ICR.reg_data = 0x0;
	INTC_IPRA.reg_data = 0x0;
	INTC_IPRB.reg_data = 0x0;
	INTC_IPRC.reg_data = 0x0;

	interrupt_vpend=0x00000000;	//rebuild & recalc
	interrupt_vmask=0xFFFFFFFF;	//no masking
	decoded_srimask=0;			//nothing is real, everything is allowed ...

	RequestSIIDRebuild();		//we have to rebuild the table.

	for (u32 i=0;i<28;i++)
		InterruptBit[i]=1<<i;
}

void intc_Term()
{

}

