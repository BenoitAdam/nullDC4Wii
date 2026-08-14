[![Discord](https://img.shields.io/discord/286429969104764928?label=NullDC4wii&logo=discord&logoColor=FFFFFF)](https://discord.gg/fZzxdeXrZ)

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
- Custom layout : a toggle to make the right analog stick with supported controllers act as buttons X (Left) - Y (Up) - A (Down) - B (Right). This is useful for twin stick shooters that use the diamond button layout for gameplay (like Xeno Crisis).
- Add a winCE preset to prevent that game isn't supported
- check fps_boost preset matching from cfg, seems not working

### Developer (Normal)

- 4/3 support (implemented, need fix on some games like Shenmue)
- Support for CHD/ELF game file
- Return to file list (instead return to homebrew menu)
- Synchronise Dreamcast clock to Wii's clock

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


### VMU (Memory card)

It seems to be supported, but 1rst you'll need to format the VMU in the bios

Files appears at root of /data/ :  
- vmu_save_A1.bin
- vmu_save_A2.bin

## Compatibility

https://wiibrew.org/wiki/NullDC4Wii/Compatibility

## Presets

Presets are grouped in the in-emulator menu across 6 pages. The order below follows that exact same order (Page 1 to Page 6).

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
| **OFF (uncapped)** | Emulator can run above 100% speed | Uncapped |
| **ON (cap 100%)** | Stops speed exceeding 100% | Capped |

#### SHOW FPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | No overlay | Nothing displayed |
| **ON** | Displays gameplay FPS and speed overlay | Overlay shown |

#### 🖼️ Graphics Preset

| Mode | Settings | Best platform | 
|------|----------| ------------------------- | 
| **LOW** | `GX_NEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **NORMAL (default)** | `GX_LINEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **HIGH** | `GX_LINEAR` · `lod_bias -0.5f` · `GX_ENABLE` · Anisotropic x2 | Wii U |
| **EXTRA** | `GX_LINEAR` · `lod_bias -0.75f` · `GX_ENABLE` · Anisotropic x4 | Wii U |

The visual difference is limited for NORMAL/HIGH/EXTRA

<img width="1844" height="1456" alt="levels" src="https://github.com/user-attachments/assets/79d5271d-0689-43d4-92c0-66674013ddce" />

- Use LOW for 240p games/modes
- Use NORMAL for other games

#### TEXTURE CACHE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **VERY_FAST** | skmp original algorythm (magic numbers). Buggy in most games  | Max FPS |
| **FAST** | Best performance/accuracy in most case  | Almost Max FPS |
| **NORMAL (default)** | Display mostly correctly | Good FPS |
| **QUALITY (SLOW)** | Best accuracy. Display correctly | Mid FPS |

Can have huge FPS impact, try to have the lowest parameter.

#### 4BPP MODE / 8BPP MODE

| Mode (4BPP/8BPP) | Settings | Rendering | 
|------|----------| ------------------------- | 
| **I4_STUB/I8_STUB** | Dummy algorythm  | Some element doesn't display at all, for max FPS |
| **OPTIMIZED** | Served as test, in the end CI4/CI(FAST) is better | Very good FPS |
| **CI4 (FAST)/CI8 (FAST)** | Best performance/quality | Very good FPS |
| **CI4 (NORMAL)/CI8 (NORMAL)** | Advanced algorythm for CI4/CI8 | Mid FPS |
| **RGB565 (ACCURATE)** | Most advanced algorythm | Can have massive FPS dropdown (1 FPS) on some games |

#### FRAMESKIPPING

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **0 (default)** | No frame skipped | Every frame drawn |
| **1** | Skip 1 frame | Faster, less smooth |
| **2** | Skip 2 frames | Faster still, less smooth |
| **AUTO** | Skips frames automatically depending on load | Adaptive |

Still on testing, doesn't have the expected effect for now  

#### 2D FRAMEBUFFER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **NO (default)** | Disable | Standard rendering |
| **YES** | Enable 2D framebuffer | Try for 2D games |

Still on testing  

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
| **OFF (faster)** | Legacy: PT polys drawn last in TR blend state | Faster, less accurate |
| **ON (correct)** | OP → PT → TR order + PT_ALPHA_REF alpha test | Correct PT list alpha test |

Needed in lot of games

#### TRANS_SORT

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (default)** | Disable | not Accurate (faster) |
| **ON** | can display stuff | Accurate |

Can resolve flickering in some games
Needed in lot of games

#### RENDER TO TEX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster)** | RTT frames dropped (legacy) | Faster, mirrors/TV screens missing |
| **ON (correct)** | EFB copied back into VRAM | Correct mirrors/TV screens |

Needed in some games

#### SPLIT SCREEN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster)** | Every render pass presented fullscreen (legacy) | Faster |
| **ON (correct)** | Partial-clip passes scissored, presented once per vblank | Correct 2P viewports, e.g. Daytona USA |

Needed for 2 players splitscreen or any 2 camera angle games.

### Page 2 : Graphics

#### FMV FORMAT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **CMPR (DXT1)** | Compressed format | Use if some movie displays white |
| **RGBA8** | Uncompressed, full quality | Slower |
| **RGB565 (default, faster)** | Uncompressed, no alpha | Faster |

#### Vertex Color

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (grey scale)** | Grey Scale | Grey scale (a tiny bit faster) |
| **ON (default)** | Intensity color | Accurate |

Color some pixel (Used in Jet Set Radio Future and Crazy Taxi 1/2)

#### DECAL_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **OFF (faster, default)** | no decal alpha  | Not accurate |
| **ON (correct)** | Decal alpha implemented | Accurate, fixes Crazy Taxi's cars |

See more : https://github.com/BenoitAdam/nullDC4Wii/issues/68

#### SEAM FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (a bit faster)** | Disable | Thin black seam lines between 2D tiles/sprites remain |
| **ON (default)** | Half-texel UV inset | Fixes black lines between 2D tiles |

Use this or LOW to fix seam lines. See https://github.com/BenoitAdam/nullDC4Wii/issues/18

#### BG POLYGON

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (faster, default)** | v0 color used for EFB clear only, no background quad drawn | Faster |
| **ON (correct)** | Barycentric-extrapolated background quad drawn | Correct bg gradient/texture, e.g. Who Wants to Be a Millionaire |

#### RGB565 ALPHA

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (fmt0 only, default)** | Only fmt0 (ARGB1555) forced opaque | Correct for POD 2 |
| **ON (fmt0+fmt1)** | Force opaque for fmt0(ARGB1555)+fmt1(RGB565) | Turn off for POD 2 |

May disapear in a future

#### HOKUTO HACK

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard sort |
| **ON (TR tier sort)** | Layer-tiered translucent sort | Specific fix for Hokuto no Ken |

Helps determine which what should be front and back trough looking at texture format/properties. For some textures it even check RAM address. Only work for stage 1/2 at the moment

#### JOJO FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | Disable | Pre-fix behavior |
| **ON (default)** | Enable fix | For JoJo's Bizarre Adventure |

Has to be used with CI4_FAST/CI8_FAST to reduce massive FPS drop in battle. May use the same technique in other games.

### Page 3 : Depth & Width

#### DEPTH_CLIP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | XF Z-clipping on, no near margin | 2D/menus can be invisible on real Wii |
| **NEAR MARGIN (Wii, default)** | Pads vtx_min_Z 0.1% so the nearest 2D layer can't land exactly on the near clip plane | Recommended for Wii |
| **NO CLIP (Dolphin)** | Matches Dolphin: out-of-range depth clamps instead of the poly vanishing | Matches Dolphin behavior |

It's basically like FIXED_DEPTH, leave it to NEAR MARGIN

#### FIXED_DEPTH

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | Disable | Good |
| **WIDE** | can help display some stuff - mostly for debug | Bad |
| **TIGHT** | can help display some stuff | Good |

FIXED_DEPTH can help flickering and Z-Fighting

#### HUD_PASS

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | -  | Not active |
| **Overlay** | Help hud to display when FIXED_DEPTH is on TIGHT | accurate |
| **Protect** | Help hud to display when FIXED_DEPTH is on TIGHT | Perfect |
Mostly needed if Fixed Depth is set to tight


#### X SCALER

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | PVR SCALER_CTL.hscale support disabled | Standard |
| **ON (default)** | PVR SCALER_CTL.hscale support | ON for Omicron / Wacky Races (render 1280 wide, scaler halves 2:1) |

For Nomad Soul and Wacky Racer. Maybe other games

#### CANVAS WIDTH

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (640, legacy, default)** | Legacy 640 canvas | Standard |
| **Custom value** | Forces canvas width in 240p modes | e.g. SF3 Double Impact = 384 (the CPS3 arcade width) |

See compatiblity wiki for more info

#### PPZ_WRITE : PER POLYGON Z WRITE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | No Per Polygon Z Write  | More compatible |
| **YES (default)** | Per Polygon Z Write | More accurate |

Try putting NO if you experience troubles, with HUD for example.

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

### Page 5 : Core / Special Hack

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

#### JIT SBP

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF** | No stale/self-modified block guard | Fastest, riskiest |
| **KNOWN (default)** | Guards known self-modifying regions | Balanced |
| **ALL RAM (slow)** | Guards all RAM | Safest, slowest |

Generally work, no difference

#### FASTMEM

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Standard PPC-MMU memory access | Slower |
| **ON (faster, default)** | Branchless JIT MMU-mapped memory access | Faster |

Crash observed in Re-Volt when launching a race. Only game that does that for now.

#### JIT BCACHE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Legacy dynamic-branch dispatch | Standard |
| **ON (flat)** | Flat, 1-cacheline dynamic jump dispatch | Faster dispatch |

L1/L2 cache related. Can help heavy scene with Fast cache like in Shenmue intro maybe

#### FPU PIN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | fr0-15 not pinned | Standard |
| **ON (experimental)** | Pins fr0-15 to real PPC FPU registers f14..f29 | Experimental, Floating Point Unit related |

Can help heavy scene with Fast cache like in Shenmue intro maybe

#### JIT ALIGN

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | No block alignment | Standard |
| **ON (32B lines)** | Pads every SH4-dynarec block entry to a 32-byte L1 cache line | Better cache hygiene, L1/L2 cache related |

L1/L2 cache related. Can help heavy scene with Fast cache like in Shenmue intro maybe

### Page 6 : Experimental/Debug

These doesn't make any change, or usually worse.

#### MIPMAPS

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (fastest, default)** | No mip chain | Fastest, more shimmer far away |
| **FAST** | Generated GX mip chain + nearest-mip bilinear | Less shimmer far away |
| **TRILINEAR (slow)** | Best quality | Best quality, halves texture fill rate (e.g. -40% in Test Drive 6) |

#### OFFSET COLOR

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Offset/specular color dropped | Standard |
| **ON (correct)** | PIX = base*tex + offset via 2nd TEV stage | Correct specular highlights |

#### ISP_DEPTH_FUNC

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard |
| **ON (opaque/PT)** | Per-poly depth test on opaque/PT lists | Experimental |
| **ON (all lists)** | Per-poly depth test on all lists | Experimental |

#### ISP_CULL

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard |
| **ON** | Per-poly backface cull | Experimental |
| **ON (swap winding)** | Per-poly backface cull, two cullable windings swapped | Experimental |

#### AUTOSORT

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy, default)** | Disable | Standard |
| **N layers (slow)** | Real per-pixel PVR autosort via GX depth peeling, N = max translucent depth layers per pixel | Very GPU-heavy (~2 extra TR walks + 2 EFB Z copies per layer) — per-game only |

#### DMA FIX

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (legacy)** | Disable | Standard |
| **ON (default)** | ch2/PVR/Sort/AICA-G2 DMA correctness fixes | Fixes related to loading CDI/GDI file |

#### SCHED (ORDER)

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **OFF (cascade, default)** | Legacy cascade scheduling | Standard |
| **ON (deadline)** | Unified cycle-deadline scheduler | Hardware-order DMA/IRQ completions, related to loading CDI/GDI file (experimental) |

#### SH4 CORE

| Mode | Settings | Rendering |
|------|----------| ------------------------- |
| **INTERPRETER** | Interpreted SH4 core | Slow, for debugging only |
| **DYNAREC (default)** | JIT recompiler | Fast |

### Game Specific Presets

HOKUTO HACK (Hokuto No Ken) help determine which what should be front and back trough looking at texture format/properties. For some textures it even check RAM address.

JOJO FIX (Jojo's Bizarre adventure) has to be used with CI4_FAST to reduce FPS drop in battle. May use the same technique in other games

Vertex color is a special method on Dreamcast to color stuff. Notable example in Crazy Taxi : The arrow and dollar sign are Vertex Colored. You will notice the cars too : They will add to the current texture so the same car with the same texture will appear red, blue, green, etc...

Same for Jet Set Radio, will add color to the logo.



See Compatiblity guide for hints depending of the games

## game_presets.cfg

game_presets is a file that reads the file name and directly apply matching presets.

game_presets.cfg needs to be in sd:/discs/

With recent version, multiple file names are suppported :

```
[crazytaxi2][crazy taxi 2] ; more specific first
depth_clip=1
tex_cache=normal
vertex_color_fix=off

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
- Advanced > Debug > Texture Format Overlay
- VSync evenutally

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

Yes...in dolphin !

Some games are running 100/120% in dolphin, but on real hardware, there is around around 20% of drop on heavy scenes.

We are still trying to improve performance, altough we already done a lot of amazing stuff :  
- Improved Dynarec
- Fastmem
- ARM7 CACHE

Another good strategy would be to really have a per-game specific emulator. That would take a long time, so for now we have presets & auto-presets implementation trough game_presets.cfg

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

Yes : https://discord.gg/fZzxdeXrZ

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
- Probably Reicast Team also
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
