// wii_audio.cpp
// ASND-based audio output for nullAICA on Wii.
//
// ---------------------------------------------------------------------------
// How ASND actually behaves (verified against the shipped libogc libasnd.a,
// not guessed - these three facts drive every size choice in this file):
//
//   1. The ASND mixer runs at 48000 Hz and is driven from the audio DMA
//      interrupt in ticks of 1024 output samples = 4096 bytes = 21.333 ms.
//      (ASND_GetSamplesPerTick() == 1024, ASND_GetAudioRate() == 48000, and
//      audio_dma_callback() flushes exactly 4096 bytes per interrupt.)
//   2. Our voice callback is invoked AT MOST ONCE PER TICK, so the voice can
//      be handed at most one buffer every 21.333 ms.
//   3. ASND_AddVoice() stores the buffer address and length raw; asndlib.h is
//      explicit that both "must be aligned and padded to 32 bytes".
//
// Consequence, and the whole reason the old 735-sample block could never work:
// a block shorter than 44100 * 0.021333 = 940.8 samples cannot keep the voice
// fed no matter what the pacing setting is (735 samples/tick delivers only
// 34.5 kHz into a 44.1 kHz voice - a permanent 22% starvation), and with
// pacing on it also caps emulation at block/940.8 of real speed (735 -> 78%).
// That is exactly the "crackling AND low FPS" row. 735*4 = 2940 bytes is not a
// multiple of 32 either, so it broke rule 3 at the same time.
//
// Every block size offered below is > 940.8 samples and a multiple of 8
// samples (= a multiple of 32 bytes).
// ---------------------------------------------------------------------------
//
// Pipeline:
//
//   AICA_Sample() -> wii_WriteSample() -> wii_audio_push_sample()
//        writes into ring slot (wr % AUD_RING_MAX); on a full block, publishes
//        it (wr++) and only then, if the ring is full, waits.
//   voice callback (audio DMA IRQ)
//        copies ring slot (rd % AUD_RING_MAX) into the next of two fixed
//        playback buffers, hands that to ASND_AddVoice(), rd++.
//
// The ring is the fix for the framerate cliff. The old code had a SINGLE
// staging buffer, so after publishing a block the producer had to sleep until
// the next DMA interrupt before it could touch the buffer again. Emulation
// therefore advanced in whole 21.333 ms ticks: one block per tick (108.8% of
// real speed) if the block's worth of emulation fitted inside one tick, and
// one block per TWO ticks (54.4%) the moment it did not. There was nothing in
// between - that cliff is the "20 to 40% FPS loss".
//
// With N slots the producer runs ahead into free slots and only blocks when it
// is genuinely more than N blocks ahead of real time, so a block that overruns
// its tick is paid back out of the ring instead of halving the frame rate.
//
// On top of that, optional dynamic rate control (the `audio_drc` preset) trims
// the playback pitch by a fraction of a percent to hold the ring near half
// full. That is what lets a game running at, say, 97% of real speed play
// without a single underrun: the output is slowed to match production instead
// of glitching. Wide settings let the pitch follow a game that sits well off
// 100%, trading a little detune for clean sound.

// config.h (pulled in transitively via types.h -> nullAICA headers) poisons
// BIG_ENDIAN / LITTLE_ENDIAN. Undef before any libogc/ASND header (which define
// them via machine/endian.h), including wii_audio.h (drags in lwp_watchdog.h).
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

#include "../plugs/nullAICA/aica.h"
#include "../plugs/nullAICA/sgc_if.h"

#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

#include "wii_audio.h"
#include <asndlib.h>
#include <string.h>
#include <unistd.h>
#include <ogc/cache.h>        // DCFlushRange
#include <ogc/lwp_watchdog.h> // gettime() for the pacing-wait timer

// Per-game / options-menu presets (wii/main.cpp).
extern "C" int get_audio_block_preset();   // 0=1024, 1=1536, 2=2048 samples
extern "C" int get_audio_drc_preset();     // 0=off, 1=+-1%, 2=+-3%, 3=+-15%
extern "C" int get_audio_stats_preset();   // 0=off, 1=one line/sec to ndclog

// Set to 1 by wii_audio_aica_ready() once AICA_Init() has run. Until then
// wii_audio_push_sample() is a no-op.
static volatile int aica_ready = 0;

#define NUM_CHANNELS        2
#define SAMPLE_RATE         44100
#define VOICE_SLOT          0

