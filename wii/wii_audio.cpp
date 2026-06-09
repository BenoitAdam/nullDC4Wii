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

// config.h (pulled in transitively via types.h → nullAICA headers) deliberately
// poisons BIG_ENDIAN and LITTLE_ENDIAN to catch accidental use.  We must undef
// those poison macros BEFORE any libogc/ASND header is included, because those
// headers legitimately define the same names via machine/endian.h.  The undefs
// must therefore precede every #include, including our own wii_audio.h which
// transitively drags in ogc/lwp_watchdog.h → machine/endian.h.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

// AICA sample-generation side (pulls in config.h poison; must come after undefs
// above so the poison definitions never collide with machine/endian.h)
#include "../plugs/nullAICA/aica.h"
#include "../plugs/nullAICA/sgc_if.h"  // for mixl / mixr

// Undef again in case aica.h re-included config.h and re-poisoned them before
// the ogc headers below get a chance to define the real values.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif

#include "wii_audio.h"
#include <asndlib.h>
#include <string.h>
#include <ogc/cache.h>        // DCFlushRange
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

// Set to 1 by wii_audio_aica_ready() once AICA_Init() has run.
// wii_audio_frame() is a no-op until then to avoid stepping null pointers.
static volatile int aica_ready = 0;

void wii_audio_aica_ready()
{
    printf("[WiiAudio] wii_audio_aica_ready: AICA ready, frame stepping enabled\n");
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
    printf("[WiiAudio] wii_audio_init: start\n");
    // Clear both buffers to silence
    memset(audio_buf[0], 0, BUF_BYTES);
    memset(audio_buf[1], 0, BUF_BYTES);

    fill_buf  = 0;
    play_buf  = 1;
    buf_ready = 0;

    printf("[WiiAudio] wii_audio_init: ASND_SetVoice slot=%d rate=%d bytes=%d\n",
           VOICE_SLOT, SAMPLE_RATE, BUF_BYTES);
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
    printf("[WiiAudio] wii_audio_init: done\n");
}

void wii_audio_term()
{
    printf("[WiiAudio] wii_audio_term\n");
    ASND_StopVoice(VOICE_SLOT);
}

// Index of the next sample slot to write in the current fill buffer.
static volatile int fill_pos = 0;

// Audio sink — called once per generated AICA sample (from wii_WriteSample(),
// i.e. from AICA_Sample()). AICA is now stepped by the SH4 timeslice via
// armUpdateARM, so this just collects the produced samples into the ASND ring
// buffer and swaps fill/play buffers each time a frame's worth is gathered.
void wii_audio_push_sample(s16 l, s16 r)
{
    if (!aica_ready)
        return;

    s16 *dst = audio_buf[fill_buf];
    dst[fill_pos * 2 + 0] = l;
    dst[fill_pos * 2 + 1] = r;
    fill_pos++;

    if (fill_pos >= SAMPLES_PER_FRAME)
    {
        fill_pos = 0;

        // Flush the fill buffer out of CPU cache so ASND's DMA sees it.
        DCFlushRange(audio_buf[fill_buf], BUF_BYTES);

        // Hand this buffer to the playback side. The ASND callback swaps
        // fill_buf/play_buf when it consumes buf_ready; if it hasn't yet
        // (CPU produced faster than playback), keep filling the same buffer
        // rather than racing the swap.
        if (!buf_ready)
            buf_ready = 1;
    }
}

// Legacy entry kept so existing call sites still link. AICA is no longer
// stepped from the video frame — sample generation is driven by the SH4
// timeslice (armUpdateARM). This is intentionally a no-op.
void wii_audio_frame()
{
}
