#include "cdi.h"

#include <ogc/system.h>
#define printf(...) SYS_Report(__VA_ARGS__)

extern "C" int get_debug_loop();

#define CDI_V2   0x80000004u
#define CDI_V3   0x80000005u
#define CDI_V35  0x80000006u

struct CdiTrack
{
    u32 FAD;
    u32 file_offset;   // byte offset in file where data sectors begin (after pregap)
    u32 sector_size;
    u32 length;        // data sectors (not counting pregap)
    u8  ctrl;
    u8  mode;
    u8  session;
};

static CdiTrack    cdi_tracks[101];
static u32         cdi_track_count;
static SessionInfo cdi_ses;
static TocInfo     cdi_toc;
static DiscType    cdi_disc_type;
static u8          cdi_sec_buf[2352];
static FILE*       cdi_fp;
static u32         cdi_leadout_fad;

// ─── sector reading ──────────────────────────────────────────────────────────

// GD-ROM high-density area starts at FAD 45150.
// Selfboot CDI discs store game data starting at FAD 150 (single density).
// When the BIOS thinks it's reading a GD-ROM HD area (FAD >= 45150),
// remap those reads to FAD 150+ so IP.BIN and the filesystem are served correctly.
#define CDI_GD_HD_START  45150u
#define CDI_GD_LD_START    150u
#define CDI_GD_REMAP_OFF (CDI_GD_HD_START - CDI_GD_LD_START)  // 45000

static void cdi_ReadOneSector(u8* out, u32 sector, u32 out_size)
{
    // FAD remapping removed: CdRom disc type means BIOS reads FAD 150 directly.

    for (s32 i = (s32)cdi_track_count - 1; i >= 0; i--)
    {
        if (cdi_tracks[i].FAD <= sector)
        {
            u32 byte_off = cdi_tracks[i].file_offset
                         + (sector - cdi_tracks[i].FAD) * cdi_tracks[i].sector_size;
            fseek(cdi_fp, byte_off, SEEK_SET);
            fread(cdi_sec_buf, cdi_tracks[i].sector_size, 1, cdi_fp);

            // ── IP.BIN header injection ───────────────────────────────────────
            // Selfboot CDIs built with BootDreams store the IP.TMPL bootstrap
            // but leave the first 0x300 bytes (disc header metadata) as zeros.
            // The Dreamcast BIOS reads FAD 150 (sector 0 of the data track) and
            // checks for "SEGA SEGAKATANA" at offset 0.  If it finds zeros, it
            // falls back to the BIOS menu.  We patch a valid header here so the
            // bootstrap can proceed.  The header fields below match ChuChu Rocket US.
            if (sector == 150 && cdi_sec_buf[0] == 0 && cdi_sec_buf[1] == 0
                && cdi_tracks[i].ctrl == 0x04)
            {
                // Check whether the whole first 16 bytes are zero (blank template)
                bool blank = true;
                for (int z = 0; z < 16; z++) if (cdi_sec_buf[z]) { blank = false; break; }
                if (blank)
                {
                    printf("[CDI] Injecting IP.BIN header into blank FAD 150 sector\n");
                    // Hardware ID  (0x000)
                    memcpy(cdi_sec_buf + 0x000, "SEGA SEGAKATANA ", 16);
                    // Maker ID     (0x010)
                    memcpy(cdi_sec_buf + 0x010, "SEGA ENTERPRISES", 16);
                    // Device info  (0x020) — selfboot CDIs are standard CDs
                    memcpy(cdi_sec_buf + 0x020, "CD-ROM1/1       ", 16);
                    // Area symbols (0x030)
                    memcpy(cdi_sec_buf + 0x030, "JUE             ", 16);
                    // Peripherals  (0x040)
                    memcpy(cdi_sec_buf + 0x040, "E000F10         ", 16);
                    // Product No   (0x050)
                    memcpy(cdi_sec_buf + 0x050, "T-8101N   ", 10);
                    // Version      (0x05A)
                    memcpy(cdi_sec_buf + 0x05A, "V1.002", 6);
                    // Release date (0x060)
                    memcpy(cdi_sec_buf + 0x060, "20000208  ", 10);
                    // Boot file    (0x070)
                    memcpy(cdi_sec_buf + 0x070, "1ST_READ.BIN    ", 16);
                    // Publisher    (0x080)
                    memcpy(cdi_sec_buf + 0x080, "SEGA ENTERPRISES", 16);
                    // Title        (0x090)
                    memcpy(cdi_sec_buf + 0x090, "ChuChu Rocket!                          ", 40);
                }
            }

            printf("[CDI] cdi_ReadOneSector FAD=%u track%d off=0x%08X sz=%u data: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                   sector, i, byte_off, cdi_tracks[i].sector_size,
                   cdi_sec_buf[0],cdi_sec_buf[1],cdi_sec_buf[2],cdi_sec_buf[3],
                   cdi_sec_buf[4],cdi_sec_buf[5],cdi_sec_buf[6],cdi_sec_buf[7]);
            ConvertSector(cdi_sec_buf, out, cdi_tracks[i].sector_size, out_size, sector);
            return;
        }
    }
    printf("[CDI] WARN: sector FAD=%u not found in any track\n", sector);
    memset(out, 0, out_size);
}

