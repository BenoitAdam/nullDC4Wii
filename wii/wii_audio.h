#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup, after ASND_Init() in main.cpp
void wii_audio_init();

// Call once at shutdown
void wii_audio_term();

// Exit path only (wii/wii_exit.cpp). Closes the sink and releases anyone
// parked in wii_audio_push_sample()'s pacing wait BEFORE stopping the voice,
// which wii_audio_term() alone does not do. Safe to call from anywhere.
void wii_audio_shutdown();

// Call once after AICA_Init() completes — enables the audio sink.
// Until this is called, wii_audio_push_sample() is a safe no-op.
void wii_audio_aica_ready();

// Audio sink. Called once per generated 44.1 kHz AICA sample from
// wii_WriteSample() (i.e. from AICA_Sample(), driven by the SH4 timeslice via
// armUpdateARM — NOT from the video frame). Fills a staging buffer that the
// voice callback ping-pongs between two 1024-sample (4 KiB) playback buffers.
// settings.emulator.AudioBuffers controls pacing: >=1 blocks the caller until
// the audio side consumes the buffer (paces emulation to audio); 0 never blocks
// (drops on overrun). Only one buffer is in flight, so values >1 behave like 1.
// SOUND_CLOCK = REAL TIME never blocks (the samples already follow real time).
void wii_audio_push_sample(s16 l, s16 r);

// Legacy no-op kept so existing call sites (gxRend present path) still link.
// AICA is no longer stepped from the video frame.
void wii_audio_frame();

// CDDA_CLOCK = REAL TIME: CD audio's own ASND voice, fed by
// plugs/nullAICA/sgc_if.cpp cdda_rt_pump() from the emulation thread. One block
// = two CD sectors, stereo s16 L/R interleaved, CD volume already applied.
#define CD_BLOCK_SAMPLES 1176
int  wii_cd_start();         // 0 = sink not up yet, 1 = running, 2 = started now (queue empty)
void wii_cd_stop();
u32  wii_cd_take_played();   // blocks played since the last call
void wii_cd_flush();         // drop everything queued
u32  wii_cd_free();          // blocks that may be queued now
s16 *wii_cd_slot();          // block to fill next...
void wii_cd_publish();       // ...then publish it

#ifdef __cplusplus
}
#endif
