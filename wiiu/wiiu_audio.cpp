// wiiu/wiiu_audio.cpp
//
// AX (sndcore2) audio output for the Wii U build. Implements the same API as
// wii/wii_audio.cpp (wii/wii_audio.h), so the shared AICA glue
// (plugs/nullAICA/wii_audiostream.cpp, sgc_if.cpp) runs unchanged:
//
//   AICA_Sample() -> wii_WriteSample(r, l) -> wii_audio_push_sample(l, r)
//
// Design: two AX voices (left, right), each looping over its own mono s16 ring
// of WIIU_AUDIO_RING_SAMPLES. The DSP plays the rings forever; the emulator
// writes ahead of the play head. AX renders at 48 kHz, so each voice resamples
// 44.1 -> 48 kHz with the hardware linear SRC. No callback, no thread: the
// producer reads the play head (AXGetVoiceOffsets) when it needs to.
//
//   - Free-run (AudioBuffers == 0, the default, or SOUND_CLOCK = REAL TIME):
//     a sample that would overrun the play head is dropped.
//   - Paced (AudioBuffers >= 1): the producer sleeps while more than 3/4 of
//     the ring is queued, like the Wii backend's blocking push.
//   - Underrun (play head passed the write head, the usual case while the
//     interpreter runs below full speed): the write head jumps a short lead
//     ahead of the play head, so the gap plays the ring's stale contents for a
//     moment instead of drifting by a whole lap.
//
// CD audio's separate real-time voice (CDDA_CLOCK = REAL TIME, wii_cd_*) is
// not implemented yet: wii_cd_start() reports "sink not up", and
// wiiu_presets.cpp forces cdda_clock=0 so CD music goes through the AICA mix
// (the emulated-clock path) and still reaches these two voices.

#include "types.h"   // first, like the Wii files (config.h's BIG_ENDIAN poison)
#include <stdio.h>
#include <string.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sndcore2/device.h>
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/memdefaultheap.h>

#include "wii/wii_audio.h"
#include "wiiu/wiiu_config.h"

extern "C" int get_sound_clock_preset();   // 1 = real time: never pace

#define RING_N        WIIU_AUDIO_RING_SAMPLES
#define FLUSH_CHUNK   256u                  // samples per cache store (512 B, 16 lines)
#define START_LEAD    2048u                 // write head lead over the play head (~46 ms)
#define PACE_HIGH     ((RING_N * 3u) / 4u)  // paced: block above this many queued

static s16     *s_ring[2]   = { nullptr, nullptr };
static AXVoice *s_voice[2]  = { nullptr, nullptr };
static bool     s_ax_up     = false;   // AXInit done + voices running
static volatile bool s_sink = false;   // AICA ready: push accepted
static u64      s_abs_wr    = 0;       // samples written since start (ring pos = % RING_N)
static u32      s_flushed   = 0;       // ring position of the first sample not yet stored
static u32      s_dropped   = 0;
static u32      s_underruns = 0;

// Absolute play position: laps * ring + offset. Reading it as an absolute
// count (not just the offset) is what tells "1 sample queued" from "the DSP
// lapped us and is RING_N-1 samples ahead". The lap count is read on both sides
// of the offset so a lap boundary between the two reads cannot skew it by one
// whole ring.
static u64 play_abs(void)
{
  AXVoiceOffsets off;
  u32 laps0, laps1;
  do
  {
    laps0 = AXGetVoiceLoopCount(s_voice[0]);
    AXGetVoiceOffsets(s_voice[0], &off);
    laps1 = AXGetVoiceLoopCount(s_voice[0]);
  } while (laps0 != laps1);
  return (u64)laps1 * RING_N + (off.currentOffset % RING_N);
}

