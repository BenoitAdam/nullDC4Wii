// wii_audiostream.cpp
//
// Thin glue between nullAICA and the Wii audio backend.
//
// The backend is sample-driven, not frame-driven:
//   - AICA is stepped from the SH4 timeslice (armUpdateARM), which calls
//     libAICA_TimeStep() once per generated 44.1 kHz sample.
//   - Each AICA_Sample() ends by calling wii_WriteSample(), which forwards
//     the mixed stereo sample to wii_audio_push_sample(). That fills a staging
//     buffer the ASND voice callback ping-pongs into two playback buffers.
//   - wii_audio_aica_ready() (called at the end of AICA_Init()) gates the sink
//     until AICA is fully up; wii_WriteSample() is a safe no-op before then.
//
// Init/Term just forward to wii_audio_init/term. wii_WriteSample() is the live
// audio path — NOT a no-op. (wii_audio_frame() is the vestigial no-op now.)

#include "wii_audiostream.h"
#include "wii/wii_audio.h"

void wii_InitAudio()
{
    // wii_audio_init() is called once at startup in main.cpp before
    // plugins are loaded, so nothing extra is needed here.
    // wii_audio_aica_ready() is called at the end of AICA_Init() in aica.cpp.
}

void wii_TermAudio()
{
    wii_audio_term();
}

// Called by AICA_Sample() at the end of each mixed sample, once per generated
// AICA sample. AICA is now stepped from the SH4 timeslice (armUpdateARM), so
// this forwards each produced sample to the audio sink, which buffers it for
// ASND playback. Note AICA_Sample() passes (r, l) in that order.
void wii_WriteSample(s16 r, s16 l)
{
    wii_audio_push_sample(l, r);
}