void cdi_DriveReadSector(u8* buff, u32 StartSector, u32 SectorCount, u32 secsz)
{
    printf("[CDI] ReadSector FAD=%u count=%u secsz=%u\n",
           StartSector, SectorCount, secsz);
    while (SectorCount--)
    {
        cdi_ReadOneSector(buff, StartSector, secsz);
        buff += secsz;
        StartSector++;
    }
}

// ─── TOC / session info ──────────────────────────────────────────────────────

void cdi_DriveGetTocInfo(TocInfo* toc, DiskArea area)
{
    memcpy(toc, &cdi_toc, sizeof(TocInfo));

    if (area == DoubleDensity)
    {
        // No high-density area on a standard CD — return empty
        printf("[CDI] GetTocInfo(DoubleDensity) => empty\n");
        memset(toc, 0xFF, sizeof(TocInfo));
        toc->LeadOut.FAD     = cdi_leadout_fad;
        toc->LeadOut.Control = 0x04;
        toc->LeadOut.Addr    = 0;
        toc->LeadOut.Session = 0;
        return;
    }

    // SingleDensity
    printf("[CDI] GetTocInfo(SingleDensity): FistTrack=%u LastTrack=%u LeadOutFAD=%u\n",
           toc->FistTrack, toc->LastTrack, toc->LeadOut.FAD);
    for (u32 i = 0; i < cdi_track_count; i++)
    {
        printf("[CDI]   toc track[%u]: FAD=%u ctrl=0x%02X addr=0x%02X session=%u\n",
               i+1, toc->tracks[i].FAD,
               toc->tracks[i].Control,
               toc->tracks[i].Addr,
               toc->tracks[i].Session);
    }
}

u32 cdi_DriveGetDiscType()
{
    printf("[CDI] GetDiscType => 0x%X (%s)\n",
           (u32)cdi_disc_type,
           cdi_disc_type == GdRom ? "GdRom" : "CdRom");
    return cdi_disc_type;
}

void cdi_GetSessionsInfo(SessionInfo* sessions)
{
    memcpy(sessions, &cdi_ses, sizeof(SessionInfo));
    printf("[CDI] GetSessionsInfo: count=%u endFAD=%u\n",
           cdi_ses.SessionCount, cdi_ses.SessionsEndFAD);
    for (u32 i = 0; i < cdi_ses.SessionCount; i++)
        printf("[CDI]   session[%u]: startTrack=%u FAD=%u\n",
               i, cdi_ses.SessionStart[i], cdi_ses.SessionFAD[i]);
}

// ─── helpers ─────────────────────────────────────────────────────────────────

static u16 read_u16(FILE* f)
{
    u8 b[2] = {0};
    fread(b, 1, 2, f);
    return (u16)b[0] | ((u16)b[1] << 8);
}

static u32 read_u32(FILE* f)
{
    u8 b[4] = {0};
    fread(b, 1, 4, f);
    return (u32)b[0] | ((u32)b[1]<<8) | ((u32)b[2]<<16) | ((u32)b[3]<<24);
}

static void skip(FILE* f, int n) { fseek(f, n, SEEK_CUR); }

// ─── header parser ───────────────────────────────────────────────────────────

