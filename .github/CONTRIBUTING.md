# Contributing to NullDC4Wii

This file holds everything that goes beyond installing and playing: how to set up
the build environment, what is left to do, and the detailed reference for every
in-emulator preset. For installation, controls, `game_presets.cfg` basics, FAQ and
credits, see the [README](README.md).

## Development environment

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

## Developer TODO

### Developer (Easy)

- Controller correct layout, for pro pad and for gamecube pad
- User custom Preset
- Fishing Rod/USB Keyboard/Lightgun/Maracas support (probably unsupported now)
- Put external config file for controllers (controls.cfg)
- fix Volgarr regresion
- DualShock 3 issue: left stick has the Y axis inverted, up is down, and down is up ?
- Custom layout : Chuchu rocket/Quake3 (bith Gamecube & wii)
- Custom layout : a toggle to make the right analog stick with supported controllers act as buttons X (Left) - Y (Up) - A (Down) - B (Right). This is useful for twin stick shooters that use the diamond button layout for gameplay (like Xeno Crisis). (like chuchu probably ? so it's done I think)
- Add zlib-compressed CHDs Support (cdzl). Make a message that CHD with cdlz is not supported

### Developer (Normal)

- 4/3 support (implemented, need fix on some games like Shenmue)
- Support for ELF file
- Return to file list (instead return to homebrew menu) (see #14)
- Synchronise Dreamcast clock to Wii's clock
- Get RGB565_opaque_alpha AUTO mode
- Get Offset color AUTO mode
- Get seam_fix to AUTO mode

### Developer (Hard)

- Fix Chuchu Rocket 16Bit PCM bug
- Fix non WinCE games not launching (San Francisco 2049 and some others)
- VMU Screen support in 4/3 and 16/9
- Improve gxRend.cpp = main file about specific rendering for Wii
- Improve with TEV
- Splitting gxRend.cpp in multiple files ? (beware this is more tricky than it look)
- Table convertion between SH4 Opcodes of SH4 and the WiiPPC ?
- Use LLVM to port code for PowerPC ? (skmp says its not a good idea in this case)
- Dynarec improvement (very performant right now, but if we can find boost...)
- WinCE Games support ? https://github.com/BenoitAdam94/nullDC4Wii/issues/37

## Presets

Presets are grouped in the in-emulator menu across **9 pages**. The order below follows the exact same order shown on screen (Page 1 to Page 9). "Default" below is what actually ships out of the box, from `game_presets.cfg`'s `[default]` section (applied on every launch before any per-game section) - see [game_presets.cfg](README.md#game_presetscfg) in the README for how to override any of this per game.

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

#### CABLE TYPE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **COMPOSITE (default)** | Guest sees a composite cable plugged in | Standard |
| **RGB SCART** | Guest sees SCART | Standard |
| **VGA** | Guest sees VGA (480p, 60Hz progressive) - drops the ~17% PAL-on-60Hz judder for games that support VGA | A game without VGA support just refuses to boot (real Dreamcast behavior) |

Haven't tested much for now. Some games (like SFA3) are set to VGA in `game_presets.cfg` since it fixes judder there.

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
| **NORMAL+** | NORMAL's cache, but a texture is re-decoded when any VRAM byte it is built from was written, not only its first word. For games that refill sprite atlases tile by tile (characters that "move like a puzzle": Street Fighter III 3rd Strike, Guilty Gear X) | Between NORMAL and QUALITY |
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

#### TEX ALPHA

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Texture alpha read as the TSP reports it | Standard |
| **ON** | Applies TSP bit 19 (IgnoreTexAlpha) to the TEV, which the renderer never did before | Fixes black panels (menu note: Headhunter) |

#### PCW LISTTYPE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Polygons drawn from the list the TA filed them in | Standard |
| **BLEND** | Translucent polygons the TA filed in the OPAQUE list are blended | Needs BLEND MODE on (default) |
| **BLEND + NO ZWRITE** | Same, and those polygons also skip the depth write, the way the translucent list does | Needs PPZ WRITE or ISP DEPTH FUNC on (the block it rides) |

Treats the symptom in the renderer, not the cause: whether the TA should have opened the list at all is a separate question. Found on Headhunter, whose black panels sit in the opaque range with blend=-1/-1.

#### TA GUARD

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Bad TA data stops the emulator (`die()`) | Standard |
| **ON (default)** | Bad TA records (list-set, types 3/6, invalid polygon type) are skipped | No hang on bad data |

#### INF VTX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | 1/W = +inf vertices sent to GX as they are | Standard, shows spikes/garbage |
| **ON (default)** | Those vertices are cut out of their strips; the finite runs are drawn as their own strips | Fixes the Soul Calibur spikes; VCD unchanged |

#### DECAL_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (faster, default)** | no decal alpha  | Not accurate |
| **ON (correct)** | Decal alpha implemented | Accurate, fixes Crazy Taxi's cars |

See more : https://github.com/BenoitAdam/nullDC4Wii/issues/48

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

#### FOG INDEX FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Fog table indexed by W x density | Standard |
| **ON (density/W, default)** | Fog table indexed by density/W, as the PVR does with its 1/W depth | Correct fog falloff. Only has an effect while FOG is ON |

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

#### FAR CLAMP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Depth floor 0.0001 (W <= 10000) | Standard |
| **ON (default)** | Depth floor 0.00001 (W <= 100000) | Far scenery keeps its depth order (Maximum Speed, Dolphin-confirmed) |

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
Probably needs Punch-Trough=ON to be effective

### Page 4 : Audio

#### AUDIO BUFFERS (Default : 4)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **DEFAULT (saved)** | Leaves the value at its cfg/UI setting (`game_presets.cfg` ships `audio_buffers=4`) | Depends on saved config |
| **0 (free run)** | Never blocks/drops on overrun | Fastest, most likely to drop |
| **1 / 2** | Blocks until below N queued buffers | More paced |
| **4 (recommended)** | Blocks until below 4 queued buffers | Recommended balance |
| **8 (most slack)** | Most conservative pacing | Most paced, safest |

Put audio buffers = 1 generally leads to good audio. To the cost of FPS unfortunatly.

#### AUDIO BLOCK (Default = 1024)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **1024 (default)** | 23ms per ASND buffer | 109% - lowest delay |
| **1536** | 35ms per ASND buffer | 163% |
| **2048** | 46ms per ASND buffer | 218% - most slack |

Bigger = more delay, but more slack against underruns.

#### AUDIO DRC (Default = normal)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | No pitch trim | Standard, crackles possible when speed isn't 100% |
| **GENTLE (+-1%)** | Small pitch retune | Subtle |
| **NORMAL (+-3%, default)** | Retunes output to kill crackle | Balanced |
| **WIDE (+-15%)** | Strong pitch retune | Most aggressive |

Instead of crackles, this detunes audio if the speed isn't 100%.

#### SOUND CLOCK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **EMULATED (exact, default)** | Sequenced music runs at emulated speed | Exact timing, slows down with the game |
| **REAL TIME** | Sequenced music keeps its tempo when the game is slow | Breaks streamed music, so keep SPEED LIMITER on. Per game only, resets to the saved value on each game |

#### CDDA MUSIC

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | CD audio tracks silent | No CD music |
| **ON (default)** | GD-ROM Red Book audio fed to the AICA mixer | CDDA music plays in games |

#### CDDA CLOCK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **EMULATED (legacy)** | CD music runs at emulated speed | Standard |
| **REAL TIME (drive, default)** | CD music on its own voice at real speed, like the real drive | CD music keeps its tempo when the game is slow |

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

#### AICA REL CULL

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (exact, default)** | Release fades are mixed to the end | Exact |
| **ON (-72 dB tails)** | Release fades below -72 dB are cut | Small speed gain, slight sound change |
| **ON (-60 dB tails)** | Cut below -60 dB | More speed |
| **ON (-48 dB tails)** | Cut below -48 dB | Most speed, most audible change |

Per game only (e.g. Sega Tetris), because it changes the sound slightly.

#### ADPCM LOOP FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Looped ADPCM effects reset their decoder state every lap | Standard |
| **ON (keep state, default)** | Looped ADPCM keeps its decoder state and volume on every lap | Fixes held/fading SFX in Sonic Adventure 2 |

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

ARM7 SPEED and ARM7 JIT both govern the sound co-processor, so they moved here from Page 5 (Core) to sit next to the other audio presets.

#### ARM7 BATCH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **1** | Sound CPU runs every AICA update | Standard |
| **2 / 4 (default) / 8 / 16** | Sound CPU runs once every N AICA updates | 4 is Wii-measured +1.5%; 8 and up wobble |

#### ARM7 STR ALIGN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Unaligned ARM7 word stores write the bytes as given | Standard |
| **ON (aligned word, default)** | Unaligned word stores write the aligned word, as the real ARM7DI does | Fixes ChuChu Rocket's echoey / slow SFX |

#### AICA RACE FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Old G2 DMA / ADEN / busy timing | Standard |
| **ON (Flycast, default)** | G2 DMA end, ADEN and busy rules, SCIEB/MCIEB written together, ARM gets +50 cycles after an SH4 IRQ | Fixes DMA/IRQ timing in Looney Tunes |

#### AICA DMA READ

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Sound RAM to main RAM DMA (`SB_ADDIR=1`) refused | Standard |
| **ON (Flycast, default)** | Sound RAM to main RAM DMA performed, as Flycast does | Sound RAM copies work as on hardware |

Not what fixed WSB 2K2's black menu, that was MMIO PAIR FIX (Page 5).

#### AICA CA CLAMP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (real HW, default)** | CA read as the hardware gives it | Standard |
| **ON (LEA-3)** | CA read clamped 3 below the loop end | Game workaround for the Visual Concepts sports games only. Per game |

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

#### VBL FLIP FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Frames go on screen asynchronously, so a pass can flip a black frame | Standard |
| **ON (default)** | Frames go on screen at the vblank; the last render of the vblank wins | Fixes the Toy Racer 2 black screen |

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

#### DISC BULK READ

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (per sector, default)** | One fseek+fread per sector | Standard |
| **ON (bulk, GDI/ISO)** | One fseek+fread per RUN of sectors instead | Meant to cut loading-screen hitches |

Unused for now.

#### GDROM DELAY

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (instant, default)** | GD-ROM data arrives immediately | Fastest, can freeze a frame on a big stage load |
| **1800 KB/S (real)** | Paces reads like a real Dreamcast GD-ROM (1x) | Spreads the load over several frames instead of freezing one |

Can read GDROM exactly like a real Dreamcast at 1x speed. Unused for now.

#### FLASH BROADCAST

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (flash value)** | The BIOS flash decides 50/60 Hz | Standard |
| **DISC (IP.BIN, default)** | 60 Hz for JP/US discs, 50 Hz for EU-only discs | Fixes PAL root / 50 Hz discs |
| **NTSC (always)** | Always 60 Hz | Use if a disc is stuck on 50 Hz |

#### GDROM SPINUP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (no disc)** | Drive reports NODISC straight after reset | Standard |
| **ON (busy 3 s, default)** | No disc: drive is BUSY for 3 s after reset, then NODISC | The BIOS swirl intro plays |

#### RTC FROM HOST

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (saved value, default)** | Dreamcast clock is the value saved at last exit | Standard |
| **ON (Wii clock)** | Dreamcast clock is set from the Wii clock at every boot | Correct date/time in games that read it |

#### IDLE KAMUI

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Vsync wait loop runs its full timeslice | Standard |
| **ON (1 turn/slice, default)** | The vsync wait loop ends its timeslice after 1 turn (exact code match) | Gives time back to the rest of the frame. Ikaruga, Rez, CvS2 |

#### IDLE HOOK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Sega's `while (busy) hook()` wait runs its full timeslice | Standard |
| **ON (1 turn/slice, default)** | The same wait loop ends its timeslice after 1 turn (code-shape match) | Sega Tetris, ChuChu Rocket |

#### IDLE SHINOBI

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Idle task walk keeps its slice debt | Standard |
| **ON (debt til IRQ, default)** | The 15-slice debt is dropped by the next interrupt (exact code match) | Garou, Border Down |

#### IDLE X3 BACK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (x30, legacy, default)** | Idle-loop rule x30 | Standard |
| **ON (x3 backward)** | Idle-loop rule x3 (`bf` / backward `bt`) | About x1.5 SH4 host time. Per game only |

#### MMIO PAIR FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | `fmov.d` pairs to OC-RAM / 32-bit VRAM / sound RAM / registers in the wrong word order | Standard |
| **ON (FRn at addr, default)** | `FRn` at the address, like RAM | Fixes the WSB 2K2 black menu (Dolphin-confirmed) |

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

#### FLASH CHIP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (plain RAM)** | The BIOS flash is plain RAM | Standard |
| **ON (AMD commands, default)** | The flash obeys AMD program/erase commands (Flycast's DCFlashChip); the factory partition is protected | BIOS flash program/erase behaves like the hardware |

#### FLASH SAVE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (never saved)** | Flash changes are lost at exit | Standard |
| **ON (at exit, default)** | Flash is written back to `data/dc_flash_wb.bin` at exit if the session changed it (write-to-tmp then rename) | Keeps BIOS settings between runs. Needs FLASH CHIP |

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

Puyo Puyo 4 ONLY. A menu-row twin of the cfg-only `layer_back_tex` key (see [game_presets.cfg](README.md#game_presetscfg)) so the fix works without touching the SD card at all - e.g. from inside Dolphin.

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

#### Debug Skip Tex

Default OFF, diagnostic only. Hides every polygon using one texture (VRAM address, hex or decimal, from a `[SCN]` census `addr=` field) - it REMOVES geometry, never fixes anything. Answers whether an element is missing because something draws OVER it, or because it's drawn wrong itself - this is how the Hokuto no Ken layering bug was pinned down.

The other debug/log rows (Dbg FB2D, Debug Message, Debug Loop, Debug GDROM, and the JIT diagnostics) moved to **Page 8 : LOGS**, below.

### Page 7 : JIT/DYNAREC

Perf and diagnostic presets for the SH4 dynarec and its interpreter-fallback path. Several read at COMPILE time, not per-frame - set them before launching, not mid-game.

#### JIT SBP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | No stale/self-modified block guard | Fastest, riskiest |
| **KNOWN (default)** | Guards known self-modifying regions (DOA2LE, Shenmue 1/2, ...) plus the boot-entry cache flush | Balanced |
| **ALL RAM (slow)** | Guards all RAM | Safest, slowest, diagnosis only |

#### JIT ICI

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **CLEAR ALL (default)** | A guest I-cache invalidate (`CCR.ICI`) clears the whole JIT cache, as before | Standard, can cost a big stall recompiling everything right after |
| **IGNORE (needs SBP)** | Ignores the invalidate instead, relying on JIT SBP to catch anything that actually got self-modified | Needs JIT SBP on |
| **IGNORE + SBP=ALL** | Same, and forces JIT SBP to guard all RAM | Safest of the two IGNORE modes |

Fixes some freezes/stuttering - e.g. kills a ~24ms stage-load frame (1648 blocks recompiled after one `CCR.ICI`) in Street Fighter Alpha 3. Costs ~25-30% more sh4 time, so only worth it on a game with budget to spare - don't set this on a game already near 100% SPEED.

#### JIT LINK GUARD

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (log only)** | Logs `[LINK]` but still links a static exit that a cache clear overwrote | Standard |
| **ON (skip stale, default)** | Never links a static exit that a cache clear overwrote | Stops patching a dead site. Read at compile time |

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

#### JIT NEW OPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | 9 opcodes (`stc SR`, `stc.l SR`, `ldc.l SR`, `ldc SR`, `lds FPSCR`, `lds.l FPSCR`, `rotcl`, `rotcr`, `tas.b`) always fell back to the interpreter even though dynarec code for them already existed - it just wasn't reachable from the opcode table | Full interpreter call-out + register spill every time |
| **ON (9 ops jitted, default)** | Wires all 9 into the dispatch table | `stc SR` alone measured up to 96k/s in ChuChu Rocket scenes; all 9 now skip JIT IFB FLUSH's allow-list entirely |

Changes SH4 codegen - read at compile time. Run JIT IFB PROBE first to see if a game issues enough of these.

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

#### JIT FIPR DP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (single)** | `fipr`/`ftrv` round after every multiply-add | Standard |
| **ON (double sum, default)** | `fipr`/`ftrv` sum in double and round once, like the SH4 | Accuracy preset, awaiting an A/B on Wii |

### Page 8 : LOGS

Every row here is diagnostic, default OFF, and writes to `/ndclog.txt` on the card - costing a card write every frame while on, so turn it back off once a log has answered its question.

#### Debug logs

| Row | What it logs |
|-----|--------------|
| **Dbg FB2D Log** | Whether a game ever takes the bit-24 render path 2D FRAMEBUFFER (Page 1) would act on - answers "is it worth trying on this game?" before flipping it |
| **Debug Message** | Renderer trace (`[PATH]`, `[FB]`, `[RTT]`...) |
| **Debug Loop** | Per-loop CPU/GD-ROM/IO trace. VERY slow |
| **Debug GDROM** | GD-ROM / CDDA SPI command trace |

#### JIT IFB PROBE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (logs [IFB])** | Counts interpreter fallbacks per opcode, writes an `[IFB]` breakdown to `/ndclog.txt` once a second: total/s, how much JIT IFB FLUSH could narrow, spill memory-op rate with/without it | Costs 4 instructions per fallback site - turn off for timing runs |

Read at compile time - set before launching.

#### JIT HOTBLOCKS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (logs [HOT])** | Counts executions per compiled SH4 block, writes the hottest ones to `/ndclog.txt` once a second as `[HOT]` lines with codegen density (PPC bytes/SH4-opcode), plus a full SH4+PPC dump of the top block twice per session | Costs 4 instructions per block entry - turn off for timing runs. Covers everything JIT IFB PROBE doesn't (i.e. non-fallback codegen) |

Read at compile time - set before launching.

#### JIT CCALL CENSUS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON** | Counts every C function the dynarec still calls out to, prints a `[CC]` block once a second (per-second AND per-frame rates: scheduler tick, interrupt, `bm_GetCode`, `do_sqw`/TA, `UpdateSR`, `UpdateFPSCR`, fsqrt, fsrra, ReadMem/WriteMem misses, mac saturation) | Far cheaper than JIT HOTBLOCKS since counters live inside the callees, but still re-measure speed with it off before trusting a comparison |

The census that found `writemem` at 1.1M calls/s in Crazy Taxi (57% of all JIT call-outs), which led to BLOCKCOPY.

#### TA PROFILE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (stats ta:)** | Measures the TA decoder's own share of wall time and folds it into the stats line as `ta:%` | Costs ~1% |

#### STRIP DEDUP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | Disable | Standard |
| **CENSUS (log only)** | Per-strip/per-texture render census to `/ndclog.txt`, nothing skipped | Diagnostic only |
| **SKIP REPEATED** | Also skips strips it detects as exact repeats | Closed/not worth shipping - measured ~0.17% gain, keep on CENSUS or OFF |

#### AUDIO STATS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (1 line/sec)** | Logs underruns, drops, ring depth, pitch trim once a second | Can help debugging audio |

#### FRAME PROF

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No profiling | Standard |
| **ON (logs [FPROF])** | Per-frame p50/p95/p99 timing breakdown | Helps chasing down freezes/stutters |
| **ON + TAIL FRAMES** | Same, plus logs the individual tail (p95/p99) frames | For digging into what specifically stutters |

#### DISC CENSUS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No counting | Standard |
| **ON (logs [DISC])** | Logs sectors/call and DISC BULK READ's hit rate | For GDROM |

#### SCENE LOG

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (default)** | No dump | Standard |
| **ON (logs [SCN])** | Per-pass dump of what is drawn ([SCN] / [SIG]) to `/ndclog.txt` | Diagnostic for blank or black screens only |

### Page 9 : FIXES

The `*_FIX` presets: each one corrects a specific hardware or emulation deviation and ships ON unless noted. SEAM FIX, JOJO FIX and EXIT FIX stay on Pages 2, 2 and 6.

#### TEX CLAMP FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | A cached texture keeps the FIRST polygon's ClampU/V+FlipU/V wrap mode forever, since those are per-POLYGON TSP bits baked into `GXTexObj` only on decode | A tiled floor inheriting `GX_CLAMP` smears its edge texels to infinity - "streaked"/"infinite X or Y" symptom |
| **ON** | Every texture bind re-applies the binding polygon's own wrap mode via `GX_InitTexObjWrapMode` before `GX_LoadTexObj` | Wii-CONFIRMED: fixes streaked textures in Crazy Taxi (VERY_FAST+ trees), Deadly Skies (FAST/VERY_FAST+ floors), Jet Set Radio (VERY_FAST+ intro). Does NOT fix Shadow Man's sky (QUALITY cache re-decodes every frame, never susceptible) |

#### YUV TWIDDLE FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Twiddled YUV422 textures decode with luma/chroma swapped | Green<->magenta duotone with 1-column striping on affected art |
| **ON (default)** | Correct luma/chroma extraction for twiddled YUV422 | Fixes static YUV artwork such as Virtua Fighter 3tb's "FIRST MATCH" loading screen |

Static YUV artwork only, NOT movie playback - real (planar) FMV is unaffected either way.

#### TLUT FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (ring, legacy)** | CI4/CI8 palettes come from a 16-slot ring that overruns at full speed | Standard, colour flicker in Street Fighter III |
| **ON (per-render copy, default)** | Palettes copied per render, not from the ring | Fixes the CI4/CI8 colour flicker |

#### DECAL FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (modulate, legacy)** | Decal polygons are modulated by the vertex colour | Black world textures in Soldier of Fortune |
| **ON (texture only, default)** | Decal polygons show the texture as-is, vertex colour ignored, like the PVR | Fixes the Soldier of Fortune black level |

Not the same as DECAL ALPHA (Page 2), which is alpha on decals.

#### TA ARENA FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Unbounded TA buffers; a mid-strip render loses the strip | Standard |
| **ON (bounded + carry, default)** | Bounded TA buffers, and the strip is carried across a mid-strip render | Fixes issue #16: mad vertices then a FIFO error |

#### TA CTX FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | A render of the finished list can draw the list already being built | Standard |
| **ON (split at list init)** | A render of the finished list skips the next list already being built | Fixes issue #16 flicker, and the geometry flicker in Magforce Racing |

#### VBL FB FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | A video-RAM framebuffer can be shown over a 3D frame in the same vblank | Flicker |
| **ON (default)** | No video-RAM framebuffer is shown over a 3D frame in the same vblank | Fixes Toy Racer 3D's old-image flicker. Pairs with VBL FLIP FIX (Page 5) |

#### CDDA PAUSE FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (to FAD 150)** | Pausing CD music jumps back to the disc start | Legacy |
| **ON (keep position, default)** | Pausing CD music keeps its place, as the real Dreamcast does | Music resumes where it paused |

#### CDDA STATE FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Old CD-DA state handling | Standard |
| **ON (Flycast, default)** | Seek pauses, resume only from a pause, a data read stops audio, end and "ended" states as Flycast | Correct play/pause/end/read behaviour |

#### ARM7 RESET FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (system mode, legacy)** | Sound CPU reset leaves it in system mode | Standard |
| **ON (SVC + latch, default)** | Reset enters SVC mode and clears the FIQ latch, as the real ARM7DI | Sound CPU reset like the hardware. Part of the Looney Tunes group |

#### CDI HDR FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | V2/V3 `.cdi` images fail to load | Standard |
| **ON (cdirip rule, default)** | V2/V3 `.cdi` images find their header, as cdirip does | Those images load |

#### INTERP FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Interpreter `div1` is wrong about 3 steps in 4, and `addv`/`subv`/`mul.l`/`mac` saturation is off | Standard |
| **ON (manual ops, default)** | `div1`/`addv`/`subv`/`mul.l`/`mac` as the SH4 manual describes | Correct maths in the interpreter fallback path |

FPSCR and `fneg` are deliberately left alone by this preset.

#### GDROM STAT FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Drive position reports are not from the TOC | Standard |
| **ON (FAD + track)** | `REQ_STAT` / `GET_SCD` report the real FAD, track and type from the TOC | Correct CD position and subcode reports |

#### GDROM HANG FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (die / verify)** | Rare drive commands (die/verify) freeze the emulator | Standard |
| **ON (abort / clamp, default)** | Rare drive commands are answered like a real drive | Fixes about 10 hangs |

#### TOC FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | TOC without the ADR 1 per track, lead-out entry or GDI session table | Standard |
| **ON (ADR1 + lead-out, default)** | ADR 1 per track, data lead-out entry, GDI track-2 end from file size, GDI session table | Correct TOC for GDI and CD-based images |

#### SPG FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Field bit toggled wrongly; HBlank IRQ fixed for one mode; VBLANK_INT not reset | Standard |
| **ON (Flycast, default)** | Field bit once per frame, HBlank IRQ per `SPG_HBLANK_INT` mode, VBLANK_INT reset value | Correct video timing |

#### DMA FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Disable | Standard |
| **ON (default)** | ch2/PVR/Sort/AICA-G2 DMA correctness fixes ported from the verified-working NullDC PSP port | Fixes related to loading CDI/GDI file |

#### MMUCR FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | MMU control register not at its real bits | Standard |
| **BITS (AT/TI/URC, default)** | MMUCR at its real bits | Rez, Ikaruga, Border Down boot |
| **FULL (PS3 tested)** | The full PS3-tested set | Use if BITS is not enough |

#### JIT DIV32 FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (32/32, legacy)** | Matched `rotcl`/`div1` divide done 32/32, wrong quotients | Standard |
| **ON (64/32 exact, default)** | Matched divide done as 64/32 one's complement | Exact quotients |

### Game Specific Presets

LAYER SORT (Hokuto No Ken, Street Fighter III) helps determine what should be front and back trough looking at texture format/properties. HOKUTO HACK adds to it a RAM address check for a few Hokuto no Ken textures that those properties cannot separate — Hokuto no Ken only, and it needs LAYER SORT on.

JOJO FIX (Jojo's Bizarre adventure) has to be used with CI4_FAST to reduce FPS drop in battle. May use the same technique in other games

Vertex color is a special method on Dreamcast to color stuff. Notable example in Crazy Taxi : The arrow and dollar sign are Vertex Colored. You will notice the cars too : They will add to the current texture so the same car with the same texture will appear red, blue, green, etc...

Same for Jet Set Radio, will add color to the logo.

LEGACY DEPTH (Buggy Heat) reproduces one specific old commit's depth pipeline verbatim, for a game that only ever rendered correctly at that point in history.

PUYO HACK (Puyo Puyo 4 / DA!) and its cfg-only twin `layer_back_tex`/`layer_front_tex` (see [game_presets.cfg](README.md#game_presetscfg)) fix 2D games that submit a backdrop or overlay plate at the same depth as the sprites it should stay behind/in front of - painter order alone can't tell them apart.

`wince=yes` in game_presets.cfg is not a rendering preset - it just shows a warning after the options menu ("this is a WinCE game, not supported yet - A: launch anyway, B: back to file list") instead of silently failing to boot, for the handful of Dreamcast games built on Sega's Windows CE devkit instead of plain Katana.

See Compatiblity guide for hints depending of the games

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
