// ---------------------------------------------------------------------------
// FRAME_PROF - per-frame tail profiler. See frame_prof.h for what it is for and
// why a once-a-second average cannot answer the same question.
// ---------------------------------------------------------------------------
#include "frame_prof.h"
#include "Renderer_if.h"   // RenderTicks / TaPerf / ArmTicks / AicaTicks / SndWaitTicks
#include "drkPvr.h"        // spg_PaceFrameSec

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HOST_OS == OS_WII
#include <ogc/video.h>
#endif

// The preset lives here rather than in wii/main.cpp so that plugs/ and dc/ can
// reference it without pulling in a symbol from wii/ - same arrangement as
// g_ta_profile_preset in Renderer_if.cpp.
extern "C" { int g_frame_prof_preset = 0; }

// No initializer on purpose: static storage zero-initializes, and a field list
// here would silently fall out of step the next time a bucket is added.
FrameProfAcc g_fp;

// -- Buckets ---------------------------------------------------------------
// Disjoint by construction: sh4 is the residual, and the two things that
// happen INSIDE DoRender() (tex, and the GP waits DoRender itself takes) are
// subtracted out of rend when the sample is built, so the ten columns sum to
// the frame time and their percentages are real shares.
enum { FPB_SH4, FPB_REND, FPB_TEX, FPB_GXW, FPB_TA, FPB_ARM,
       FPB_AICA, FPB_SND, FPB_JIT, FPB_DISC, FPB_PAD, FP_NB };

static const char *const kFpName[FP_NB] =
  { "sh4", "rend", "tex", "gxw", "ta", "arm", "aica", "snd", "jit", "disc", "pad" };

struct FpSample
{
  u32 seq;          // monotonic emulated-frame number, for the tail listing
  u32 total;        // whole frame, pacing sleep and this profiler excluded
  u32 b[FP_NB];
  u16 n_tex, n_jit, n_clear, n_disc, n_wrap, n_pad;
  u8  vi;           // VI fields that elapsed during this frame (255 = clamped)
  u8  drawn;        // 0 when frameskip dropped the render
};

// ~8.5 s of history at 60 Hz. Deep enough that p99 is the 5th-worst frame of a
// real sample rather than the worst of sixty, which is what a 1-second window
// would give and is not a percentile in any useful sense.
#define FP_RING       512
#define FP_DUMP_EVERY 2   // stats-block ticks (~1 s each) between dumps
#define FP_TAIL_LIST  8   // frames listed individually in mode 2

// Lazily allocated, so the preset costs no MEM1 while off - the JIT HOTBLOCKS
// table is allocated the same way and for the same reason.
static FpSample *s_ring   = 0;
static u32      *s_sorted = 0;
static bool      s_alloc_failed = false;

static u32 s_head  = 0;   // next slot to write
static u32 s_count = 0;   // valid samples, saturating at FP_RING
static u32 s_seq   = 0;

// Span bookkeeping. s_resume is the time base reading taken at the BOTTOM of
// the vblank handler; the next vblank closes the span against it.
static u32  s_resume = 0;
static bool s_have_resume = false;

// Previous values of the shared accumulators, so this profiler can take
// per-frame deltas of counters that SPG.cpp zeroes once a second. A zeroing is
// detected as cur < last, and then cur IS the delta: the reset happens later in
// the same vblank handler that sampled them, so nothing is lost between the two.
static u32 s_last_rend = 0, s_last_ta = 0, s_last_arm = 0;
static u32 s_last_aica = 0, s_last_snd = 0;

#if HOST_OS == OS_WII
// VI field counter. The callback runs in interrupt context, so it does exactly
// one store; any previously installed user callback is chained. Hooked on first
// use and never removed - it is one increment per field.
static volatile u32 s_vi_ticks = 0;
static VIRetraceCallback s_vi_prev = 0;
static bool s_vi_hooked = false;

static void fp_vi_cb(u32 cnt)
{
  s_vi_ticks = s_vi_ticks + 1;   // not ++: -Wvolatile deprecates it
  if (s_vi_prev) s_vi_prev(cnt);
}
#endif
static u32 s_vi_last = 0;

#if HOST_OS == OS_WII
#define FP_MS(t) ((double)ticks_to_microsecs((u64)(u32)(t)) / 1000.0)
#else
#define FP_MS(t) ((void)(t), 0.0)
#endif