// Per-track record layout, ported verbatim from devcast's cdipsr.cpp
// (CDI_read_track).  This is the authoritative DiscJuggler track descriptor
// walk; the previous hex-dump-derived layout was missing the two track start
// marks, the DJ extra-data / DJ4 markers, and read the sector size by track
// type instead of from the on-disk sector_size_value field.
//
//   u32  temp            ; if != 0 -> skip 8  (DJ 3.00.780+ extra data)
//   u8[10] start_mark     ; { 00 00 01 00 00 00 FF FF FF FF }
//   u8[10] start_mark     ; (same, twice)
//   skip 4
//   u8   filename_length  ; + filename[filename_length]
//   skip 11
//   skip 4
//   skip 4
//   u32  temp            ; if == 0x80000000 -> skip 8  (DJ4)
//   skip 2
//   u32  pregap_length
//   u32  length
//   skip 6
//   u32  mode             ; 0=audio, 1=Mode1, 2=Mode2
//   skip 12
//   u32  start_lba
//   u32  total_length
//   skip 16
//   u32  sector_size_value; 0->2048 1->2336 2->2352 4->2448
//   skip 29
//   if version != V2:
//       skip 5
//       u32 temp ; if == 0xffffffff -> skip 78  (DJ 3.00.780+ extra data)

