// wii_exit.cpp
// Ordered shutdown for the in-game exit combo (EXIT FIX preset).
//
// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS
// ---------------------------------------------------------------------------
// Symptom: pressing the exit combo sometimes blanks the screen and then hangs
// forever with the Wii still powered on.
//
// The old path was a bare exit(0) called from inside the emulation itself
// (SH4 -> maple DMA -> MapleConfigMap::GetInput -> UpdateInputState). Nothing
// was shut down first, so libogc had to unwind a machine that was still fully
// running. Disassembling devkitPPC's exit path shows what it actually does:
//
//   exit() -> _exit() -> __syscall_exit()  (libogc system.o)
//        -> SYS_ResetSystem(SYS_SHUTDOWN, 0, 0)
//        -> jump to the loader stub at 0x80001800 ("STUB"/"HAXX" at +4/+8)
//
// and SYS_ResetSystem starts with three groups of UNBOUNDED spins, in order:
//
//   1. DSP quiesce:  while (DSPCR & 0x400); while (DSPCR & 0x200);
//                    ... DSPCR |= 1; while (DSPCR & 1);
//      Run while ASND's voice, its DSP task and the AI DMA are all still live.
//
//   2. __call_resetfuncs(0) in a LOOP until every registered reset function
//      returns non-zero. Three are registered in this app:
//        * __gx_onreset  -> GX_Flush() then GX_AbortFrame(). The Flush pushes
//          32 more bytes into a FIFO whose GP may still be chewing on the
//          frame ASYNC_RENDER left queued; if the FIFO is full the CPU blocks
//          on the write-gather pipe.
//        * __pad_onreset -> returns 0 (i.e. "call me again") while
//          __pad_resettingbits is set or SI_Busy(), then issues a pad
//          recalibration and waits for it. A GameCube channel mid-transfer
//          here means the loop never ends.
//        * __wpad_onreset -> WPAD_Shutdown(), which does BTE_Close(),
//          LWP_JoinThread() on the wiiuse thread and CONF_SaveChanges()
//          (a NAND write through IOS).
//
//   3. __IOS_ShutdownSubsystems(), with SD/USB still mounted and, if the
//      Sixaxis path was used, a hand-reloaded IOS58 underneath.
//
// One of those reset functions is also what blanks the display - which is
// exactly the reported symptom: the screen goes black (teardown started), then
// the console sits there (teardown wedged). It is intermittent because it
// depends on what the DSP / GP / SI happened to be doing at the instant the
// combo was pressed.
//
// The fix is to reach exit() from a quiet machine: stop the audio voice and
// the DSP, abort the GP's queued frame, give SI a few retraces to drain, flush
// the log, and drop Bluetooth ourselves from a calm point instead of from the
// middle of SYS_ResetSystem.
//
// Every step is logged and flushed to /ndclog.txt before it runs, so if it
// still wedges the log names the exact step it died on.
// ---------------------------------------------------------------------------

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <asndlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wii_exit.h"

// EXIT FIX preset (wii/main.cpp): 0 = OFF (legacy bare exit(0)),
// 1 = SAFE (ordered teardown), 2 = SAFE + loader-stub watchdog,
// 3 = + host-side combo poll off the scanline counter (SPG.cpp - that mode
// only changes who CALLS us, the teardown below is the same as mode 2).
extern "C" int get_exit_fix_preset(void);

// Audio sink (wii/wii_audio.cpp): closes the sink so no further
// wii_audio_push_sample() can block, then stops the ASND voice.
extern "C" void wii_audio_shutdown(void);

// Renderer (plugs/drkPvr/gxRend.cpp): aborts the frame ASYNC_RENDER left
// queued and clears the deferred-present state.
extern "C" void gxRend_ExitQuiesce(void);

// Sixaxis/DualShock3 USB pads (plugs/drkMapleDevices/drkMapleDevices.cpp):
// closes any device libsicksaxis still has open. No-op when never inited.
extern "C" void SS_Term(void);

// ---------------------------------------------------------------------------
// Loader-stub watchdog (EXIT FIX >= 2)
// ---------------------------------------------------------------------------
// If the ordered teardown is not enough and exit() still wedges in one of the
// spins above, this fires and jumps straight to the Homebrew Channel return
// stub, skipping SYS_ResetSystem entirely. It is deliberately the dumbest
// possible last resort: pure PowerPC, no IOS call, and it only arms when the
// stub is actually present (a forwarder launch may have none - then there is
// nothing safe to jump to and the watchdog stays disarmed).
//
// It runs from the decrementer alarm interrupt, so it can only rescue a hang
// that happens while interrupts are still enabled - which covers all three
// spin groups listed above (SYS_ResetSystem only clears MSR[EE] later, after
// __IOS_ShutdownSubsystems).

