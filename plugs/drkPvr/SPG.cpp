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

// PVR X-scaler preset (see wii/game_presets.h, wii/main.cpp).
extern "C" int get_x_scaler_preset();
#define X_SCALER() (get_x_scaler_preset() != 0)

// Hardware-like HOLLY IRQ delays (see wii/game_presets.h, wii/main.cpp).
extern "C" int get_render_delay_preset();
#define RENDER_DELAY() (get_render_delay_preset() != 0)

u32 spg_InVblank = 0;
s32 spg_ScanlineSh4CycleCounter = 0;
u32 spg_ScanlineCount = 512;
u32 spg_CurrentScanline = (u32)-1;  // Explicit -1 cast, avoids signed/unsigned warning
u32 spg_VblankCount = 0;
u32 spg_VblankCountTotal = 0; // monotonic, never reset — frameskip AUTO pace reference (gxRend.cpp)
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

    // SCALER_CTL.hscale: the TA renders the scene at DOUBLE width (1280) and
    // the video scaler halves it 2:1 on framebuffer write (horizontal SSAA —
    // Omicron The Nomad Soul, Wacky Races). With the x_scaler preset on, widen
    // the projected canvas to 2x so the 1280-wide geometry maps to the full
    // screen; without it only the left half shows. Preset off keeps the legacy
    // 640 canvas (SCALER_CTL writes don't even resync then — see regs.cpp).
    float scale_x = (X_SCALER() && SCALER_CTL.hscale) ? 2.0f : 1.0f;

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

    // [SPG] diagnostic: dump the sync setup on every mode change. The FPS
    // stats line, the speed limiter and rend_vblank() all key off the
    // scanline counter hitting SPG_VBLANK.vbstart — and the counter wraps at
    // vcount+1. If a game programs vbstart beyond that wrap point, the
    // internal vblank event never fires and the stats go silent while the
    // game itself keeps running (its interrupts use SPG_VBLANK_INT instead).
    // printf("[SPG] sync: vcount=%d hcount=%d interlace=%d NTSC=%d PAL=%d lines=%d line_cycles=%d vbstart=%d vbend=%d%s\n",
    //     (int)SPG_LOAD.vcount, (int)SPG_LOAD.hcount, (int)SPG_CONTROL.interlace,
    //     (int)SPG_CONTROL.NTSC, (int)SPG_CONTROL.PAL,
    //     (int)spg_ScanlineCount, (int)spg_LineSh4Cycles,
    //     (int)SPG_VBLANK.vbstart, (int)SPG_VBLANK.vbend,
    //     (SPG_VBLANK.vbstart >= spg_ScanlineCount) ? "  <-- vbstart OUT OF RANGE, vblank event will NEVER fire!" : "");
    // fflush(stdout); // mode changes are rare; make sure this survives a hard power-off
}

s32 render_end_pending_cycles = 0;

// ── RENDER_DELAY() preset: hardware-like HOLLY IRQ scheduling ────────────────
// Some games (Marvel vs Capcom 2) pace their main loop off the TA list-complete
// and render-done interrupts. Raising them (nearly) instantly makes the game's
// frame scheduler collapse: emulation speed reads >100% while the game only
// submits a handful of renders per second. Real hardware takes time for both
// the TA list processing and the CORE render, so with the preset on we delay:
//   * each list-complete IRQ by 200 SH4 cycles after its END-OF-LIST parameter
//   * render-done ISP / TSP / Video at 800k / 850k / 900k SH4 cycles after
//     STARTRENDER (staggered, like real HW raises them in that order)
// Values come from originaldave_'s emulator (holly.cpp), where they fixed the
// low-FPS games without hurting others. Preset off = legacy instant list IRQs
// and the single VtxCnt*15 (min 50000) render-done burst below.

static const HollyInterruptID s_ListEndIrqID[5] =
{
    holly_OPAQUE,
    holly_OPAQUEMOD,
    holly_TRANS,
    holly_TRANSMOD,
    holly_PUNCHTHRU
};

#define LIST_END_IRQ_DELAY 200

static s32 s_list_irq_cycles[5] = { 0, 0, 0, 0, 0 };
static u32 s_list_irq_active = 0;   // bitmask of lists with a pending IRQ

