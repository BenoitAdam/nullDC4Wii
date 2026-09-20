# Audio tuning — advanced procedure

How to get clean sound out of a game without giving up more speed than you have to,
using the `AUDIO STATS` counters instead of guessing.

You do not need this for normal play. Use it when a specific game crackles, or when
you want to know what pacing is costing you in that game.

---

## The goal

There are four audio settings, and between them roughly a hundred combinations —
per game. Trying them one at a time is a long evening.

`AUDIO STATS` collapses each test run to **one** decision. It does that by separating
the two failure modes that sound identical through the TV speakers but need *opposite*
fixes:

- the emulator briefly stalled (a spike), or
- the emulator is simply not fast enough in this game (a deficit).

Both crackle. More buffering fixes the first and does nothing for the second. Retuning
fixes the second and cannot react fast enough for the first. The counters tell you
which one you have before you change anything.

---

## The settings

| Setting | Where | Values |
|---|---|---|
| `AUDIO BUFFERS` | Options, page 4 (AUDIO) | `0` = free run, `1`–`8` = ring depth. `4` is the usual answer |
| `AUDIO BLOCK` | Options, page 4 (AUDIO) | `1024` / `1536` / `2048` samples |
| `AUDIO DRC` | Options, page 4 (AUDIO) | `OFF` / `GENTLE ±1%` / `NORMAL ±3%` / `WIDE ±15%` |
| `AUDIO STATS` | Options, page 8 (LOGS) | `OFF` / `ON` |

The same four exist as per-game keys in `game_presets.cfg`:
`audio_buffers`, `audio_block`, `audio_drc`, `audio_stats`.

### What each one does

**AUDIO BUFFERS** is how many blocks of finished audio the emulator may run ahead by.
Depth is *slack*: it lets a slow moment be paid back by a fast one. At `1` there is no
slack at all. At `0` the emulator is never paced by audio — it keeps every frame it can
render, and audio that doesn't fit is thrown away.

**AUDIO BLOCK** is how much audio is handed over at a time. The Wii's sound library
accepts exactly one block every 21.3 ms, so the block size is also a hard ceiling on
how fast the emulator can be allowed to run while audio is pacing it:

| Block | Length | Speed ceiling when paced |
|---|---|---|
| 1024 | 23 ms | 109% of real speed |
| 1536 | 35 ms | 163% |
| 2048 | 46 ms | 218% |

Bigger blocks buy slack and a higher ceiling, and cost output delay.

**AUDIO DRC** retunes playback by a fraction of a percent, continuously, to keep the
buffer about half full. This is what lets a game running at 97% play perfectly instead
of crackling: the output is slowed to match, rather than glitching. It moves slowly on
purpose — a fast correction is an audible wobble.

---

## Procedure

### 0. Turn the counters on

Options → page 8 (LOGS) → `AUDIO STATS` = `ON`.

Turn it back off afterwards. It writes to the SD card once a second.

### 1. Make a run worth reading

Play the **worst part of the game** for at least 60 seconds — the busiest fight, the
most crowded track. A menu or an intro tells you nothing.

Exit normally, then read `/ndclog.txt` from the card and look for the `[WiiAudio]` lines:

```
[WiiAudio] blocks=43 under=0 drop=0 wait=41 timeout=0 ring=2/4 pitch=43950
```

One line per second.

| Field | Meaning |
|---|---|
| `blocks` | blocks handed to the sound hardware in the last second |
| `under` | the buffer was **empty** — you heard concealment, not your game |
| `drop` | the buffer was **full** and a block was thrown away (free run only) |
| `wait` | blocks where the emulator was held back by audio pacing |
| `timeout` | a wait gave up after 100 ms — the sound callback stopped firing |
| `ring=N/M` | how full the buffer was at that instant, out of the depth in use |
| `pitch` | what DRC has retuned playback to. `44100` means no correction |

### 2. Check `blocks` before anything else

It should sit at roughly:

