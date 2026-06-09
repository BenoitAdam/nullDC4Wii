#pragma once

#include "types.h"
#include "sh4_if.h"


//Enums

void SetInterruptPend(InterruptID intr);
void ResetInterruptPend(InterruptID intr);
#define InterruptPend(intr,v) ((v)==0?ResetInterruptPend(intr):SetInterruptPend(intr))

void SetInterruptMask(InterruptID intr);
void ResetInterruptMask(InterruptID intr);
#define InterruptMask(intr,v) ((v)==0?ResetInterruptMask(intr):SetInterruptMask(intr))

int UpdateINTC();
int UpdateINTC_pending();	// cheap predicate, no GPR side effects (see intc.cpp)
void FASTCALL RaiseInterrupt_(InterruptID intr);
void RaiseExeption(u32 code,u32 vector);
bool Do_Exeption(u32 lvl, u32 expEvn, u32 CallVect);

union INTC_ICR_type
{
	u16 reg_data;
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 res:7;
		u32 IRLM:1;
		u32 NMIE:1;
		u32 NMIB:1;
		u32 res_2:4;
		u32 MAI:1;
		u32 NMIL:1;
#else
		u32 NMIL:1;
		u32 MAI:1;
		u32 res_2:4;
		u32 NMIB:1;
		u32 NMIE:1;
		u32 IRLM:1;
		u32 res:7;
#endif
	};
};

union INTC_IPRA_type
{
	u16 reg_data;
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 RTC:4;
		u32 TMU2:4;
		u32 TMU1:4;
		u32 TMU0:4;
#else
		u32 TMU0:4;
		u32 TMU1:4;
		u32 TMU2:4;
		u32 RTC:4;
#endif
	};
};

union INTC_IPRB_type
{
	u16 reg_data;
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 Reserved:4;
		u32 SCI1:4;
		u32 REF:4;
		u32 WDT:4;
#else
		u32 WDT:4;
		u32 REF:4;
		u32 SCI1:4;
		u32 Reserved:4;
#endif
	};
};

union INTC_IPRC_type
{
	u16 reg_data;
	struct
	{
#if HOST_ENDIAN==ENDIAN_LITTLE
		u32 Hitachi_UDI:4;
		u32 SCIF:4;
		u32 DMAC:4;
		u32 GPIO:4;
#else
		u32 GPIO:4;
		u32 DMAC:4;
		u32 SCIF:4;
		u32 Hitachi_UDI:4;
#endif
	};
};

extern INTC_ICR_type  INTC_ICR;
extern INTC_IPRA_type INTC_IPRA;
extern INTC_IPRB_type INTC_IPRB;
extern INTC_IPRC_type INTC_IPRC;

void intc_Init();
void intc_Reset(bool Manual);
void intc_Term();

bool SRdecode();

