/*
	sh4_sched.h — unified cycle-deadline scheduler (ported from the nullDC PSP
	port / libswirl, adapted for nullDC4Wii).

	WHY THIS EXISTS
	---------------
	Our default timing is a hierarchical timeslice cascade (sh4_interpreter.cpp
	UpdateSystem -> Medium/Slow/VerySlow): each peripheral group is bulk-advanced
	by a FIXED cycle count at a FIXED period (TMU/PVR every 448cy, AICA/DMA every
	~3584cy, GD-ROM every ~7168cy, maple/RTC every ~14336cy). Events therefore
	fire at their group's timeslice boundary, NOT at their true cycle deadline,
	and two events with close real deadlines can fire in the WRONG RELATIVE ORDER
	depending only on which cascade tier they live in.

	This scheduler fires each registered callback at its exact requested cycle
	deadline, and — within one tick window — in ascending-deadline order, so the
	relative ordering of GD-ROM-done / DMA-done / render-done / IRQ events matches
	real hardware. That relative ordering is the leading remaining suspect for the
	cross-game "logo renders then the game stalls waiting on a request that never
	completes" bug (see the Rez investigation).

	DIFFERENCE FROM THE PSP ORIGINAL
	--------------------------------
	PSP keeps its clock in Sh4cntx.sh4_sched_next, which the SH4 execution loop
	decrements directly, and only runs the CPU up to the nearest deadline. Wiring
	that into our rec_v2 JIT would mean changing the generated mainloop and the
	Sh4Context layout — high risk. Instead this port keeps its OWN monotonic clock
	(sched_now) advanced explicitly by sh4_sched_tick(cycles), which we call once
	per timeslice from UpdateSystem. We therefore resolve ordering at timeslice
	(448cy) granularity rather than the PSP's exact per-event granularity — still
	~8-32x finer than the current Medium/Slow/VerySlow cascade, and enough to fix
	relative ordering, with none of the JIT/context churn.

	Entirely inert unless the `sched` preset is on AND callbacks have been
	registered: with no registrations, sh4_sched_tick() is an empty-list no-op.
*/

#pragma once

#include "types.h"

/*
	Scheduler callback.
	  tag       — the value passed to sh4_sched_register (lets one function serve
	              several ids, e.g. the 3 TMU channels).
	  sch_cycl  — the cycle duration originally requested for this firing.
	  jitter    — cycles this callback was late by (0..tick granularity). Always 0
	              in this decoupled port unless a deadline fell mid-tick.
	Return value: >0 re-arms the callback that many cycles later (periodic);
	<=0 leaves it disabled (single-shot).
*/
typedef int sh4_sched_callback(int tag, int sch_cycl, int jitter);

// Register a callback. Returns an id used by sh4_sched_request/elapsed.
// Registration is permanent for the session (ids are stable indices).
int sh4_sched_register(int tag, sh4_sched_callback* ssc);

// Current scheduler time in SH4 cycles since reset. 32-bit form wraps
// (~21 emulated seconds); 64-bit form effectively never wraps.
u32 sh4_sched_now();
u64 sh4_sched_now64();

// Schedule id to fire `cycles` SH4 cycles from now. cycles == -1 disables it.
// Calling again replaces any previous pending request for that id.
void sh4_sched_request(int id, int cycles);

// Cycles elapsed for this id since it was armed / last queried (-1 if disabled).
int sh4_sched_elapsed(int id);

// Advance the scheduler clock by `cycles` and fire every callback whose deadline
// falls in the advanced window, in ascending-deadline order. Called once per
// timeslice from UpdateSystem when the `sched` preset is on.
void sh4_sched_tick(int cycles);

// Drop all pending requests and zero the clock (called on hard reset). Keeps the
// registered callback list intact so re-registration is not required.
void sh4_sched_reset();

// Remove every registration too (for a full teardown). Rarely needed.
void sh4_sched_cleanup();

struct sched_list
{
	sh4_sched_callback* cb;
	int tag;
	s64 start;   // absolute cycle at which the pending request was armed
	s64 end;     // absolute cycle deadline; -1 = disabled
};
