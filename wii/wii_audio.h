#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup, after ASND_Init() in main.cpp
void wii_audio_init();

// Call once at shutdown
void wii_audio_term();

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
void wii_audio_push_sample(s16 l, s16 r);

// Legacy no-op kept so existing call sites (gxRend present path) still link.
// AICA is no longer stepped from the video frame.
void wii_audio_frame();

#ifdef __cplusplus
}
#endif
