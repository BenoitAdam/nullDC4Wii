// nullGDR.cpp : Defines the entry point for the DLL application.
//

#include "ImgReader.h"
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "plugs/drkPvr/frame_prof.h" // FRAME_PROF: disc bucket

// ---------------------------------------------------------------------------
// DISC census (disc_census preset, page 8 LOGS).
//
// Built because the first disc_bulk A/B came back INCONCLUSIVE: per-call cost
// did not move (0.93 -> 1.04 ms/call, and a 7.45 ms rd=1 frame appears
// byte-identical in the before and after logs). Two very different things
// produce that, and they need opposite fixes:
//   * the preset was simply off, or
//   * the guest asks for ONE sector per call, so iso_ReadRun() never has a run
//     to coalesce and the 32-sectors-per-call premise was wrong.
// Guessing costs a Wii run either way. cnt(1:...) settles it in one line.
//
// iso= counts calls that reached iso_DriveReadSector, which also says which
// reader is live -- a signal that used to come free from the [CDI] printf
// until that was (correctly) gated.
// ---------------------------------------------------------------------------
extern "C" int get_disc_census_preset();
#define DISC_CENSUS() (get_disc_census_preset() != 0)

// Bumped from here and from iso9660.cpp. Not gated: one increment beside a
// file read is not measurable, and a gated counter that reads zero is worse
// than useless -- it looks like an answer.
u32 g_dc_calls = 0, g_dc_iso = 0, g_dc_secs = 0;
u32 g_dc_c1 = 0, g_dc_c8 = 0, g_dc_c31 = 0, g_dc_c32 = 0;  // SectorCount buckets
u32 g_dc_ticks = 0, g_dc_kb = 0;
u32 g_dc_runs = 0, g_dc_runsecs = 0;                        // bulk runs served
u32 g_dc_fb_small = 0, g_dc_fb_notrk = 0, g_dc_fb_neg = 0;  // ...and why not
u32 g_dc_fb_short = 0, g_dc_fb_nomem = 0;

extern "C" void disc_census_dump(double secs)
{
  if (!DISC_CENSUS() || secs <= 0.0 || g_dc_calls == 0)
    return;   // silent when the guest is not touching the disc

  const double ms = (double)ticks_to_microsecs((u64)g_dc_ticks) / 1000.0;

  printf("[DISC] calls=%.0f/s iso=%u sec=%u cnt(1:%u 2-8:%u 9-31:%u 32:%u)"
         " bulk=%u runs/%u sec fb(small:%u notrk:%u neg:%u short:%u nomem:%u)"
         " %uKB %.1fms/s %.2fms/call\n",
         g_dc_calls / secs, g_dc_iso, g_dc_secs,
         g_dc_c1, g_dc_c8, g_dc_c31, g_dc_c32,
         g_dc_runs, g_dc_runsecs,
         g_dc_fb_small, g_dc_fb_notrk, g_dc_fb_neg, g_dc_fb_short, g_dc_fb_nomem,
         g_dc_kb, ms, ms / (double)g_dc_calls);
  fflush(stdout);

  g_dc_calls = g_dc_iso = g_dc_secs = 0;
  g_dc_c1 = g_dc_c8 = g_dc_c31 = g_dc_c32 = 0;
  g_dc_ticks = g_dc_kb = 0;
  g_dc_runs = g_dc_runsecs = 0;
  g_dc_fb_small = g_dc_fb_notrk = g_dc_fb_neg = 0;
  g_dc_fb_short = g_dc_fb_nomem = 0;
}

extern "C" int get_debug_loop();
extern "C" int get_debug_gdrom();

