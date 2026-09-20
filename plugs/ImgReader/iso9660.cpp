#include "iso9660.h"
#include <unistd.h> // For usleep
#include <stdlib.h> // malloc, for the DISC BULK READ staging buffer

// This is defined in main.cpp
extern "C" int get_debug_loop();
extern "C" int get_disc_bulk_preset();   // DISC BULK READ, see below

// DISC census counters (defined in ImgReader.cpp). Say which reader is live
// and, when a run is NOT coalesced, which of the five guards rejected it.
extern u32 g_dc_iso, g_dc_runs, g_dc_runsecs;
extern u32 g_dc_fb_small, g_dc_fb_notrk, g_dc_fb_neg, g_dc_fb_short, g_dc_fb_nomem;

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
// ---------------------------------------------------------------------------
// DISC BULK READ (get_disc_bulk_preset).
//
// Found with FRAME_PROF on Street Fighter Alpha 3: the pre-fight load produced
// a 115 ms frame -- five to six dropped frames of visible hitch -- of which
// disc=63.22 ms over rd=68 libGDR_ReadSector() calls, i.e. ~0.9 ms per call.
//
// The cost is not the bytes, it is the round trips. FillReadBuffer()
// (dc/gdrom/gdromv3.cpp) asks for up to 32 sectors at a time, and the loop
// below used to serve that with one fseek and one fread PER SECTOR: 32 trips
// through libfat for what is a single contiguous run of bytes in a single
// file. None of the buffering helps either, because the fseek before each
// read throws away whatever stdio had.
//
// iso_ReadRun() serves the longest run it can in one fseek + one fread and
// then converts out of the staging buffer. The run bound does NOT assume the
// track table is sorted: iso_ReadSSect() scans backwards and takes the first
// entry with FAD <= sector, so sector s belongs to track i exactly when no
// LATER entry also qualifies -- hence the run ends at the smallest FAD above
// StartSector found after i. Anything it declines (single sector, negative
// file offset, short read, missing track) falls through to the original
// per-sector path, which is left untouched so the two agree byte for byte.
// ---------------------------------------------------------------------------
#define ISO_BULK_MAX_SECS 32u    // FillReadBuffer() never asks for more
#define ISO_BULK_MAX_SSZ  2352u  // largest CD sector format

// Lazily allocated (73.5 KB), so the preset costs nothing while it is off.
static u8 *iso_bulk_buf = 0;
static bool iso_bulk_nomem = false;

// Returns how many sectors it served; 0 means "caller, use the slow path".
static u32 iso_ReadRun(u8 *buff, u32 StartSector, u32 SectorCount, u32 secsz)
{
	if (SectorCount < 2)
	{
		g_dc_fb_small++;       // the guest asked for one sector: nothing to coalesce
		return 0;
	}

	// Resolve the track exactly as iso_ReadSSect() would.
	s32 ti = -1;
	for (s32 i = (s32)iso_tc - 1; i >= 0; i--)
		if (iso_tracks[i].FAD <= StartSector) { ti = i; break; }
	if (ti < 0)
	{ g_dc_fb_notrk++; return 0; }

	file_TrackInfo *tr = &iso_tracks[ti];
	if (tr->f == 0 || tr->SectorSize == 0 || tr->SectorSize > ISO_BULK_MAX_SSZ)
	{ g_dc_fb_notrk++; return 0; }

	// The run may not cross into a later track (see the header comment).
	u32 run_end = 0xFFFFFFFFu;
	for (u32 j = (u32)ti + 1; j < iso_tc; j++)
		if (iso_tracks[j].FAD > StartSector && iso_tracks[j].FAD < run_end)
			run_end = iso_tracks[j].FAD;

	u32 run = SectorCount;
	if (run > ISO_BULK_MAX_SECS)                 run = ISO_BULK_MAX_SECS;
	if (run_end != 0xFFFFFFFFu && StartSector + run > run_end)
		run = run_end - StartSector;
	if (run < 2)
	{ g_dc_fb_small++; return 0; }

	s32 off2 = (s32)((StartSector - tr->FAD) * tr->SectorSize) + tr->offset;
	if (off2 < 0)
	{ g_dc_fb_neg++; return 0; }   // pre-start offset: slow path handles it

	if (iso_bulk_buf == 0)
	{
		if (iso_bulk_nomem)
			return 0;
		iso_bulk_buf = (u8*)malloc(ISO_BULK_MAX_SECS * ISO_BULK_MAX_SSZ);
		if (iso_bulk_buf == 0) { iso_bulk_nomem = true; g_dc_fb_nomem++; return 0; }
	}

	fseek(tr->f, off2, SEEK_SET);
	if (fread(iso_bulk_buf, tr->SectorSize, run, tr->f) != run)
	{ g_dc_fb_short++; return 0; }  // short read: slow path behaves as before

	for (u32 k = 0; k < run; k++)
	{
		u8 *src = iso_bulk_buf + (size_t)k * tr->SectorSize;
		u32 sec = StartSector + k;
		ConvertSector(src, buff, tr->SectorSize, secsz, sec);
		if (sec == 45000) PatchRegion_0(buff, secsz);
		if (sec == 45006) PatchRegion_6(buff, secsz);
		buff += secsz;
	}
	g_dc_runs++;
	g_dc_runsecs += run;
	return run;
}

void iso_DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{

  // DOLPHIN ERROR LOOP
	g_dc_iso++;   // census: proves the ISO reader is the live one

  if(get_debug_loop() == 1){
	  printf("GDR(ISO)->Read : Sector %d , size %d , mode %d \n",StartSector,SectorCount,secsz);
  }
	if (StartSector>150)
		StartSector-=150;
	while(SectorCount)
	{
		u32 got = get_disc_bulk_preset()
				  ? iso_ReadRun(buff, StartSector, SectorCount, secsz) : 0;
		if (got == 0)
		{
			iso_ReadSSect(buff,StartSector,secsz);
			got = 1;
		}
		buff        += secsz * got;
		StartSector += got;
		SectorCount -= got;
	}
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
	gdi_ses.SessionStart[0]=3;
	gdi_ses.SessionFAD[1]=gdi_toc.tracks[2].FAD;
	gdi_ses.SessionsEndFAD=549300;

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