static void voice_setup(int ch)
{
  AXVoice *v = s_voice[ch];
  AXVoiceBegin(v);
  AXSetVoiceType(v, 0);

  AXVoiceVeData ve;
  ve.volume = 0x8000;   // 1.0
  ve.delta  = 0;
  AXSetVoiceVe(v, &ve);

  // Left voice -> channel 0, right voice -> channel 1, on the TV and the
  // GamePad. 6 entries covers the TV's channel count; the GamePad uses 4.
  AXVoiceDeviceMixData mix[6];
  memset(mix, 0, sizeof(mix));
  mix[ch].bus[0].volume = 0x8000;
  AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_TV,  0, mix);
  AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_DRC, 0, mix);

  AXSetVoiceSrcType(v, AX_VOICE_SRC_TYPE_LINEAR);
  AXSetVoiceSrcRatio(v, 44100.0f / 48000.0f);

  AXVoiceOffsets off;
  memset(&off, 0, sizeof(off));
  off.dataType       = AX_VOICE_FORMAT_LPCM16;
  off.loopingEnabled = AX_VOICE_LOOP_ENABLED;
  off.loopOffset     = 0;
  off.endOffset      = RING_N - 1;
  off.currentOffset  = 0;
  off.data           = s_ring[ch];
  AXSetVoiceOffsets(v, &off);

  AXSetVoiceState(v, AX_VOICE_STATE_PLAYING);
  AXVoiceEnd(v);
}

extern "C" void wii_audio_init()
{
  if (s_ax_up)
    return;

  if (!AXIsInit())
  {
    AXInitParams p;
    memset(&p, 0, sizeof(p));
    p.renderer = AX_INIT_RENDERER_48KHZ;
    p.pipeline = AX_INIT_PIPELINE_SINGLE;
    AXInitWithParams(&p);
  }

  for (int ch = 0; ch < 2; ch++)
  {
    s_ring[ch] = (s16 *)MEMAllocFromDefaultHeapEx(RING_N * sizeof(s16), 64);
    if (!s_ring[ch])
    {
      printf("[WiiUAudio] ring allocation failed, audio off\n");
      return;
    }
    memset(s_ring[ch], 0, RING_N * sizeof(s16));
    DCFlushRange(s_ring[ch], RING_N * sizeof(s16));
  }

  // Priority 31 = top; the emulator owns the only voices in the process.
  for (int ch = 0; ch < 2; ch++)
  {
    s_voice[ch] = AXAcquireVoice(31, nullptr, nullptr);
    if (!s_voice[ch])
    {
      printf("[WiiUAudio] AXAcquireVoice failed, audio off\n");
      return;
    }
  }
  voice_setup(0);
  voice_setup(1);

  s_abs_wr  = START_LEAD;
  s_flushed = START_LEAD;
  s_ax_up   = true;
  printf("[WiiUAudio] init: AX 48 kHz, 2 looping voices, ring=%u samples, SRC 44.1->48 kHz\n",
         (unsigned)RING_N);
}

extern "C" void wii_audio_aica_ready()
{
  s_sink = s_ax_up;
}

// Store the samples written since the last flush so the DSP reads them.
static void flush_written(void)
{
  u32 from = s_flushed, to = (u32)(s_abs_wr % RING_N);
  if (from == to)
    return;
  if (to < from)   // wrapped: tail of the ring first
  {
    DCStoreRange(&s_ring[0][from], (RING_N - from) * sizeof(s16));
    DCStoreRange(&s_ring[1][from], (RING_N - from) * sizeof(s16));
    from = 0;
  }
  if (to > from)
  {
    DCStoreRange(&s_ring[0][from], (to - from) * sizeof(s16));
    DCStoreRange(&s_ring[1][from], (to - from) * sizeof(s16));
  }
  s_flushed = to;
}

// Once per FLUSH_CHUNK samples (not per sample: that would be three AX calls
// 44,100 times a second): compare the write head with the DSP's play head and
// decide what the next chunk does.
static u32  s_chunk_left = 0;      // samples until the next check
static bool s_chunk_drop = false;  // free-run overrun: drop this chunk