static bool fp_alloc()
{
  if (s_ring)         return true;
  if (s_alloc_failed) return false;

  s_ring   = (FpSample*)malloc(sizeof(FpSample) * FP_RING);
  s_sorted = (u32*)malloc(sizeof(u32) * FP_RING);
  if (!s_ring || !s_sorted)
  {
    free(s_ring);   s_ring   = 0;
    free(s_sorted); s_sorted = 0;
    s_alloc_failed = true;
    printf("[FPROF] out of memory (%u KB), disabled\n",
           (u32)((sizeof(FpSample) + 4) * FP_RING / 1024));
    fflush(stdout);
    return false;
  }
  memset(s_ring, 0, sizeof(FpSample) * FP_RING);
  s_head = s_count = 0;
  return true;
}

// Monotonic accumulator delta that survives SPG.cpp's once-a-second zeroing.
static INLINE u32 fp_delta(u32 cur, u32 *last)
{
  u32 d = (cur < *last) ? cur : (cur - *last);
  *last = cur;
  return d;
}

// ---------------------------------------------------------------------------
// Close the frame. Called at the TOP of the emulated vblank handler, before
// the stats block and before spg_PaceFrame(), so that neither the speed
// limiter's sleep nor the log write lands inside a measurement.
// ---------------------------------------------------------------------------
void frame_prof_vblank()
{
  if (!FRAME_PROF_ON())
  {
    s_have_resume = false;   // a later enable starts a clean span
    return;
  }
  if (!fp_alloc())
    return;

#if HOST_OS == OS_WII
  if (!s_vi_hooked)
  {
    s_vi_prev   = VIDEO_SetPostRetraceCallback(fp_vi_cb);
    s_vi_hooked = true;
    s_vi_last   = s_vi_ticks;
  }
#endif

  const u32 now = FP_TICKS32();

  // Deltas of the shared accumulators. Taken unconditionally so that `last`
  // tracks them even on a frame that is thrown away below.
  const u32 d_rend = fp_delta((u32)RenderTicks,  &s_last_rend);
  const u32 d_ta   = fp_delta((u32)TaPerf.ticks, &s_last_ta);
  const u32 d_arm  = fp_delta((u32)ArmTicks,     &s_last_arm);
  const u32 d_aica = fp_delta((u32)AicaTicks,    &s_last_aica);
  const u32 d_snd  = fp_delta((u32)SndWaitTicks, &s_last_snd);

#if HOST_OS == OS_WII
  const u32 vi_now = s_vi_ticks;
#else
  const u32 vi_now = 0;
#endif
  const u32 d_vi = vi_now - s_vi_last;
  s_vi_last = vi_now;

  if (!s_have_resume)
  {
    // First frame after the preset was switched on: there is no opening
    // timestamp to measure against, and the accumulator deltas above cover an
    // unknown span. Drop it rather than log a fabricated outlier.
    memset(&g_fp, 0, sizeof(g_fp));
    return;
  }

  const u32 total = now - s_resume;

  FpSample *s = &s_ring[s_head];
  s->seq   = s_seq++;
  s->total = total;

  // AicaTicks brackets the synthesis loop INCLUDING the pacing sleep inside it,
  // so net that out the way the stats line does; and DoRender's own GP waits
  // and texture decodes come out of rend so the columns stay disjoint.
  const u32 aica_net = (d_aica > d_snd) ? (d_aica - d_snd) : 0;
  const u32 rend_in  = g_fp.tex + g_fp.gxw_rend;
  const u32 rend_net = (d_rend > rend_in) ? (d_rend - rend_in) : 0;

  s->b[FPB_REND] = rend_net;
  s->b[FPB_TEX]  = g_fp.tex;
  s->b[FPB_GXW]  = g_fp.gxw;
  s->b[FPB_TA]   = d_ta;
  s->b[FPB_ARM]  = d_arm;
  s->b[FPB_AICA] = aica_net;
  s->b[FPB_SND]  = d_snd;
  s->b[FPB_JIT]  = g_fp.jit;
  s->b[FPB_DISC] = g_fp.disc;
  s->b[FPB_PAD]  = g_fp.pad;

  // sh4 is the residual, so a bracket that double-counts shows up as a
  // shrinking sh4 rather than as a frame time that does not add up. Clamped at
  // zero: with TA PROFILE off, ta reads 0 and its time is simply part of sh4.
  u32 acc = 0;
  for (int i = 1; i < FP_NB; i++) acc += s->b[i];
  s->b[FPB_SH4] = (total > acc) ? (total - acc) : 0;

  s->n_tex   = (u16)(g_fp.n_tex   > 65535u ? 65535u : g_fp.n_tex);
  s->n_jit   = (u16)(g_fp.n_jit   > 65535u ? 65535u : g_fp.n_jit);
  s->n_clear = (u16)(g_fp.n_clear > 65535u ? 65535u : g_fp.n_clear);
  s->n_disc  = (u16)(g_fp.n_disc  > 65535u ? 65535u : g_fp.n_disc);
  s->n_wrap  = (u16)(g_fp.n_wrap  > 65535u ? 65535u : g_fp.n_wrap);
  s->n_pad   = (u16)(g_fp.n_pad   > 65535u ? 65535u : g_fp.n_pad);
  s->vi      = (u8)(d_vi > 255u ? 255u : d_vi);
  s->drawn   = (u8)(d_rend != 0);

  memset(&g_fp, 0, sizeof(g_fp));

  s_head = (s_head + 1) % FP_RING;
  if (s_count < FP_RING) s_count++;
}