// Staggered render-done counters, armed by StartRender() (gxRend.cpp) when the
// preset is on; render_end_pending_cycles stays 0 in that mode.
s32 render_isp_pending_cycles = 0;
s32 render_tsp_pending_cycles = 0;
s32 render_vd_pending_cycles  = 0;

// Called from the TA (ta.h) instead of raising the list-complete IRQ directly.
void spg_SchedListEndIrq(u32 list)
{
    if (list >= 5)
        return;     // corrupt/unknown ListType in the EOL parameter: no IRQ

    if (!RENDER_DELAY())
    {
        params.RaiseInterrupt(s_ListEndIrqID[list]);
        return;
    }

    s_list_irq_cycles[list] = LIST_END_IRQ_DELAY;
    s_list_irq_active |= 1u << list;
}

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
            spg_VblankCountTotal++;

            // Note: holly_HBLank is actually VBlank on real HW — HBlank not yet emulated
            params.RaiseInterrupt(holly_HBLank);

            rend_vblank();

            // Update FPS display every 1 second
            double now = os_GetSeconds();
            double tdiff = now - spg_last_vps;

            if (tdiff > 1.0)
            {
                spg_last_vps = now;

                double spd_fps = FrameCount    / tdiff;
                double spd_vbs = spg_VblankCount / tdiff;
                double spd_cpu = (spd_vbs * spg_FrameSh4Cycles) / 1000000.0;
                double fullvbs = (spd_vbs / spd_cpu) * 200.0;
                double mv      = VertexCount  / 1000.0;

                VertexCount     = 0;
                FrameCount      = 0;
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

                sprintf(
                    fpsStr,
                    "FPS: %.2f\nSPEED: %.2f%%",
                    spd_fps,
                    spd_cpu * 100.0 / 200.0);

                rend_set_fps_text(fpsStr);

#ifndef TARGET_PSP
                printf(
                    "%3.2f%% VPS:%3.2f(%s%s%3.2f)RPS:%3.2f vt:%4.2fK %4.2fK\n",
                    spd_cpu * 100.0 / 200.0, spd_vbs,
                    mode, res, fullvbs,
                    spd_fps,
                    (spd_fps > 0.0 ? mv / spd_fps / tdiff : 0.0),
                    mv / tdiff);
                fflush(stdout); // once per 1s: keep the log tail intact if the Wii is powered off
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

    // Deferred render completion interrupt (legacy path, RENDER_DELAY() off)
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

    // RENDER_DELAY() staggered render-done: ISP, then TSP, then Video.
    // rend_end_render() runs with the last (Video) IRQ = true end of render.
    if (render_isp_pending_cycles > 0)
    {
        render_isp_pending_cycles -= (s32)cycles;
        if (render_isp_pending_cycles <= 0)
            params.RaiseInterrupt(holly_RENDER_DONE_isp);
    }

    if (render_tsp_pending_cycles > 0)
    {
        render_tsp_pending_cycles -= (s32)cycles;
        if (render_tsp_pending_cycles <= 0)
            params.RaiseInterrupt(holly_RENDER_DONE);
    }

    if (render_vd_pending_cycles > 0)
    {
        render_vd_pending_cycles -= (s32)cycles;
        if (render_vd_pending_cycles <= 0)
        {
            params.RaiseInterrupt(holly_RENDER_DONE_vd);
            rend_end_render();
        }
    }

    // RENDER_DELAY() deferred list-complete IRQs
    if (s_list_irq_active)
    {
        for (u32 i = 0; i < 5; i++)
        {
            if (!(s_list_irq_active & (1u << i)))
                continue;

            s_list_irq_cycles[i] -= (s32)cycles;
            if (s_list_irq_cycles[i] <= 0)
            {
                s_list_irq_active &= ~(1u << i);
                params.RaiseInterrupt(s_ListEndIrqID[i]);
            }
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
    // Drop any in-flight delayed IRQs — a reset mid-render must not fire
    // stale render-done / list-complete interrupts into the fresh state.
    render_end_pending_cycles = 0;
    render_isp_pending_cycles = 0;
    render_tsp_pending_cycles = 0;
    render_vd_pending_cycles  = 0;
    s_list_irq_active = 0;

    CalculateSync();
}