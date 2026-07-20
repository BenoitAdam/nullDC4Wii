#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"
#include <math.h>

#ifdef _WII
#include "wii/wii_timeprof.h"   // split ARM7 CPU vs AICA sample synthesis

// Per-game ARM7 speed preset (wii/game_presets.cpp `arm7_speed`):
// effective bias = arm_sh4_bias << stage. 0=off (legacy ~10 MHz),
// 1=half (~5 MHz), 2=quarter (~2.5 MHz). Safe headroom: the AICA driver
// free-runs a poll/scan main loop ~21.6k laps/s at stage 0 — orders of
// magnitude more service than command latency or envelope pacing need.
extern "C" int get_arm7_speed_preset();
#endif

// Real AICA sample step (nullAICA). Declared here to avoid pulling the whole
// nullAICA header into the ARM core; one call generates exactly one 44.1 kHz
// stereo sample and pushes it to the audio sink via wii_WriteSample().
extern void libAICA_TimeStep();

// SH4 cycles per AICA sample: 200 MHz / 44100 Hz ~= 4535.
// We keep a fractional accumulator so the long-run rate is exact.
#define AICA_SAMPLE_CYCLES (SH4_CLOCK / 44100)

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

	// Timing split (wii/wii_timeprof.h): the combined UpdateArm cost measured
	// 18-22% of the Broadway core (2026-07-20), but this function hosts TWO
	// tenants — the ARM7 interpreter AND the 44.1 kHz sample synthesis loop.
	// Bracket them separately (TP_ARM7 vs TP_AICA) so the [TIME] line shows
	// which one actually owns the cost before anything gets optimized.
#ifdef _WII
	unsigned int _t0 = tprof_now();
#endif

	// Run the ARM7 for this slice (cycle budget scaled by arm_sh4_bias,
	// further divided by the per-game arm7_speed preset stage on Wii).
#ifdef _WII
	{
		int stage = get_arm7_speed_preset();
		if (stage < 0) stage = 0;
		if (stage > 3) stage = 3;
		arm_Run((Cycles / arm_sh4_bias) >> stage);
	}
#else
	arm_Run(Cycles / arm_sh4_bias);
#endif

#ifdef _WII
	unsigned int _t1 = tprof_now();
	tprof_acc[TP_ARM7] += _t1 - _t0;
#endif

	// Advance the AICA sample generator by however many whole samples elapsed
	// during this slice, carrying the remainder for exact long-run timing.
	aica_cycle_acc += Cycles;
	while (aica_cycle_acc >= AICA_SAMPLE_CYCLES)
	{
		aica_cycle_acc -= AICA_SAMPLE_CYCLES;
		libAICA_TimeStep();
	}

#ifdef _WII
	tprof_acc[TP_AICA] += tprof_now() - _t1;
#endif
}
