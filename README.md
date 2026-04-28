# NullDC4Wii - Dreamcast Emulator for Wii

a fork from https://github.com/skmp/nullDCe

## TODO (Maybe you can help !)

### Simple

- Test current state with every game and report compatibility (see "compatibility" below)
- Help me finding regression (NOT bugs or glitch, only regression for now please)
- Test USB Key support (games in usb:/dreamcast/) and report to me

### Developer (Easy)

- Player 2 Gamecube/Wiimote (1rst step)
- Player 3/4 Gamecube/Wiimote (2nd step)
- Fishing Rod/USB Keyboard/Lightgun/Maracas support
- Put external config file for controllers (controls.cfg)
- Clean Clean Clean
- Optimize Optimize Optimize
- Comment / Guides / Documentation

### Developer (Normal)

- Coding routine adjustement for accuracy (FAST/BALANCED/ACCURATE)
- Coding routine adjustement for performance (LOW/NORMAL/HIGH/EXTRA)
- 4/3 support (implemented, need fix on some games like Shenmue)
- Wii U gamepad support like WiiStation ? ( https://github.com/FIX94/libwiidrc )
- Wii U Gamepad, Dualshock 3 and Wii U Pro Controller support ? ( https://github.com/BenoitAdam94/nullDC4Wii/issues/15 )

### Developer (Hard)

- Texture Cache management (see) #22
- Improve gxRend.cpp = main file about specific rendering for Wii
- Splitting gxRend.cpp in multiple files ? (beware this is more tricky than it look)
- Fix alpha/transparent stuff (may be costly in term of performances)
- Table convertion between SH4 Opcodes of SH4 and the WiiPPC ?
- Use LLVM to port code for PowerPC ?
- Full Dynarec implementation (AI seems to know about this)
- Sound implementation step 1 : NullAICA
- Sound implementation step 2 : AICA ARM7


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

#### Game file in SD:/discs/

**Test with castlevania Resurrection and Sega Tetris to begin**

Put your folders with GDI in this directory

CDI doesn't work now

Might work for ISO / BIN / CUE / NRG / MDS

BIN/CUE/ELF, but you probably need IP.bin/syscalls.bin (take IP.TMPL from bootdreams and rename it IP.Bin)


## Configuration

### General configuration

Check nullDC.cfg at root

### Controls

#### Wiimote :

DC - Wii

- A = A  
- B = B  
- Y = 1  
- X = 2  
- START = Home
- D-PAD = D-PAD  
- STICK = Nunchuck Stick
- L = - (and Nunchuck Z)
- R = +  

To Exit : - and +  

#### Gamecube controller :

DC - Gamecube

- A = A  
- B = B  
- Y = Y  
- X = X  
- START = START  
- D-PAD = D-PAD
- STICK = STICK
- L = L  
- R = R


To exit : R + L + Z  


### VMU (Memory card)

It seems to be supported, but 1rst you'll need to format the VMU in the bios

Files appears at root of /data/ :  
- vmu_save_A1.bin
- vmu_save_A2.bin

## Status (13/03/2026)

launch on dolphin an real Wii with FPS between 8 and 50 Fps  
game selector implemented  
1 player controler implemented  
Few games are runnable :
- Castlevania (Demo)
- Crazy Taxi
- Sega Tetris
- Shenmue
- Jojo


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
| **CI4 (FAST)/CI8 (FAST)** | Basic algirythm | Display mostly correctly (consume a bit of FPS) |
| **CI4 (NORMAL)/CI8 (NORMAL)** | Advanced algorythm for CI4/CI8 | Should display better |
| **RGB565** | Most advanced algorythm | Can have massive FPS dropdown (1 FPS) on some games |

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

## Ressources

### Dreamcast Emulators 

NullDC https://code.google.com/archive/p/nulldc/source/default/source  
NullDC (github) https://github.com/skmp/nullDCe  
NullDC for PSP : https://github.com/PSP-Archive/nulldce-psp  
NullDC for Xbox360 https://github.com/gligli/nulldc-360  
Reicast : https://github.com/skmp/reicast-emulator  
Flycast : https://github.com/flyinghead/flycast  

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

All testers and all futur testers

### Special no thanks

To all people not believing in this project






 











































































