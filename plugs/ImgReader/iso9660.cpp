#include "iso9660.h"
#include <unistd.h> // For usleep

// This is defined in main.cpp
extern "C" int get_debug_loop();

bool inbios=true;
FILE* f_1=0;
FILE* f_2=0;



u8 isotemshit[5000];
struct file_TrackInfo
{
	FILE* f;
	u32 FAD;
	u32 SectorSize;
	u32 ctrl;
	s32 offset;

	bool ReadSector(u8 * buff,u32 sector,u32 secsz)
	{
		if (sector>=FAD)
		{
			if (SectorSize==0 || f==0)
				printf("Read from missing sector %d\n",sector);
			else
			{
				u8* ptr=isotemshit;
				s32 off2=(sector-FAD)*SectorSize + offset;
				// Rez freeze investigation: unconditional trace right around the
				// actual SD-card file I/O — if this is a genuine blocked syscall
				// (fits observed near-0% real Dolphin speed at the freeze point),
				// ENTER will print but EXIT never will.
				printf("[TRK] ReadSector ENTER sector=%u FAD=%u off2=%d f=%p\n", sector, FAD, off2, (void*)f);
				if (off2>=0)
				{
					fseek(f,off2,SEEK_SET);
					fread(ptr,SectorSize,1,f);
				}
				else
				{
					fseek(f,0,SEEK_SET);
					fread(ptr,SectorSize-off2,1,f);
					ptr+=off2;
				}
				printf("[TRK] ReadSector EXIT sector=%u\n", sector);
			//	printf("readed %d bytes from file 0x%X , converting to %d [sec %d]\n",
			//		SectorSize,f,secsz,sector);
				ConvertSector(ptr,buff,SectorSize,secsz,sector);
				if (sector==45000)
					PatchRegion_0(buff,secsz);
				if (sector==45006)
					PatchRegion_6(buff,secsz);
			}
			return true;
		}
		return false;
	}
};

file_TrackInfo iso_tracks[101];
TocInfo gdi_toc;
SessionInfo gdi_ses;
u32 iso_tc=0;

void iso_ReadSSect(u8* p_out,u32 sector,u32 secsz)
{
	for (s32 i=(s32)iso_tc-1;i>=0;i--)
	{
		if (iso_tracks[i].ReadSector(p_out,sector,secsz))
			break;
	}
}


void rss(u8* buff,u32 ss,FILE* file)
{
	fseek(file,ss*2352+0x10,SEEK_SET);
	fread(buff,2048,1,file);
}
void iso_DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
  // Rez freeze investigation: unconditional (the get_debug_loop-gated print
  // below never fires now that it defaults off).
  printf("[ISO] iso_DriveReadSector ENTER StartSector=%u count=%u secsz=%u\n", StartSector, SectorCount, secsz);

  // DOLPHIN ERROR LOOP
  if(get_debug_loop() == 1){
	  printf("GDR(ISO)->Read : Sector %d , size %d , mode %d \n",StartSector,SectorCount,secsz);
  }
	if (StartSector>150)
		StartSector-=150;
	while(SectorCount--)
	{
		iso_ReadSSect(buff,StartSector,secsz);
		buff+=secsz;
		StartSector++;
	}
	printf("[ISO] iso_DriveReadSector EXIT\n");
	return;
}
/*
void iso_GetSessionsInfo(SessionInfo* sessions)
{
	printf("iso_GetSessionsInfo\n");
}
void iso_DriveGetTocInfo(TocInfo* toc,DiskArea area)
{
	printf("GDROM toc\n");
	memset(toc,0,sizeof(TocInfo));
}
*/
void iso_DriveGetTocInfo(TocInfo* toc,DiskArea area)
{
	memcpy(toc,&gdi_toc,sizeof(TocInfo));
	if(area==SingleDensity)
	{
		toc->LeadOut.FAD=13085;
		toc->FistTrack=1;
		toc->LastTrack=2;

		for (int tr=2;tr<99;tr++)
		{
			toc->tracks[tr].FAD=0xFFFFFFF;
			toc->tracks[tr].Addr=0xFF;
			toc->tracks[tr].Control=0xFF;
		}
	}
	else
	{
		toc->LeadOut.FAD=549300;
		toc->FistTrack=3;
		toc->LastTrack=gdi_toc.LastTrack;

		for (int tr=0;tr<2;tr++)
		{
			toc->tracks[tr].FAD=0xFFFFFFF;
			toc->tracks[tr].Addr=0xFF;
			toc->tracks[tr].Control=0xFF;
		}
	}
}
void iso_GetSessionsInfo(SessionInfo* sessions)
{
	memcpy(sessions,&gdi_ses,sizeof(SessionInfo));
}
//TODO : fix up
u32 iso_DriveGetDiscType()
{
	if (iso_tc==0)
		return NoDisk;
	else
		return GdRom;
}

