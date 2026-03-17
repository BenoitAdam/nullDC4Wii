// wii_audiostream.cpp
//
// Thin glue between nullAICA and the Wii audio backend.
//
// wii_audio.h uses a frame-driven model:
//   - wii_audio_frame() is called once per video frame (from DoRender).
//   - It internally runs 735 AICA timesteps (44100/60) and pushes the
//     resulting samples into an ASND voice.
//   - wii_audio_aica_ready() gates sample generation until AICA_Init()
//     has fully completed.
//
// Therefore this file has almost nothing to do: Init/Term just forward
// to wii_audio_init/term, and wii_WriteSample() is a no-op because
// sample collection is handled inside wii_audio.cpp's timestep loop,
// which reads mixl/mixr directly after each AICA_Sample() call.

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

// Called by AICA_Sample() at the end of each mixed sample.
// With the frame-based wii_audio.h model, wii_audio_frame() drives the
// timestep loop itself and reads mixl/mixr directly, so there is nothing
// to do here. The function must exist to satisfy the linker.
void wii_WriteSample(s16 r, s16 l)
{
    (void)r;
    (void)l;
}
