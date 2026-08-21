# Yabause Wii — Frame Skipping & Auto Frame Skipping

**Deep analysis of the timing / frame-pacing subsystem, with a porting recipe for other 3D console emulators.**

Target tree: `yabause-r2926wii-beta26` (Yabause r2926, Wii port "beta 26", `GEKKO` build).
Analysis date: 2026-08-21.

---

## 0. TL;DR — why it "always runs at the right speed"

The Wii port keeps games at correct wall-clock speed on a host that is far too slow, using **four cooperating mechanisms**. Most people only notice the first one; the other three are what actually make it hold together.

| # | Mechanism | Where | What it does |
|---|-----------|-------|--------------|
| 1 | **Auto frame skip** — a bang-bang controller with a ±½-frame deadband | [vdp2.c:386-430](source/src/vdp2.c#L386-L430) | Drops *rendering only*, never emulation. Re-decides every emulated frame. |
| 2 | **Software frame limiter** — a busy-wait against a per-second absolute schedule | [vdp2.c:416-426](source/src/vdp2.c#L416-L426) | Same code block, the "too fast" arm. No vsync involved anywhere. |
| 3 | **Real-time-driven audio** — SCSP produces samples on demand for the DMA ring | [scsp.c:3688-3709](source/src/scsp.c#L3688-L3709), [sndwii.c](source/src/wii/sndwii.c) | Audio pitch is locked to the 48 kHz DAC, *not* to the emulated frame count, so skipping is inaudible. |
| 4 | **Guest underclocking** — `declinenum` / `dividenumclock` | [yabause.c:66-67](source/src/yabause.c#L66-L67), [yabause.c:108-135](source/src/yabause.c#L108-L135) | Reduces the amount of *emulation* work per frame. Default config runs the SH2 at **66.7 % of Saturn speed**. |

The single most important design decision, and the one worth copying verbatim:

> **The emulated machine always executes a complete, cycle-scheduled frame. Only the rasterisation is dropped — and even then, the display-list is still walked and the "drawing finished" interrupt is still raised.**

That last clause ([`Vdp1NoDraw()`](source/src/vdp1.c#L481-L589)) is why skipping doesn't desynchronise games. A naive "return early from the render function" skipper breaks any game that polls the GPU end-of-draw status or waits on the draw-end IRQ.

---

## 1. Code map

| Concern | File / lines |
|---|---|
| Skip state + controller | [vdp2.c:320-433](source/src/vdp2.c#L320-L433) (`Vdp2VBlankOUT`) |
| Skip enable/disable | [vdp2.c:1086-1097](source/src/vdp2.c#L1086-L1097) |
| Module statics | [vdp2.c:39-48](source/src/vdp2.c#L39-L48) |
| FPS counter | [vdp2.c:291-304](source/src/vdp2.c#L291-L304) |
| Fixed 1-in-7 throttle (dead code on Wii) | [vdp2.c:308-316](source/src/vdp2.c#L308-L316), [vdp2.c:387-392](source/src/vdp2.c#L387-L392) |
| Frame boundary / emulation loop | [yabause.c:485-795](source/src/yabause.c#L485-L795) (`YabauseEmulate`) |
| Cycle budget derivation | [yabause.c:108-135](source/src/yabause.c#L108-L135) (`YabauseChangeTiming`) |
| Host tick source | [yabause.c:823-841](source/src/yabause.c#L823-L841), [yabause.c:845-867](source/src/yabause.c#L845-L867) |
| Frame-advance / pause gate | [yabause.c:448-474](source/src/yabause.c#L448-L474) (`YabauseExec`) |
| VDP1 draw vs. no-draw | [vdp1.c:346-477](source/src/vdp1.c#L346-L477) / [vdp1.c:481-589](source/src/vdp1.c#L481-L589) |
| Where the actual pixels + present happen | [vidsoft.c:2943-3179](source/src/vidsoft.c#L2943-L3179) (`VIDSoftVdp2DrawEnd`) |
| Wii present (no vsync!) | [wii/yui.c:1228-1376](source/src/wii/yui.c#L1228-L1376) (`YuiSwapBuffers`) |
| Host main loop | [wii/yui.c:1187-1197](source/src/wii/yui.c#L1187-L1197) → [wii/perwii.c:532](source/src/wii/perwii.c#L532) |
| Audio backend | [wii/sndwii.c](source/src/wii/sndwii.c) |
| SCSP pump | [scsp.c:3614-3712](source/src/scsp.c#L3614-L3712) |
| Upstream (unmodified) reference | [vdp2.c.orig:315-416](source/src/vdp2.c.orig#L315-L416) |

---

## 2. What "a frame" means in this emulator

The host main loop is dead simple:

```c
/* wii/yui.c:1187 */
while (!done) {
    if (PERCore->HandleEvents() != 0)   /* -> perwii.c:532 -> YabauseExec() */
        return -1;
    ...
}
```

`YabauseExec()` → `YabauseEmulate()` runs **exactly one emulated frame** and returns. The loop in `YabauseEmulate` ([yabause.c:575-783](source/src/yabause.c#L575-L783)) is a *scanline/deciline scheduler*:

```
for each of MaxLineCount (263 NTSC / 313 PAL) lines:
    for each of `declinenum` sub-line slices:
        SH2Exec(MSH2, sh2cycles)
        SH2Exec(SSH2, sh2cycles)          if slave running
        Vdp2HBlankIN()                    at slice declinenum-1
        ScuExec(sh2cycles/2)
        M68KSync() / M68KExec(cycles)
        SmpcExec(usec) ; Cs2Exec(usec)
    Vdp2HBlankOUT()   -> snapshots Vdp2 regs into Vdp2Lines[line]
    ScspExec()        -> SCSP timers + audio generation
    if line == VBlankLineCount:  Vdp2VBlankIN()   + CheatDoPatches()
    if line == MaxLineCount:     Vdp2VBlankOUT()  + oneframeexec = 1
```

Consequences that matter for the skipper:

* **One host-loop iteration == one emulated frame == one input poll.** Input latency and input sampling rate are unaffected by skipping.
* `Vdp2VBlankOUT()` is reached **once per emulated frame regardless of skipping** — so the controller gets one sample per emulated frame, never per rendered frame. This is what makes the pacing loop stable.
* Everything except pixel-pushing (CPU, SCU, DMA, SMPC, CD block, 68K, SCSP, timers, interrupts) has already run by the time the skip decision is even consulted.

---

## 3. The auto-frameskip controller

### 3.1 The state

All function-local `static`s in [`Vdp2VBlankOUT`](source/src/vdp2.c#L320-L433):

```c
static int framestoskip   = 0;   /* countdown of frames still to be skipped */
static int framesskipped  = 0;   /* consecutive skips, reset on every drawn frame */
static int skipnextframe  = 0;   /* decision carried from the previous frame */
static u64 curticks       = 0;
static u64 diffticks      = 0;
static u32 framecount     = 0;   /* 1..50/60, index within the current 1-second window */
static u64 onesecondticks = 0;   /* real ticks accumulated in the current window */
```
plus file-scope [`u64 lastticks`](source/src/vdp2.c#L41) — timestamp of the previous frame boundary (post-wait).

### 3.2 Phase A — act on the previous decision (Wii variant)

```c
/* vdp2.c:334-384, GEKKO branch */
if (!skipnextframe)
{
    VIDCore->Vdp2DrawStart();                 /* back screen + line screen        */
    if (Vdp2Regs->TVMD & 0x8000) {
        VIDCore->Vdp2DrawScreens();           /* NBG0-3 / RBG0 tile rasterisation */
        Vdp1Draw();                           /* full VDP1 command list + raster  */
    } else
        Vdp1NoDraw();
    FPSDisplay();
    VIDCore->Vdp2DrawEnd();                   /* Titan compositing + present      */

    framesskipped = 0;
    if (framestoskip > 0) skipnextframe = 1;
}
else
{
    framestoskip--;
    skipnextframe = (framestoskip >= 1);
    Vdp1NoDraw();                             /* <-- THE critical line            */
    framesskipped++;
}
```

Upstream PC Yabause does this differently ([vdp2.c.orig:327-336](source/src/vdp2.c.orig#L327-L336)): it hot-swaps `VIDCore` to `&VIDDummy` so that every renderer entry point is still *called* but is a no-op stub, then swaps back. The Wii port replaced this with a hard branch and left a comment:

```c
#ifndef GEKKO // new autoskip does not work in Wii
```

The hard-branch version is cheaper (no indirect calls at all, no `Vdp2DrawStart`/`Vdp2DrawEnd`), but it is **less state-correct** — see §7.

### 3.3 Phase B — the controller

```c
/* vdp2.c:386-430 */
if (throttlespeed)                                   /* never enabled on Wii */
{
    if (framestoskip < 1) framestoskip = 6;          /* crude fixed 1-in-7   */
}
else if (autoframeskipenab && FrameAdvanceVariable == 0)
{
    framecount++;
    if (framecount > (yabsys.IsPal ? 50 : 60)) {     /* new 1-second window  */
        framecount     = 1;
        onesecondticks = 0;
    }

    curticks  = YabauseGetTicks();
    diffticks = curticks - lastticks;                /* cost of this frame   */

    /* --- BEHIND schedule by more than half a frame -> skip one frame ----- */
    if ((onesecondticks + diffticks) >
            ((yabsys.OneFrameTime * (u64)framecount) + (yabsys.OneFrameTime / 2))
        && framesskipped < 9)
    {
        skipnextframe = 1;
        framestoskip  = 1;
    }
    /* --- AHEAD of schedule by more than half a frame -> busy-wait -------- */
    else if ((onesecondticks + diffticks) <
             ((yabsys.OneFrameTime * (u64)framecount) - (yabsys.OneFrameTime / 2)))
    {
        for (;;) {
            curticks  = YabauseGetTicks();
            diffticks = curticks - lastticks;
            if ((onesecondticks + diffticks) >= (yabsys.OneFrameTime * (u64)framecount))
                break;                               /* wait to EXACTLY on-time */
        }
    }

    onesecondticks += diffticks;
    lastticks       = curticks;
}
```

### 3.4 The control law, stated plainly

Let

* `T` = `yabsys.OneFrameTime` — host ticks per emulated frame (see §4)
* `n` = `framecount`, 1…60 (NTSC) / 1…50 (PAL)
* `A` = `onesecondticks + diffticks` — real time elapsed since the window opened
* `S = n·T` — *ideal* time at which frame `n` should complete

Error `e = A − S` (positive = running late). Then:

```
e >  +T/2   and  framesskipped < 9   ->  skip exactly one frame
e <  −T/2                            ->  busy-wait until e == 0
otherwise                            ->  do nothing
```

Four properties fall out of this, and every one of them is deliberate:

1. **Absolute schedule, not per-frame delta.** The comparison is against `n·T` — a running target — not against "did this one frame take 16.7 ms". A frame that overran by 5 ms is remembered; the next frame is judged against cumulative time. This is what keeps *average* speed exact rather than merely "roughly right".

2. **±½-frame deadband.** Nothing happens inside `|e| ≤ T/2`. Without it, the controller would alternate skip/wait every frame and produce visible judder. With it, the system settles into a stable duty cycle (e.g. a steady 2-drawn / 1-skipped pattern) instead of oscillating.

3. **Asymmetric correction.** The "too slow" arm applies a *bounded, minimal* correction (`framestoskip = 1`, re-evaluated next frame). The "too fast" arm applies an *exact* correction (waits until `e == 0`, not merely until `e > −T/2`). Slowness is nudged; speed is clamped hard. That asymmetry is what makes it feel like a speed limiter rather than a rubber band.

4. **The one-second window resets the integrator.** When `framecount` wraps, `onesecondticks = 0` — accumulated debt *or* credit is forgiven. This is the anti-death-spiral valve: after a heavy 800 ms cutscene the emulator does not owe 30 frames of catch-up that it can never repay. The cost is that long-term drift is not corrected across window boundaries — but since the error is bounded by the deadband at each boundary, drift stays under ~½ frame per second worst case.

### 3.5 The skip cap

`framesskipped < 9`, with `framesskipped` reset to 0 on every drawn frame, means **at most 9 consecutive skips → at least 1 frame in 10 is drawn**. Floor display rate: 6 fps (NTSC) / 5 fps (PAL). Below that the emulator gives up on real-time and just runs slow — a deliberate choice, because a display that stops updating entirely reads as a hang.

Note `framestoskip` is only ever set to `1` in the auto path, so the sequence is always *decide → skip one → re-measure*. There is no "skip N in a row" burst. The 9-cap therefore limits a *chain* of independent decisions, not a single burst.

### 3.6 Interaction with pause / frame-advance

The controller is gated on `FrameAdvanceVariable == 0` (`RunNormal`, [movie.h:28-30](source/src/movie.h#L28-L30)). During single-step debugging the pacing and skipping are both disabled so every stepped frame is rendered, and `lastticks` goes stale — the first frame after resuming will show a huge `diffticks` and trigger one skip. Harmless, but worth knowing if you copy the structure.

---

## 4. The time base

```c
/* yabause.c:852-853 */
#elif defined(GEKKO)
   yabsys.tickfreq = secs_to_ticks(1);          /* libogc timebase = bus clock / 4 */

/* yabause.c:861-862 */
   yabsys.OneFrameTime = type ? (yabsys.tickfreq / 50)
                              : (yabsys.tickfreq * 1001 / 60000);
```

On Wii the timebase is 60.75 MHz (243 MHz bus / 4), read via `gettime()` ([yabause.c:830-831](source/src/yabause.c#L830-L831)) — a monotonic 64-bit counter with ~16 ns resolution and no syscall cost. Measured values:

| Format | `OneFrameTime` | Period | Rate |
|---|---|---|---|
| NTSC | 1 013 512 ticks | 16.683 ms | **59.940 Hz** |
| PAL  | 1 215 000 ticks | 20.000 ms | **50.000 Hz** |

Deadband half-width: 506 756 ticks ≈ **8.34 ms** (NTSC).

Two things to note if you port this:

* All arithmetic is `u64` against a high-resolution monotonic counter. Do not attempt this with a millisecond timer — the deadband is 8 ms wide and you would be quantising the error to 12 % of the deadband.
* There is a 0.1 % inconsistency in the Wii build: the *pacing* target is 59.94 Hz (`1001/60000`), but the *emulated* frame period used to derive cycle budgets is exactly 1/60 ([yabause.c:124-125](source/src/yabause.c#L124-L125), GEKKO branch uses `1.0/60.0`, upstream uses `1.0/(60/1.001)`). Emulated SMPC/CD microseconds therefore run 0.1 % fast relative to the wall clock. Negligible in practice, but it is a real discrepancy, not rounding.

### 4.1 There is no vsync anywhere

`YuiSwapBuffers` ([wii/yui.c:1228-1376](source/src/wii/yui.c#L1228-L1376)) ends with:

```c
GX_DrawDone();                       /* blocks until GP is idle */
GX_CopyDisp(xfb[fbsel], GX_TRUE);
GX_Flush();
VIDEO_SetNextFramebuffer(xfb[fbsel]);
VIDEO_Flush();                       /* no VIDEO_WaitVSync() */
```

`VIDEO_WaitVSync()` appears only in menu code ([wii/yui.c:1113](source/src/wii/yui.c#L1113), [1211](source/src/wii/yui.c#L1211), [1612](source/src/wii/yui.c#L1612), [1705](source/src/wii/yui.c#L1705)) — never in the emulation loop. This is essential and easy to get wrong when porting: **if you leave a vsync wait in the present path, the busy-wait limiter and the display refresh fight each other** and you get beat-frequency judder. Yabause Wii accepts tearing in exchange for the software limiter owning the clock outright.

---

## 5. Why audio survives the skipping

This is the half of the design that people forget, and the reason the emulator *feels* like it is running at full speed even at 12 fps.

### 5.1 Sample generation is decoupled from frames

The Wii front-end explicitly selects non-frame-accurate audio:

```c
/* wii/yui.c:1182-1183 */
ScspSetFrameAccurate(0);
YabauseSetDecilineMode(1);
```

With `scspframeaccurate == 0`, `ScspExec()` takes this path **every scanline** ([scsp.c:3688-3709](source/src/scsp.c#L3688-L3709)):

```c
if ((audiosize = SNDCore->GetAudioSpace()))
{
    if (audiosize > scspsoundlen) audiosize = scspsoundlen;
    scsp_update(bufL, bufR, audiosize);
    SNDCore->UpdateAudio(bufL, bufR, audiosize);
}
```

`SNDWiiGetAudioSpace()` ([wii/sndwii.c:216-226](source/src/wii/sndwii.c#L216-L226)) returns free space in a 12-block ring buffer whose read pointer `soundpos` is advanced **by the AI DMA completion interrupt** ([wii/sndwii.c:53-66](source/src/wii/sndwii.c#L53-L66)). So:

> Audio production is rate-limited by the real 48 kHz DAC. The emulator generates exactly as many samples as the hardware has consumed, no more.

Pitch is therefore *structurally* correct no matter how the emulation is performing. Skipping a video frame consumes no audio time and produces no audio discontinuity. Compare with frame-accurate mode (`scspframeaccurate = 1`), where a fixed `scspsoundlen` block is generated per emulated frame — that couples audio rate to emulation rate and would stutter the moment the frame budget is missed.

There is a 12-block × (1/60 s) ≈ **200 ms** buffer absorbing jitter ([wii/sndwii.c:26-29](source/src/wii/sndwii.c#L26-L29)), plus a 44.1 kHz → 48 kHz fixed-point resampler ([wii/sndwii.c:146-177](source/src/wii/sndwii.c#L146-L177)) since the Wii AI is 48 kHz-only.

### 5.2 But SCSP *timers* stay on emulated time

```c
/* scsp.c:3624-3628 */
ScspInternalVars->scsptiming2 += ((scspsoundlen << 16) + scsplines / 2) / scsplines;
scsp_update_timer(ScspInternalVars->scsptiming2 >> 16);
```

`scsplines` is 263/313 ([scsp.c:3470-3471](source/src/scsp.c#L3470-L3471)) and this runs once per emulated scanline. SCSP timer interrupts — which drive most Saturn sound drivers and occasionally game logic — therefore advance with **emulated** time, not real time. Getting this split right is subtle and important:

| Quantity | Clocked by | Rationale |
|---|---|---|
| Sample *output rate* | real time (DAC drain) | pitch must never wobble |
| SCSP timer IRQs | emulated time | game logic must stay deterministic |
| 68K sound-driver execution | emulated cycles | ditto |

---

## 6. The second speed lever: `declinenum` and `dividenumclock`

These are Wii-only globals ([yabause.c:66-67](source/src/yabause.c#L66-L67)) exposed in the on-console Timing menu ([wii/yui.c:2514-2525](source/src/wii/yui.c#L2514-L2525)) and persisted to XML ([wii/yui.c:2888-2891](source/src/wii/yui.c#L2888-L2891)):

```c
int declinenum     = 15;   // 10 in original yabause
int dividenumclock = 1;    // 1  in original yabause
```

Superficially `declinenum` looks like "how many sub-line slices per scanline" — i.e. a pure interrupt-granularity knob. **It is not.** The `GEKKO` branches introduce `declinenum` into *both* the deciline duration and the per-slice cycle increment, so it appears squared in the denominator:

```c
/* yabause.c:124-125 — deciline_time now divides by declinenum */
const double deciline_time = yabsys.IsPal ? 1.0 / 50.0 / 313.0 / (double)declinenum
                                          : 1.0 / 60.0 / 263.0 / (double)declinenum;
yabsys.DecilineStop = (u32)(freq_shifted * deciline_time + 0.5);

/* yabause.c:492 — and cyclesinc divides by declinenum AGAIN */
const u32 cyclesinc = yabsys.DecilineMode ? yabsys.DecilineStop * 10 / declinenum
                                          : yabsys.DecilineStop * 10;
```

Cycles actually executed per scanline = `declinenum × (DecilineStop·10/declinenum)` = `DecilineStop·10`, while a correct line is `DecilineStop·declinenum`. So:

> **Effective SH2 clock ratio = 10 / `declinenum`.**

Verified numerically (NTSC, `CLKTYPE_26MHZ` → 26.847 MHz nominal, correct 1701.3 cycles/line):

| `declinenum` | cycles/line | ratio vs. Saturn | effective SH2 clock | note |
|---:|---:|---:|---:|---|
| 2  | 314.5  | 0.185 | — | **u32 overflow**, value is garbage |
| 4  | 4253.3 | 2.500 | — | **u32 overflow**, value is garbage |
| 5  | 3402.6 | 2.000 | 53.7 MHz | 2× overclock |
| 10 | 1701.3 | **1.000** | 26.85 MHz | exact — matches upstream |
| **15 (default)** | **1134.2** | **0.667** | **17.90 MHz** | ~⅓ less CPU work per frame |
| 17 | 1000.8 | 0.588 | 15.8 MHz | menu maximum |

So the shipped default **underclocks the emulated Saturn CPU by one third**. That is a second, independent speed knob: it reduces the amount of interpretation work per emulated frame, which reduces how often the frameskipper has to intervene. The trade is authenticity — games that were CPU-bound on real hardware will internally drop to a lower update rate, but *wall-clock* pacing stays correct because the frame boundary is still 263 scanlines and the limiter still pins it to 16.68 ms.

The same `10/declinenum` factor is applied to the microsecond clock:

```c
/* yabause.c:498 */
const u32 usecinc = yabsys.DecilineMode ? yabsys.DecilineUsec * 10 / declinenum
                                        : yabsys.DecilineUsec * 10;
```

so `SmpcExec()` and `Cs2Exec()` (SMPC and CD block) also advance at 66.7 % of real rate by default. That is almost certainly why the port needed the extra `smpcperipheraltiming` / `smpcothertiming` compensation knobs ([smpc.c:46-49](source/src/smpc.c#L46-L49), [smpc.c:627](source/src/smpc.c#L627), [smpc.c:648](source/src/smpc.c#L648)).

`dividenumclock` (1…9) is cleaner: it divides `freq_base` only ([yabause.c:117](source/src/yabause.c#L117)) and does *not* touch `usec_shifted` ([yabause.c:119](source/src/yabause.c#L119)). It is a pure SH2/SCU underclock with the peripheral clock left at real rate.

---

## 7. Bugs, drift and limitations found

These are real defects in the shipped code. If you port the design, port it without these.

### 7.1 `u32` overflow for `declinenum ≤ 4`

`DecilineStop * 10` overflows 32 bits once `declinenum < ~4.15`. The menu allows 2…17 ([wii/yui.c:2515-2516](source/src/wii/yui.c#L2515-L2516)), so settings 2, 3 and 4 silently produce wrong cycle budgets (measured: `declinenum=2` → 0.185× instead of 5×; `declinenum=4` → 2.5× instead of 2.5× only by coincidence of the wrap). Fix: compute in `u64`, or restructure so `declinenum` appears once.

### 7.2 The 68K runs 1.5× too fast at the default setting

```c
/* yabause.c:525-526, GEKKO/NTSC */
m68kcycles      = yabsys.DecilineMode ? (7154/declinenum)/10 : 716;
m68kcenticycles = yabsys.DecilineMode ? 7154-((7154/declinenum)/10)*100 : 20;
```

The intent (per the comment at [yabause.c:524](source/src/yabause.c#L524)) is 715.4 cycles per scanline spread over `declinenum` calls. The actual average per call is ~71.54 regardless of `declinenum`:

| `declinenum` | avg cycles/call | cycles/line | correct | ratio |
|---:|---:|---:|---:|---:|
| 10 | 71.54 | 715.4  | 715.4 | 1.00 |
| 15 | 71.54 | 1073.1 | 715.4 | **1.50** |

The formula is only correct at `declinenum == 10`. At the default 15 the sound CPU runs 50 % fast — and 2.25× fast *relative to* the (underclocked) SH2. Audible impact is limited because sample rate and SCSP timers are clocked elsewhere (§5), so this mostly just means the sound driver has spare headroom. Still unintentional.

### 7.3 VDP1 framebuffer state drifts on skipped frames

On the Wii skip path, `VIDCore->Vdp2DrawEnd()` is not called, and the VDP1 double-buffer flip lives *inside* it:

```c
/* vidsoft.c:3162 */
VIDSoftVdp1SwapFrameBuffer();
/* vidsoft.c:3174-3178 — and the manual erase */
if ((Vdp1Regs->FBCR & 2) && (Vdp1Regs->TVMR & 8)) {
    Vdp1External.manualerase = 1;
    VIDSoftVdp1EraseFrameBuffer();
}
```

So on a skipped frame the front/back VDP1 buffers do **not** swap and the erase does not happen, while `Vdp1External.manualchange` / `manualerase` continue to be set by guest register writes ([vdp1.c:289-294](source/src/vdp1.c#L289-L294)). Likewise `VIDSoftVdp1DrawStart()` — which recomputes `vdp1width`/`vdp1height`/interlace from `TVMR`/`FBCR` and erases the back buffer ([vidsoft.c:1768-1865](source/src/vidsoft.c#L1768-L1865)) — is reached only via `Vdp1Draw()`, so it is skipped too.

Practical symptom: games using manual framebuffer change/erase (VDP1 as a scratch surface, some transparency and motion-blur effects) can show a stale or mis-erased sprite layer on the first frame *after* a skip run. The upstream `VIDDummy` swap approach does not have this problem because every entry point is still invoked. This is the price the Wii port paid for the cheaper hard branch, and it is the one part of the design I would **not** copy as-is.

### 7.4 `throttlespeed` is dead code on Wii

`SpeedThrottleEnable()`/`Disable()` ([vdp2.c:308-316](source/src/vdp2.c#L308-L316)) are never called from the Wii front-end — only from GTK/Qt/Windows/Cocoa. The `framestoskip = 6` branch at [vdp2.c:387-392](source/src/vdp2.c#L387-L392) is unreachable here. Mentioned only so you do not waste time on it: it is a crude fixed 1-in-7 skipper, not part of the adaptive system.

### 7.5 The FPS number is the *render* rate

`FPSDisplay()` ([vdp2.c:291-304](source/src/vdp2.c#L291-L304)) is called only inside the drawn branch, so `fps` counts drawn frames per second. This is exactly the user-visible symptom described as "very low FPS but running at the right speed": the counter reads 12, the game plays at 60 Hz timing. Also note `OSDPushMessage` does a `vsprintf` + `strdup` + `free` on every drawn frame ([osdcore.c:89-102](source/src/osdcore.c#L89-L102)) — a small but non-zero per-frame allocation.

### 7.6 Per-line register snapshot cost is not skippable

`Vdp2HBlankOUT()` `memcpy`s the whole `Vdp2` register struct into `Vdp2Lines[]` on **every** scanline of **every** frame, skipped or not ([vdp2.c:276-281](source/src/vdp2.c#L276-L281)). ~263 copies of ~290 bytes per frame that the skipper cannot remove, and which is only consumed by the renderer. An obvious optimisation for a port: gate it on `!skipnextframe`.

---

## 8. Worked traces

Assume NTSC, `T = 16.68 ms`, deadband `±8.34 ms`. "Cost" = time to fully emulate one frame *including* rendering; skipped frames cost only the emulation part.

### 8.1 Host at ~50 % speed (emulate 11 ms, render 22 ms → 33 ms drawn / 11 ms skipped)

| n | drawn? | cost | A | S = n·T | e | action |
|---|---|---|---|---|---|---|
| 1 | yes | 33.0 | 33.0 | 16.7 | +16.3 | e > +8.34 → skip next |
| 2 | no  | 11.0 | 44.0 | 33.4 | +10.6 | e > +8.34 → skip next |
| 3 | no  | 11.0 | 55.0 | 50.0 |  +5.0 | deadband → draw next |
| 4 | yes | 33.0 | 88.0 | 66.7 | +21.3 | skip next |
| 5 | no  | 11.0 | 99.0 | 83.4 | +15.6 | skip next |
| 6 | no  | 11.0 |110.0 |100.1 |  +9.9 | skip next |
| 7 | no  | 11.0 |121.0 |116.8 |  +4.2 | deadband → draw next |

Settles to roughly 1 drawn in 3.5 → ~17 fps displayed, 60 fps emulated, error bounded within ±1.3 frames. Note how the error never runs away: each skip removes 22 ms of debt while only 16.7 ms accrues.

### 8.2 Host faster than needed (12 ms/frame)

Every frame `e ≈ −4.7·n` grows negative; on the first frame where `e < −8.34` the busy-wait fires and pulls `e` to exactly 0. Result: a wait roughly every other frame, average exactly 59.94 Hz, all frames drawn.

### 8.3 Host catastrophically slow (emulation alone > 16.68 ms)

`e` grows every frame regardless of skipping. `framesskipped` reaches 9, the skip is inhibited, one frame is drawn, `framesskipped` resets, and the cycle repeats — a hard 1-in-10 render floor. The game runs slow; audio keeps its pitch (§5) but the SCSP is starved of new material and the DMA ring underruns audibly. This is the regime where `declinenum` (§6) is the only remaining tool.

---

## 9. Porting recipe for another 3D console emulator

### 9.1 Preconditions — check these before you start

The design pays off only if all of these hold:

1. **Rendering is a large, separable fraction of frame cost.** On Yabause Wii the software rasteriser + Titan compositor + RGBA→GX blit dominate. If your emulator is CPU-bound (dynarec-limited, or a hardware renderer where the host GPU is idle anyway), skipping presentation saves nothing and you need a different lever entirely (§9.6).
2. **You can drop rasterisation without dropping the display-list walk.** This is the hard requirement. See §9.3.
3. **Audio output is, or can be made, decoupled from emulated frame count.**
4. **You have a high-resolution monotonic host clock** (µs or better; ns preferred).
5. **Your emulation loop has a clean "one frame" boundary** you can hook, reached whether or not anything was rendered.

### 9.2 The controller, generalised

Drop-in, host-agnostic. This is the Yabause logic with the bugs removed and the parameters named.

```c
typedef struct {
    /* --- config --- */
    uint64_t frame_period;     /* host ticks per emulated frame            */
    uint64_t deadband;         /* = frame_period / 2                       */
    uint32_t window_frames;    /* = target fps, e.g. 60. Integrator reset  */
    int      max_consec_skip;  /* = 9. Guarantees a render floor           */

    /* --- state --- */
    uint64_t last_ticks;
    uint64_t window_ticks;     /* == onesecondticks                        */
    uint32_t frame_in_window;  /* == framecount, 1-based                   */
    int      skip_next;
    int      frames_to_skip;
    int      consec_skipped;
} fs_state;

void fs_init(fs_state *s, uint64_t tickfreq, double fps, uint64_t now)
{
    s->frame_period    = (uint64_t)(tickfreq / fps);
    s->deadband        = s->frame_period / 2;
    s->window_frames   = (uint32_t)(fps + 0.5);
    s->max_consec_skip = 9;
    s->last_ticks      = now;
    s->window_ticks    = 0;
    s->frame_in_window = 0;
    s->skip_next = s->frames_to_skip = s->consec_skipped = 0;
}

/* Call ONCE per emulated frame, at the frame boundary, AFTER the
   render-or-skip has happened for this frame. Returns nothing;
   read s->skip_next to decide what to do with the NEXT frame.      */
void fs_frame_end(fs_state *s, int this_frame_was_drawn, uint64_t (*now)(void))
{
    /* --- bookkeeping for the frame that just finished --- */
    if (this_frame_was_drawn) {
        s->consec_skipped = 0;
        s->skip_next = (s->frames_to_skip > 0);
    } else {
        s->frames_to_skip--;
        s->skip_next = (s->frames_to_skip >= 1);
        s->consec_skipped++;
    }

    /* --- controller --- */
    if (++s->frame_in_window > s->window_frames) {
        s->frame_in_window = 1;
        s->window_ticks    = 0;          /* forgive accumulated error */
    }

    uint64_t cur  = now();
    uint64_t diff = cur - s->last_ticks;
    uint64_t elapsed  = s->window_ticks + diff;
    uint64_t schedule = s->frame_period * (uint64_t)s->frame_in_window;

    if (elapsed > schedule + s->deadband) {
        if (s->consec_skipped < s->max_consec_skip) {
            s->skip_next      = 1;
            s->frames_to_skip = 1;
        }
    } else if (elapsed + s->deadband < schedule) {
        for (;;) {                        /* see 9.5 about sleeping here */
            cur  = now();
            diff = cur - s->last_ticks;
            if (s->window_ticks + diff >= schedule) break;
        }
    }

    s->window_ticks += diff;
    s->last_ticks    = cur;
}
```

Usage at your frame boundary:

```c
int draw = !fs.skip_next;
if (draw) { render_frame(); present(); }
else      { render_frame_headless(); }     /* <-- see 9.3, NOT a no-op */
fs_frame_end(&fs, draw, host_ticks);
```

### 9.3 The part everyone gets wrong: what a skipped frame must still do

`Vdp1NoDraw()` ([vdp1.c:481-589](source/src/vdp1.c#L481-L589)) is a near-clone of `Vdp1Draw()` that walks the *entire* display list, executes only the state commands, and produces the same side effects:

```c
void Vdp1NoDraw(void) {
    Vdp1Regs->addr = 0;
    Vdp1Regs->EDSR >>= 1;                        /* BEF <- CEF, CEF <- 0 */
    command = T1ReadWord(Vdp1Ram, Vdp1Regs->addr);
    while (!(command & 0x8000) && commandCounter < 2000) {
        if (!(command & 0x4000)) switch (command & 0x000F) {
            case 0: case 1: case 2: case 4: case 5: case 6:
                break;                            /* DRAW ops: skipped   */
            case 8:  VIDCore->Vdp1UserClipping();     break;   /* STATE ops: */
            case 9:  VIDCore->Vdp1SystemClipping();   break;   /* still run  */
            case 10: VIDCore->Vdp1LocalCoordinate();  break;
            default: Vdp1Regs->EDSR |= 2; ... return;          /* same abort */
        }
        /* identical link/jump/call/return traversal */
    }
    Vdp1Regs->EDSR |= 2;                         /* end-of-draw status   */
    ScuSendDrawEnd();                            /* end-of-draw IRQ      */
}
```

Translate this into a checklist for your target console. On a skipped frame you must still:

* **Walk the entire command/display list** at the same cost model — games time themselves against it and some *write into* the list as it is consumed.
* **Execute all state-setting commands** (clip windows, matrix/local-coordinate ops, texture/blend mode setup). Their effects persist into the next drawn frame.
* **Reproduce the same abort/error behaviour** on malformed commands, including any status-register bits.
* **Update every status register the guest can poll** — busy/idle flags, current-command pointers (`COPR`/`LOPR` here), end-of-frame bits.
* **Raise every interrupt the drawn path raises** — draw-end, DMA-complete, vblank, hblank.
* **Advance every clock**: CPU, coprocessors, timers, DMA, peripheral, disc.
* **Consume the same GPU-visible memory state** — if the guest can read back a framebuffer or a depth buffer, you cannot skip the write that produces it (see §9.4).

And on a skipped frame you must **not**:
* touch the presentation path (no swap, no vsync, no blit),
* generate audio "for a frame" (audio is time-driven, not frame-driven — §5),
* skip the input poll (Yabause polls once per *emulated* frame, which is correct).

### 9.4 Adaptations for a hardware-rendered 3D emulator

Yabause Wii is a software rasteriser, which makes "skip the pixels" trivially safe. For an N64/PS1/Dreamcast/PSP-class emulator with a GPU backend, three extra hazards:

1. **Framebuffer readback / CPU-side effects.** Any game that reads the framebuffer back (motion blur, feedback, software post-processing, or the many N64 titles that CPU-touch the colour buffer) will see stale data if you skipped the frame that produced it. Mitigation: track whether the guest has read from the render target since the last draw; if it has, mark that target "readback-live" and never skip a frame that writes it. Cheaper heuristic: never skip two frames in a row for titles on a known list — ugly but it is what most emulators actually do.

2. **Render-to-texture chains.** A skipped frame that would have produced an RT which a *later, drawn* frame samples gives you a stale or blank texture. Either keep RT passes and skip only the final on-screen pass, or forbid skipping when any RT was written in the previous frame. Skipping only the final present pass is usually the right answer and still recovers most of the cost, because RT passes are typically small.

3. **Deferred/queued GPU work.** If your backend queues commands and the host GPU is the bottleneck, skipping CPU-side command translation does not free the GPU. Measure where the time actually goes before assuming the skipper will help. Note that Yabause Wii's `GX_DrawDone()` is a *blocking* GP sync on every drawn frame — the port deliberately keeps the pipeline shallow so that `diffticks` is a truthful measurement of the frame's real cost. If your renderer is deeply pipelined, `diffticks` measures submission, not completion, and the controller will systematically underestimate the cost. Either sync before sampling the clock, or measure with GPU timestamps.

### 9.5 Recommended deviations from the original

If I were reimplementing this today, five changes:

| # | Change | Why |
|---|---|---|
| 1 | `u64` throughout the cycle-budget math | Fixes §7.1. The Wii code overflows for a third of its own menu range. |
| 2 | Replace the pure spin with `sleep(remaining − slack); spin(slack)` | The Wii spins the CPU for up to 8 ms per frame. Free on a console; on a modern host it burns a core and wrecks battery/thermals. Sleep to within ~1 ms, then spin. |
| 3 | Adopt the upstream `VIDDummy` core-swap rather than the Wii hard branch, *or* explicitly replicate the framebuffer swap/erase on the skip path | Fixes §7.3. The hard branch is faster but leaks renderer state. |
| 4 | Gate per-line register snapshotting on `!skip_next` | Fixes §7.6, free win. |
| 5 | Make `max_consec_skip` and the deadband configurable | 9 and ½-frame are good defaults, not universal ones. A deadband of ¼ frame tracks tighter at the cost of more skip/draw alternation; ¾ is smoother but sloppier. |

One deviation I would **not** make: do not replace the bang-bang controller with a PI controller or a moving-average predictor. The deadband + absolute-schedule + window-reset combination is remarkably well matched to the problem — the disturbance (frame cost) is bursty and bimodal, not smooth, and a predictive controller mostly succeeds at lagging behind scene changes.

### 9.6 If your emulator is CPU-bound instead

Then frame skipping alone will not save you, and the Wii port's second lever is the one to study: **underclock the guest** (§6). Reducing emulated CPU cycles per frame while keeping the frame *boundary* pinned to real time preserves wall-clock pacing at the cost of the game's internal update rate. Do it deliberately and cleanly, though — one factor, in one place, in 64-bit math — rather than the accidental `10/declinenum` the Wii build ended up with. And keep the peripheral/µs clock separate from the CPU clock (as `dividenumclock` does and `declinenum` does not), or you will find yourself adding compensation knobs for the disc drive and the controller port exactly as this port did.

---

## 10. Parameter reference

| Parameter | Location | Default (Wii) | Range | Effect |
|---|---|---|---|---|
| `autoframeskipenab` | [vdp2.c:39](source/src/vdp2.c#L39) | 1 | 0/1 | Master enable. Set via `EnableAutoFrameSkip()` / `DisableAutoFrameSkip()` ([vdp2.c:1086-1097](source/src/vdp2.c#L1086-L1097)), driven by the `frameskipoff` menu item ([wii/yui.c:1177-1180](source/src/wii/yui.c#L1177-L1180), [wii/yui.c:2449-2464](source/src/wii/yui.c#L2449-L2464)). |
| deadband | [vdp2.c:407](source/src/vdp2.c#L407), [416](source/src/vdp2.c#L416) | `OneFrameTime/2` | hard-coded | Hysteresis. ~8.34 ms NTSC. |
| max consecutive skips | [vdp2.c:408](source/src/vdp2.c#L408) | 9 | hard-coded | Render floor = 6 fps NTSC / 5 fps PAL. |
| integrator window | [vdp2.c:398](source/src/vdp2.c#L398) | 60 / 50 frames | hard-coded | Error forgiveness period. |
| `framestoskip` per decision | [vdp2.c:414](source/src/vdp2.c#L414) | 1 | hard-coded | No burst skipping. |
| `declinenum` | [yabause.c:66](source/src/yabause.c#L66) | **15** (upstream 10) | 2–17 menu | SH2 + µs clock ratio = `10/declinenum`. **≤4 overflows.** |
| `dividenumclock` | [yabause.c:67](source/src/yabause.c#L67) | 1 | 1–9 menu | Pure SH2/SCU underclock, peripherals unaffected. |
| `scspframeaccurate` | [scsp.c:3056](source/src/scsp.c#L3056) | **0** | 0/1 | 0 = audio driven by DAC drain. Set at [wii/yui.c:1182](source/src/wii/yui.c#L1182). |
| audio ring depth | [wii/sndwii.c:26](source/src/wii/sndwii.c#L26) | 12 blocks | — | ~200 ms of jitter absorption. |
| `smpcperipheraltiming` | [smpc.c:47](source/src/smpc.c#L47) | 1000 | 500–3200 menu | Compensates the `declinenum` µs-clock distortion. |
| `smpcothertiming` | [smpc.c:48](source/src/smpc.c#L48) | 1050 | 1000–1200 menu | Ditto. |
| `throttlespeed` | [vdp2.c:40](source/src/vdp2.c#L40) | 0 | — | Fixed 1-in-7 skipper. **Unreachable on Wii.** |

---

## 11. Summary judgement

The auto-frameskipper itself is about 40 lines of code and is genuinely good: absolute-schedule error tracking, a well-chosen deadband, asymmetric correction, a hard render floor, and a periodic integrator reset that prevents catch-up spirals. It is worth copying almost unchanged.

What makes it *work* on this platform, though, is the surrounding architecture, and that is the part worth internalising before porting:

* the emulated machine is never skipped, only the rasteriser;
* the display list is walked and the completion interrupt fired even on skipped frames;
* audio is clocked by the DAC, while audio *timers* are clocked by emulation;
* no vsync anywhere in the emulation loop — the software limiter owns the clock;
* and a separate underclock knob handles the case where even zero rendering is too slow.

Copy the controller, but copy the contract in §9.3 first. A frameskipper that gets the controller right and the contract wrong will look correct in menus and desynchronise in games.