// Ring geometry. AUD_BLOCK_MIN is rule 1 above (940.8 rounded up to a multiple
// of 8); nothing smaller may ever reach ASND_AddVoice().
#define AUD_RING_MAX        8
#define AUD_RING_FREERUN    4     // depth used when AudioBuffers == 0
#define AUD_BLOCK_MAX       2048
#define AUD_BLOCK_MIN       944

// Selectable block sizes. All > AUD_BLOCK_MIN and all multiples of 8 samples
// (32 bytes). Larger = more slack per tick (the voice can be fed up to
// block/940.8 of real time per tick) and a higher pacing ceiling, at the cost
// of latency and of a coarser gap when a glitch does happen.
//   1024 -> 23.2 ms/block, pacing ceiling 108.8% of real speed
//   1536 -> 34.8 ms/block, pacing ceiling 163.2%
//   2048 -> 46.4 ms/block, pacing ceiling 217.7%
static const int k_block_sizes[3] = { 1024, 1536, 2048 };

// Live geometry, set by wii_audio_apply_settings() (called per game, from
// wii_audio_aica_ready(), i.e. AFTER the per-game presets have been applied).
static int block_samples = 1024;
static int block_bytes   = 1024 * NUM_CHANNELS * 2;

// Ring of producer blocks + the two fixed playback buffers ASND alternates
// between. Keeping ASND pinned to two addresses it owns for their whole
// lifetime (rather than handing it ring slots directly) makes the ownership
// rule trivially correct; the copy costs ~3 us per 21 ms tick.
static s16 ring[AUD_RING_MAX][AUD_BLOCK_MAX * NUM_CHANNELS] __attribute__((aligned(32)));
static s16 play_buf[2][AUD_BLOCK_MAX * NUM_CHANNELS]        __attribute__((aligned(32)));

// Monotonic block counters. Single producer (emu thread) writes ring_wr, single
// consumer (audio DMA IRQ) writes ring_rd, so no lock is needed - the unsigned
// difference (ring_wr - ring_rd) is the occupancy and wraps correctly.
//
// Slot indexing is ALWAYS counter % AUD_RING_MAX, never counter % ring_slots:
// ring_slots is read live from the options menu, and if the two sides ever
// computed their index against different depths the callback would read a slot
// the producer never wrote. The depth is a limit on occupancy only, so changing
// it mid-game is safe and costs nothing.
static volatile u32 ring_wr = 0;
static volatile u32 ring_rd = 0;
static int ring_slots = AUD_RING_FREERUN;  // live occupancy limit, 1..AUD_RING_MAX
static int ring_paced = 0;     // AudioBuffers != 0

// Slot the producer is currently filling; refreshed on each block boundary so
// the 44.1 kHz per-sample path costs one store pair and a compare.
static s16 *cur_slot = ring[0];

static volatile int play_idx   = 0;   // playback buffer ASND is using
static int fill_pos            = 0;   // sample offset inside the slot being filled
static int slot_ok             = 1;   // 0 = no free slot, current block is being dropped

// Underrun concealment state (IRQ side only).
static s16 conceal_l = 0, conceal_r = 0;
static int fade_in_pending = 0;

// Diagnostics (see the `audio_stats` preset).
static volatile u32 stat_underruns = 0;   // callback found the ring empty
static volatile u32 stat_drops     = 0;   // producer had no free slot (free-run)
static volatile u32 stat_blocks    = 0;   // blocks handed to ASND
static volatile u32 stat_waits     = 0;   // blocks that hit the pacing wait
static volatile u32 stat_timeouts  = 0;   // pacing waits that gave up

// Dynamic rate control state (IRQ side only).
static int drc_pitch = SAMPLE_RATE;
static int drc_acc   = 0;

// ---------------------------------------------------------------------------
// Dynamic rate control
//
// Every tick the callback reports the ring occupancy. A slow PI controller
// trims the voice pitch to hold that occupancy at half the ring. The loop is
// deliberately lazy - it takes several seconds to walk to its limit - because
// a fast correction is an audible wobble; the ring, not the controller, is
// what absorbs frame-to-frame jitter.
//
// Gains are in parts per million of 44100 Hz. One block of error is ~23 ms of
// audio, so correcting it in one second would need ~23000 ppm; KP is a sixth
// of that on purpose.
// ---------------------------------------------------------------------------
#define DRC_KP   4000    // ppm per half-block of instantaneous error
#define DRC_KI     90    // ppm per half-block of accumulated error

