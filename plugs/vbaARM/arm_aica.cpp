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
// SOUND_CLOCK = REAL TIME (below): the time base, TB_TIMER_CLOCK ticks per ms.
#define RT_CLOCK_NOW() ((u32)gettime())
#define RT_CLOCK_HZ    ((u64)TB_TIMER_CLOCK * 1000u)
#else
#define ARM_PERF_TICKS() ((u64)0)
#include <chrono>
#define RT_CLOCK_NOW() ((u32)std::chrono::duration_cast<std::chrono::microseconds>( \
                            std::chrono::steady_clock::now().time_since_epoch()).count())
#define RT_CLOCK_HZ    ((u64)1000000u)
#endif

// SOUND_CLOCK preset (wii/main.cpp `sound_clock`): 0 = the ARM7 and the AICA
// follow the SH4's emulated time (exact, default), 1 = they follow the real
// clock. Per game, see armUpdateARM() below.
extern "C" int get_sound_clock_preset();

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
//
// stepSound() is that slice; armUpdateARM() below decides which clock the
// slices are measured on.
static void stepSound(u32 Cycles)
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

// SOUND_CLOCK = REAL TIME (ported from the Initial D 3 Wii port, where it is
// Wii-confirmed). Music that is a sequencer running on the ARM7 off the AICA's
// timers plays at the emulation's speed: at 80% the music is at 80% tempo,
// whatever the output does. Here the ARM7 and the AICA are handed the SH4
// cycles that elapsed on the REAL clock instead of the emulated ones, in
// slices the size of the emulated one, so the ARM, its timers and the samples
// stay interleaved exactly as in EMULATED mode. The game itself still runs at
// its own speed; only the sound side keeps real time. Its timer and sample
// interrupts then reach the SH4 a little more often per emulated second when
// the emulation is slow (that is the trade this per-game option makes).
//
// Costs: when the game is slow, the sound side still does a full real-time
// second of work per second, so it takes a bigger share of the frame. Pacing
// on the audio ring is off in this mode (wii/wii_audio.cpp), so the speed cap
// comes from the speed_limiter preset alone.
//
// Stalls longer than RT_MAX_MS (a disc load, the console's own pauses) are
// not caught up: the sound resumes from where it was.
//
// NOT for music the SH4 STREAMS to the AICA (ADX/ADPCM rings it refills, e.g.
// Initial D 3's two 4-bit ADPCM stream channels): in real time the AICA
// overtakes the refills and replays stale pieces. Those games' music tempo can
// only come from the emulation itself running at full speed. CD audio is not
// affected either way (and has its own preset, CDDA_CLOCK).
#define RT_MAX_MS     50u
#define RT_MIN_CHUNK  256u

void FASTCALL armUpdateARM(u32 Cycles)
{
	static bool started = false;
	static u32 last = 0, rt_mhz = 0;
	static u64 cycles_per_tick = 0;	// SH4 cycles per clock tick, 16.16 fixed point
	static u64 acc = 0;

	if (!get_sound_clock_preset())
	{
		started = false;	// a later switch to REAL TIME starts a fresh clock
		stepSound(Cycles);
		return;
	}

	const int mhz = get_sh4_clock_preset();
	if (!started || mhz != (int)rt_mhz)
	{
		started = true;
		rt_mhz = (u32)mhz;
		cycles_per_tick = ((u64)rt_mhz * 1000000u << 16) / RT_CLOCK_HZ;
		last = RT_CLOCK_NOW();
		acc = 0;
		return;
	}

	const u32 now = RT_CLOCK_NOW();
	u32 delta = now - last;		// u32 wrap (~70 s on Wii) is harmless here
	last = now;
	const u32 max_delta = (u32)(RT_CLOCK_HZ * RT_MAX_MS / 1000u);
	if (delta > max_delta)
		delta = max_delta;
	acc += (u64)delta * cycles_per_tick;
	u32 cycles = (u32)(acc >> 16);
	acc &= 0xFFFF;

	const u32 chunk = (Cycles > RT_MIN_CHUNK) ? Cycles : RT_MIN_CHUNK;
	while (cycles)
	{
		const u32 c = (cycles < chunk) ? cycles : chunk;
		stepSound(c);
		cycles -= c;
	}
}