// Reopen the span. Called at the BOTTOM of the vblank handler, after the stats
// block and after spg_PaceFrame().
void frame_prof_resume()
{
  if (!FRAME_PROF_ON()) return;
  s_resume      = FP_TICKS32();
  s_have_resume = true;
}

// Shell sort, Ciura gaps. 512 u32 once every couple of seconds, and it runs
// inside the stats block, which is already outside the measured span.
static void fp_sort(u32 *a, u32 n)
{
  static const u32 gaps[] = { 301, 132, 57, 23, 10, 4, 1 };
  for (u32 g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++)
  {
    const u32 gap = gaps[g];
    if (gap >= n) continue;
    for (u32 i = gap; i < n; i++)
    {
      const u32 v = a[i];
      u32 j = i;
      while (j >= gap && a[j - gap] > v) { a[j] = a[j - gap]; j -= gap; }
      a[j] = v;
    }
  }
}

static INLINE u32 fp_pct(const u32 *sorted, u32 n, double p)
{
  u32 i = (u32)((double)(n - 1) * p + 0.5);
  if (i >= n) i = n - 1;
  return sorted[i];
}

static void fp_print_row(const char *label, const double *ms, int n)
{
  printf("[FPROF] %s", label);
  for (int i = 0; i < FP_NB; i++)
    printf(" %s=%.2f", kFpName[i], ms[i]);
  printf("   (n=%d)\n", n);
}