#define HBC_STUB_ADDR   0x80001800
#define HBC_STUB_MAGIC0 0x53545542  // 'STUB'
#define HBC_STUB_MAGIC1 0x48415858  // 'HAXX'

#define EXIT_WATCHDOG_SECONDS 6

static syswd_t s_exit_alarm;

static bool hbc_stub_present(void)
{
  const volatile u32 *magic = (const volatile u32 *)(HBC_STUB_ADDR + 4);
  return magic[0] == HBC_STUB_MAGIC0 && magic[1] == HBC_STUB_MAGIC1;
}

static void exit_watchdog_cb(syswd_t alarm, void *arg)
{
  (void)alarm;
  (void)arg;

  if (!hbc_stub_present())
    return; // nothing safe to jump to; let the hang stand rather than guess

  ((void (*)(void))HBC_STUB_ADDR)();
}

static void arm_exit_watchdog(void)
{
  if (!hbc_stub_present())
  {
    printf("[EXIT] watchdog NOT armed: no loader stub at %08X\n", HBC_STUB_ADDR);
    return;
  }

  if (SYS_CreateAlarm(&s_exit_alarm) < 0)
  {
    printf("[EXIT] watchdog NOT armed: SYS_CreateAlarm failed\n");
    return;
  }

  struct timespec tp;
  tp.tv_sec  = EXIT_WATCHDOG_SECONDS;
  tp.tv_nsec = 0;
  SYS_SetAlarm(s_exit_alarm, &tp, exit_watchdog_cb, NULL);

  printf("[EXIT] watchdog armed (%d s -> loader stub)\n", EXIT_WATCHDOG_SECONDS);
}

// ---------------------------------------------------------------------------

static void step(const char *what)
{
  printf("[EXIT] %s\n", what);
  fflush(stdout);
}

void WiiExitToLoader(void)
{
  // The combo is tested once per maple port per guest poll, so this is
  // reachable several times before the first call gets anywhere. Only the
  // first one may run the teardown; the rest park until it takes effect.
  static volatile bool s_exiting = false;
  if (s_exiting)
    for (;;) usleep(10 * 1000); // park; the first caller owns the teardown
  s_exiting = true;

  if (get_exit_fix_preset() == 0)
  {
    exit(0); // legacy path, kept so the fix can be A/B'd on hardware
  }

  step("exit combo: shutting down");

  // 1. USB HID pads first: they hold IOS58 device handles open, and IOS is the
  //    last thing SYS_ResetSystem tears down.
  step("closing USB pads");
  SS_Term();

  // 2. Audio. wii_audio_shutdown() closes the sink BEFORE stopping the voice:
  //    wii_audio_push_sample() blocks waiting on that voice's callback, so
  //    stopping the callback first would be a deadlock if anything still fed
  //    it. ASND_End() takes the DSP task and the AI DMA down with it, which is
  //    what SYS_ResetSystem's opening DSPCR spins are waiting for.
  step("stopping audio (voice, DSP, AI DMA)");
  wii_audio_shutdown();
  ASND_Pause(1);
  ASND_End();

  // 3. GPU. ASYNC_RENDER() queues a frame (draws + CopyDisp + SetDrawDone) and
  //    presents it a vblank later; on this path nobody ever will. Abort it
  //    here so libogc's own __gx_onreset finds an idle CP.
  step("aborting queued GX frame");
  gxRend_ExitQuiesce();

  // 4. Let the Serial Interface drain. __pad_onreset refuses to complete while
  //    SI_Busy(), and it is called in an unbounded retry loop - three retraces
  //    is far longer than any SI transfer and also gives libogc's own threads
  //    (wiiuse, IPC) a chance to run before we join them below.
  step("settling SI / retrace");
  VIDEO_WaitVSync();
  VIDEO_WaitVSync();
  VIDEO_WaitVSync();

  // 5. Get the log on the card while the FS is certainly still healthy.
  //    fatUnmount() is deliberately NOT called: exit() closes stdout for us,
  //    and unmounting first would hand it a dead file system.
  step("flushing log");
  fflush(NULL);

  // 6. Last resort, armed before the two riskiest calls below.
  if (get_exit_fix_preset() >= 2)
    arm_exit_watchdog();

  // 7. Bluetooth. libogc's __wpad_onreset does exactly this from inside
  //    SYS_ResetSystem; doing it here means it happens with the DSP, the GP
  //    and the emulation already stopped instead of racing them.
  step("dropping Bluetooth");
  for (int chan = 0; chan < 4; chan++)
    WPAD_Flush(chan);
  WPAD_Shutdown();

  step("exit(0)");
  fflush(NULL);
  exit(0);
}
