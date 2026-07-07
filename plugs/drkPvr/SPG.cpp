/*
    SPG emulation; Scanline/Raster beam registers & interrupts
    H blank interrupts aren't properly emulated
*/

#include "spg.h"
#include "Renderer_if.h"
#include "regs.h"
#include <ogc/system.h>
#include <unistd.h>

// Game-preset controlled toggle (see wii/game_presets.h, wii/main.cpp).
extern "C" int get_speed_limiter_preset();
#define SPEED_LIMITER() (get_speed_limiter_preset() != 0)

// Rez launch-crash investigation: SH4 dynarec + block-cache counters.
// Defined in dc/sh4/rec_v2/driver.cpp (ENABLE_PERF_MONITORING) and
// dc/sh4/rec_v2/blockmanager.cpp (BM_ENABLE_STATS), both enabled for this
// investigation. Reset every 2s so the numbers printed below are a
// per-report delta, not a running total.
void recSh4_GetPerfStats(u32* blocks_compiled, u32* cache_clears, u32* check_failures);
void recSh4_ResetPerfStats();
extern "C" void bm_GetStats(u32* hits, u32* misses, u32* cache_hits, u32* total_blocks);
extern "C" void bm_ResetStats();

// Rez freeze investigation: incremented in dc/gdrom/gdromv3.cpp and
// plugs/drkPvr/gxRend.cpp. Read here only — this report runs on the main
// emulation thread, same as the code that increments them, so no cross-
// thread synchronization is needed (unlike the watchdog-thread approach
// tried earlier, which raced with this thread's own printf/console use).
extern "C" volatile u32 g_gdrom_cmd_count;
extern "C" volatile u32 g_render_call_count;

u32 spg_InVblank = 0;
s32 spg_ScanlineSh4CycleCounter = 0;
u32 spg_ScanlineCount = 512;
u32 spg_CurrentScanline = (u32)-1;  // Explicit -1 cast, avoids signed/unsigned warning
u32 spg_VblankCount = 0;
s32 spg_LineSh4Cycles = 0;
u32 spg_FrameSh4Cycles = 0;

double spg_last_vps = 0;
static double s_limiter_next_deadline = 0.0; // os_GetSeconds() deadline for the next vblank (speed limiter)

// 54 MHz pixel clock (register defines it as 27 MHz, doubled here)
//54 mhz pixel clock (actually, this is defined as 27 .. why ? --drk)

#define PIXEL_CLOCK (27000000) // (54 * 1000 * 1000 / 2)

// Called when SPG registers are updated
void CalculateSync()
{
    u32 pixel_clock = FB_R_CTRL.vclk_div ? PIXEL_CLOCK : PIXEL_CLOCK / 2;

    spg_ScanlineCount = SPG_LOAD.vcount + 1;

    spg_LineSh4Cycles = (u32)(
        (u64)SH4_CLOCK * (u64)(SPG_LOAD.hcount + 1) / (u64)pixel_clock
    );

    // SCALER_CTL.hscale halves the active horizontal resolution (low-res 2D
    // titles), independently of the vertical interlace/240p scale below.
    float scale_x = SCALER_CTL.hscale ? 0.5f : 1.0f;

    if (SPG_CONTROL.interlace)
    {
        spg_LineSh4Cycles /= 2;
        rend_set_fb_scale(scale_x, 1.0f);
    }
    else
    {
        rend_set_fb_scale(scale_x,
            ((SPG_CONTROL.NTSC == 0 && SPG_CONTROL.PAL == 0) ||
             (SPG_CONTROL.NTSC == 1 && SPG_CONTROL.PAL == 1)) ? 1.0f : 0.5f);
    }

    spg_FrameSh4Cycles = spg_ScanlineCount * spg_LineSh4Cycles;
}

s32 render_end_pending_cycles = 0;

