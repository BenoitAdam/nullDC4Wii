# Frame Skipping in Flycast

An analysis of how Flycast implements manual and automatic frame skipping, based on a read
of the source tree.

---

## 1. One choke point: `QueueRender()`

All frame skipping in Flycast happens in a single function,
[`core/hw/pvr/ta_ctx.cpp:49-81`](core/hw/pvr/ta_ctx.cpp#L49-L81), called from
[`rend_start_render()`](core/hw/pvr/Renderer_if.cpp#L417) when the game triggers
STARTRENDER:

```cpp
bool QueueRender(TA_context* ctx)
{
    verify(ctx != 0);

    bool skipFrame = !rend_is_enabled();
    if (!skipFrame)
    {
        RenderCount++;
        if (RenderCount % (config::SkipFrame + 1) != 0)
            skipFrame = true;                                  // (1) manual skip
        else if (config::ThreadedRendering && rqueue != nullptr
                && (config::AutoSkipFrame == 0 || (config::AutoSkipFrame == 1 && SH4FastEnough)))
            // The previous render hasn't completed yet so we wait.
            // If autoskipframe is enabled (normal level), we only do so if the CPU is
            // running fast enough over the last frames
            frame_finished.Wait();                             // (2) stall instead of skipping
    }

    if (skipFrame || rqueue)
    {
        tactx_Recycle(ctx);                                    // (3) throw the display list away
        if (rend_is_enabled())
            fskip++;
        return false;
    }
    // disable net rollbacks until the render thread has processed the frame
    rend_disable_rollback();
    frame_finished.Reset();
    verify(rqueue == nullptr);
    rqueue = ctx;

    return true;
}
```

`rqueue` is the **single-slot render queue**. It is set here and cleared in
[`FinishRender()`](core/hw/pvr/ta_ctx.cpp#L92) at the end of the render thread's work. So:

> **`rqueue != nullptr` means "the render thread is still busy with the previous frame".**

That single pointer test is the entire GPU-load signal. There is no timing measurement of
the GPU anywhere.

Crucially, `scheduleRenderDone(ctx)` is called *before* `QueueRender`, and
[`rend_end_render()`](core/hw/pvr/Renderer_if.cpp#L427) raises the `RENDER_DONE` /
`RENDER_DONE_isp` / `RENDER_DONE_vd` interrupts either way. **The emulated machine never
notices a frame was dropped**; only the host-side render is discarded. This is what makes
frame skipping safe with respect to game logic.

---

## 2. Manual frame skipping — `config::SkipFrame`

| | |
|---|---|
| Config key (native) | `ta.skip` — [`core/cfg/option.cpp:100`](core/cfg/option.cpp#L100) |
| Config key (libretro) | `<core>_frame_skipping` — [`shell/libretro/option.cpp:83`](shell/libretro/option.cpp#L83) |
| Range | 0 – 6 |
| UI | [`core/ui/settings_video.cpp:311`](core/ui/settings_video.cpp#L311) |

Pure modulo arithmetic: render 1 frame out of `SkipFrame + 1`. Unconditional, no
measurement of anything.

Two things to note:

- It counts **every** render, including render-to-texture passes — there is no
  `ctx->rend.isRTT` check in `QueueRender`. So manual frameskip also drops RTT passes,
  which is why it visually breaks games that rely on them (mirrors, in-game monitors, some
  shadow/reflection effects).
- It is evaluated *before* the auto path, and the two compose: a manual skip
  short-circuits the `else if`, so on manually skipped frames the emu thread never stalls
  waiting for the GPU.

---

## 3. Automatic frame skipping — `config::AutoSkipFrame`

| | |
|---|---|
| Config key (native) | `pvr.AutoSkipFrame` — [`core/cfg/option.cpp:102`](core/cfg/option.cpp#L102) |
| Config key (libretro) | `<core>_auto_skip_frame` — [`shell/libretro/option.cpp:85`](shell/libretro/option.cpp#L85) |
| Values | 0 = Disabled, 1 = Normal, 2 = Maximum |
| UI | [`core/ui/settings_video.cpp:302-309`](core/ui/settings_video.cpp#L302-L309) |

This is **not** "skip N frames" logic. It is a **back-pressure policy**. The question it
answers is:

> *The render thread is behind. Do I block the SH4 thread, or do I drop this frame?*

### Truth table

| Mode | GPU behind + CPU fast | GPU behind + CPU slow | Effect |
|---|---|---|---|
| **0 — Disabled** | **wait** | **wait** | Emulation slows down to GPU speed; every frame is shown |
| **1 — Normal** | **wait** | **skip** | Self-regulating (see below) |
| **2 — Maximum** | **skip** | **skip** | Emulation never blocks on the GPU |

Read the condition in `QueueRender` as:

> *"Wait (i.e. do **not** skip) if the policy says stalling is acceptable."*

Mode 2 makes the `else if` condition false, so control falls straight through to
`if (skipFrame || rqueue)` and — because `rqueue` is non-null — the frame is recycled and
dropped.

### Requires threaded rendering

Without `config::ThreadedRendering`, `pvrQueue.enqueue(Render)`
[executes synchronously](core/hw/pvr/Renderer_if.cpp#L92-L99) on the emu thread, so
`rqueue` is always `nullptr` by the time `QueueRender` is reached and auto-skip is
effectively dead code.

That is why both the [GUI](core/ui/settings_video.cpp#L302-L309) and the
[libretro core option](shell/libretro/libretro_core_options.h#L614-L631) document it as
"only applies when Threaded Rendering is enabled".

---

## 4. The `SH4FastEnough` heuristic

Computed once per vblank in [`core/hw/pvr/spg.cpp:159-172`](core/hw/pvr/spg.cpp#L159-L172):

```cpp
u64 now = getTimeMs();
cpu_time_idx = (cpu_time_idx + 1) % cpu_cycles.size();      // ring buffer of 4
if (cpu_cycles[cpu_time_idx] != 0)
{
    u32 cycle_span = (u32)(sh4_sched_now64() - cpu_cycles[cpu_time_idx]);  // emulated SH4 cycles
    u64 time_span  = now - real_times[cpu_time_idx];                       // host milliseconds
    float cpu_speed = ((float)cycle_span / time_span) / (SH4_MAIN_CLOCK / 100000);
    SH4FastEnough = cpu_speed >= 85.f;
}
else {
    SH4FastEnough = false;
}
cpu_cycles[cpu_time_idx] = sh4_sched_now64();
real_times[cpu_time_idx] = now;
```

- `cpu_cycles` / `real_times` are **4-entry ring buffers** indexed by
  `cpu_time_idx = (idx + 1) % 4`, so the sample compared against is always **4 vblanks
  old** — a ~66 ms sliding window, which gives the measurement built-in hysteresis.
- With `SH4_MAIN_CLOCK = 200 MHz` ([`core/build.h:121`](core/build.h#L121)), the divisor
  `200e6 / 100000 = 2000` turns cycles-per-millisecond into a **percentage of real
  Dreamcast speed**.
- Threshold: **85%**.
- It is reset to `false` in [`spg_Reset()`](core/hw/pvr/spg.cpp#L276), so the first frames
  after a reset always behave as "CPU slow".

### The feedback loop — the interesting part

`cpu_speed` is emulated cycles over **wall clock**, and the emu thread's wall clock
includes the time it spends blocked in `frame_finished.Wait()` and in
[`renderEnd.Wait()`](core/hw/pvr/Renderer_if.cpp#L442). That makes mode 1 self-regulating:

1. GPU falls behind → emu thread stalls (the preferred behaviour: no dropped frames).
2. Stalling costs wall time → measured `cpu_speed` falls.
3. Once it drops under 85%, `SH4FastEnough` goes false → mode 1 stops waiting and starts
   dropping frames.
4. Dropping frames removes the stall → speed recovers above 85% → it goes back to waiting.

It oscillates around the 85% line, dropping only as many frames as needed to hold roughly
full speed. That is exactly the "GPU **and** CPU both running slow" wording in the UI: a
GPU-bound-but-still-fast-enough system keeps every frame, and only when stalling has
actually dragged emulation below 85% does it start sacrificing frames.

Note that `SH4FastEnough` is a mix of both bottlenecks by construction — a genuinely
CPU-bound game (heavy SH4 recompiled code) also pushes it below 85%, at which point mode 1
stops piling GPU stalls on top of an already-late CPU.

Mode 2 skips the arbitration entirely and always favours emulation speed over frame
completeness.

---

## 5. What a skipped frame actually saves

`tactx_Recycle(ctx)` returns the TA context to the pool without rendering it. Since
[`ta_parse()` runs inside `Renderer::Process()`](core/rend/gles/gles.cpp#L1073) — i.e. on
the *render* thread, not the emu thread — skipping a frame avoids:

- display-list parsing → vertex/index buffer building (`ta_parse`,
  [`core/hw/pvr/ta_vtx.cpp:1317`](core/hw/pvr/ta_vtx.cpp#L1317))
- texture cache lookups, texture conversion and GPU upload
- all draw calls
- the presentation / buffer swap

It does **not** save SH4 time: the game has already written the TA FIFO, and that raw data
has already been stored into `ta_tad`.

> **Frame skipping in Flycast is a GPU / render-thread relief valve, not a CPU one.**
> The only CPU benefit is indirect, via the stalls it eliminates.

---

## 6. Secondary interactions worth knowing

- **`rend_is_enabled()`** is the third skip source and is unrelated to performance: it is
  false only during GGPO rollback re-simulation
  ([`core/network/ggpo.cpp:268`](core/network/ggpo.cpp#L268)), where frames must not be
  drawn. Those skips deliberately do **not** increment `fskip`.

- **Asymmetric `renderEnd.Set()`** — in
  [`PvrMessageQueue::render()`](core/hw/pvr/Renderer_if.cpp#L173-L211): for on-screen
  frames it is signalled right after `Process()`
  ([`:201`](core/hw/pvr/Renderer_if.cpp#L201)), releasing the emu thread as soon as the
  display list has been consumed; for RTT / `EmulateFramebuffer` it is held until after
  `Render()` ([`:209`](core/hw/pvr/Renderer_if.cpp#L209)) because VRAM must contain the
  result. This is what lets the emu thread run ~one frame ahead of the renderer in the
  first place — and therefore what makes `rqueue != nullptr` a meaningful signal.

- **Two synchronisation points** exist between the emu thread and the render thread:
  1. `frame_finished` in `QueueRender()` — at STARTRENDER time.
  2. `renderEnd` in `rend_end_render()` — at the scheduled "render done" time, guarded by
     `pend_rend`, which is only true when `QueueRender` returned `true`. So a skipped
     frame does not stall here either.

- **libretro** disables its vsync-swap-interval detection when auto-skip is on
  ([`shell/libretro/libretro.cpp:894`](shell/libretro/libretro.cpp#L894)) — dropped frames
  make that pacing measurement meaningless. Options map by **value index**:
  `disabled` / `some` / `more` → 0 / 1 / 2. Note libretro uses its own
  [`shell/libretro/option.cpp`](shell/libretro/option.cpp), separate from
  [`core/cfg/option.cpp`](core/cfg/option.cpp#L100-L102). The libretro default is `some`
  on `LOW_END` builds, `disabled` otherwise.

- **`fskip`** (the dropped-frame counter,
  [`core/hw/pvr/spg.cpp:38`](core/hw/pvr/spg.cpp#L38)) is only reported in the debug FPS
  log ([`spg.cpp:212-219`](core/hw/pvr/spg.cpp#L212-L219), `#if !defined(NDEBUG)`), not in
  the release OSD.

- **Fast-forward** does not touch this code at all — it only mutes audio
  ([`sgc_if.cpp:1596`](core/hw/aica/sgc_if.cpp#L1596)) and disables vsync in the WSI
  layers. The resulting frame drops come out of the auto-skip mechanism naturally.

- **`Emulator::vblank()`** has an independent safety net
  ([`core/emulator.cpp:1076-1088`](core/emulator.cpp#L1076-L1088)): if no frame has been
  rendered for ~50 ms of emulated time it sets `renderTimeout` to keep the non-threaded
  loop alive. Unrelated to frameskip policy, but it is the other place where "no frame was
  produced" is handled.

---

## 7. Summary

| Mechanism | Trigger | Granularity | Saves |
|---|---|---|---|
| `SkipFrame` (manual) | `RenderCount % (N+1)` | Every render, incl. RTT | GPU work |
| `AutoSkipFrame = 1` | `rqueue` busy **and** `cpu_speed < 85%` | Per frame, feedback-regulated | GPU work + removes emu-thread stalls |
| `AutoSkipFrame = 2` | `rqueue` busy | Per frame | GPU work + never stalls the emu thread |
| `!rend_is_enabled()` | GGPO rollback | Per re-simulated frame | Correctness, not performance |

---

## 8. File index

| File | Role |
|---|---|
| [`core/hw/pvr/ta_ctx.cpp`](core/hw/pvr/ta_ctx.cpp) | `QueueRender` / `DequeueRender` / `FinishRender` — the skip decision |
| [`core/hw/pvr/spg.cpp`](core/hw/pvr/spg.cpp) | vblank handling, `SH4FastEnough` measurement, `fskip` counter |
| [`core/hw/pvr/Renderer_if.cpp`](core/hw/pvr/Renderer_if.cpp) | `PvrMessageQueue`, render thread, `rend_start_render` / `rend_end_render` |
| [`core/cfg/option.cpp`](core/cfg/option.cpp) | Native option definitions (`ta.skip`, `pvr.AutoSkipFrame`) |
| [`shell/libretro/option.cpp`](shell/libretro/option.cpp) | libretro option definitions (separate from the native ones) |
| [`core/ui/settings_video.cpp`](core/ui/settings_video.cpp) | Video settings UI |