static int drc_max_ppm()
{
    switch (get_audio_drc_preset()) {
        case 1:  return  10000;   // +-1%   - inaudible, absorbs jitter only
        case 2:  return  30000;   // +-3%   - absorbs a game sitting slightly off 100%
        case 3:  return 150000;   // +-15%  - follows a game well off 100%; audibly detuned
        default: return 0;        // off
    }
}

static void drc_update(u32 used)
{
    const int max_ppm = drc_max_ppm();

    if (max_ppm == 0)
    {
        if (drc_pitch != SAMPLE_RATE)
        {
            drc_pitch = SAMPLE_RATE;
            drc_acc   = 0;
            ASND_ChangePitchVoice(VOICE_SLOT, drc_pitch);
        }
        return;
    }

    // Error in half-slots, so an odd or 1-deep ring still has a sane target.
    int err = (int)(used * 2) - ring_slots;

    drc_acc += err;
    {
        // Clamp the integral so it alone can never exceed the allowed deviation.
        const int acc_lim = max_ppm / DRC_KI;
        if (drc_acc >  acc_lim) drc_acc =  acc_lim;
        if (drc_acc < -acc_lim) drc_acc = -acc_lim;
    }

    int ppm = err * DRC_KP + drc_acc * DRC_KI;
    if (ppm >  max_ppm) ppm =  max_ppm;
    if (ppm < -max_ppm) ppm = -max_ppm;

    // Ring fuller than target -> play faster (consume more); emptier -> slower.
    int pitch = SAMPLE_RATE + (int)(((s64)SAMPLE_RATE * ppm) / 1000000);

    // Ignore sub-Hz churn: every change costs an interrupt-off critical section
    // inside ASND_ChangePitchVoice().
    if (pitch > drc_pitch + 1 || pitch < drc_pitch - 1)
    {
        drc_pitch = pitch;
        ASND_ChangePitchVoice(VOICE_SLOT, pitch);
    }
}

// ---------------------------------------------------------------------------
// Underrun concealment
//
// The old code replayed whatever the playback buffer still held, which is a
// 23 ms chunk of audio repeated verbatim - a very audible blip, and it clicks
// at both ends. Instead, ramp the last real sample down to silence over ~1.5 ms
// and stay silent, then ramp the next real block back in. A short quiet gap is
// far less noticeable than a repeat, and neither edge clicks.
// ---------------------------------------------------------------------------
#define CONCEAL_RAMP 64   // samples, ~1.5 ms at 44.1 kHz

static void conceal_fill(s16 *dst, int samples)
{
    int n = (samples < CONCEAL_RAMP) ? samples : CONCEAL_RAMP;

    for (int i = 0; i < n; i++)
    {
        int sc = n - i;
        dst[i * 2 + 0] = (s16)(((s32)conceal_l * sc) / n);
        dst[i * 2 + 1] = (s16)(((s32)conceal_r * sc) / n);
    }
    memset(dst + n * 2, 0, (size_t)(samples - n) * NUM_CHANNELS * sizeof(s16));

    conceal_l = 0;
    conceal_r = 0;
}

static void conceal_fade_in(s16 *dst, int samples)
{
    int n = (samples < CONCEAL_RAMP) ? samples : CONCEAL_RAMP;

    for (int i = 0; i < n; i++)
    {
        dst[i * 2 + 0] = (s16)(((s32)dst[i * 2 + 0] * i) / n);
        dst[i * 2 + 1] = (s16)(((s32)dst[i * 2 + 1] * i) / n);
    }
}

// ---------------------------------------------------------------------------
// Voice callback - audio DMA IRQ context.
//
// Runs at most once per 21.333 ms tick. Must not printf (stdout is freopen'd to
// the SD card) and must not block.
// ---------------------------------------------------------------------------
static void audio_callback(s32 voice)
{
    (void)voice;

    const int next = play_idx ^ 1;
    const u32 used = ring_wr - ring_rd;

    if (used != 0)
    {
        const int rd = (int)(ring_rd % (u32)AUD_RING_MAX);

        memcpy(play_buf[next], ring[rd], (size_t)block_bytes);

        // Publish the retire only after the copy is complete, or the producer
        // could start overwriting the slot while we are still reading it.
        __sync_synchronize();
        ring_rd = ring_rd + 1;

        conceal_l = play_buf[next][block_samples * 2 - 2];
        conceal_r = play_buf[next][block_samples * 2 - 1];

        if (fade_in_pending)
        {
            conceal_fade_in(play_buf[next], block_samples);
            fade_in_pending = 0;
        }
    }
    else
    {
        stat_underruns = stat_underruns + 1;
        conceal_fill(play_buf[next], block_samples);
        fade_in_pending = 1;
    }

    DCFlushRange(play_buf[next], (u32)block_bytes);
    ASND_AddVoice(VOICE_SLOT, (void *)play_buf[next], block_bytes);
    play_idx = next;

    stat_blocks = stat_blocks + 1;
    drc_update(used);
}

