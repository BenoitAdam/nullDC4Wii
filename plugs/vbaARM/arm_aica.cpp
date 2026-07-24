#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"
#include <math.h>

// Real AICA sample step (nullAICA). Declared here to avoid pulling the whole
// nullAICA header into the ARM core; one call generates exactly one 44.1 kHz
// stereo sample and pushes it to the audio sink via wii_WriteSample().
extern void libAICA_TimeStep();

// Per-game ARM7 speed preset (wii/game_presets.cpp `arm7_speed`): effective
// bias = arm_sh4_bias << stage. 0=off (legacy ~10 MHz), 1=half (~5 MHz),
// 2=quarter (~2.5 MHz). Safe headroom: the AICA driver free-runs a poll/scan
// main loop far more often than command latency or envelope pacing need, so
// halving/quartering its clock reclaims host CPU. Default off; per-game,
// stage 2 has been found to break audio timing on real hardware.
extern "C" int get_arm7_speed_preset();

// SH4 cycles per AICA sample: 200 MHz / 44100 Hz ~= 4535 (fewer when the
// sh4_clock underclock preset is active — 150 MHz => ~3401). A fractional
// accumulator keeps the long-run rate exact. Derived from SH4_CLOCK_EFF so the
// audio anchor scales with the same clock as the SPG video anchor, keeping
// sound in sync with picture under underclock. Cached and only recomputed when
// the preset actually changes, so the per-slice hot loop stays division-free.
static u32 aica_sample_cycles = SH4_CLOCK / 44100;
static int aica_clock_mhz     = 200;

//Mainloop
//
// Drives the ARM7 and the AICA sample generator together from the SH4
// timeslice, the way devcast's SoundCPU::Update does (run the CPU, step the
// sound generator, repeat). nullDC4Wii's MediumUpdate cadence is finer than one
// AICA sample, so we advance AICA whenever enough SH4 cycles have accumulated
// rather than a fixed 32x loop, then run the ARM for this slice. The ARM and
// AICA therefore stay in lockstep instead of being bulk-updated independently
// (and AICA is no longer stepped from the video/audio frame).
void FASTCALL armUpdateARM(u32 Cycles)
{
	static u32 aica_cycle_acc = 0;

	// Run the ARM7 for this slice (cycle budget scaled by arm_sh4_bias,
	// further divided by the per-game arm7_speed preset stage).
	{
		int stage = get_arm7_speed_preset();
		if (stage < 0) stage = 0;
		if (stage > 3) stage = 3;
		arm_Run((Cycles / arm_sh4_bias) >> stage);
	}

	// Refresh the cached samples-per-cycle only when the underclock preset
	// changed (never during gameplay — set at game load / in the menu).
	{
		int mhz = get_sh4_clock_preset();
		if (mhz != aica_clock_mhz)
		{
			aica_clock_mhz     = mhz;
			aica_sample_cycles = (u32)((u64)mhz * 1000000u / 44100u);
		}
	}

	// Advance the AICA sample generator by however many whole samples elapsed
	// during this slice, carrying the remainder for exact long-run timing.
	aica_cycle_acc += Cycles;
	while (aica_cycle_acc >= aica_sample_cycles)
	{
		aica_cycle_acc -= aica_sample_cycles;
		libAICA_TimeStep();
	}
}
