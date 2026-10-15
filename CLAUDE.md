# NullDC4Wii

A fork of [skmp/nullDCe](https://github.com/skmp/nullDCe) (Dreamcast emulator) ported to
run on Nintendo Wii / Wii U (vWii). This file covers project-specific conventions and
workflow rules that aren't derivable from reading the code. For general Wii/libogc
hardware knowledge (GX, memory layout, endianness, cache coherency, build flags), see
`SKILL.md` — load it whenever the task touches Wii-specific hardware behavior.

## Repo layout

- `dc/` — the Dreamcast core: `sh4/` (CPU interpreter + `rec_v2` dynarec), `aica/`
  (sound chip), `pvr/` (PowerVR2 GPU/tile accelerator), `gdrom/`, `maple/` (controller
  bus), `mem/` (MMU, address decoding). This is portable emulator logic, mostly
  hardware-accurate to real Dreamcast.
- `plugs/drkPvr/gxRend.cpp` — the Wii-side renderer. This is the single largest and
  most sensitive file: it translates PVR2 draw lists into libogc GX calls. Almost all
  render fixes and per-game hacks live here.
- `plugs/ImgReader/` — disc image loaders (GDI/CDI/ISO/MDS/ELF).
- `plugins/` — the plugin manager/type system gluing `dc/` to the `plugs/` backends.
- `wii/` — Wii platform glue: `main.cpp` (menu system, all `OPT_*` option rows, boot
  flow), `game_presets.cpp`/`.h` (per-game cfg parser), `wii_audio.cpp`, `wii_exit.cpp`,
  `wii_fastmem.cpp`. Also holds the Wii build scripts (`vs_make.bat`, `Makefile`).
- `apps/discs/game_presets.cfg` — the shipped, documented per-game preset file (this is
  the reference for the cfg key format/comments — read it before adding a new key).
- `SKILL.md` — general Nintendo Wii homebrew hardware/SDK knowledge (not project
  history).
- `frameskipping-flycast.md`, `yabause-wii-frameskipping.md`, `FPS_GOAL.md` — reference
  notes on performance work from comparable emulator ports.

## Build

Toolchain is devkitPPC + libogc (Wii-specific), triplet `powerpc-eabi-`. The user
builds and flashes to real hardware / vWii himself — **never run `make`,
`vs_make.bat`, or otherwise invoke the real build after an edit**, even to check it
compiles. Describe the change and stop.

Instead, syntax-check every edited translation unit with the real compiler
(parse-only, produces no build artifacts):

```sh
CXX=/c/devkitPro/devkitPPC/bin/powerpc-eabi-g++.exe
"$CXX" -fsyntax-only -mrvl -mcpu=750 -meabi -mhard-float \
       -D_WII -DRELEASE -DGEKKO \
       -I. -Iwii -Iplugs/drkPvr -I/c/devkitPro/libogc/include \
       <file.cpp>
```

Add `-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers` to also
surface warnings the real build would print. Report this honestly as
"syntax-checked, not built or tested" — it is not a substitute for the user's own
build/flash/test cycle.

## The preset system (how every fix/hack ships)

Renderer fixes and per-game hacks are never hardcoded or `#define`-gated — they go
through a runtime preset so the user can toggle them without a rebuild, and default
**OFF** until confirmed on real hardware. A `#define` is never an acceptable
substitute.

Every new switch ships in **both** places, every time:

1. A `game_presets.cfg` key: struct field in `wii/game_presets.cpp`, default `-1`
   (unset), `apply_kv` wiring, and a doc comment following the style already in
   `apps/discs/game_presets.cfg`.
2. A real preset in `wii/main.cpp`: `g_<name>_preset` global + `get_<name>_preset()`
   + an `OPT_*` menu row + both cycle handlers (there are two switch statements —
   grep the existing `OPT_*` case to find both) + the boot dump line.

Plus, in `plugs/drkPvr/gxRend.cpp`: an `extern "C"` getter and a `*_FIX()` macro at
the top of the file, following the existing preset plumbing there (e.g.
`punch_through`) as the template.

Gotchas specific to this system:

- **Numeric cfg keys use `atoi`, not a bool parser.** `atoi("on")` returns `0`, and
  the apply guard is `if (p->key >= 0)`, so `key=on` silently sets the preset to
  `0`/OFF instead of erroring. Trust the boot log's `key -> N` line in
  `/ndclog.txt`, never the cfg text, when confirming what a preset is actually set
  to.
- **Dolphin test runs never read `game_presets.cfg`** — the preset loader runs
  before stdout is even redirected, and getting a cfg onto a Dolphin SD image means
  rebuilding the image, which the user won't do mid-debugging-round. For anything
  the user needs to toggle to reproduce a bug on Dolphin, ship it as a `main.cpp`
  default they can edit directly, or a menu row — never a cfg-only key. A cfg key
  is still fine as an extra, real-Wii-only override on top of a menu preset.
- **Options menu rows wrap silently past 77 characters** (libogc's console over the
  640px XFB doesn't truncate, it wraps onto the next row and visually breaks the
  menu layout — this can't be caught by static review). Before adding a row, count
  the full printed string — `"  NAME : [< VALUE >] description"` — against the
  *widest* case in the value switch, not the shortest.
- Read presets once per frame into a cached bool/int, never per-vertex or
  per-polygon.
- The GX FIFO is fragile: the vertex descriptor (VCD) must never change mid-stream,
  and it must exactly match per-vertex submission. Past render "fixes" have caused
  FPS regressions or FIFO desyncs that only show up on real hardware.

### Per-game hacks specifically

When a fix turns out to be specific to one game, it does **not** get a generic,
address/parameter-configurable mechanism — it gets a single named on/off row next
to the existing per-game hacks on options page 6 (`OPT_HOKUTO_HACK`,
`OPT_PUYO_HACK`, ...). Copy that row's pattern exactly: `OPT_*` id, boolean display,
same toggle in both switch statements. Keep a general mechanism underneath only if
it's independently worth having — the thing the user actually touches is one
switch with the game's name on it.

## Testing: Dolphin vs. real hardware

The user tests on both and switches freely. Both matter, but they're not
interchangeable:

- **Dolphin** is fast to iterate on but does not emulate D-cache coherency or
  hardware timing, and three Hacks-tab settings independently make any EFB→RAM
  read-back return zeros with no error: **Defer EFB Copies to RAM** (ON by
  default!), **Store EFB Copies to Texture Only**, and **Skip EFB Access from
  CPU**. Before trusting any CPU read-back of GPU-written memory in a Dolphin
  session, confirm those three are unchecked. Also never trust Dolphin FPS numbers
  as a proxy for hardware performance, in either direction.
- **Real Wii/vWii hardware** is ground truth for correctness and performance, but
  the user does the build/flash/run themselves — you cannot see the screen or the
  log live.
- All `printf` output on the Wii build goes to `/ndclog.txt` on the SD card root
  (`InitRenderer()` in `gxRend.cpp` freopens stdout there right after boot), never
  to the screen — nothing debug-related is expected to appear on screen during
  gameplay. The file is truncated per run and stdout is fully buffered, so a
  capture that ends in a power cycle loses the tail; any new diagnostic burst
  should `fflush(stdout)` and print an "ARMED" marker at init.
- Temporary `[TAG]`-style debug printfs (e.g. `[SPG]`, `[CANARY]`) added mid-
  investigation should be re-commented out again in the same turn once they've
  confirmed or ruled out their hypothesis — leave a one-line note on what was
  found and that it's safe to re-enable, matching the existing commented-block
  convention in the codebase. Don't leave throwaway diagnostics printing forever.

## Git

**Never run `git commit` (or `git push`) without the user explicitly approving
that specific commit.** Approval for one commit does not carry to the next —
after finishing an edit, leave it in the working tree, describe what changed and
what the commit message would be, and wait. This also means: if a user reports "no
change" after an edit that should plausibly have done something, check `git diff`/
`git status` on the files just touched before re-deriving the theory — confirm the
edit is actually still on disk first.

## Environment notes (this Windows/Git Bash setup)

- Bash-tool heredocs strip backslashes even when quoted, so a pattern meant to
  match a literal `\` (e.g. inside a C string escape like `\n`) can silently fail
  to match. When a search/replace needs a literal backslash, build it with
  `chr(92)` in a script, or use the Write tool instead of a heredoc.