void FASTCALL libGDR_ReadSubChannel(u8 * buff, u32 format, u32 len)
{
	//printf("libGDR_ReadSubChannel\n");
}/*
FILE* fiso;
u32 fiso_fad;
u32 fiso_ssz;
u32 fiso_offs;

bool ConvertSector(u8* in_buff , u8* out_buff , int from , int to)
{
	//if no convertion
	if (to==from)
	{
		memcpy(out_buff,in_buff,to);
		return true;
	}
	switch (to)
	{
	case 2048:
		{
			//verify(from>=2048);
			//verify((from==2448) || (from==2352) || (from==2336));
			if ((from == 2352) || (from == 2448))
			{
				if (in_buff[15]==1)
				{
					memcpy(out_buff,&in_buff[0x10],2048); //0x10 -> mode1
				}
				else
					memcpy(out_buff,&in_buff[0x18],2048); //0x18 -> mode2 (all forms ?)
			}
			else
				memcpy(out_buff,&in_buff[0x8],2048);	//hmm only possible on mode2.Skip the mode2 header
		}
		break;
	case 2352:
		//if (from >= 2352)
		{
			memcpy(out_buff,&in_buff[0],2352);
		}
		break;
	default :
		printf("Sector convertion from %d to %d not supported \n", from , to);
		break;
	}

	return true;
}

void FASTCALL libGDR_ReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
	printf("libGDR_ReadSector\n");

	if (!fiso)
	{
		FILE* p=fopen("gdrom/disc.txt","rb");
		fscanf(p,"%d %d %d",&fiso_fad,&fiso_ssz,&fiso_offs);
		fclose(p);
		fiso=fopen("gdrom/disc.gdrom","rb");
	}
	while(SectorCount)
	{
		u8 temp[5200];
		fseek(fiso,fiso_offs+(StartSector-fiso_fad)*fiso_ssz,SEEK_SET);
		fread(temp,1,fiso_ssz,fiso);
		ConvertSector(temp,buff,fiso_ssz,secsz);
		buff+=secsz;
		StartSector++;
		SectorCount--;
	}
}

void FASTCALL libGDR_GetToc(u32* toc,u32 area)
{
	printf("libGDR_GetToc\n");
	FILE* f=fopen("gdrom/toc.bin","rb");
	fread(toc,1,102,f);
	fclose(f);
}
//TODO : fix up
u32 FASTCALL libGDR_GetDiscType()
{
	return CdRom_XA;
}
void FASTCALL libGDR_GetSessionInfo(u8* out,u8 ses)
{
	printf("libGDR_GetSessionInfo\n");
	FILE* f=fopen(ses==0?"gdrom/ses0.bin":"gdrom/ses2.bin","rb");
	fread(out,1,6,f);
	fclose(f);
}


*/
void FASTCALL libGDR_ReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
  // FRAME_PROF: the SD/USB fread behind the emulated GD-ROM. The guest reads
  // ahead in bursts, so this is bounded by the card, not by the emulator, and
  // one slow read is enough to blow a frame. Counted as rd= as well as timed,
  // because "one 40 ms read" and "sixty 0.7 ms reads" want different fixes.
  FP_T0(_fp_disc0);
  const u32 _dc0 = DISC_CENSUS() ? FP_TICKS32() : 0u;
  if (get_debug_gdrom() && get_debug_loop()) {
	  printf("[GDR] libGDR_ReadSector FAD=%u count=%u secsz=%u\n", StartSector, SectorCount, secsz);
  }
	if (CurrDrive)
		CurrDrive->ReadSector(buff,StartSector,SectorCount,secsz);

  if (DISC_CENSUS())
  {
    g_dc_ticks += FP_TICKS32() - _dc0;
    g_dc_calls++;
    g_dc_secs += SectorCount;
    g_dc_kb   += (SectorCount * secsz) >> 10;
    if      (SectorCount == 1)  g_dc_c1++;
    else if (SectorCount <= 8)  g_dc_c8++;
    else if (SectorCount <= 31) g_dc_c31++;
    else                        g_dc_c32++;
  }

  FP_ACC(disc, _fp_disc0);
  FP_BUMP(n_disc);
}

void FASTCALL libGDR_GetToc(u32* toc,u32 area)
{
	if (CurrDrive)
		GetDriveToc(toc,(DiskArea)area);
}
//TODO : fix up
u32 FASTCALL libGDR_GetDiscType()
{
	if (CurrDrive)
		return CurrDrive->GetDiscType();
	else
		return NoDisk;
}

void FASTCALL libGDR_GetSessionInfo(u8* out,u8 ses)
{
	GetDriveSessionInfo(out,ses);
}


//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL libGDR_Load()
{
	return rv_ok;	
}

//called when plugin is unloaded by emu , olny if dcInitGDR is called (eg , not called to enumerate plugins)
void FASTCALL libGDR_Unload()
{
	
}

//It's suposed to reset everything (if not a manual reset)
void FASTCALL libGDR_Reset(bool Manual)
{

}

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
s32 FASTCALL libGDR_Init(gdr_init_params* prm)
{
	if (!InitDrive())
		return rv_serror;
	NotifyEvent_gdrom(DiskChange,0);
	return rv_ok;
}

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void FASTCALL libGDR_Term()
{
	TermDrive();
}