| Block size | Expected `blocks` |
|---|---|
| 1024 | ~43 per second |
| 1536 | ~29 per second |
| 2048 | ~22 per second |

If it is far below that, the audio path itself is not running and nothing else on the
line means anything. Stop here and report it.

### 3. Read `under` against `drop`

This is the decision. Ignore every other field for now.

| What you see | What it means | What to change |
|---|---|---|
| `under=0 drop=0` | It is already working | Nothing. Try *lowering* `AUDIO BUFFERS` for less delay |
| `under>0`, **bursty** — some seconds 0, some 3 | A stall longer than your buffer | Raise **AUDIO BUFFERS** |
| `under>0`, **steady** — every second, similar count | The game cannot sustain 100% | Raise **AUDIO DRC**, or use free run + `WIDE` |
| `drop>0` | Free run, and the emulator is outrunning the ceiling | Raise **AUDIO BLOCK**, or switch to paced |
| `timeout>0` | The sound callback stopped firing | Not a tuning problem — report it |

**Bursty versus steady is the whole point.** Depth cannot fix a sustained deficit: a
bigger bucket does not help when the tap is slower than the drain, it only postpones
the first underrun. DRC cannot fix a single spike: it takes several seconds to move,
by design. Choose the wrong one and the run was wasted.

### 4. Use `pitch` as a speedometer

With DRC on and the buffer stable, `pitch ÷ 44100` is the speed the game is actually
sustaining.

| `pitch` | Sustained speed |
|---|---|
| 44100 | 100% |
| 43650 | 99% |
| 42800 | 97% |
| 41000 | 93% |

If it settles well below 44100, no amount of buffering will ever fix that game. Your
real choices are to accept the detune (`WIDE`), or to make the game faster with the
other performance settings.

### 5. Use `wait` to price the pacing

Compare `wait` with `blocks`:

- `wait` close to `blocks` — the emulator is being held at the ceiling on almost every
  block. That surplus speed is exactly what you are paying for clean audio.
- `wait` at or near 0 in a paced game — pacing is costing you nothing at all. Leave it on.

### 6. Write it down

Once a game settles, put it in `game_presets.cfg` under that game so you never redo it:

```
audio_buffers = 4
audio_block   = 0
audio_drc     = 2
```

Turn `AUDIO STATS` back off.

---

## Where to start

If you do not want to think about it, try these first and only run the procedure if
one of them is not enough.

| Situation | Try |
|---|---|
| Game reaches 100% and crackles | `AUDIO BUFFERS 4`, `AUDIO DRC NORMAL` |
| Game cannot reach 100% | `AUDIO BUFFERS 0`, `AUDIO DRC WIDE` — keeps every frame, retunes instead of crackling |
| Still hitching in heavy scenes | `AUDIO BLOCK 1536` |
| Sound is fine, you want less delay | Lower `AUDIO BUFFERS` until `under` stops being 0 |

---

## Why the ceiling exists

The Wii's sound library mixes in fixed ticks of 21.333 ms and will take exactly one
block from the emulator per tick. To keep a 44.1 kHz stream fed, a block must therefore
carry at least **941 samples**.

This is worth knowing because it is not a tunable — it is the shape of the hardware:

- A block below 941 samples starves the stream permanently, at *every* other setting.
  An earlier build used 735 and crackled no matter what else was changed.
- When audio is pacing the emulator, that same rule caps sustained speed at
  `block ÷ 941` of real time — which is where the 109% / 163% / 218% figures come from.

So `AUDIO BLOCK` is not really a quality setting. It is how much rope you give the
emulator per tick.

---

## Note on pacing and frame rate

Pacing is a speed limiter, by construction. A game that used to show 130% will sit near
100% once `AUDIO BUFFERS` is 1 or more. That is correct speed, not a regression — the
game was running too fast and the sound was paying for it.

If you would rather keep the raw numbers, use free run (`AUDIO BUFFERS 0`) with
`AUDIO DRC WIDE`.
