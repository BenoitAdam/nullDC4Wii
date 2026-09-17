#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"
#include <math.h>

// Wall-time split for the stats line (plugs/drkPvr/Renderer_if.h has the
// rationale). Declared here rather than by including Renderer_if.h, which
// would drag the whole renderer into the ARM core.
extern u64 ArmTicks;
extern u64 AicaTicks;

#if HOST_OS == OS_WII
// config.h poisons BIG_ENDIAN/LITTLE_ENDIAN; libogc defines them. Same dance
// as arm7.cpp.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif
#include <ogc/lwp_watchdog.h>   // gettime() - inline mftb read, no call
#define ARM_PERF_TICKS() gettime()
#else
#define ARM_PERF_TICKS() ((u64)0)
#endif

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

// ARM7 slice batching (wii/main.cpp `arm7_batch`): run the ARM once every N
// calls with the cycles of all N, instead of once per call. Same ARM clock and
// same AICA sample stepping (still every call); only the SH4<->ARM interleave
// gets coarser, which cuts the number of switches between the two JITs' code
// and data (the Wii-measured arm7_speed=1 run showed most of arm:% does not
// scale with ARM instructions run). 1 = every call (default, legacy).
extern "C" int get_arm7_batch_preset();

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
	static u32 arm_batch_acc = 0;     // SH4 cycles owed to the ARM (arm7_batch > 1)
	static u32 arm_batch_calls = 0;

	// Run the ARM7 for this slice (cycle budget scaled by arm_sh4_bias,
	// further divided by the per-game arm7_speed preset stage). The clock reads
	// bracket the run itself, so a call that only defers cycles (arm7_batch > 1)
	// costs nothing to measure.
	{
		int stage = get_arm7_speed_preset();
		if (stage < 0) stage = 0;
		if (stage > 3) stage = 3;
		int batch = get_arm7_batch_preset();
		u32 ticks;
		if (batch <= 1)
		{
			ticks = (Cycles / arm_sh4_bias) >> stage;
			arm_batch_acc = 0;
			arm_batch_calls = 0;
		}
		else
		{
			if (batch > 16) batch = 16;
			arm_batch_acc += Cycles;
			if (++arm_batch_calls < (u32)batch)
				goto arm_deferred;                  // this call only banks cycles
			ticks = (arm_batch_acc / arm_sh4_bias) >> stage;
			arm_batch_acc = 0;
			arm_batch_calls = 0;
		}
		{
			const u64 t_arm = ARM_PERF_TICKS();
			arm_Run(ticks);
			ArmTicks += ARM_PERF_TICKS() - t_arm;
		}
	}
arm_deferred:

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
	if (aica_cycle_acc >= aica_sample_cycles)
	{
		// Clock reads skipped when no sample ran (about 1 call in 5 in ACCURATE).
		const u64 t_aica = ARM_PERF_TICKS();
		do
		{
			aica_cycle_acc -= aica_sample_cycles;
			libAICA_TimeStep();
		}
		while (aica_cycle_acc >= aica_sample_cycles);

		AicaTicks += ARM_PERF_TICKS() - t_aica;
	}
}