// ---------------------------------------------------------------------------
// Geometry / voice setup
// ---------------------------------------------------------------------------
static void audio_start_voice()
{
    memset(play_buf, 0, sizeof(play_buf));
    DCFlushRange(play_buf, sizeof(play_buf));

    play_idx        = 0;
    fill_pos        = 0;
    slot_ok         = 1;
    ring_wr         = 0;
    ring_rd         = 0;
    cur_slot        = ring[0];
    conceal_l       = 0;
    conceal_r       = 0;
    fade_in_pending = 0;
    drc_pitch       = SAMPLE_RATE;
    drc_acc         = 0;

    // Non-NULL callback is required: with a NULL callback the voice stops once
    // its buffers drain.
    ASND_SetVoice(VOICE_SLOT,
                  VOICE_STEREO_16BIT,
                  SAMPLE_RATE,
                  0,                    // delay (ms)
                  (void *)play_buf[0],
                  block_bytes,
                  255, 255,             // L / R volume
                  audio_callback);

    ASND_Pause(0);   // ASND starts paused after ASND_Init()
}

// Re-reads the block-size preset and restarts the voice with it. Called from
// wii_audio_aica_ready(), i.e. once per game start, after the per-game presets
// have been applied. The ring DEPTH is not fixed here - it is read live in the
// producer, so the options menu can change it mid-game.
static void audio_apply_block_size()
{
    int sel = get_audio_block_preset();
    if (sel < 0 || sel > 2)
        sel = 0;

    int samples = k_block_sizes[sel];

    // Rules 1 and 3 from the header, enforced rather than trusted: anything
    // below AUD_BLOCK_MIN starves the voice permanently, and anything whose
    // byte length is not 32-byte aligned breaks ASND's DMA.
    if (samples < AUD_BLOCK_MIN || samples > AUD_BLOCK_MAX || (samples & 7) != 0)
    {
        printf("[WiiAudio] block %d samples rejected (min %d, max %d, multiple of 8)"
               " - using 1024\n", samples, AUD_BLOCK_MIN, AUD_BLOCK_MAX);
        samples = 1024;
    }

    // The callback reads block_samples/block_bytes, so silence it before they
    // change. audio_start_voice() brings the voice back with the new geometry.
    aica_ready = 0;
    ASND_StopVoice(VOICE_SLOT);

    block_samples = samples;
    block_bytes   = samples * NUM_CHANNELS * 2;

    audio_start_voice();
}

void wii_audio_init()
{
    printf("[WiiAudio] init: rate=%d AudioBuffers=%d\n",
           SAMPLE_RATE, (int)settings.emulator.AudioBuffers);

    memset(ring, 0, sizeof(ring));
    audio_apply_block_size();
}

void wii_audio_aica_ready()
{
    // Per-game presets are live by now, so pick up the block size they asked
    // for before letting any sample through.
    audio_apply_block_size();

    printf("[WiiAudio] AICA ready: block=%d samples (%d bytes, %.1f ms), "
           "AudioBuffers=%d, drc=%d, ceiling=%d%% of real speed\n",
           block_samples, block_bytes, block_samples * 1000.0 / SAMPLE_RATE,
           (int)settings.emulator.AudioBuffers, get_audio_drc_preset(),
           (block_samples * 100) / 941);

    aica_ready = 1;
}

void wii_audio_term()
{
    ASND_StopVoice(VOICE_SLOT);
}

// Exit path (wii/wii_exit.cpp). Order matters: close the sink FIRST, so a
// producer already parked in wii_audio_push_sample()'s pacing wait is released
// and no new sample can enter it, and only then stop the voice whose callback
// that wait depends on. Stopping the voice first would strand the producer
// forever.
extern "C" void wii_audio_shutdown()
{
    aica_ready = 0;
    ring_wr    = ring_rd;   // releases anyone in the pacing wait below
    ASND_StopVoice(VOICE_SLOT);
}

// Idle time spent in the pacing wait below; subtracted from the stats line's
// aica% (plugs/drkPvr/Renderer_if.h).
extern u64 SndWaitTicks;