static void chunk_check(void)
{
  const u64 play   = play_abs();
  s64       queued = (s64)(s_abs_wr - play);

  // Underrun: the DSP caught up with (or passed) the write head. Re-seat the
  // write head a short lead ahead; the gap replays stale ring contents once.
  if (queued < (s64)FLUSH_CHUNK)
  {
    flush_written();
    s_abs_wr  = play + START_LEAD;
    s_flushed = (u32)(s_abs_wr % RING_N);
    queued    = START_LEAD;
    s_underruns++;
  }

  s_chunk_drop = false;
  const bool paced = settings.emulator.AudioBuffers != 0 && !get_sound_clock_preset();
  if (paced)
  {
    // Block while the ring is 3/4 full. Bounded: never more than ~100 ms, so
    // a stalled DSP (HOME menu, shutdown) cannot wedge the emulator.
    int guard = 100;
    while (queued > (s64)PACE_HIGH && guard-- > 0 && s_sink)
    {
      OSSleepTicks(OSMillisecondsToTicks(1));
      queued = (s64)(s_abs_wr - play_abs());
    }
  }
  else if (queued > (s64)PACE_HIGH)
  {
    s_chunk_drop = true;
  }
  s_chunk_left = FLUSH_CHUNK;
}

extern "C" void wii_audio_push_sample(s16 l, s16 r)
{
  if (!s_sink)
    return;

  if (s_chunk_left == 0)
    chunk_check();
  s_chunk_left--;

  if (s_chunk_drop)
  {
    s_dropped++;
    return;
  }

  const u32 wr = (u32)(s_abs_wr % RING_N);
  s_ring[0][wr] = l;
  s_ring[1][wr] = r;
  s_abs_wr++;

  if ((((u32)(s_abs_wr % RING_N)) - s_flushed + RING_N) % RING_N >= FLUSH_CHUNK)
    flush_written();
}

extern "C" void wii_audio_frame() {}

extern "C" void wii_audio_shutdown()
{
  s_sink = false;
  if (!s_ax_up)
    return;
  for (int ch = 0; ch < 2; ch++)
  {
    if (!s_voice[ch])
      continue;
    AXVoiceBegin(s_voice[ch]);
    AXSetVoiceState(s_voice[ch], AX_VOICE_STATE_STOPPED);
    AXVoiceEnd(s_voice[ch]);
  }
}

extern "C" void wii_audio_term()
{
  wii_audio_shutdown();
  if (!s_ax_up)
    return;
  printf("[WiiUAudio] term: underruns=%u dropped=%u\n", s_underruns, s_dropped);
  for (int ch = 0; ch < 2; ch++)
  {
    if (s_voice[ch])
      AXFreeVoice(s_voice[ch]);
    s_voice[ch] = nullptr;
  }
  AXQuit();
  for (int ch = 0; ch < 2; ch++)
  {
    if (s_ring[ch])
      MEMFreeToDefaultHeap(s_ring[ch]);
    s_ring[ch] = nullptr;
  }
  s_ax_up = false;
}

// --- CD audio real-time voice (CDDA_CLOCK = REAL TIME): not implemented ------
// wii_cd_start() == 0 tells sgc_if.cpp's cdda_rt_pump() the sink is not up, so
// it never queues anything. wiiu_presets.cpp forces cdda_clock=0 anyway.
static s16 s_cd_scratch[CD_BLOCK_SAMPLES * 2];

extern "C" int  wii_cd_start()       { return 0; }
extern "C" void wii_cd_stop()        {}
extern "C" u32  wii_cd_take_played() { return 0; }
extern "C" void wii_cd_flush()       {}
extern "C" u32  wii_cd_free()        { return 0; }
extern "C" s16 *wii_cd_slot()        { return s_cd_scratch; }
extern "C" void wii_cd_publish()     {}
