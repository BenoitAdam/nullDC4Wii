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
- Fix alpha/transparent stuff (may be costly in term of performances)
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

If you experience Freeze in some heavy games like Shenmue, put FAST or BALANCED. BALANCED may be the default setting in future versions

If you experience various bugs (example that may happens : weird AI controled NPC) put ACCURATE

---

### 🖼️ Graphics Preset

| Mode | Settings | Best platform | 
|------|----------| ------------------------- | 
| **LOW (default from a0.06 to a0.09)** | `GX_NEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **NORMAL (default from a0.10)** | `GX_LINEAR` · `lod_bias 0.0f` · `GX_DISABLE`  | Wii |
| **HIGH** | `GX_LINEAR` · `lod_bias -0.5f` · `GX_ENABLE` · Anisotropic x2 | Wii U |
| **EXTRA** | `GX_LINEAR` · `lod_bias -1.0f` *(may need to adjust to -0.75)* · `GX_ENABLE` · Anisotropic x4 | Wii U |

While the emulator is still in alpha, the visual difference is limited for now.

<img width="1844" height="1456" alt="levels" src="https://github.com/user-attachments/assets/79d5271d-0689-43d4-92c0-66674013ddce" />

- Use LOW for 2D and heavy 3D games
- Use NORMAL for other games

| Mode (4BPP/8BPP) | Settings | Rendering | 
|------|----------| ------------------------- | 
| **I4_STUB/I8_STUB** | Dummy algorythm  | Some element doesn't display at all, for max FPS |
| **I4 (FAST)/I8 (FAST)** | Basic algorythm  | Display in grey Scale (Use for debug) |
| **CI4 (FAST)/CI8 (FAST)** | Basic algorythm | Display mostly correctly (consume a bit of FPS on some games) |
| **CI4 (NORMAL)/CI8 (NORMAL)** | Advanced algorythm for CI4/CI8 | Should display better |
| **RGB565** | Most advanced algorythm | Can have massive FPS dropdown (1 FPS) on some games |

Cache setting (starting alpha 0.21)

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **CACHE_VERY_FAST** | skmp original algorythm (magic numbers). Very fast but buggy  | Max FPS |
| **CACHE_FAST** | Broken for now, generally worse than VERY_FAST and NORMAL  | ??? FPS |
| **CACHE_NORMAL** | Best accuracy. Display mostly correctly | Mid FPS |
| **CACHE_QUALITY** | Best accuracy. Display mostly correctly | Mid FPS |
| **CACHE_EXTRA** | Redraw every frame. Accurate (Only for dev & Debug) | Low FPS |

ADVANCED_ALPHA

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | basic alpha threathment  | Not accurate |
| **YES (default starting alpha 0.30)** | additionnal alpha threatment | Near perfect |

PPZ_WRITE : PER POLYGON Z WRITE

| Mode | Settings | Rendering | 
|------|----------| ------------------------- | 
| **NO** | No Per Polygon Z Write  | More compatible |
| **YES (default)** | Per Polygon Z Write | More accurate |

Try putting NO if you experience troubles, with HUD for example.


See Compatiblity guide for hints depending of the games

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


### Why this emulator ?

Because why not ? At first it was a POC (Proof Of Concept) but digging more and more, it seems somes games are achievable to 100% speed (in PAL50 mode). So yeah !

### What games does work ?

Please check the compatiblity Wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Does it work on Wii U ?

We have a Wii U fowarder that also use the speed boot of the Wii U (overcloack). A native port for Wii U could be done if someone wants to do port it.

### Controls are messy / this doesn't work, etc...

Working on it, this will need testing considering the amount of controller possibility existing.

### Will THAT AMAZING ADVENTURE GAME be supported one day ?

Probably not, this emulator prioritize multiplayer fun games to play friends with. Playing an adventure game like Shenmue or an RPG could be a very frustrating experience that I strongly do not recommand. Use Gamecube/Wii version (if it does exist) or a PC for emulation instead.

### What can I do to help ?

Try different games, parameters and report in the compatibility wiki : https://wiibrew.org/wiki/NullDC4Wii/Compatibility

### Is 100% speed of the emulator achievable ?

I think it could be achievable on some games. PAL50 helps in that case.

Nonetheless, rewriting the emulator in pure assembly could definitly make the emulator way faster, but we don't have time to spend for this. Also, before doing that, it should be 100% functionnal in C.

### Can it read Original games (GD-Rom) ?

The Wii can't read those disc format. Maybe if you change the optical drive and with some code implementation, but that's not worth it.

### Can it read CDI/Utopia disc ?

Maybe it's possible, but we don't have time to focus on this. CDI Files on SD Card/USB are supported since alpha 0.28 anyway.

### Will WinCE games be implemented ?

That an additionnal ressources in CPU and we are limited. That may would make sense for a WiiU Port

### How is AI involved in the project ?

Since I (BenoitAdam) digged the NullDC code, AI has been heavily used to make improvement to the emulator. The very first state of the emulator (alpha 0.02) is 99% hand written code by SKMP and NullDC contributors at the time. Only some few changes have been made to be able to recompile it and make it run. Various AI are used : MistalAI/ChatGPT/Codex/Claude and Deepseek. Claude is very convenient because of artifact. Gemini helped on some improvements, and Deepseek too.

### I hate AI !

It's ok, you have the right, but without AI this project wouldn't have been resurected. AI for code is really a big help, definitly not the same thing with AI generated images and videos. For information AI for image ask ~10x more power, AI for video ask ~100x more power.

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

## Credits

- skmp (original NullDC creator)
- NullDC contributors
- Joseph Jordan - libiso  
- Xale00 (also know as Benoit Adam) - 2026 recompilation
- gligli (Xbox 360 port)
- Xeihro (Xiro28 PSP Port)
- Flyinghead/Flycast Team  
- AI because this project would have never existed otherwise lol
- Probably Reicast Team also who knows
- Welcome to the IA-age guys and good luck everyone.

All together, let's Cast the Dream.

### Special thanks

- skmp because he's the god
- Senryoku, develloper of Deecy emulator
- Everyone on emuvdev/ discord
- All testers and all futur testers
- People actually helping me on the wiibrew wiki

### Special no thanks

- To all people not believing in this project
- People constantly critisizing the fact AI is used in this project, being condescending or worse. Please Grow up.






 











































