void frame_prof_dump(double secs)
{
  (void)secs;

  if (!FRAME_PROF_ON() || !s_ring || s_count < 16)
    return;

  static int s_tick = 0;
  if (++s_tick < FP_DUMP_EVERY) return;
  s_tick = 0;

  const u32 n = s_count;

  for (u32 i = 0; i < n; i++) s_sorted[i] = s_ring[i].total;
  fp_sort(s_sorted, n);

  const u32 p50 = fp_pct(s_sorted, n, 0.50);
  const u32 p90 = fp_pct(s_sorted, n, 0.90);
  const u32 p95 = fp_pct(s_sorted, n, 0.95);
  const u32 p99 = fp_pct(s_sorted, n, 0.99);
  const u32 pmx = s_sorted[n - 1];

  // Budget = one emulated video frame. Anything past it is a frame the console
  // could not have delivered on time, whatever the one-second average says.
  const double budget_ms = (spg_PaceFrameSec > 0.0) ? spg_PaceFrameSec * 1000.0 : 16.68;
#if HOST_OS == OS_WII
  const u32 budget_ticks = (u32)PPCUsToTicks((u64)(budget_ms * 1000.0));
#else
  const u32 budget_ticks = 0xFFFFFFFFu;
#endif

  // One pass for the aggregates: typical frames (<= p50), tail frames (>= p95),
  // the over-budget count, the skip rate and the VI cadence histogram.
  double base[FP_NB], tail[FP_NB];
  for (int k = 0; k < FP_NB; k++) { base[k] = 0.0; tail[k] = 0.0; }

  u32 nbase = 0, ntail = 0, nover = 0, ndrawn = 0;
  u32 vi0 = 0, vi1 = 0, vi2 = 0, vi3 = 0;
  const FpSample *worst = &s_ring[0];
  double win_ms = 0.0;

  for (u32 i = 0; i < n; i++)
  {
    const FpSample *s = &s_ring[i];
    win_ms += FP_MS(s->total);
    if (s->total > budget_ticks) nover++;
    if (s->drawn)                ndrawn++;
    if      (s->vi == 0) vi0++;
    else if (s->vi == 1) vi1++;
    else if (s->vi == 2) vi2++;
    else                 vi3++;
    if (s->total > worst->total) worst = s;

    if (s->total <= p50)
    {
      for (int k = 0; k < FP_NB; k++) base[k] += FP_MS(s->b[k]);
      nbase++;
    }
    if (s->total >= p95)
    {
      for (int k = 0; k < FP_NB; k++) tail[k] += FP_MS(s->b[k]);
      ntail++;
    }
  }

  if (nbase) for (int k = 0; k < FP_NB; k++) base[k] /= (double)nbase;
  if (ntail) for (int k = 0; k < FP_NB; k++) tail[k] /= (double)ntail;

  printf("[FPROF] win=%uf/%.1fs budget=%.2fms | p50=%.2f p90=%.2f p95=%.2f p99=%.2f max=%.2f ms"
         " | over %u (%.1f%%) | drawn %.0f%% | VI 0x:%u 1x:%u 2x:%u 3+:%u\n",
         n, win_ms / 1000.0, budget_ms,
         FP_MS(p50), FP_MS(p90), FP_MS(p95), FP_MS(p99), FP_MS(pmx),
         nover, 100.0 * (double)nover / (double)n,
         100.0 * (double)ndrawn / (double)n,
         vi0, vi1, vi2, vi3);

  fp_print_row("typical(<=p50):", base, (int)nbase);
  fp_print_row("tail   (>=p95):", tail, (int)ntail);

  // The answer line. Rank the per-bucket difference between a tail frame and a
  // typical one: whatever is at the front of this list IS the stutter.
  double exc[FP_NB];
  double exc_sum = 0.0;
  for (int k = 0; k < FP_NB; k++)
  {
    exc[k] = tail[k] - base[k];
    if (exc[k] > 0.0) exc_sum += exc[k];
  }

  printf("[FPROF] EXCESS in tail:");
  for (int rank = 0; rank < 5; rank++)
  {
    int best = -1;
    for (int k = 0; k < FP_NB; k++)
      if (exc[k] > 0.005 && (best < 0 || exc[k] > exc[best])) best = k;
    if (best < 0) break;
    printf(" %s +%.2fms(%.0f%%)", kFpName[best], exc[best],
           exc_sum > 0.0 ? 100.0 * exc[best] / exc_sum : 0.0);
    exc[best] = -1.0;
  }
  printf("  => +%.2fms over a typical frame\n", exc_sum);

  printf("[FPROF] worst f%u %.2fms:", worst->seq, FP_MS(worst->total));
  for (int k = 0; k < FP_NB; k++)
    printf(" %s=%.2f", kFpName[k], FP_MS(worst->b[k]));
  printf(" | dec=%u blk=%u clr=%u rd=%u wrap=%u pad=%u vi=%u%s\n",
         (u32)worst->n_tex, (u32)worst->n_jit, (u32)worst->n_clear,
         (u32)worst->n_disc, (u32)worst->n_wrap, (u32)worst->n_pad,
         (u32)worst->vi,
         worst->drawn ? "" : " SKIPPED");

  // Mode 2: the individual tail frames. The aggregate above says which bucket
  // grew; this says whether it grew on one catastrophic frame or on a dozen
  // mediocre ones, which is the difference between a load hitch and a steady
  // leak. Walked newest-first so the listing is bounded and recent.
  if (g_frame_prof_preset >= 2)
  {
    int listed = 0;
    for (u32 j = 0; j < n && listed < FP_TAIL_LIST; j++)
    {
      const FpSample *s = &s_ring[(s_head + FP_RING - 1 - j) % FP_RING];
      if (s->total < p95) continue;
      printf("[FPROF]  f%u %.2fms:", s->seq, FP_MS(s->total));
      for (int k = 0; k < FP_NB; k++)
        if (s->b[k]) printf(" %s=%.2f", kFpName[k], FP_MS(s->b[k]));
      printf(" | dec=%u blk=%u clr=%u rd=%u wrap=%u pad=%u vi=%u%s\n",
             (u32)s->n_tex, (u32)s->n_jit, (u32)s->n_clear,
             (u32)s->n_disc, (u32)s->n_wrap, (u32)s->n_pad, (u32)s->vi,
             s->drawn ? "" : " SKIPPED");
      listed++;
    }
  }

  fflush(stdout); // log is freopen'd to SD; without this the tail is lost
}
