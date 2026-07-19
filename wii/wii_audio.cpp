// wii_audio.cpp
// ASND-based audio output for nullAICA on Wii.
//
// Two fixed playback buffers are alternated by the voice callback; a staging
// buffer is filled by wii_audio_push_sample(). settings.emulator.AudioBuffers
// gates pacing: >=1 blocks the emu thread until the callback consumes a buffer
// (paces emulation to audio); 0 never blocks (drops on overrun).

// config.h (pulled in transitively via types.h → nullAICA headers) poisons
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

// Set to 1 by wii_audio_aica_ready() once AICA_Init() has run. Until then
// wii_audio_push_sample() is a no-op.
static volatile int aica_ready = 0;

void wii_audio_aica_ready()
{
    printf("[WiiAudio] AICA ready, audio sink enabled\n");
    aica_ready = 1;
}

// 1024 stereo s16 samples per buffer = 4 KiB; ~23.2 ms at 44100 Hz.
#define SAMPLES_PER_BUF     1024
#define NUM_CHANNELS        2
#define BUF_BYTES           (SAMPLES_PER_BUF * NUM_CHANNELS * 2 /*s16*/)
#define SAMPLE_RATE         44100
#define VOICE_SLOT          0

// Two fixed playback buffers (alternated by the callback) + one staging buffer
// (filled by the producer). 32-byte aligned for ASND DMA.
static s16 play_buf[2][SAMPLES_PER_BUF * NUM_CHANNELS] __attribute__((aligned(32)));
static s16 stage_buf[SAMPLES_PER_BUF * NUM_CHANNELS]   __attribute__((aligned(32)));

static volatile int play_idx    = 0;   // playback buffer ASND is using
static volatile int stage_ready = 0;   // producer -> callback: staging buffer full
static volatile u32 underrun_count = 0;

// Voice callback (IRQ). Copies the staging buffer into the next playback buffer
// and queues it, so ASND only ever sees the two fixed addresses, alternating.
static void audio_callback(s32 voice)
{
    (void)voice;

    int next = play_idx ^ 1;

    if (stage_ready)
    {
        memcpy(play_buf[next], stage_buf, BUF_BYTES);
        stage_ready = 0;
    }
    else
    {
        underrun_count++;   // no fresh data: replay the buffer's prior contents
    }

    DCFlushRange(play_buf[next], BUF_BYTES);
    ASND_AddVoice(VOICE_SLOT, (void *)play_buf[next], BUF_BYTES);
    play_idx = next;
}

void wii_audio_init()
{
    printf("[WiiAudio] init: rate=%d bytes=%d AudioBuffers=%d\n",
           SAMPLE_RATE, BUF_BYTES, (int)settings.emulator.AudioBuffers);

    memset(play_buf, 0, sizeof(play_buf));
    memset(stage_buf, 0, sizeof(stage_buf));
    DCFlushRange(play_buf, sizeof(play_buf));

    play_idx    = 0;
    stage_ready = 0;

    // Non-NULL callback is required: with a NULL callback the voice stops once
    // its buffers drain.
    ASND_SetVoice(VOICE_SLOT,
                  VOICE_STEREO_16BIT,
                  SAMPLE_RATE,
                  0,                    // delay (ms)
                  (void *)play_buf[0],
                  BUF_BYTES,
                  255, 255,             // L / R volume
                  audio_callback);

    ASND_Pause(0);   // ASND starts paused after ASND_Init()
}

void wii_audio_term()
{
    ASND_StopVoice(VOICE_SLOT);
}

static volatile int fill_pos = 0;

// Audio sink — one 44.1 kHz stereo sample per call from AICA_Sample() (driven
// by the SH4 timeslice via armUpdateARM). Fills the staging buffer; on
// completion publishes it and, when AudioBuffers >= 1, blocks until the
// callback consumes it (pacing emulation to audio). AudioBuffers == 0 never
// blocks (overwrites / drops on overrun).
void wii_audio_push_sample(s16 l, s16 r)
{
    if (!aica_ready)
        return;

    stage_buf[fill_pos * 2 + 0] = l;
    stage_buf[fill_pos * 2 + 1] = r;
    fill_pos++;

    if (fill_pos < SAMPLES_PER_BUF)
        return;
    fill_pos = 0;

    DCFlushRange(stage_buf, BUF_BYTES);

    // Ensure the staging writes are visible before the callback sees the flag.
    __sync_synchronize();
    stage_ready = 1;

    if (settings.emulator.AudioBuffers == 0)
        return;

    while (stage_ready)
        usleep(50);
}

// Legacy no-op kept so existing call sites (gxRend present path) still link.
void wii_audio_frame()
{
}