static bool cdi_read_track(FILE* f, u32 version, u32 ses,
                           u32* out_pregap, u32* out_length, u32* out_total,
                           u32* out_mode, u32* out_lba, u32* out_ssize)
{
    static const u8 TRACK_START_MARK[10] =
        { 0, 0, 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
    u8 mark[10];

    u32 temp = read_u32(f);
    if (temp != 0) skip(f, 8); // DJ 3.00.780+ extra data

    fread(mark, 10, 1, f);
    if (memcmp(TRACK_START_MARK, mark, 10) != 0)
        printf("[CDI] WARN: missing track start mark (1)\n");
    fread(mark, 10, 1, f);
    if (memcmp(TRACK_START_MARK, mark, 10) != 0)
        printf("[CDI] WARN: missing track start mark (2)\n");

    skip(f, 4);

    u8 fname_len = 0;
    fread(&fname_len, 1, 1, f);
    char fname[256] = {0};
    fread(fname, 1, fname_len, f);
    skip(f, 11);

    skip(f, 4);
    skip(f, 4);

    temp = read_u32(f);
    if (temp == 0x80000000u) skip(f, 8); // DJ4

    skip(f, 2);
    u32 pregap   = read_u32(f);
    u32 length   = read_u32(f);
    skip(f, 6);
    u32 mode     = read_u32(f);
    skip(f, 12);
    u32 lba      = read_u32(f);
    u32 total    = read_u32(f);
    skip(f, 16);
    u32 ssize_v  = read_u32(f);

    u32 sector_size;
    switch (ssize_v)
    {
    case 0:  sector_size = 2048; break;
    case 1:  sector_size = 2336; break;
    case 2:  sector_size = 2352; break;
    case 4:  sector_size = 2448; break;
    default:
        printf("[CDI] WARN: unsupported sector_size_value %u, defaulting 2352\n", ssize_v);
        sector_size = 2352;
        break;
    }

    if (mode > 2)
        printf("[CDI] WARN: unsupported track mode %u\n", mode);

    skip(f, 29);
    if (version != CDI_V2)
    {
        skip(f, 5);
        temp = read_u32(f);
        if (temp == 0xFFFFFFFFu) skip(f, 78); // DJ 3.00.780+ extra data
    }

    printf("[CDI]   ses%u '%s' pregap=%u length=%u total=%u mode=%u lba=%u ssize=%u\n",
           ses, fname, pregap, length, total, mode, lba, sector_size);

    *out_pregap = pregap;
    *out_length = length;
    *out_total  = total;
    *out_mode   = mode;
    *out_lba    = lba;
    *out_ssize  = sector_size;
    return true;
}

// Skip the per-session header that precedes a session's track records, ported
// from CDI_skip_next_session (cdipsr.cpp): skip 4 + 8, and 1 more for non-V2.
static void cdi_skip_next_session(FILE* f, u32 version)
{
    skip(f, 4);
    skip(f, 8);
    if (version != CDI_V2) skip(f, 1);
}

static bool cdi_ParseHeader(FILE* f, u32 version, long file_size, u32 hdr_offset)
{
    printf("\n********************************CDI********************************\n");
    printf("[CDI] ParseHeader: version=0x%08X (%s) pos=0x%08lX\n",
           version,
           version==CDI_V2?"V2":version==CDI_V3?"V3":"V3.5",
           ftell(f));

    // CDI_get_sessions(): the header begins with a u16 session count.
    u16 session_count = read_u16(f);
    printf("[CDI] session_count=%u\n", session_count);

    if (session_count == 0 || session_count > 99)
    {
        printf("[CDI] ERROR: implausible session_count %u\n", session_count);
        printf("********************************END********************************\n\n");
        return false;
    }

    memset(&cdi_toc, 0xFF, sizeof(cdi_toc));
    memset(&cdi_ses, 0, sizeof(cdi_ses));

    cdi_track_count = 0;
    bool have_m1 = false, have_m2 = false, have_da = false;

    // file_position: running byte offset where the current track's data begins
    // in the image (DiscJuggler stores track data sequentially from byte 0).
    long file_position = 0;
    u32  end_fad = 0;

    for (u32 ses = 0; ses < session_count; ses++)
    {
        // CDI_get_tracks(): u16 track count for this session.
        u16 track_count = read_u16(f);
        printf("[CDI] Session %u: %u tracks\n", ses, track_count);

        if (track_count == 0)
        {
            printf("[CDI] Open session (0 tracks)\n");
        }
        else if (track_count > 99)
        {
            printf("[CDI] ERROR: implausible track_count=%u\n", track_count);
            printf("********************************END********************************\n\n");
            return false;
        }

        bool first_in_session = true;

        for (u32 trk = 0; trk < track_count; trk++)
        {
            u32 pregap, length, total, mode, lba, sector_size;
            if (!cdi_read_track(f, version, ses,
                                &pregap, &length, &total,
                                &mode, &lba, &sector_size))
            {
                printf("********************************END********************************\n\n");
                return false;
            }

            if (mode == 1) have_m1 = true;
            else if (mode == 2) have_m2 = true;
            else if (mode == 0) have_da = true;

            u8  ctrl     = (mode == 0) ? 0x00 : 0x04;
            u32 start_fad = lba + pregap;            // matches devcast t.StartFAD

            // Data for this track starts after its pregap (which is stored in
            // the image), so the readable data offset is:
            long data_offset = file_position + (long)pregap * sector_size;

            if (cdi_track_count < 100)
            {
                cdi_tracks[cdi_track_count].FAD         = start_fad;
                cdi_tracks[cdi_track_count].file_offset = (u32)data_offset;
                cdi_tracks[cdi_track_count].sector_size = sector_size;
                cdi_tracks[cdi_track_count].length      = length;
                cdi_tracks[cdi_track_count].ctrl        = ctrl;
                cdi_tracks[cdi_track_count].mode        = (u8)mode;
                cdi_tracks[cdi_track_count].session     = (u8)ses;
                cdi_track_count++;
            }

            if (first_in_session)
            {
                first_in_session = false;
                if (cdi_ses.SessionCount < 99)
                {
                    cdi_ses.SessionStart[cdi_ses.SessionCount] = cdi_track_count; // 1-based track #
                    cdi_ses.SessionFAD[cdi_ses.SessionCount]   = start_fad;
                    cdi_ses.SessionCount++;
                }
            }

            if (total < length + pregap)
                printf("[CDI] WARN: track seems truncated (total=%u < length=%u+pregap=%u)\n",
                       total, length, pregap);

            // Advance to the next track's data. Mirrors devcast's
            // track.position += total_length * sector_size.
            file_position += (long)total * sector_size;

            end_fad = lba + total;  // matches devcast rv->EndFAD
        }

        cdi_skip_next_session(f, version);
    }

    if (cdi_track_count == 0)
    {
        printf("[CDI] ERROR: no tracks parsed\n");
        printf("********************************END********************************\n\n");
        return false;
    }

    // Disc type — verbatim from devcast GuessDiscType(m1, m2, da).
    if (have_m1 && !have_da && !have_m2)
        cdi_disc_type = CdRom;
    else if (have_m2)
        cdi_disc_type = CdRom_XA;
    else if (have_da && have_m1)
        cdi_disc_type = CdRom_Extra;
    else
        cdi_disc_type = CdRom;
    printf("[CDI] disc type: 0x%X (M1=%d M2=%d DA=%d)\n",
           (u32)cdi_disc_type, have_m1, have_m2, have_da);

    // Leadout — verbatim from devcast: rv->LeadOut.StartFAD = rv->EndFAD.
    cdi_leadout_fad = end_fad;

    // ── Build TOC ─────────────────────────────────────────────────────────────
    for (u32 i = 0; i < cdi_track_count && i < 99; i++)
    {
        cdi_toc.tracks[i].FAD     = cdi_tracks[i].FAD;
        cdi_toc.tracks[i].Control = cdi_tracks[i].ctrl;
        cdi_toc.tracks[i].Addr    = 1;   // matches devcast t.ADDR=1
        cdi_toc.tracks[i].Session = cdi_tracks[i].session + 1;
    }
    cdi_toc.FistTrack = 1;
    cdi_toc.LastTrack = (u8)cdi_track_count;
    cdi_toc.LeadOut.FAD     = cdi_leadout_fad;
    cdi_toc.LeadOut.Control = 0;   // matches devcast LeadOut.CTRL=0
    cdi_toc.LeadOut.Addr    = 0;
    cdi_toc.LeadOut.Session = 0;

    cdi_ses.SessionsEndFAD = cdi_leadout_fad;

    printf("[CDI] TOC: %u tracks FistTrack=%u LastTrack=%u LeadOutFAD=%u\n",
           cdi_track_count, cdi_toc.FistTrack, cdi_toc.LastTrack, cdi_toc.LeadOut.FAD);
    printf("[CDI] Ses: count=%u endFAD=%u\n",
           cdi_ses.SessionCount, cdi_ses.SessionsEndFAD);
    for (u32 i = 0; i < cdi_ses.SessionCount; i++)
        printf("[CDI]   session[%u]: startTrack=%u FAD=%u\n",
               i, cdi_ses.SessionStart[i], cdi_ses.SessionFAD[i]);
    printf("********************************END********************************\n\n");
    return true;
}

// ─── init / term ─────────────────────────────────────────────────────────────

bool cdi_init(char* file)
{
    printf("\n********************************CDI********************************\n");
    printf("[CDI] cdi_init('%s')\n", file);

    FILE* f = fopen(file, "rb");
    if (!f)
    {
        printf("[CDI] ERROR: cannot open '%s'\n", file);
        printf("********************************END********************************\n\n");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    printf("[CDI] File size: %ld bytes (%.2f MB)\n",
           file_size, (float)file_size / 1048576.0f);

    fseek(f, -8, SEEK_END);
    u32 version    = read_u32(f);
    u32 hdr_offset = read_u32(f);

    printf("[CDI] version=0x%08X (%s)  hdr_offset=%u\n",
           version,
           version==CDI_V2?"V2":version==CDI_V3?"V3":
           version==CDI_V35?"V3.5":"UNKNOWN",
           hdr_offset);

    if (version != CDI_V2 && version != CDI_V3 && version != CDI_V35)
    {
        printf("[CDI] ERROR: bad version 0x%08X\n", version);
        printf("********************************END********************************\n\n");
        fclose(f); return false;
    }

    long hdr_pos;
    if (version == CDI_V2)
    {
        hdr_pos = 0;
    }
    else
    {
        hdr_pos = file_size - (long)hdr_offset;
        printf("[CDI] hdr_pos = %ld - %u = %ld (0x%08lX)\n",
               file_size, hdr_offset, hdr_pos, hdr_pos);
        if (hdr_pos < 0 || hdr_pos >= file_size)
        {
            printf("[CDI] ERROR: hdr_pos out of bounds\n");
            printf("********************************END********************************\n\n");
            fclose(f); return false;
        }
    }

    printf("********************************END********************************\n\n");

    cdi_fp = f;  // set early so peek in ParseHeader works
    fseek(f, hdr_pos, SEEK_SET);
    if (!cdi_ParseHeader(f, version, file_size, hdr_offset))
    {
        fclose(f);
        cdi_fp = NULL;
        return false;
    }

    return true;
}

void cdi_term()
{
    if (cdi_fp) { fclose(cdi_fp); cdi_fp = NULL; }
    cdi_track_count = 0;
}