bool load_gdi(char* file_)
{
	char file[512];
	strcpy(file,file_);
	memset(&gdi_toc,0xFFFFFFFF,sizeof(gdi_toc));
	memset(&gdi_ses,0xFFFFFFFF,sizeof(gdi_ses));
	FILE* t=fopen(file,"rb");
	if (!t)
		return false;
	
	iso_tc=0;

	fscanf(t,"%d\r\n",&iso_tc);
	if (iso_tc==0 || iso_tc>99)
		return false;

	printf("\nGDI : %d tracks\n",iso_tc);

	char temp[512];
	char path[512];
	strcpy(path,file);
	size_t len=strlen(file);
	while (len>2)
	{
		if (path[len]=='/' || path[len]=='\\')
			break;
		len--;
	}
	len++;
	char* pathptr=&path[len];
	u32 TRACK=0,FADS=0,CTRL=0,SSIZE=0;
	s32 OFFSET=0;
	for (u32 i=0;i<iso_tc;i++)
	{

		//TRACK FADS CTRL SSIZE file OFFSET

		fscanf(t,"%d %d %d %d %s %d\r\n",&TRACK,&FADS,&CTRL,&SSIZE,temp,&OFFSET);
		printf("file %s[%d] : FAD:%d,CTRL : %d, SSIZE :%d,OFFSET:%d\n",temp,TRACK,FADS,CTRL,SSIZE,OFFSET);

		if (SSIZE!=0)
		{
			strcpy(pathptr,temp);
			iso_tracks[i].f=fopen(path,"rb");
		}

		iso_tracks[i].FAD=FADS;
		iso_tracks[i].offset=OFFSET;
		iso_tracks[i].SectorSize=SSIZE;
		iso_tracks[i].ctrl=CTRL;

		gdi_toc.tracks[i].Addr=0;
		gdi_toc.tracks[i].Control=CTRL;
		gdi_toc.tracks[i].FAD=FADS+150;
	}


	gdi_toc.LastTrack=iso_tc;

	gdi_ses.SessionCount=2;
	//session 1 : start @ track 1, and its fad
	gdi_ses.SessionStart[0]=1;
	gdi_ses.SessionFAD[0]=gdi_toc.tracks[0].FAD;


	//session 2 : start @ track 3, and its fad
	gdi_ses.SessionStart[1]=3;
	gdi_ses.SessionFAD[1]=gdi_toc.tracks[2].FAD;
	gdi_ses.SessionsEndFAD=549300;

	printf("GDI sessions: count=%u ses1(track=%u fad=%u) ses2(track=%u fad=%u) endFAD=%u\n",
	       gdi_ses.SessionCount,
	       gdi_ses.SessionStart[0], gdi_ses.SessionFAD[0],
	       gdi_ses.SessionStart[1], gdi_ses.SessionFAD[1],
	       gdi_ses.SessionsEndFAD);

	return true;
}
bool iso_init(char* file)
{
	size_t len=strlen(file);
	if (len>4)
	{
		if (strcmp( &file[len-4],".gdi")==0)    //stricmp on *nix is?
		{
			return load_gdi(file);
		}
	}
	return false;
}

void iso_term()
{
}