// Pacing wait, BOUNDED. The emulation thread is the only thing that reads the
// pads (the exit combo is tested from the guest's maple DMA), so if the voice
// callback ever stopped firing - voice dropped, DSP task lost, ASND paused -
// an open wait would freeze the emulator with no way out at all. 100 ms is
// nearly 5 ticks, so a healthy callback never reaches the cap; giving up just
// drops this block, which costs an audio glitch instead of a dead console.
static void pace_wait()
{
    const u64 t0 = gettime();
    int spins;

    stat_waits = stat_waits + 1;

    for (spins = 0; spins < 500; spins++)
    {
        if (!aica_ready)
            break;
        if ((u32)(ring_wr - ring_rd) < (u32)ring_slots)
            break;
        usleep(200);
    }
    if (spins >= 500)
        stat_timeouts = stat_timeouts + 1;

    SndWaitTicks += gettime() - t0;
}

// One line per second to /ndclog.txt when the `audio_stats` preset is on.
// Called from the producer (thread context) on block boundaries, never from
// the callback - printf goes to the SD card and has no business in an IRQ.
static void audio_stats_tick()
{
    static u64 last = 0;

    if (!get_audio_stats_preset())
        return;

    const u64 now = gettime();
    if (last != 0 && ticks_to_millisecs(now - last) < 1000)
        return;
    last = now;

    printf("[WiiAudio] blocks=%u under=%u drop=%u wait=%u timeout=%u "
           "ring=%u/%d pitch=%d\n",
           stat_blocks, stat_underruns, stat_drops, stat_waits, stat_timeouts,
           (unsigned)(ring_wr - ring_rd), ring_slots, drc_pitch);

    stat_blocks    = 0;
    stat_underruns = 0;
    stat_drops     = 0;
    stat_waits     = 0;
    stat_timeouts  = 0;
}

// Audio sink - one 44.1 kHz stereo sample per call from AICA_Sample() (driven
// by the SH4 timeslice via armUpdateARM).
//
// settings.emulator.AudioBuffers is the ring depth and is read live, so the
// options menu takes effect without a restart:
//   0    - free-run: buffered exactly like depth 4, but the producer is never
//          blocked; when the ring is full the block is dropped. Emulation is
//          not paced by audio at all, so a game keeps every frame it can
//          render, and overruns are dropped rather than torn. Paired with a
//          wide `audio_drc` this is the setting for a game that cannot reach
//          100%: the output is retuned to whatever rate the game actually
//          produces instead of crackling, and nothing is slowed down.
//   1..8 - pace: block only when N blocks are already queued. This is a speed
//          limiter by construction - the ring can only be drained one block per
//          21.333 ms tick, so sustained emulation is capped at
//          block_samples/940.8 of real speed.
void wii_audio_push_sample(s16 l, s16 r)
{
    if (!aica_ready)
        return;

    if (slot_ok)
    {
        cur_slot[fill_pos * 2 + 0] = l;
        cur_slot[fill_pos * 2 + 1] = r;
    }

    if (++fill_pos < block_samples)
        return;
    fill_pos = 0;

    if (slot_ok)
    {
        // No DCFlushRange: the callback consumes the ring via a CPU memcpy
        // (same core, same D-cache), not DMA. Only play_buf is ever handed to
        // ASND, and that is flushed in the callback. What is needed here is a
        // memory barrier - ordering the block's writes before the publish -
        // not a cache flush.
        __sync_synchronize();
        ring_wr = ring_wr + 1;
    }
    else
    {
        stat_drops = stat_drops + 1;
    }

    // Pick up the depth for the block about to be filled. Free-run gets the
    // same buffering as depth 4 - it just never blocks - so the producer never
    // writes into the slot the callback is copying out of. That tearing was the
    // second source of crackle at AudioBuffers=0, on top of the plain overruns,
    // and the depth is also what gives dynamic rate control something to steer
    // when emulation is left unpaced.
    {
        int want = (int)settings.emulator.AudioBuffers;
        if (want <= 0) { ring_paced = 0; want = AUD_RING_FREERUN; }
        else           { ring_paced = 1; }
        if (want > AUD_RING_MAX) want = AUD_RING_MAX;
        ring_slots = want;
    }

    if ((u32)(ring_wr - ring_rd) >= (u32)ring_slots && ring_paced)
        pace_wait();

    slot_ok  = ((u32)(ring_wr - ring_rd) < (u32)ring_slots);
    cur_slot = ring[ring_wr % (u32)AUD_RING_MAX];

    audio_stats_tick();
}

// Legacy no-op kept so existing call sites (gxRend present path) still link.
void wii_audio_frame()
{
}