// Called from SH4 context each dispatch; updates PVR/TA state
void FASTCALL libPvr_UpdatePvr(u32 cycles)
{
    spg_ScanlineSh4CycleCounter -= (s32)cycles;

    if (spg_ScanlineSh4CycleCounter < spg_LineSh4Cycles)
    {
        // A full scanline has elapsed
        spg_CurrentScanline = (spg_CurrentScanline + 1) % spg_ScanlineCount;
        spg_ScanlineSh4CycleCounter += spg_LineSh4Cycles;

        // Scanline-triggered interrupts
        if (SPG_VBLANK_INT.vblank_in_interrupt_line_number == spg_CurrentScanline)
            params.RaiseInterrupt(holly_SCANINT1);

        if (SPG_VBLANK_INT.vblank_out_interrupt_line_number == spg_CurrentScanline)
            params.RaiseInterrupt(holly_SCANINT2);

        // Vblank window
        if (SPG_VBLANK.vbstart == spg_CurrentScanline)
            spg_InVblank = 1;

        if (SPG_VBLANK.vbend == spg_CurrentScanline)
            spg_InVblank = 0;

        // Interlaced field toggle
        SPG_STATUS.fieldnum = SPG_CONTROL.interlace ? (~SPG_STATUS.fieldnum & 1) : 0;

        SPG_STATUS.vsync   = spg_InVblank;
        SPG_STATUS.scanline = spg_CurrentScanline;

        // Start of vblank
        if (SPG_VBLANK.vbstart == spg_CurrentScanline)
        {
            spg_VblankCount++;

            // Note: holly_HBLank is actually VBlank on real HW — HBlank not yet emulated
            params.RaiseInterrupt(holly_HBLank);

            rend_vblank();

            // Update FPS display every 2 seconds
            double now = os_GetSeconds();
            double tdiff = now - spg_last_vps;

            if (tdiff > 2.0)
            {
                spg_last_vps = now;

                double spd_fps = FrameCount    / tdiff;
                double spd_vbs = spg_VblankCount / tdiff;
                double spd_cpu = (spd_vbs * spg_FrameSh4Cycles) / 1000000.0;
                double fullvbs = (spd_vbs / spd_cpu) * 200.0;
                double mv      = VertexCount  / 1000.0;

                VertexCount    = 0;
                FrameCount     = 0;
                spg_VblankCount = 0;

                // Determine video mode strings
                const char* mode;
                const char* res;

                if (SPG_CONTROL.NTSC == 0 && SPG_CONTROL.PAL == 1)
                {
                    mode = "PAL";
                    res  = SPG_CONTROL.interlace ? "480i" : "240p";
                }
                else if (SPG_CONTROL.NTSC == 1 && SPG_CONTROL.PAL == 0)
                {
                    mode = "NTSC";
                    res  = SPG_CONTROL.interlace ? "480i" : "240p";
                }
                else
                {
                    mode = "VGA";
                    res  = SPG_CONTROL.interlace ? "480i" : "480p";
                }

                char fpsStr[256];
                sprintf(fpsStr,
                    "%3.2f%% VPS:%3.2f(%s%s%3.2f)RPS:%3.2f vt:%4.2fK %4.2fK",
                    spd_cpu * 100.0 / 200.0, spd_vbs,
                    mode, res, fullvbs,
                    spd_fps,
                    (spd_fps > 0.0 ? mv / spd_fps / tdiff : 0.0),
                    mv / tdiff);

                rend_set_fps_text(fpsStr);

#ifndef TARGET_PSP
                printf("%s\n", fpsStr);
#endif
                // PSP profiler logging removed for Wii build — not applicable

                // Rez launch-crash investigation: dump recompiler/block-cache
                // deltas for this 2s window right next to the fps line, so a
                // death-spiral (fps collapsing while these numbers explode)
                // is visible in the same already-working report channel.
                u32 blocks_compiled = 0, cache_clears = 0, check_fails = 0;
                recSh4_GetPerfStats(&blocks_compiled, &cache_clears, &check_fails);
                recSh4_ResetPerfStats();

                u32 bm_hits = 0, bm_misses = 0, bm_cache_hits = 0, bm_total_blocks = 0;
                bm_GetStats(&bm_hits, &bm_misses, &bm_cache_hits, &bm_total_blocks);
                bm_ResetStats();

                static u32 s_last_gdrom_cmds = 0, s_last_render_calls = 0;
                u32 gdrom_cmds   = g_gdrom_cmd_count;
                u32 render_calls = g_render_call_count;

                char recStr[256];
                sprintf(recStr,
                    "  [rec] compiled=%u cacheClears=%u checkFails=%u | bm hits=%u misses=%u cacheHits=%u totalBlocks=%u | gdrom=%u(+%u) render=%u(+%u)",
                    blocks_compiled, cache_clears, check_fails,
                    bm_hits, bm_misses, bm_cache_hits, bm_total_blocks,
                    gdrom_cmds, gdrom_cmds - s_last_gdrom_cmds,
                    render_calls, render_calls - s_last_render_calls);
                printf("%s\n", recStr);

                s_last_gdrom_cmds   = gdrom_cmds;
                s_last_render_calls = render_calls;

                // Also append both lines to a persistent SD/USB log, flushed
                // immediately, so the numbers survive a hard freeze that
                // never lets the on-screen console be read/photographed.
                static FILE* s_dbg_log = nullptr;
                static bool s_dbg_log_tried = false;
                if (!s_dbg_log_tried)
                {
                    s_dbg_log_tried = true;
                    s_dbg_log = fopen("sd:/nulldc_debug.log", "w");
                    if (!s_dbg_log)
                        s_dbg_log = fopen("usb:/nulldc_debug.log", "w");
                }
                if (s_dbg_log)
                {
                    fprintf(s_dbg_log, "%s\n%s\n", fpsStr, recStr);
                    fflush(s_dbg_log);
                }
            }

            // ── Speed limiter: cap emulation at real-hardware (100%) speed ──
            // The SH4 thread doesn't VIDEO_WaitVSync() (see gxRend.cpp) so it
            // normally runs as fast as the host CPU allows; light frames push
            // speed past 100%. Sleeping only the amount we're AHEAD of the
            // real-hardware vblank period caps the top end without ever
            // penalizing frames that are already at/below real speed.
            //
            // Uses a fixed-cadence absolute deadline (deadline += target_sec)
            // rather than re-anchoring to the actual wake time. usleep() can
            // overshoot by a few ms depending on host scheduler granularity;
            // re-anchoring to the (overshot) wake time bakes that overshoot
            // into every subsequent frame permanently (observed as a steady
            // FPS well below the target, e.g. 50Hz PAL collapsing to ~37).
            // Anchoring to the deadline instead means one frame's overshoot
            // just shortens the next frame's wait, so the average converges
            // on the true target instead of drifting.
            if (SPEED_LIMITER())
            {
                double target_sec = (double)spg_FrameSh4Cycles / (double)SH4_CLOCK;

                // Rez freeze investigation: target_sec is only ever supposed to be
                // ~1/60 - 1/50 sec (one video frame). It has no sanity bound, so a
                // transient garbage SPG_LOAD (hcount/vcount) write — e.g. during a
                // BIOS-to-game video-timing handoff — can blow this up to something
                // enormous, which turns the usleep() below into a multi-second (or
                // much longer) genuine block: near-0% real speed, total silence,
                // looks exactly like a freeze. Clamp defensively and log if it fires.
                if (target_sec > 0.5 || target_sec < 0.0)
                {
                    printf("[SPG] speed limiter: bogus target_sec=%.6f (spg_FrameSh4Cycles=%u) — clamping to 1/60s\n",
                           target_sec, spg_FrameSh4Cycles);
                    target_sec = 1.0 / 60.0;
                }

                double cur = os_GetSeconds();

                if (s_limiter_next_deadline == 0.0)
                {
                    s_limiter_next_deadline = cur + target_sec;
                }
                else
                {
                    if (cur < s_limiter_next_deadline)
                    {
                        double sleep_sec = s_limiter_next_deadline - cur;
                        if (sleep_sec > 0.5)
                        {
                            printf("[SPG] speed limiter: bogus sleep_sec=%.6f — clamping to 0.1s\n", sleep_sec);
                            sleep_sec = 0.1;
                        }
                        usleep((useconds_t)(sleep_sec * 1000000.0));
                    }

                    s_limiter_next_deadline += target_sec;

                    // If we fell badly behind (debugger stall, frame hitch...),
                    // don't burn through a burst of unthrottled catch-up frames —
                    // resync the schedule to "now".
                    cur = os_GetSeconds();
                    if (s_limiter_next_deadline < cur - 0.25)
                        s_limiter_next_deadline = cur + target_sec;
                }
            }
            else
            {
                s_limiter_next_deadline = 0.0; // reset so re-enabling doesn't use a stale deadline
            }
        }
    }

    // Deferred render completion interrupt
    if (render_end_pending_cycles > 0)
    {
        render_end_pending_cycles -= (s32)cycles;
        if (render_end_pending_cycles <= 0)
        {
            params.RaiseInterrupt(holly_RENDER_DONE);
            params.RaiseInterrupt(holly_RENDER_DONE_isp);
            params.RaiseInterrupt(holly_RENDER_DONE_vd);
            rend_end_render();
        }
    }
}

bool spg_Init()
{
    return true;
}

void spg_Term()
{
}

void spg_Reset(bool Manual)
{
    CalculateSync();
}