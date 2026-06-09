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

// Audio sink. Called once per generated AICA sample from wii_WriteSample()
// (i.e. from AICA_Sample(), which is now driven by the SH4 timeslice via
// armUpdateARM — NOT from the video frame). Pushes one stereo sample into the
// ASND ring buffer and swaps buffers on frame boundaries.
void wii_audio_push_sample(s16 l, s16 r);

// Legacy no-op kept so existing call sites (gxRend present path) still link.
// AICA is no longer stepped from the video frame.
void wii_audio_frame();

#ifdef __cplusplus
}
#endif
