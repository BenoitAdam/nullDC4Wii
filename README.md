# NullDC4Wii - Dreamcast Emulator for Wii

a fork from https://github.com/skmp/nullDCe

## TODO (Maybe you can help !)

### Simple

- Test current state with every game and report compatibility (see "compatibility" below)
- Create presets for games
- Test 2 player mode on wiimote & gamecube also, please report
- Help me finding regression (NOT bugs or glitch, only regression for now please)
- Comment / Guides / Documentation (WiiBrew Wiki)
- Test and report Fishing Rod/USB Keyboard/Lightgun/Maracas support


### Developer (Easy)

- Controller correct layout, for pro pad and for gamecube pad
- User custom Preset
- Player 3/4 Gamecube/Wiimote
- Fishing Rod/USB Keyboard/Lightgun/Maracas support (probably unsupported now)
- Put external config file for controllers (controls.cfg)
- User custom presets file

### Developer (Normal)

- Coding routine adjustement for accuracy (FAST/BALANCED/ACCURATE)
- 4/3 support (implemented, need fix on some games like Shenmue)
- Wii U gamepad support like WiiStation ? ( https://github.com/FIX94/libwiidrc )
- Wii U Gamepad, Dualshock 3 and Wii U Pro Controller support ? ( https://github.com/BenoitAdam94/nullDC4Wii/issues/15 )
- Support for CHD/ELF game file

### Developer (Hard)

- Improve gxRend.cpp = main file about specific rendering for Wii
- Splitting gxRend.cpp in multiple files ? (beware this is more tricky than it look)
- Fix alpha/transparent stuff
- Table convertion between SH4 Opcodes of SH4 and the WiiPPC ?
- Use LLVM to port code for PowerPC ? (skmp says its not a good idea in this case)
- Full Dynarec implementation (AI seems to know about this)
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

#### Wiimote :

| Dreamcast | Wiimote |
| --------- | ------- |
| A | A |
| B | B |
| Y | 1 |
| X | 2 |
| START | Home |
| D-PAD | D-PAD |
| STICK | Nunchuck Stick |
| L | - (and Nunchuck Z) |
| R | + |

To Exit : - and +  

#### Wiimote (Chuchu Rocket!)

Not implemented yes, but soon ! :)

| Dreamcast | Wiimote |
| --------- | ------- |
| A | down & A |
| B | right & B |
| Y | up |
| X | left |
| START | Home|
| D-PAD | - no implementation - |
| STICK | Nunchuck Stick |
| L | - (and Nunchuck Z) |
| R | + |

To Exit : - and +  

#### Gamecube controller :

| Dreamcast | Gamecube |
| --------- | -------- |
| A | A |
| B | B |
| Y | Y |
| X | X |
| START | START |
| D-PAD | D-PAD |
| STICK | STICK |
| L | L |
| R | R |

To exit : R + L + Z  

#### Gamecube controller (Chuchu Rocket !) :

Not implemented -
Need to swap B and X

| Dreamcast | Gamecube |
| --------- | -------- |
| A | A |
| B | X |
| Y | Y |
| X | B |
| START | START |
| D-PAD | D-PAD |
| STICK | STICK |
| L | L |
| R | R |

To exit : R + L + Z  


### VMU (Memory card)

It seems to be supported, but 1rst you'll need to format the VMU in the bios

Files appears at root of /data/ :  
- vmu_save_A1.bin
- vmu_save_A2.bin

## Status (10/06/2026)

launch on dolphin an real Wii with FPS between 8 and 50 Fps  
game selector implemented  
2 players controler implemented  
Games tested : See compatibility

Wii Dynarec has been improved & completed by AI. Compile but doesn't run (Black Screen). See /archive folder

https://www.youtube.com/watch?v=Ug8V2XXO52Q

## Compatibility

https://wiibrew.org/wiki/NullDC4Wii/Compatibility

## Presets

### 🧮 Calculation Accuracy Preset

| Mode | Description |
|------|------------|
| **FAST** | Maximum FPS (higher frame rate), less loading times |
| **BALANCED** | Good balance between speed and accuracy |
| **ACCURATE (default)** | Closest behavior to original hardware |

If you experience Freeze in some heavy games like Shenmue, put FAST or BALANCED. FAST may be the default setting in future versions

If you experience various bugs (example that may happens : weird AI controled NPC, weird timing) put ACCURATE

---

### 🖼️ Graphics Preset

| Mode | Settings | Best platform | 
|------|----------| ------------------------- | 
| **LOW** | `GX_NEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **NORMAL (default)** | `GX_LINEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **HIGH** | `GX_LINEAR` · `lod_bias -0.5f` · `GX_ENABLE` · Anisotropic x2 | Wii U |
| **EXTRA** | `GX_LINEAR` · `lod_bias -1.0f` *(may need to adjust to -0.75)* · `GX_ENABLE` · Anisotropic x4 | Wii U |

While the emulator is still in alpha, the visual difference is limited for now.

<img width="1844" height="1456" alt="levels" src="https://github.com/user-attachments/assets/79d5271d-0689-43d4-92c0-66674013ddce" />

- Use LOW for 2D and heavy 3D games
- Use NORMAL for other games

| Mode (4BPP/8BPP) | Settings | Rendering | 
|------|----------| ------------------------- | 
| **I4_STUB/I8_STUB** | Dummy algorythm  | Some element doesn't display at all, for max FPS |
| **OPTIMIZED (defaut)** | Best performance/quality | Very good FPS |
| **CI4 (FAST)/CI8 (FAST)** | Basic algorythm Display mostly correctly | Very good FPS |
| **CI4 (NORMAL)/CI8 (NORMAL)** | Advanced algorythm for CI4/CI8 | Mid FPS |
| **RGB565** | Most advanced algorythm | Can have massive FPS dropdown (1 FPS) on some games |

####  Cache setting

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **CACHE_VERY_FAST** | skmp original algorythm (magic numbers). Very fast but buggy  | Max FPS |
| **CACHE_FAST** | Works better  | Almost Max FPS |
| **CACHE_NORMAL (default)** | Best performance/accuracy. Display mostly correctly | Mid FPS |
| **CACHE_QUALITY** | Best accuracy. Display correctly | Mid FPS |
| **CACHE_EXTRA** | Redraw every frame. Accurate (Only for dev & Debug) | Low FPS |


#### DECAL_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | no decal alpha  | Not accurate |
| **YES** | Decal alpha implemented | Accurate |

See more : https://github.com/BenoitAdam/nullDC4Wii/issues/68

#### ADVANCED_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | basic alpha threathment  | Not accurate |
| **YES (default)** | additionnal alpha threatment | Near perfect |


#### > BLEND_MODE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | Disable | Not accurate |
| **YES (default)** | Activate BLEND_MODE | Accurate |

If flickerings, try turning off

note : ADVANCED ALPHA needs to be on for BLEND_MODE

#### >> FPS_BOOST

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | Disable | Accurate |
| **YES** | Boost FPS to the cost of wrong Alpha/Transparency | Not Accurate |

note : ADVANCED ALPHA and BLEND_MODE needs to be on for FPS_BOOST

#### TRANS_SORT

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | Disable | not Accurate (faster) |
| **YES** | can display stuff | Accurate |

Can resolve flickering in some games

#### ASYNC_RENDER

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | Disable | 0 frame latency |
| **YES** | Some CPU stuff are drawn by GPU | Faster, to the cost of 1 frame |

ASYNC_RENDER is generally faster, may be defaut yes in future version. Can resolve flickering

#### FIXED_DEPTH

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO (default)** | Disable | Good |
| **WIDE** | can help display some stuff - mostly for debug | Bad |
| **TIGHT** | can help display some stuff | Good |

FIXED_DEPTH can help flickering and Z-Fighting

#### DEPTH_CLIP

It's basically like FIXED_DEPTH, leave it to NEAR MARGIN

#### PPZ_WRITE : PER POLYGON Z WRITE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | No Per Polygon Z Write  | More compatible |
| **YES (default)** | Per Polygon Z Write | More accurate |

Try putting NO if you experience troubles, with HUD for example.

#### Vertex Color

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | Grey Scale | Grey scale (a tiny bit faster) |
| **YES** | Intensity color | Accurate |

Vertex color is a special method on Dreamcast to color stuff. Notable example in Crazy Taxi : The arrow and dollar sign are Vertex Colored. You will notice the cars too : They will add to the current texture so the same car with the same texture will appear red, blue, green, etc...

Same for Jet Set Radio, will add color to the logo.



See Compatiblity guide for hints depending of the games

## game_presets.cfg

game_presets is a file that reads the file name and directly apply matching presets.

game_presets.cfg needs to be in sd:/discs/

With recent version, multiple file names are suppported (up to 8) :

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

## Questions and Answers (FAQ)

### It doesn't work

First, try on SD card with a know game that work (Castlevana, Chuchu Rocket or Sega Tetris are best candidate). For USB, try a FAT32 USB key first before switching to any other device. It works on my 500HDD FAT32 for information

### Does VMU work ?

It's reported working. Step 1 : go to bios. Step 2 : format VMU. Step 3 : exit properly the emulator (+ and -)

### Why this emulator ?

Because why not ? At first it was a POC (Proof Of Concept) but digging more and more, it seems somes games are achievable to 100% speed (in PAL50 mode). So yeah !

### What games does work ?

Please check the compatiblity Wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Does it work on Wii U ?

We have a Wii U fowarder that also use the speed boot of the Wii U (overclock). A native port for Wii U could be done if someone wants to do port it.

### Controls are messy / this doesn't work / My Propad isn't recognize etc...

Working on it, this will need testing considering the amount of controller possibility existing. Any help is welcome.

### Will THAT AMAZING ADVENTURE GAME be considered as "Supported" one day ?

Probably not, this emulator prioritize multiplayer fun games to play friends with. Playing an adventure game like Shenmue or an RPG could be a very frustrating experience that I strongly do not recommand. Use Gamecube/Wii version (if it does exist) or a PC for emulation instead.

### What can I do to help ?

Try different games, parameters and report in the compatibility wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Is 100% speed of the emulator achievable on Wii ?

Yes and no.

Currently we are on 60/70% emulator speed in average. We can't really go further than 80%. Maybe if rewriten in assembly partially or totally this could help. Libogc may limit MMU (this is being investigated at the moment).

The port of KallistiOS for Gamecube/Wii could open new possibility and MMU control, but that would also means reprogramming a lot of stuff.

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

### How is AI involved in the project ?

Since I (BenoitAdam) digged the NullDC code, AI has been heavily used to make improvement to the emulator. The very first state of the emulator (alpha 0.02) is 99% hand written code by SKMP and NullDC contributors at the time. Only some few changes have been made to be able to recompile it and make it run. Various AI are used : MistalAI/ChatGPT/Codex/Claude and Deepseek. Claude is very convenient because of artifact and Claude Code. Gemini helped on some improvements, and Deepseek too.

### I hate AI !

It's ok, you have the right, but without AI this project wouldn't have been resurected. AI for code is really a big help, definitly not the same thing with AI generated images and videos. For information AI for image ask ~10x more power, AI for video ask ~100x more power.

Reduce CO2 emission & grow trees is the plan for the planet.

## Ressources

### Dreamcast Emulators 

NullDC https://code.google.com/archive/p/nulldc/source/default/source  
NullDC (github) https://github.com/skmp/nullDCe  
NullDC for PSP : https://github.com/PSP-Archive/nulldce-psp  
NullDC for Xbox360 https://github.com/gligli/nulldc-360  
Reicast : https://github.com/skmp/reicast-emulator  
Flycast : https://github.com/flyinghead/flycast  
Deecy : https://github.com/Senryoku/Deecy

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
- Flyinghead/Flycast Team  
- AI because this project would have never existed otherwise lol
- Probably Reicast Team also
- Welcome to the IA-age guys and good luck everyone.

All together, let's Cast the Dream.

### Special thanks

- skmp because he's the god
- Senryoku, develloper of Deecy emulator
- OriginalDave developer of Jocasta
- Everyone helping on emuvdev/discord
- Jilou04 for constantly testing on Wii U and reporting
- All testers and all futur testers
- People actually helping me on the wiibrew wiki

### Special no thanks

- To all people not believing in this project
- People constantly critisizing the fact AI is used in this project (this project wouldn't have existed otherwise...).
