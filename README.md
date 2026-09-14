[![Discord](https://img.shields.io/discord/286429969104764928?label=NullDC4wii&logo=discord&logoColor=FFFFFF)](https://discord.gg/sJst6jmQyH)

# NullDC4Wii - Dreamcast Emulator for Wii

a fork from https://github.com/skmp/nullDCe

## TODO (Maybe you can help !)

### Simple

- Test current state with every game and report compatibility (see "compatibility" below)
- Create presets for games
- Test 2/3/4 player mode on wiimote & gamecube also, please report
- Help me finding regression (NOT bugs or glitch, only regression for now please)
- Comment / Guides / Documentation (WiiBrew Wiki)
- Test and report Fishing Rod/USB Keyboard/Lightgun/Maracas support


### Developer (Easy)

- Controller correct layout, for pro pad and for gamecube pad
- User custom Preset
- Fishing Rod/USB Keyboard/Lightgun/Maracas support (probably unsupported now)
- Put external config file for controllers (controls.cfg)
- fix Volgarr regresion
- DualShock 3 issue: left stick has the Y axis inverted, up is down, and down is up.
- Custom layout : Chuchu rocket/Quake3 (bith Gamecube & wii)
- Custom layout : a toggle to make the right analog stick with supported controllers act as buttons X (Left) - Y (Up) - A (Down) - B (Right). This is useful for twin stick shooters that use the diamond button layout for gameplay (like Xeno Crisis). (like chuchu probably ? so it's done I think)
- Add a winCE preset to put an additional menu message (after option screen) to prevent that game is WinCE and isn't supported (wince=yes in game_presets.cfg). Message be like : "This is a WinCE game, it's not supported yet by NullDC4Wii (and probably never will). Press A to launch Anyway, B to return to file selection."
- Add zlib-compressed CHDs Support (cdzl). Make a message that CHD with cdlz is not supported

### Developer (Normal)

- 4/3 support (implemented, need fix on some games like Shenmue)
- Support for CHD/ELF game file
- Return to file list (instead return to homebrew menu)
- Synchronise Dreamcast clock to Wii's clock
- Get RGB565_opaque_alpha AUTO mode
- Get Offset color AUTO mode

### Developer (Hard)

- Fix Chuchu Rocket 16Bit PCM bug
- Fix non WinCE games not launching (Rez, San Francisco 2049...)
- Improve gxRend.cpp = main file about specific rendering for Wii
- Splitting gxRend.cpp in multiple files ? (beware this is more tricky than it look)
- Table convertion between SH4 Opcodes of SH4 and the WiiPPC ?
- Use LLVM to port code for PowerPC ? (skmp says its not a good idea in this case)
- Dynarec improvement (very performant right now, but if we can find boost...)
- WinCE Games support https://github.com/BenoitAdam94/nullDC4Wii/issues/37

## Installation

### Put BIOS file and game file

#### Mandatory BIOS files in SD:/data/

- dc_boot.bin  
- dc_flash.bin  
- fsca-table.bin (included)

#### Optional BIOS files in SD:/data/

- dc_flash_wb.bin (this is the dc_flash but already saved)  
- syscalls.bin (needed for elf/bin)
- IP.bin  (needed for elf/bin)

dc_nvmem.bin  
vmu_default.bin  

#### Game file in SD:/discs/ or USB:/dreamcast/

**Test with castlevania Resurrection and Sega Tetris to begin with**

Put your folders with GDI in this directory. CDI also works

Might work for ISO / BIN / CUE / NRG / MDS

BIN/CUE/ELF, but you probably need IP.bin/syscalls.bin (take IP.TMPL from bootdreams and rename it IP.Bin)


## Configuration

### General configuration

Check nullDC.cfg at root

### Controls

| Dreamcast | Wiimote | Wiimote (ChuChu Rocket!) | Gamecube | Gamecube (ChuChu Rocket!) |
| --------- | ------- | ------------------------ | -------- | ------------------------- |
| A         | A       | down & A                 | A        | A                         |
| B         | B       | right & B                | B        | X                         |
| Y         | 1       | up                       | Y        | Y                         |
| X         | 2       | left                     | X        | B                         |
| START     | Home    | Home                     | START    | START                     |
| D-PAD     | D-PAD   | - no implementation -    | D-PAD    | D-PAD                     |
| STICK     | Nunchuck Stick | Nunchuck Stick    | STICK    | STICK                     |
| L         | - (and Nunchuck Z) | - (and Nunchuck Z) | L    | L                         |
| R         | +       | +                        | R        | R                         |
| To Exit   | - and + | - and +                  | R + L + Z | R + L + Z                |

Exit :  
Press - and + (wiimote) or Press L + R + Z (or L + R + Start)  


### Special layouts (CONTROLS menu)

The last menu before launch has a **SPECIAL LAYOUT** row. It is not saved -
it goes back to OFF on every boot, like every other menu setting.

| Value | What it does |
| ----- | ------------ |
| OFF | Normal per-device mapping (the table above) |
| CHUCHU ROCKET | The ChuChu Rocket columns of the table above |
| DDR SELECT | Dance Dance Revolution Club Mix / 2nd Mix: holding the Gamecube Z button reads as "analog stick pushed fully down", which is how those games take a Select input (dancer / arrow skin / sequence type, and the Left, Right and Shuffle option codes) |
| USER CFG | The whole mapping comes from `user_controls.cfg` instead - see below |

A game preset can set this row for you with `layout=off|chuchu|ddr_select|user_cfg`
in `game_presets.cfg`. `[default]` sets `layout=off`, which is what resets the row
between game selections - do not delete that line, or a layout set by one game
stays active for every game you launch afterwards in the same session.

### user_controls.cfg (USER CFG layout)

Fully remappable controls, from a file you edit by hand. Put `user_controls.cfg`
next to `boot.dol` or in your games folder - same places `game_presets.cfg` is
looked for - then set SPECIAL LAYOUT to USER CFG. The file itself documents the
format and lists every target and every physical source you can bind.

The CONTROLS menu tells you whether it was actually found: with USER CFG
selected the row reads `(user_controls.cfg LOADED)` or `(user_controls.cfg NOT
FOUND)`. If the file is missing, USER CFG quietly falls back to the normal
mapping, so it can never lock you out.

**Note:** the `user_controls.cfg` that ships with the emulator deliberately
reproduces the default mapping line for line. Selecting USER CFG without
editing it first will look like nothing happened - that is expected. Edit the
file, then reboot the emulator: it is read once at startup.

### Sixaxis / DualShock 3 (USB) - hold Gamecube B at boot

DS3 support is **off by default** and has to be switched on at boot: **hold B on
a Gamecube controller** (any port) while the emulator starts. The CONTROLS menu
shows `SIXAXIS/DS3 (USB): [OFF]` when it was not switched on.

It has to be opt-in because raw USB access to the pad needs IOS58, and switching
the console to it restarts the whole I/O system - Bluetooth included. Every
connected Wiimote drops its link and has to reconnect right as the emulator is
starting up, which is why Wiimotes would sometimes refuse to connect in the menu
or in game. Leave the button alone and none of that happens.

It has to be a *Gamecube* button, not a Wiimote one: the Gamecube ports are read
directly by the console's main CPU, so they work before any of this is decided,
while reading a Wiimote would need the very Bluetooth stack in question - and no
Wiimote has connected yet that early in boot anyway.

A console with no Gamecube ports (Wii Family Edition, Wii Mini, Wii U vWii) can
therefore not enable DS3 support.


### VMU (Memory card)

It seems to be supported, but 1rst you'll need to format the VMU in the bios

Files appears at root of /data/ :  
- vmu_save_A1.bin
- vmu_save_A2.bin

## Compatibility

https://wiibrew.org/wiki/NullDC4Wii/Compatibility

## Presets

Presets are grouped in the in-emulator menu across **7 pages**. The order below follows the exact same order shown on screen (Page 1 to Page 7). "Default" below is what actually ships out of the box, from `game_presets.cfg`'s `[default]` section (applied on every launch before any per-game section) - see [game_presets.cfg](#game_presetscfg) further down for how to override any of this per game.

### Page 1 : General

#### RATIO

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **ORIGINAL** | 4/3 pillarbox | Original Dreamcast ratio |
| **FULLSCREEN (default)** | Stretched to fill screen | Fullscreen |
| **AUTO** | Picks by console aspect ratio setting (4:3 console → full width, 16:9 console → pillarbox) | Depends on console setting |

#### SPEED LIMITER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (uncapped, default)** | Emulator can run above 100% speed | Uncapped |
| **ON (cap 100%)** | Stops speed exceeding 100% | Capped |

#### SHOW FPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No overlay | Nothing displayed |
| **ON** | Displays gameplay FPS and speed overlay | Overlay shown |

#### 🖼️ Graphics Preset

OLD Behavior (Before alpha0.64) :

| Mode | Settings | Best platform | 
|------|----------| ------------------------- | 
| **LOW** | `GX_NEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **NORMAL (default)** | `GX_LINEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **HIGH** | `GX_LINEAR` · `lod_bias -0.5f` · `GX_ENABLE` · Anisotropic x2 | Wii U |
| **EXTRA** | `GX_LINEAR` · `lod_bias -0.75f` · `GX_ENABLE` · Anisotropic x4 | Wii U |

The visual difference is limited for NORMAL/HIGH/EXTRA

<img width="1844" height="1456" alt="levels" src="https://github.com/user-attachments/assets/79d5271d-0689-43d4-92c0-66674013ddce" />

NEW behavior (from alpha 0.64) : 

| Mode | Settings |
|------|----------|
| **LOW** | `GX_NEAR`  |
| **NORMAL (default)** | `GX_LINEAR`   |


- Use LOW for 240p games/modes
- Use NORMAL for other games

Important note : LOW can cause Z-Fighting (example in jet set radio, see https://github.com/BenoitAdam/nullDC4Wii/issues/115)

The old HIGH/EXTRA levels were nothing but NORMAL plus a fixed `lod_bias`/bias-clamp/anisotropic bundle - those are now the three separate presets below (GX / LOD BIAS / ANISO), so any combination is possible instead of only the 4 fixed tiers.

#### GX (LOD extras)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `GX_DISABLE` on `GX_InitTexObjLOD`'s biasclamp + edgelod | Standard |
| **ON (default)** | `GX_ENABLE` on biasclamp + edgelod | biasclamp stops LOD_BIAS pushing a minified texel past the point its footprint no longer covers the pixel; edgelod computes LOD from adjacent instead of diagonal texels |

ANISO forces this on by itself, since libogc requires edgelod whenever anisotropy > 1.

#### LOD BIAS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **-1.0 / -0.75 / -0.5** | Sharpen: samples a larger mip level than the footprint asks for | Sharper, can shimmer |
| **0.0 (default)** | Hardware default | True no-op |
| **+0.5** | Blur | Softer |

#### ANISO

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **0x (off, default)** | No anisotropic filtering | Standard |
| **2x / 4x** | Anisotropic filtering (Hollywood tops out at 4x - no 8x on this GPU) | Sharper on steep-angle surfaces, costs fill rate |

Requires MIPMAP set to FAST or TRILINEAR (Page 6) - does nothing while mipmaps are off, since anisotropy is only iterated with a `GX_LIN_MIP_LIN` min filter.

#### TEXTURE CACHE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **VERY_FAST** | skmp original algorythm (magic numbers). Buggy in most games  | Max FPS |
| **VERY_FAST+** | very_fast's address-derived slots, but oversized textures route into a properly-sized arena instead of overrunning the next slot (3 targeted fixes - stride surfaces, stride sizing, mip sentinel). Try this first when very_fast is the only fast option but shows corruption | Max FPS, safer than VERY_FAST |
| **FAST** | Best performance/accuracy in most case  | Almost Max FPS |
| **NORMAL (default)** | Display mostly correctly | Good FPS |
| **QUALITY (SLOW)** | Best accuracy. Display correctly | Mid FPS |

Can have huge FPS impact, try to have the lowest parameter.

#### VQ CMPR

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | VQ textures decode to 16bpp | Standard |
| **ON** | VQ textures decode straight to GX CMPR/DXT1 (4 bits/texel) | Pairs with TEXTURE CACHE VERY_FAST/VERY_FAST+: a VQ texture now fits its address-derived slot instead of overrunning its neighbour - the classic VERY_FAST VQ corruption. Second lossy pass on top of VQ, so smooth gradients can band; 565 VQ only |

#### FRAMESKIPPING

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **0 (default)** | No frame skipped | Every frame drawn |
| **1** | Skip 1 frame | Faster, less smooth |
| **2** | Skip 2 frames | Faster still, less smooth |
| **AUTO** | Skips frames while behind real time, min 1 render in 4 | Adaptive, holds emulated time at 100% |
| **AUTO_MAX** | Same as AUTO, but up to 9 skips in a row | Speed over framerate |

The FPS counter shows the RENDER rate, so a low FPS with SPEED at 100% is working as intended.

#### 2D FRAMEBUFFER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **NO (default)** | Disable | Standard rendering |
| **YES** | Enable 2D framebuffer | Try for 2D games |

Still on testing. `debug_log_framebuffer2d` (Page 6 debug logs) tells you whether a given game would even take this path before enabling it.

#### ADVANCED_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | basic alpha threathment  | Not accurate |
| **YES (default)** | additionnal alpha threatment | Near perfect |

Mostly for debug. Should always be on

#### > BLEND_MODE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (LEGACY)** | Disable | Not accurate |
| **ON (CORRECT) (default)** | Activate BLEND_MODE | Accurate, correct for Resident Evil 3 |

If flickerings, try turning off

Note : ADVANCED ALPHA needs to be on for BLEND_MODE

#### >> FPS_BOOST

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (CORRECT) (default)** | Disable | Accurate |
| **ON (FASTER)** | +2 FPS in BLEND MODE, to the cost of wrong Alpha/Transparency | Not Accurate |

note : ADVANCED ALPHA and BLEND_MODE needs to be on for FPS_BOOST

#### PUNCH THROUGH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | PT polys drawn last in TR blend state | Faster, less accurate |
| **ON (correct)** | OP → PT → TR order + PT_ALPHA_REF alpha test | Correct PT list alpha test |

Needed in lot of games

#### TRANS_SORT

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (default)** | Disable | not Accurate (faster) |
| **ON** | can display stuff | Accurate |

Can resolve flickering in some games. Needed in lot of games. Superseded by AUTOSORT (Page 3) for intersecting/interleaved geometry that a painter sort alone can't fix.

#### RENDER TO TEX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster, default)** | RTT frames dropped (legacy) | Faster, mirrors/TV screens missing |
| **ON (correct)** | EFB copied back into VRAM | Correct mirrors/TV screens |
| **OVERLAY (carry)** | Pass not resolved as a texture - its geometry is carried into the next display frame and drawn last, flat on top (parked on the near plane, `GX_ALWAYS`, no Z-write) | For passes the game composites itself as an overlay rather than as a texture, e.g. Silent Scope's sniper crosshair |
| **KEEP** | ON, plus the render does NOT consume the TA list, matching real hardware (the ISP/TSP just walk the tile arrays, which stay valid until `TA_LIST_INIT`) | For a game that renders one accumulated list twice with different write addresses/clip windows, e.g. Silent Scope's scope disc + 24x-magnified world. Pair with SPLIT SCREEN so the magnified half stays inside its own tile clip |

Needed in some games. With OFF, a dropped pass's geometry still leaks into the next frame (drawn first, misclassified as opaque) - that's why some overlays half-show even with this off.

#### SPLIT SCREEN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster, default)** | Every render pass presented fullscreen (legacy) | Faster |
| **ON / TILE CLIP** | Both viewports in ONE render pass, each carrying a PVR User Tile Clip rect | Correct 2P viewports, e.g. Daytona USA |
| **MULTI-PASS** | One RENDER_START per viewport, into its own band of the EFB; ONE assembled frame is presented | Fixes heavy 2P flicker (player1/player2/player1 alternating) in Le Mans 24 Hours, Demolition Racer, Magical Racing Tour |
| **BOTH** | Per-poly tile clips inside multi-pass renders | Combination of the two above |

Needed for 2 players splitscreen or any 2 camera angle games.

### Page 2 : Graphics

#### FMV FORMAT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **CMPR (DXT1)** | Compressed format | Use if some movie displays white |
| **RGBA8** | Uncompressed, full quality | Slower |
| **RGB565** | Uncompressed, no alpha | Faster |
| **TEV (default)** | No CPU colour math at all: the YUV422 source uploads as raw I8 luma + IA8 chroma planes and the BT.601 matrix runs in the GameCube/Wii TEV combiner instead | Fastest, chroma gets hardware bilinear the CPU paths never had; 8-bit TEV coefficients drift ~1-2 LSB from the CPU matrix |

#### YUV STRIDE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | Always decode at the texture's declared (power-of-two) width | Slices a non-padded FMV frame into repeated strips |
| **AUTO** | Use the real per-row pitch only for a texture the YUV converter actually wrote | Fixed Bomberman's intro, Test Drive 6, Dino Crisis |
| **ALWAYS** | Take the pitch from `TA_YUV_TEX_CTRL` for EVERY YUV422 texture | Old behaviour; a game that never programs that register gets it decoded as 16x16 (Soul Calibur's character select went mostly black) |
| **TEXCTL (default)** | AUTO, plus `TCW.StrideSel` (`TEXT_CONTROL[4:0] x 32` texels) as a fallback for surfaces AUTO's converter-address tracking never saw (it only remembers 4 base addresses) | Also fixes the planar RGB path. If a movie is still messy under AUTO, try this before ALWAYS |

#### YUV TWIDDLE FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Twiddled YUV422 textures decode with luma/chroma swapped | Green<->magenta duotone with 1-column striping on affected art |
| **ON (default)** | Correct luma/chroma extraction for twiddled YUV422 | Fixes static YUV artwork such as Virtua Fighter 3tb's "FIRST MATCH" loading screen |

Static YUV artwork only, NOT movie playback - real (planar) FMV is unaffected either way.

#### Vertex Color

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (grey scale)** | Grey Scale | Grey scale (a tiny bit faster) |
| **ON (default)** | Intensity color | Accurate |

Color some pixel (Used in Jet Set Radio Future and Crazy Taxi 1/2)

#### SPRITE COLOR

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Every sprite's Base Colour field discarded, drawn as hardcoded white | A black sprite with `ModulateAlpha` shading draws white instead |
| **ON (default)** | Honours a Sprite's own packed Base Colour (all 4 corners) | Correct - found via Fighting Vipers 2's SEGA screen, whose full-screen black backdrop is a sprite that drew as a white plate |

Pair with VTX ALPHA for sprite-based fades.

#### VTX ALPHA

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Vertex alpha forced to 0xFF on every ARGB1555 polygon regardless of TSP.UseAlpha - a deliberate hack for ARGB1555 cutout fonts (Test Drive 6) | Keeps cutout fonts correct |
| **ON** | Honours `TSP.UseAlpha` as the hardware does | Needed by anything that FADES via vertex alpha on an ARGB1555 surface, e.g. Fighting Vipers 2's SEGA screen full-screen fade |

#### DECAL_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (faster, default)** | no decal alpha  | Not accurate |
| **ON (correct)** | Decal alpha implemented | Accurate, fixes Crazy Taxi's cars |

See more : https://github.com/BenoitAdam/nullDC4Wii/issues/68

#### SEAM FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (a bit faster, default)** | Disable | Thin black seam lines between 2D tiles/sprites remain |
| **ON** | Half-texel UV inset | Fixes black lines between 2D tiles |

Use this or LOW graphics to fix seam lines. See https://github.com/BenoitAdam/nullDC4Wii/issues/18

Warning : ON causes a bug with Vertex Displacement (water mostly) : https://github.com/BenoitAdam/nullDC4Wii/issues/119

#### FOG

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | `TSP.FogCtrl` decoded but never applied | Nothing ever fogged |
| **ON** | Per-polygon PVR2 fog honouring `TSP.FogCtrl`: LUT (`FOG_TABLE`/`FOG_DENSITY`), per-vertex (offset-colour alpha), or LUT mode 2, evaluated per vertex and blended by one extra TEV stage | Distance haze instead of geometry popping in at the draw distance - racers/outdoor games mostly |

Costs 4 bytes/vertex + one TEV stage on fogged polygons only, and recolours every fogged polygon in the scene - stays per-game.

#### BG POLYGON

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster, default)** | v0 color used for EFB clear only, no background quad drawn | Faster |
| **ON (correct)** | Barycentric-extrapolated background quad drawn | Correct bg gradient/texture, e.g. Who Wants to Be a Millionaire |

Caused an FPS regression in other games when left unconditionally on - enable per-game only.

#### RGB565 ALPHA

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (fmt0 only, default)** | Only fmt0 (ARGB1555) forced opaque | Correct for POD 2 |
| **ON (fmt0+fmt1)** | Force opaque for fmt0(ARGB1555)+fmt1(RGB565) | Turn off for POD 2 |

May disapear in a future

#### JOJO FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | Disable | Pre-fix behavior |
| **ON (default)** | TLUT-reupload-skip + CACHE_FAST PalSelect-masking | For JoJo's Bizarre Adventure |

Has to be used with CI4_FAST/CI8_FAST to reduce massive FPS drop in battle. May use the same technique in other games.

#### OFFSET COLOR

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Offset/specular color dropped | Standard |
| **ON (default)** | PIX = base*tex + offset via 2nd TEV stage | Correct specular highlights |

Costs 4 bytes/vertex of FIFO + a second TEV stage on offset polys. May cause white surface on some games (ie = berserk, Tokyo highway challenge...). May fix black surface on some games (ie = sega worldwide soccer)

#### 4BPP MODE / 8BPP MODE

| Mode (4BPP/8BPP) | Settings | Rendering | 
|------|----------| ------------------------- | 
| **I4_STUB/I8_STUB** | Dummy algorythm  | Some element doesn't display at all, for max FPS |
| **OPTIMIZED** | Served as test, in the end CI4/CI(FAST) is better | Very good FPS |
| **CI4 (FAST)/CI8 (FAST) (default)** | Best performance/quality | Very good FPS |
| **CI4 (NORMAL)/CI8 (NORMAL)** | Advanced algorythm for CI4/CI8 | Mid FPS |
| **RGB565 (ACCURATE)** | Most advanced algorythm | Can have massive FPS dropdown (1 FPS) on some games |

### Page 3 : Depth & Width

#### DEPTH_CLIP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | XF Z-clipping on, no near margin | 2D/menus can be invisible on real Wii |
| **NEAR MARGIN (default)** | Pads vtx_min_Z 0.1% so the nearest 2D layer can't land exactly on the near clip plane | Recommended for Wii - CONFIRMED, fixes ChuChu Rocket and Crazy Taxi menu/intro with no downside seen |
| **NO CLIP (Dolphin)** | Matches Dolphin: out-of-range depth clamps instead of the poly vanishing | Regressed Crazy Taxi when combined with FIXED DEPTH=TIGHT - avoid that combo |

It's basically like FIXED_DEPTH, leave it to NEAR MARGIN

#### FIXED_DEPTH

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF / DYNAMIC (default)** | Legacy per-vertex min/max W tracking, fits Z range to the scene each frame | Good |
| **WIDE** | Skips that tracking, fixed planes W=[0.0001..100000] - safe everywhere, coarser Z | can help display some stuff - mostly for debug, more Z-fighting risk |
| **TIGHT** | Fixed planes W=[0.1..25000] - much finer Z, but geometry outside that range clips | can help display some stuff |

FIXED_DEPTH can help flickering and Z-Fighting

#### HUD_PASS

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (default)** | -  | Not active |
| **OVERLAY (no Z-write)** | Help hud to display when FIXED_DEPTH is on TIGHT | accurate, but may be overdrawn by geometry drawn after it |
| **PROTECT (Z-write at near plane)** | Help hud to display when FIXED_DEPTH is on TIGHT | Perfect - use if OVERLAY leaves polys in front of the HUD |

Mostly needed if Fixed Depth is set to tight. A no-op on its own (dynamic/wide ranges never clip the HUD). Fixes the "Z-fighting gone but HUD vanished" case in Rayman 2 / Cannon Spike.

#### LEGACY DEPTH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | Current depth pipeline | Standard |
| **ON** | Reproduces the depth pipeline exactly as it stood at commit `1bb8c27`: fixed planes NEAR=0.001/FAR=10000*1.001, `vert_base` 1/W clamp at 0.001 (not 0.0001), no per-vertex min/max tracking or margin/HUD fixups | For a game that only rendered correctly at that old commit - Buggy Heat's logo/VMU screen/gameplay. Overrides FIXED DEPTH; the 0.001 clamp is the one thing FIXED DEPTH cannot express |

Shown first on this page in-game, right after LAUNCH.

#### SUBPASS ZCLEAR

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Single shared depth pass - a later pass inherits whatever the main scene left in the Z buffer | Standard |
| **ON** | Re-parks the whole Z buffer at a known W via a full-canvas `GX_ALWAYS` quad (colour untouched) right before a later geometry group | Gives e.g. a HUD PASS=PROTECT overlay a clean depth baseline instead of the main scene's leftovers |

#### PPZ_WRITE : PER POLYGON Z WRITE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | No Per Polygon Z Write  | More compatible |
| **YES (default)** | Per Polygon Z Write | More accurate |

Try putting NO if you experience troubles, with HUD for example.

#### ISP_DEPTH_FUNC

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard |
| **ON (opaque/PT)** | Per-poly depth test on opaque/PT lists, honouring each polygon's actual `isp.DepthMode` register instead of the fixed GEQUAL painter compare (translucent list stays GEQUAL, matching real PVR autosort) | Experimental |
| **ON (all lists)** | Same, applied to all lists | Experimental |

Different from LAYER SORT below: this reads the hardware's own per-polygon compare mode instead of a heuristic sort.

#### ISP_CULL

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable (never cull) | Standard |
| **ON** | Per-poly backface cull from `isp.CullMode` | Experimental |
| **ON (swap winding)** | Per-poly backface cull, two cullable windings swapped | Use if plain ON makes geometry vanish / look inside-out |

#### AUTOSORT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard |
| **1-4 layers (slow)** | Real per-pixel PVR autosort via GX depth peeling, value = max translucent depth layers composited per pixel | Stronger than TRANS_SORT's per-strip painter sort, for intersecting/interleaved translucent geometry. Very GPU-heavy (~2 extra TR walks + 2 EFB Z copies per layer) - use 2 or 3 only where needed, per-game only. Best paired with PUNCH THROUGH=ON |

Overrides TRANS_SORT; LAYER SORT overrides this.

#### LAYER SORT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard sort |
| **ON (TR tier sort)** | Layer-tiered translucent sort | For 2D scenes drawn at ONE depth |

Helps determine what should be front and back trough looking at texture format/properties. For games that submit their whole 2D scene at a single depth and rely on the Dreamcast's per-pixel autosort: background plates draw first, then stage art, then stage sprites, then everything else. Game-agnostic — used by Hokuto no Ken and Street Fighter III. Was part of the HOKUTO HACK before alpha0.66.

#### LIST ORDER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Flat strip buffer walked in submission order | Assumes the game submits its opaque list first |
| **ON** | If the opaque list opened AFTER the translucent one, its range is drawn first, as real hardware would | Fixes "gameplay visible for a moment, then a big background image covers everything" - Puyo Puyo 4/DA!, whose two full-screen background plates go out in the OPAQUE list after the gameplay sprites |

Bit-identical to OFF for any game that submits OP first (condition never fires).

#### X SCALER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | PVR SCALER_CTL.hscale support disabled | Only the LEFT HALF of the image shows in affected games |
| **ON (default)** | PVR SCALER_CTL.hscale support | For Omicron / Wacky Races (render 1280 wide, scaler halves 2:1) |

For Nomad Soul and Wacky Racer. Maybe other games

#### Y SCALER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | `SCALER_CTL.vscalefactor` ignored | Only a SLICE of the image shows in affected games |
| **ON** | Scales the projected canvas height by that factor (6.10 fixed point, 0x400=1.0 - above 1.0 the CORE renders taller and the scaler shrinks on write / vertical SSAA, below 1.0 the opposite) | Whole scene shows - needed for Silent Scope's sniper scope to be 100% accurate |

The vertical counterpart of X SCALER - same register family, other axis.

#### H SCALER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | `VO_CONTROL.pixel_double` ignored | In low-res video modes the framebuffer holds a HALF-width (320) image doubled by the video DAC - scene is drawn in a 320-wide screen space, filling only the LEFT HALF of a 640 canvas |
| **ON** | Halves the projected canvas to match | Whole scene shows - mandatory for several games, and needed for 2-player splitscreen in some |

Register-driven counterpart of CANVAS WIDTH below, which stays the manual override for games that render narrow WITHOUT setting the bit. An explicit CANVAS WIDTH wins over this.

#### CANVAS WIDTH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (640, legacy, default)** | Legacy 640 canvas | Standard |
| **Custom value** | Forces canvas width in 240p modes | e.g. SF3 Double Impact = 384 (the CPS3 arcade width) |

See compatiblity wiki for more info

#### POLY OFFSET

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No bias | Legacy |
| **Tier 1-3** | Native polygon offset / Z bias via a Z-texture in ADD mode (`GX_SetZTexture`), applied to the Punch-Through list - increasing bias strength | GX equivalent of the co-planar decal/shadow/road-marking sort that real PVR tile order used to give for free (real hardware has no `glPolygonOffset`-style register) |

### Page 4 : Audio

#### AUDIO BUFFERS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **DEFAULT (saved)** | Leaves the value at its cfg/UI setting | Depends on saved config |
| **0 (never block)** | Never blocks/drops on overrun | Fastest, most likely to drop |
| **1 / 2** | Blocks until below N queued buffers | More paced |
| **3 (most paced)** | Most conservative pacing | Most paced, safest |

Put audio buffers = 1 generally leads to good audio. To the cost of FPS unfortunatly.

#### CDDA MUSIC

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | CD audio tracks silent | No CD music |
| **ON (default)** | GD-ROM Red Book audio fed to the AICA mixer | CDDA music plays in games |

#### MUTE 16BIT PCM

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | All AICA sample formats audible | Standard |
| **ON (silence 16B)** | 16-bit PCM channels silenced at KEY_ON | Fixes ChuChu Rocket's echoey 16-bit SFX (also mutes any other 16-bit music/voices, so game-specific) |

#### AICA FAST

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | All 64 AICA channels visited every sample, even idle ones | Standard |
| **ON (default)** | Only playing channels are visited, empty filter-envelope call skipped, no DSP send while the DSP is off, master volume cached, silent CD-audio skipped | Same output samples, cheaper mixer - Wii-CONFIRMED: aica% 10.0->7.7, speed +2.6%, no audible change |

### Page 5 : Core

#### 🧮 Calculation Accuracy Preset

| Mode | Description |
|------|------------|
| **FAST (default)** | Maximum FPS (higher frame rate), less loading times |
| **BALANCED** | Good balance between speed and accuracy |
| **ACCURATE** | Closest behavior to original hardware |

If you experience Freeze in some heavy games like Shenmue, put FAST or BALANCED. FAST may be the default setting in future versions

If you experience various bugs (example that may happens : weird AI controled NPC, weird timing) put ACCURATE

#### ASYNC_RENDER

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (legacy)** | CPU blocks in GX_DrawDone until the GPU finishes each frame | 0 frame latency |
| **ON (faster, default)** | Frame queued, presented one vblank later; SH4 emulates while GPU draws | Faster, to the cost of 1 frame input-lag |

ASYNC_RENDER is generally faster. Can resolve flickering. Works better on real hardware than dolphin

#### RENDER DELAY

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster, default)** | Legacy: instant list-complete IRQs | Faster |
| **ON (hw-like)** | Hardware-like staggered ISP/TSP/Video timings | ON for MvC2 and CvSNK |

#### TMEM CACHE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Full GPU texture cache invalidate every frame | Standard |
| **ON (faster?, default)** | Invalidate only on texture re-decode | Keeps GPU texture cache warm |

Haven't seen any effect but keeping on for now

#### SH4 CLOCK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **200MHZ (full, default)** | Nominal Dreamcast speed | Full speed |
| **Underclock (150-200MHz, step 5)** | Lower value = fewer emulated cycles per real second | Lower = faster host, slower game |

Underclocking is supposed to raise FPS. Didn't see any difference

#### ARM7 SPEED

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **10MHZ (default)** | Sound CPU at normal speed | Standard audio |
| **5MHZ (faster)** | Underclocked sound CPU | Faster, check audio! |
| **2.5MHZ (risky)** | Heavily underclocked sound CPU | Risky, check audio! |

5 mhz generally works and bring FPS boost

#### ARM7 JIT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (interpreter)** | Cached ARM7 interpreter | Standard audio |
| **ON (basic)** | ARM code translated to PPC, same results/timing as the interpreter | Faster, Wii-confirmed |
| **ON (linked, default)** | Same, plus blocks branch straight to each other instead of going back through the dispatcher after every branch | Fastest, Wii-confirmed |

A conformance test suite runs once at ARM init (see `[ARM7JIT]` in `/ndclog.txt`); a failure falls back to the interpreter automatically. Compare `arm:%` on the stats line and listen before keeping a change.

### Page 6 : Experimental Stuff & Debug

#### MIPMAPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (fastest, default)** | No mip chain | Fastest, more shimmer far away |
| **FAST** | Generated GX mip chain + nearest-mip bilinear | Less shimmer far away |
| **TRILINEAR (slow)** | Best quality | Best quality, halves texture fill rate (e.g. -40% in Test Drive 6) |

Required for ANISO (Page 1) to have any effect.

#### TEX WRAP GUARD

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Persistent texture arena wraps without draining the GPU first | If the GPU is still sampling old textures when the 14 MB cache arena wraps, their bytes get overwritten mid-sample - arena-wrap race corruption |
| **ON** | Mid-frame, if the arena fills, calls `GX_DrawDone()` to drain the GPU queue before wrapping; a second exhaustion in the same frame falls back to the skimp slot | Wii-CONFIRMED mechanism (watch `[TEXC] drains=/wraps=`); did NOT fix the JSR intro streaking it was tried against, so it's not a general streak fix |

#### TEX CLAMP FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | A cached texture keeps the FIRST polygon's ClampU/V+FlipU/V wrap mode forever, since those are per-POLYGON TSP bits baked into `GXTexObj` only on decode | A tiled floor inheriting `GX_CLAMP` smears its edge texels to infinity - "streaked"/"infinite X or Y" symptom |
| **ON** | Every texture bind re-applies the binding polygon's own wrap mode via `GX_InitTexObjWrapMode` before `GX_LoadTexObj` | Wii-CONFIRMED: fixes streaked textures in Crazy Taxi (VERY_FAST+ trees), Deadly Skies (FAST/VERY_FAST+ floors), Jet Set Radio (VERY_FAST+ intro). Does NOT fix Shadow Man's sky (QUALITY cache re-decodes every frame, never susceptible) |

#### DMA FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Disable | Standard |
| **ON (default)** | ch2/PVR/Sort/AICA-G2 DMA correctness fixes ported from the verified-working NullDC PSP port | Fixes related to loading CDI/GDI file |

#### SCHED (ORDER)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (cascade, default)** | Legacy Medium/Slow/VerySlow timeslice cascade | Standard |
| **ON (deadline)** | Unified cycle-deadline scheduler: GD-ROM read-done, ch2/PVR/AICA-DMA completion, render-done and TA list-end fire through one deadline queue in true hardware order | Experimental, related to loading CDI/GDI file |

#### EXIT FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Bare `exit(0)` | Can leave a black screen with the Wii still on when returning to the Homebrew Channel |
| **SAFE SHUTDOWN (default)** | Ordered teardown before exit | Fixes the black-screen hang |
| **SAFE + WATCHDOG** | Safe shutdown + loader-stub watchdog | For a hang the plain safe shutdown doesn't catch |
| **SAFE + WATCHDOG + HOST POLL** | Same, plus polls the exit combo host-side once a frame | For a game that stopped polling Maple, so the exit combo itself can still fire |

#### TRANS ZWRITE (debug only)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **ON (default)** | Legacy: the TR list inherits the frame-wide GEQUAL compare WITH depth write, so the first translucent strip submitted near depth stamps the Z buffer and depth-rejects farther translucent strips behind it | One near full-screen quad can hide the rest of the list |
| **OFF** | Keeps the depth TEST (opaque geometry still occludes translucent behind it) but drops the write, so translucent strips composite in submission order | DEBUG ONLY: hides the Dreamcast BIOS boot logo, which needs the depth ordering this removes |

Survives purely as an investigation tool for scenes where translucent strips wrongly occlude each other.

#### HOKUTO HACK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard sort |
| **ON (HnK addresses)** | Hardcoded RAM address hack | Specific fix for Hokuto no Ken |

Refines LAYER SORT using Hokuto no Ken's own VRAM texture addresses, for the debris tiles that texture format/properties alone cannot tell apart from the fighters. Needs LAYER SORT on — it does nothing by itself — and only works for stage 1/2 at the moment. Leave it off in every other game.

#### PUYO HACK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | Disable | Standard |
| **ON** | Folds Puyo Puyo 4's own two hardcoded backdrop VRAM addresses into the LAYER BACK TEX tier-4 sort | Fixes the gameplay playfields (puyos going dim behind the grid) and the intro/main screen background (fading UI/logo pieces buried under it) |

Puyo Puyo 4 ONLY. A menu-row twin of the cfg-only `layer_back_tex` key (see game_presets.cfg below) so the fix works without touching the SD card at all - e.g. from inside Dolphin.

#### DINO CRISIS INVENTORY FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | Disable | Standard |
| **ON (redecode)** | Per-game hardcoded texture redecode hack (icon slot) | Dino Crisis inventory fix |

Hardcoded to one address - meaningless for any other game.

#### SH4 CORE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **INTERPRETER** | Interpreted SH4 core | Slow, for debugging only |
| **DYNAREC (default)** | JIT recompiler | Fast |

#### Debug logs

All default OFF, all write to `/ndclog.txt` on the card, costing a card write every frame while on - turn back off once a log has answered its question.

| Row | What it logs |
|-----|--------------|
| **Dbg FB2D Log** | Whether a game ever takes the bit-24 render path 2D FRAMEBUFFER (Page 1) would act on - answers "is it worth trying on this game?" before flipping it |
| **Debug Message** | Renderer trace (`[PATH]`, `[FB]`, `[RTT]`...) |
| **Debug Loop** | Per-loop CPU/GD-ROM/IO trace. VERY slow |
| **Debug GDROM** | GD-ROM / CDDA SPI command trace |
| **Debug Skip Tex** | Hides every polygon using one texture (VRAM address, hex or decimal, from a `[SCN]` census `addr=` field). Diagnostic only - it REMOVES geometry, never fixes anything. Answers whether an element is missing because something draws OVER it, or because it's drawn wrong itself - this is how the Hokuto no Ken layering bug was pinned down |

### Page 7 : JIT/DYNAREC

Perf and diagnostic presets for the SH4 dynarec and its interpreter-fallback path. Several read at COMPILE time, not per-frame - set them before launching, not mid-game.

#### JIT SBP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | No stale/self-modified block guard | Fastest, riskiest |
| **KNOWN (default)** | Guards known self-modifying regions (DOA2LE, Shenmue 1/2, ...) plus the boot-entry cache flush | Balanced |
| **ALL RAM (slow)** | Guards all RAM | Safest, slowest, diagnosis only |

#### FASTMEM

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Standard PPC-MMU memory access | Slower |
| **ON (faster, default)** | Branchless JIT MMU-mapped memory access via segment regs + hashed page table; MMIO/SQ/BIOS accesses DSI-fault once and back-patch to slow-path trampolines | Faster |

Crash observed in Re-Volt when launching a race. Only game that does that for now.

#### JIT BCACHE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Dynamic jumps (jmp/rts/bsrf) chase `cache[]` -> `DynarecBlock` across two cache lines + a counter write | Standard |
| **ON (flat, default)** | One flat, 1-cacheline `{addr, code}` dispatch entry per dynamic jump | Faster dispatch. L1/L2 cache related - can help heavy scenes like Shenmue's intro |

#### JIT DYN IC

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | No inline cache on dynamic branch exits | Standard |
| **ON (JSR/JMP)** | Per-site inline cache on JSR/JMP dynamic exits | Experimental |
| **ON (+RTS, default)** | Same, plus RTS | Wii-measured +0.98% - the whole win is in RTS sites; JSR prediction actually runs backwards, so there isn't much more to gain here |

#### FPU PIN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | fr0-15 not pinned | Standard |
| **ON (default)** | Pins SH4 fr0-15 to real PPC FPU registers f14..f29 for the whole session | Speeds up geometry-heavy games (fadd/fmul/fmac/fipr/ftrv/cvt_* stop round-tripping through memory) |

#### JIT ALIGN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | No block alignment | Standard |
| **ON (32B lines, default)** | Pads every SH4-dynarec block entry to a 32-byte Broadway L1 cache line | Cache-hygiene only, no logic change, marginal effect |

#### JIT IFB FLUSH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (full spill)** | Every interpreter-fallback opcode (28 of them, incl. `div1`, `addc`, `subc`, `mac.l`, `tas.b`...) gets a FULL register-file spill - 15 stores + 15 loads, +32 more with FPU PIN on | Correct, slow |
| **ON (selective, default)** | Narrows the spill per opcode to only the GPR/float registers that opcode actually touches, via a closed allow-list | Same result, cheaper. `div1` is the costly one - SH4 has no divide instruction, so a 32-bit software division is ~32 `div1` calls, each paying the spill |

An opcode not on the allow-list keeps the full spill either way - slow, never wrong. Best candidates: games doing lots of integer division. Pair with JIT IFB PROBE to see if a game issues enough to matter.

#### JIT IFB PROBE (diagnostic)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (logs [IFB])** | Counts interpreter fallbacks per opcode, writes an `[IFB]` breakdown to `/ndclog.txt` once a second: total/s, how much JIT IFB FLUSH could narrow, spill memory-op rate with/without it | Costs 4 instructions per fallback site - turn off for timing runs |

Read at compile time - set before launching.

#### JIT NEW OPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | 9 opcodes (`stc SR`, `stc.l SR`, `ldc.l SR`, `ldc SR`, `lds FPSCR`, `lds.l FPSCR`, `rotcl`, `rotcr`, `tas.b`) always fell back to the interpreter even though dynarec code for them already existed - it just wasn't reachable from the opcode table | Full interpreter call-out + register spill every time |
| **ON (9 ops jitted, default)** | Wires all 9 into the dispatch table | `stc SR` alone measured up to 96k/s in ChuChu Rocket scenes; all 9 now skip JIT IFB FLUSH's allow-list entirely |

Changes SH4 codegen - read at compile time. Run JIT IFB PROBE first to see if a game issues enough of these.

#### JIT HOTBLOCKS (diagnostic)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (logs [HOT])** | Counts executions per compiled SH4 block, writes the hottest ones to `/ndclog.txt` once a second as `[HOT]` lines with codegen density (PPC bytes/SH4-opcode), plus a full SH4+PPC dump of the top block twice per session | Costs 4 instructions per block entry - turn off for timing runs. Covers everything JIT IFB PROBE doesn't (i.e. non-fallback codegen) |

Read at compile time - set before launching.

#### JIT T-FORWARD

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | A compare followed by `bt`/`bf` (the commonest branch shape on SH4) emits `stw T` then `lwz T` back to back - a load-hit-store stall on Broadway to recover a value that never left the register | Standard |
| **ON (no T reload, default)** | Forwards the T bit straight from the register it was just computed in to the branch that consumes it - only when the compare is the immediately preceding op; the store stays since a later block may read T | Found via JIT HOTBLOCKS on the #1 block of a ChuChu scene |

Changes SH4 codegen - read at compile time.

#### JIT FMOV DIRECT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (GPR bounce)** | `fmov` reads/writes bounce through a GPR and memory | Standard |
| **ON (LFS/STFS, default)** | `fmov` reads/writes straight to/from the pinned FPRs on the fastmem path | **+10% Wii-CONFIRMED** (Castlevania 103%→113%). Needs FPU PIN + FASTMEM both on - this "Phase B" of FPU PIN had been disabled in every real run until fixed |

#### JIT CARRY OPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `addc`/`subc`/`negc`/`addv`/`subv`/`div1`/`cmp-str`/`xtrct`/`swap.b`/`clrmac` always call the interpreter | Standard |
| **ON (10 ops jitted, default)** | Real SHIL ops mapping T onto PPC `XER[CA]`/`XER[OV]` | With JIT IFB FLUSH on, a fallback is ~35-45 cycles; expect tenths of a percent, not a breakthrough (~0.3% of frame in Castlevania). Also a correctness fix: the interpreter's `div1` had the wrong Q^M^carry step |

#### JIT MAC OPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `mac.l`/`mac.w` call the interpreter, including their memory reads | Standard |
| **ON (fastmem reads, default)** | Their `@Rm+`/`@Rn+` reads become ordinary `readm` ops through FASTMEM instead of the interpreter's `ReadMem`; `mullw`/`mulhw` + `addc`/`adde` inlined | Also makes `SR.S=1` saturation mode work - it used to be fatal for `mac.l` and silently skipped for `mac.w` |

Expect little effect for most games - the SH4 has a real FPU, so MAC is rarer here than on the SH2 this technique came from.

#### JIT FSCHG FAST

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `fschg` (toggles `FPSCR.SZ` only) still calls the full `UpdateFPSCR()` - a C call + 16 `lfs` + 16 `stfs` just to keep `old_fpscr` in step | Standard |
| **ON (1 stw, default)** | One `stw` instead | DC's vertex T&L brackets its loop with `fschg`, firing ~2.3x per transformed vertex. **Wii-CONFIRMED +10%** in ChuChu mouse mania at matched load; null in Castlevania (doesn't run this hot) |

#### JIT CR0 BRANCH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | A conditional block exit rebuilds T via `mfcr`/`rlwinm`/`stw`/`cmpi` | Standard |
| **ON (default)** | Branches directly on the PPC CR0 bit the compare already set (sr_T is still written, since a later block may read it) | Wii-measured +2.8% on BIOS boot phase; null in Castlevania gameplay. Only fires when the flag producer is the block's LAST op |

#### JIT CCALL CENSUS (diagnostic)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON** | Counts every C function the dynarec still calls out to, prints a `[CC]` block once a second (per-second AND per-frame rates: scheduler tick, interrupt, `bm_GetCode`, `do_sqw`/TA, `UpdateSR`, `UpdateFPSCR`, fsqrt, fsrra, ReadMem/WriteMem misses, mac saturation) | Far cheaper than JIT HOTBLOCKS since counters live inside the callees, but still re-measure speed with it off before trusting a comparison |

The census that found `writemem` at 1.1M calls/s in Crazy Taxi (57% of all JIT call-outs), which led to BLOCKCOPY below.

#### BLOCKCOPY

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Store-queue flushes and PVR/ch2 DMA transfers run the memory dispatcher once per 32-bit word | Standard |
| **ON (default)** | Bulk copy instead | **Wii-CONFIRMED +2%**. Crazy Taxi measured 1,104,636 system-RAM writes/s, 95.6% from plain C code rather than JIT code. MMIO, mirror wraps and region crossings still take the per-word path |

#### JIT FSQRT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `fsqrt` calls newlib `sqrtf` - a 25-iteration shift-and-subtract bit loop bracketed by two load-hit-store stack round-trips (~300 cycles) | Standard |
| **ON (default)** | Inline `frsqrte` + 3 Newton-Raphson steps + an exact-residual step, verified bit-identical to `sqrtf` over 2.07M simulated cases | **Wii-CONFIRMED +3.5-4.3%** (Crazy Taxi, which measured 167,906 fsqrt/s). Zero, negative, Inf and NaN still take the old libm call - `frsqrte` alone is only a 5-bit estimate and once distorted the BIOS swirl |

### Game Specific Presets

LAYER SORT (Hokuto No Ken, Street Fighter III) helps determine what should be front and back trough looking at texture format/properties. HOKUTO HACK adds to it a RAM address check for a few Hokuto no Ken textures that those properties cannot separate — Hokuto no Ken only, and it needs LAYER SORT on.

JOJO FIX (Jojo's Bizarre adventure) has to be used with CI4_FAST to reduce FPS drop in battle. May use the same technique in other games

Vertex color is a special method on Dreamcast to color stuff. Notable example in Crazy Taxi : The arrow and dollar sign are Vertex Colored. You will notice the cars too : They will add to the current texture so the same car with the same texture will appear red, blue, green, etc...

Same for Jet Set Radio, will add color to the logo.

LEGACY DEPTH (Buggy Heat) reproduces one specific old commit's depth pipeline verbatim, for a game that only ever rendered correctly at that point in history.

PUYO HACK (Puyo Puyo 4 / DA!) and its cfg-only twin `layer_back_tex`/`layer_front_tex` (see game_presets.cfg below) fix 2D games that submit a backdrop or overlay plate at the same depth as the sprites it should stay behind/in front of - painter order alone can't tell them apart.

`wince=yes` in game_presets.cfg is not a rendering preset - it just shows a warning after the options menu ("this is a WinCE game, not supported yet - A: launch anyway, B: back to file list") instead of silently failing to boot, for the handful of Dreamcast games built on Sega's Windows CE devkit instead of plain Katana.

See Compatiblity guide for hints depending of the games

## game_presets.cfg

game_presets is a file that reads the file name and directly apply matching presets.

game_presets.cfg needs to be in sd:/discs/

With recent version, multiple file names are suppported :

```
[crazytaxi2][crazy taxi 2] ; more specific first
depth_clip=1
tex_cache=normal
vertex_color=off

[crazytaxi][crazy taxi]
depth_clip=1
tex_cache=normal
graphics=normal
```

So in that configuration, your file name should be crazytaxi.gdi or crazytaxi.gdi

I'm not enterely sure spaces are supported right now

Maybe it needs to be more specific, so in this example, just add [crazytaxi1] like this :

```[crazytaxi][crazytaxi1][crazy taxi]```

Then rename your gdi crazytaxi1.gdi or crazytaxi1.cdi

If everything is correct, in the option menu, you should see something like this :

<img width="754" height="93" alt="image" src="https://github.com/user-attachments/assets/9f59b4a4-c5ed-40e6-b5d5-46cea8b52350" />

## Creating the optimal Preset :

Best thing to do : 

1/ Launch with default preset  
2/ play a little bit the game (default stage, just input A A A), 2/3 minutes is enough  
3/ If some things doesn't display good, try to change settings. Already we know that works :   

- fine (seam) lines in 2D sprites : put "LOW" as graphics
- Z fighting : put FIXED DEPTH to "tight"
- 2 player viewport (or 2 cameras in the same scene) = SPLITSCREEN

4/ Flickering, fix can be  :  

- BLEND_MODE (set to off)
- TRANS_MODE (set to off)
- ASYNC_RENDER (set to on)
- DEPTH-CLIP (set to tight)

5/ Other graphical things not displaying (or black screen) : try flipping all the other setting, particulary :   

- 4BPP/8BPP to CI4_FAST or CI8_FAST
- BG POLYGON to on
- PUNCH Trough to ON
- TRANS_SORT to ON
- RENDER TO TEX to ON
- 2D Famebuffer = ON

6/ off Canvas problem (screen too small or too big)  

- try xratio = on (or off)
- CANVAS_WIDTH (try different values)

7/ Restart the game, try CACHE_VERY_FAST and CACHE_FAST. Find the lowest compatible option for the game. They may crash heavy-scene thus, like Shenmue intro  

8/ Try if FPS_BOOST is possible and don't break too much the game

9/ If you want to dig more, try the game in early version of NullDC4Wii :  

- alpha0.25
- alpha0.28
- alpha0.40

Report to me if you seen any regressions


### Some examples of common problems : 

Fine (seam) lines in 2D example : 

<img width="1040" height="670" alt="Image" src="https://github.com/user-attachments/assets/23f5ba00-4a21-477f-a885-3789b09f110c" />

CACHE related problem examples : 

<img width="1117" height="835" alt="Image" src="https://github.com/user-attachments/assets/64ad3b87-68e3-48d4-9df0-77f74a3ea7ed" />

<img width="880" height="598" alt="Image" src="https://github.com/user-attachments/assets/b5fc1694-a4d8-4f3d-bc43-329c6989bc23" />

Off Canvas : 

<img width="802" height="639" alt="Image" src="https://github.com/user-attachments/assets/99c2411d-4dd0-4ef2-82ce-e3a69f9d6924" />

Z Fighting example : 

<img width="628" height="268" alt="Image" src="https://github.com/user-attachments/assets/f479152c-74ea-4af9-84d5-e693a929adf4" />

Fastmem crash example :


<img width="813" height="499" alt="Image" src="https://github.com/user-attachments/assets/9b731ce5-bb29-4fd2-aecf-8dc400eb680a" />

## For Developpers :

### Compilation Process (Windows)

#### 0/ Download/clone source code

#### 1/ Install devkitpro/devkitPPC

https://wiibrew.org/wiki/DevkitPPC

Just tick PPC (not ARM, x86, etc).

See this issue : https://github.com/BenoitAdam94/nullDC4Wii/issues/13

#### 2/ Launch MSys2 terminal

Devkitpro has it's own UNIX terminal, by default it's located here :  
C:\devkitPro\msys2\usr\bin\mintty.exe

#### 3/ Install additional development packages :

pacman -Syu  # updates MSYS2 and package database  
pacman -S wii-dev

#### 4/ PATH & System variable configuration (Windows)

##### PATH 

In windows variable environnement add C:\devkitPro\devkitPPC\bin to Uservariable PATH

*UPDATE MARCH 2026* :  This folder seems to be needed also for elf2dol : C:\devkitPro\tools\bin

##### System variables

Modify these system variable

DEVKITPPC : C:\devkitPro\devkitPPC  
DEVKITPRO : C:\devkitPro\

**Strongly advise you to completly reboot Windows after that (not just relaunching CMD)**

![path_fornulldcwii](https://github.com/user-attachments/assets/a08a0396-ec1e-4cbe-85a7-0259da89ace9)


#### 5/ launch wii/vs_make.bat in a standard CMD windows terminal

Correct errors if they are some errors



#### ~~Use dollz3~~

dollz3 is a compress tool for *.dol files, and it is in the original "vs_make.bat" file, but it seems not to work

~~https://wiibrew.org/wiki/Dollz~~

### Compilation Process (Linux & Mac)

It should work for Linux & Mac with similar process

Leaving this link for now :

https://wiibrew.org/wiki/DevkitPPC

### Dolphin (for debug/testing)

Activate :
- SD Card
- Display FPS
- For log, in config.ini add "DebugModeEnabled = True" under [Interface]
- (optional) VSync eventually
- (optional) Advanced > Debug > Texture Format Overlay


<img width="1366" height="728" alt="image" src="https://github.com/user-attachments/assets/4e1e3f65-2638-40c6-85a0-bcca3e4f43da" />

## Frequently Asked Question (FAQ)

### My SD Card isn't recognized

Make sure you have a good brand (Samsung, Kingston). Optimal card would be between 2Gb and 32Gb (more should work, we support SDXC), formated in FAT32 with 32kb cluster (16kb or 64kb could work). Try another SD card if that's not working

### Games doesn't work

First, try on SD card with a know working game (Castlevania, Chuchu Rocket or Sega Tetris are best candidate). 

### My USB key/HDD isn't recognized

For USB, try a FAT32 USB key first (32kb cluster) before switching to any other device. It works on my 500HDD FAT32 for information (with a cluster of 4kb)

### Does VMU work ?

It's reported working. Step 1 : go to bios. Step 2 : format VMU. Step 3 : exit properly the emulator (+ and -)

### Why this emulator ?

Because why not ? At first it was a POC (Proof Of Concept) but digging more and more, it seems somes games are achievable to 100% speed (in PAL50 mode). So yeah !

### What games does work ?

Please check the compatiblity Wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Does it work on Wii U ?

We have a Wii U fowarder that also use the speed boot of the Wii U (overclock). A native port for Wii U could be done if someone wants to do port it.

### Does it support Atomiswave ?

Not yet, this could ask between 0.03 and 0.5Mb of RAM. Doable yes, maybe not really worth the effort. But you can use Atomiswave hacks for dreamcast, lots of them do work.

### Will it support NAOMI ?

Definitly not, the recent Dynarec/JIT (= fps boost) and ARM7 cache ask for more RAM, we have no more place to add 16Mb for NAOMI Ram

### Controls are messy / this doesn't work / My Propad isn't recognize etc...

Open an issue, everything should be ok now

### Will THAT AMAZING ADVENTURE GAME be considered as "Supported" one day ?

Probably not, this emulator prioritize multiplayer fun games to play friends with. Playing an adventure game like Shenmue or an RPG could be a very frustrating experience that I strongly do not recommand. Use Gamecube/Wii version (if it does exist) or a PC for emulation instead.

### What can I do to help ?

Try different games, parameters and report in the compatibility wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Is 100% speed of the emulator achievable on Wii ?

The only way to know this answer is to try our best ! Actually, some 2D games run 100% speed, and some 3D games also ! But we are mostly around 60 or 80% of speed...

We are still trying to improve performance, altough we already done a lot of amazing stuff :  
- Improved Dynarec
- Fastmem
- ARM7 CACHE

We also need to work more on TEV (Texture Environnement - a specialized hardware unit inside the graphics chip), it has been tested but only on dolphin. Maybe on real hardware this would have better result.

Another good strategy would be to really have a per-game specific emulator. That would take a long time, so for now we have presets & auto-presets implementation trough game_presets.cfg

Also, it's suggested we build a custom Dolphin-Emulator with some tools to help fine-tuning everything and gain more speed. More info here : https://github.com/BenoitAdam/nullDC4Wii/issues/116

### Is 100% speed of the emulator achievable on Wii U?

Probably ! Wii U has the additional CPU power we need. Test the overlocked fowarder !

### Can it read Original games (GD-Rom) ?

The Wii can't read those disc format. Maybe if you change the optical drive and with some code implementation, but that's not worth it.

### Can it read CDI/Utopia disc ?

Maybe it's possible, but we don't have time to focus on this. CDI Files on SD Card/USB are supported since alpha 0.28 anyway.

### Will WinCE games be implemented ?

That an additionnal ressources in CPU and we are limited. That may would make sense for a WiiU Port.

We are currently testing a branch with some implementation but it doesn't seem to work.

### Will Retroachievement be implemented ?

Probably not, we are super tight on RAM, and it also cost 1/2% of cpu cycle

### Will Netplay be implemented ?

Probably not, again, we are very short on RAM/CPU. And on Wii we don't have the 100% speed everytime.

### How is AI involved in the project ?

Since I (BenoitAdam) digged the NullDC code, AI has been heavily used to make improvement to the emulator. The very first state of the emulator (alpha 0.02) is 99% hand written code by SKMP and NullDC contributors at the time. Only some few changes have been made to be able to recompile it and make it run. Various AI are used : MistalAI/ChatGPT/Codex/Claude and Deepseek. Claude is very convenient because of artifact and Claude Code. Gemini helped on some improvements, and Deepseek too.

### I hate AI !

It's ok, you have the right, but without AI this project wouldn't have been resurected. AI for code is really a big help, definitly not the same thing with AI generated images and videos. For information AI for image ask ~10x more power, AI for video ask ~100x more power. A Wii is also using 20x less power than a PS4/PS5.

Reduce CO2 emission & grow trees is the plan for the planet. Also prevent stupid people throwing their cigarett butt & firmly condemn pyromaniacs.

### Do you have a discord ?

Yes : https://discord.gg/sJst6jmQyH

### Do you accept donation ?

Yes ! Initially I was against donation but spending this much time & effort (and sometimes money) to this project led me to ask for donation. Not trying to getting rich, just staying afloat.

#### Buy Me a Coffee / Ko-Fi

This is very popular amongst small dev project, I don't know what's the difference or if some people would prefer one over the other one, anyway here are the links : 

- https://buymeacoffee.com/nulldc4wii
- https://ko-fi.com/nulldc4wii

#### Patreon

The classic "Patreon" subscribing is here :

https://www.patreon.com/cw/NullDC4Wii

#### Paypal

For those used to Paypal, the link is here :
https://www.paypal.com/ncp/payment/2NE7AYS77K6AJ


## Ressources

### Dreamcast Emulators 

NullDC https://code.google.com/archive/p/nulldc/source/default/source  
NullDC (github) https://github.com/skmp/nullDCe  
NullDC for PSP : https://github.com/PSP-Archive/nulldce-psp  
NullDC for Xbox360 https://github.com/gligli/nulldc-360  
Reicast : https://github.com/skmp/reicast-emulator  
Flycast : https://github.com/flyinghead/flycast  
Deecy : https://github.com/Senryoku/Deecy
Redream : http://redream.io/

### Devkitpro
 
Main website : https://devkitpro.org  
​GitHub : https://github.com/devkitPro  
Installer (releases) : https://github.com/devkitPro/installer/releases  
Wii ​Examples : https://github.com/devkitPro/wii-examples

### libOGC

GitHub (Wii/GameCube system librairy) : https://github.com/devkitPro/libogc

### Emulators​

Dolphin Official Website : https://dolphin-emu.org  
GitHub (for timings Gekko/CPU) : https://github.com/dolphin-emu/dolphin

### Wiibrew Wiki

Emulation Page : https://wiibrew.org/wiki/Emulation  
Homebrew tutorials : https://wiibrew.org/wiki/Main_Page

## Media Coverage

France :  
https://www.programmez.com/actualites/programmez-numero-gaming-et-developpement-de-jeux-est-disponible-39720

Italy :  
https://www.biteyourconsole.net/2026/05/13/nulldc4wii-alpha-v0-22-porta-il-dreamcast-su-wii-con-supporto-usb-multiplayer-e-miglioramenti-prestazionali/

## Credits

- skmp (original NullDC creator)
- NullDC contributors
- Joseph Jordan - libiso  
- Xale00 (also know as Benoit Adam) - 2026 recompilation
- gligli (Xbox 360 port)
- Xeihro (Xiro28 PSP Port)
- AI because this project would have never existed otherwise lol
- Probably Reicast/Flycast Team also
- Welcome to the IA-age guys and good luck everyone.

All together, let's Cast the Dream.

### Special thanks

- skmp because he's the god
- Senryoku, develloper of Deecy emulator
- OriginalDave, developer of Jocasta
- MetalliC, developer of Demul
- Dolphin Team, would have been a pain to test on real hardware everytime otherwise
- devkitPPC/Libogc
- Everyone helping on emuvdev/discord
- Jilou04 for constantly testing on Wii U and reporting
- All testers and all futur testers
- People actually helping me on the wiibrew wiki

### Special no thanks

- To all people not believing in this project
- People constantly critisizing the fact AI is used in this project (this project wouldn't have existed otherwise...).
