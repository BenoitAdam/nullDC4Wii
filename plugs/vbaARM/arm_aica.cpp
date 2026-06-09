#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"
#include <math.h>

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

	// Run the ARM7 for this slice (cycle budget scaled by arm_sh4_bias).
	arm_Run(Cycles / arm_sh4_bias);

	// Advance the AICA sample generator by however many whole samples elapsed
	// during this slice, carrying the remainder for exact long-run timing.
	aica_cycle_acc += Cycles;
	while (aica_cycle_acc >= AICA_SAMPLE_CYCLES)
	{
		aica_cycle_acc -= AICA_SAMPLE_CYCLES;
		libAICA_TimeStep();
	}
}
