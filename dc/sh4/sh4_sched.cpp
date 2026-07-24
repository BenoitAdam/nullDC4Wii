/*
	sh4_sched.cpp — unified cycle-deadline scheduler (see sh4_sched.h for the
	rationale and how this differs from the PSP original).

	Model: a monotonic SH4-cycle clock `sched_now`, advanced explicitly by
	sh4_sched_tick(cycles). Each registered callback holds an absolute-cycle
	deadline `end` (-1 = disabled). A tick advances the clock in ascending-
	deadline order, firing each due callback exactly at its deadline so that
	relative ordering of events landing in the same tick window is preserved.
*/

#include "types.h"
#include "sh4_sched.h"

#include <vector>

using namespace std;

// Monotonic scheduler clock, in SH4 cycles since the last sh4_sched_reset().
// 64-bit so it never wraps for the life of a session; the 32-bit accessor just
// truncates (matching the PSP contract that sh4_sched_now() wraps).
static s64 sched_now = 0;

// Registered callbacks. Index into this vector is the id returned to callers;
// indices are stable for the whole session, so ids stay valid across resets.
static vector<sched_list> sch_list;

int sh4_sched_register(int tag, sh4_sched_callback* ssc)
{
	sched_list t = { ssc, tag, -1, -1 };
	sch_list.push_back(t);
	return (int)(sch_list.size() - 1);
}

u32 sh4_sched_now()   { return (u32)sched_now; }
u64 sh4_sched_now64() { return (u64)sched_now; }

void sh4_sched_request(int id, int cycles)
{
	sched_list& e = sch_list[id];
	e.start = sched_now;

	if (cycles == -1)
		e.end = -1;                       // disable
	else
		e.end = sched_now + (s64)cycles;  // absolute deadline
}

int sh4_sched_elapsed(int id)
{
	sched_list& e = sch_list[id];
	if (e.end == -1)
		return -1;

	int rv  = (int)(sched_now - e.start);
	e.start = sched_now;
	return rv;
}

// Fire one callback: snapshot its parameters, disable it, invoke it, and re-arm
// if it asked to repeat. Mirrors PSP's handle_cb (jitter is 0 here because we
// advance the clock exactly to each deadline before firing).
static void handle_cb(int id)
{
	sched_list& e = sch_list[id];

	int sched_cycl = (int)(e.end - e.start);
	e.start = sched_now;
	e.end   = -1;                          // single-shot until re-armed

	int re_sch = e.cb(e.tag, sched_cycl, 0);

	if (re_sch > 0)
		sh4_sched_request(id, re_sch);
}

void sh4_sched_tick(int cycles)
{
	const s64 target = sched_now + (s64)cycles;

	// Fire due callbacks in ascending-deadline order. Re-scan each iteration so
	// that a callback re-armed with a deadline still inside this window (rare,
	// only for very short periods) is honoured, and so ordering stays correct
	// if a fired callback arms another. Bounded: the list is tiny (< ~16).
	for (;;)
	{
		int slot = -1;
		s64 best = target + 1;

		for (size_t i = 0; i < sch_list.size(); i++)
		{
			const s64 end = sch_list[i].end;
			if (end != -1 && end <= target && end < best)
			{
				best = end;
				slot = (int)i;
			}
		}

		if (slot == -1)
			break;

		// Advance the clock to the event so sh4_sched_now() reads correctly
		// inside the callback (a migrated TMU may sample it).
		sched_now = sch_list[slot].end;
		handle_cb(slot);
	}

	sched_now = target;
}

void sh4_sched_reset()
{
	sched_now = 0;
	for (size_t i = 0; i < sch_list.size(); i++)
	{
		sch_list[i].start = -1;
		sch_list[i].end   = -1;
	}
}

void sh4_sched_cleanup()
{
	sch_list.clear();
	sched_now = 0;
}
