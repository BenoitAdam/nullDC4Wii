// wii_audio.cpp
// ASND-based audio output for nullAICA on Wii.
//
// Architecture:
//   - Double-buffered: two s16 stereo buffers of SAMPLES_PER_FRAME samples each.
//   - wii_audio_frame() steps AICA 735 times per call, writing one (L,R) s16
//     pair per step into the "fill" buffer.
//   - ASND calls our voice callback when it has consumed the previous buffer;
//     we swap fill/play buffers there.
//   - ASND voice is started once; it keeps looping via the callback.

#include "wii_audio.h"

// config.h (pulled in via types.h) deliberately poisons BIG_ENDIAN and
// LITTLE_ENDIAN to catch accidental use. Undef them before pulling in
// libogc/ASND headers which legitimately define them.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

#include <asndlib.h>
#include <string.h>
#include <ogc/cache.h>   // for DCFlushRange
#include <ogc/lwp_watchdog.h>  // for LWP_MutexInit etc.
#include <ogc/mutex.h>

// AICA sample-generation side
#include "../plugs/nullAICA/aica.h"
#include "../plugs/nullAICA/sgc_if.h"  // for mixl / mixr

// Set to 1 by wii_audio_aica_ready() once AICA_Init() has run.
// wii_audio_frame() is a no-op until then to avoid stepping null pointers.
static volatile int aica_ready = 0;

void wii_audio_aica_ready()
{
    aica_ready = 1;
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// 44100 Hz / 60 fps = 735 samples per frame exactly.
// NTSC = 60.002 fps; we use 735 and let ASND buffer absorb the ~1 sample/sec drift.
#define SAMPLES_PER_FRAME   735
#define SAMPLE_RATE         44100
#define NUM_CHANNELS        2       // stereo
#define BYTES_PER_SAMPLE    2       // s16
#define BUF_BYTES           (SAMPLES_PER_FRAME * NUM_CHANNELS * BYTES_PER_SAMPLE)

// ASND voice slot (0–15; 0 is fine for a single mono/stereo stream)
#define VOICE_SLOT          0

// ---------------------------------------------------------------------------
// Double buffers (must be 32-byte aligned for ASND DMA)
// ---------------------------------------------------------------------------
static s16 audio_buf[2][SAMPLES_PER_FRAME * NUM_CHANNELS] __attribute__((aligned(32)));
static volatile int fill_buf  = 0;   // CPU writes here
static volatile int play_buf  = 1;   // ASND plays this
static volatile int buf_ready = 0;   // set by CPU, cleared by callback

// ---------------------------------------------------------------------------
// ASND voice callback — called from IRQ context when the voice needs more data
// ---------------------------------------------------------------------------
static void audio_callback(s32 voice)
{
    (void)voice;

    if (buf_ready)
    {
        // Swap buffers
        int tmp  = play_buf;
        play_buf = fill_buf;
        fill_buf = tmp;
        buf_ready = 0;

        // Queue the freshly swapped play buffer
        ASND_AddVoice(VOICE_SLOT,
                      (void *)audio_buf[play_buf],
                      BUF_BYTES);
    }
    else
    {
        // CPU didn't finish in time — replay the same buffer (glitch-free silence
        // is better than a crash or underrun with garbage data)
        ASND_AddVoice(VOICE_SLOT,
                      (void *)audio_buf[play_buf],
                      BUF_BYTES);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void wii_audio_init()
{
    // Clear both buffers to silence
    memset(audio_buf[0], 0, BUF_BYTES);
    memset(audio_buf[1], 0, BUF_BYTES);

    fill_buf  = 0;
    play_buf  = 1;
    buf_ready = 0;

    // Set up a stereo s16 voice with our callback
    ASND_SetVoice(VOICE_SLOT,
                  VOICE_STEREO_16BIT,   // format
                  SAMPLE_RATE,          // frequency
                  0,                    // delay (ms) before starting
                  (void *)audio_buf[play_buf],
                  BUF_BYTES,
                  255,                  // left  volume (0–255)
                  255,                  // right volume (0–255)
                  audio_callback);
}

void wii_audio_term()
{
    ASND_StopVoice(VOICE_SLOT);
}

void wii_audio_frame()
{
    // Don't step AICA until it's been fully initialized
    if (!aica_ready)
        return;

    s16 *dst = audio_buf[fill_buf];

    for (int i = 0; i < SAMPLES_PER_FRAME; i++)
    {
        // Step the AICA emulation by one sample
        libAICA_TimeStep();

        // mixl / mixr are s32 but already clipped to s16 range by sgc_if.cpp
        // Clamp defensively anyway
        int l = mixl;
        int r = mixr;
        if (l >  32767) l =  32767;
        if (l < -32768) l = -32768;
        if (r >  32767) r =  32767;
        if (r < -32768) r = -32768;

        // Interleaved stereo: L, R
        dst[i * 2 + 0] = (s16)l;
        dst[i * 2 + 1] = (s16)r;
    }

    // Flush the fill buffer out of CPU cache so ASND's DMA sees it
    DCFlushRange(audio_buf[fill_buf], BUF_BYTES);

    // Signal the callback that a fresh buffer is waiting
    buf_ready = 1;
}
