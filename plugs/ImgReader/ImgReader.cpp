// nullGDR.cpp : Defines the entry point for the DLL application.
//

#include "ImgReader.h"
#include <stdio.h>
#include <string.h>
#include "common.h"

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
  // Rez freeze investigation: unconditional (the get_debug_gdrom/get_debug_loop
  // gated print below never fires now that those default off) — need to see
  // whether a read attempt even starts, and whether it returns, once the
  // freeze happens right after the GET_TOC exchange.
  printf("[GDR] libGDR_ReadSector ENTER FAD=%u count=%u secsz=%u CurrDrive=%p\n",
         StartSector, SectorCount, secsz, (void*)CurrDrive);

  if (get_debug_gdrom() && get_debug_loop()) {
	  printf("[GDR] libGDR_ReadSector FAD=%u count=%u secsz=%u\n", StartSector, SectorCount, secsz);
  }
	if (CurrDrive)
		CurrDrive->ReadSector(buff,StartSector,SectorCount,secsz);

  printf("[GDR] libGDR_ReadSector EXIT FAD=%u\n", StartSector);
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

