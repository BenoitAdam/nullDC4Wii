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
                double cur        = os_GetSeconds();

                if (s_limiter_next_deadline == 0.0)
                {
                    s_limiter_next_deadline = cur + target_sec;
                }
                else
                {
                    if (cur < s_limiter_next_deadline)
                        usleep((useconds_t)((s_limiter_next_deadline - cur) * 1000000.0));

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