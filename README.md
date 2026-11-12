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
- Test and report USB Keyboard on typing of the Dead

## Installation

### Put BIOS file and game file

#### Mandatory BIOS files in SD:\apps\nulldc4wii\data\

- dc_boot.bin  
- dc_flash.bin  
- fsca-table.bin (included)

#### Optional BIOS files in SD:\apps\nulldc4wii\data\

- dc_flash_wb.bin (this is the dc_flash but already saved)  
- syscalls.bin (needed for elf/bin)
- IP.bin  (needed for elf/bin)
- dc_nvmem.bin  
- vmu_default.bin  

#### Game file in SD:/discs/ or USB:/dreamcast/

**Test with Castlevania Resurrection and Sega Tetris to begin with**

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

The in-emulator options menu has **8 pages** of presets (general, graphics, depth & width, audio, core, experimental, JIT/dynarec, logs). The full per-preset reference, and a guide to finding the best settings for a game, are in [CONTRIBUTING.md](CONTRIBUTING.md#presets). To contribute code, see [CONTRIBUTING.md](CONTRIBUTING.md#development-environment) for the build environment and the developer TODO list.

## game_presets.cfg

game_presets is a file that reads the file name and directly apply matching presets.

game_presets.cfg needs to be in sd:/discs/ (or usb:/dreamcast/ if you use USB)

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

For every available key, see the comments in `apps/discs/game_presets.cfg`. To find the best settings for a game, see [Creating the optimal Preset](CONTRIBUTING.md#creating-the-optimal-preset-).

## Frequently Asked Question (FAQ)

### My SD Card isn't recognized

Make sure you have a good brand (Samsung, Kingston). Optimal card would be between 2Gb and 32Gb (more should work, we support SDXC), formated in FAT32 with 32kb cluster (16kb or 64kb could work). Try another SD card if that's not working

### Games doesn't launch

First, try on SD card with a know working game (Castlevania, Chuchu Rocket or Sega Tetris are best candidate). Try other file (cdi/gdi) and other bios.

### Games have poor Graphic/performance

Be sure to rename your file according to game_presets.cfg. For example : bomberman.gdi for Bomberman. 

<img width="754" height="93" alt="image" src="https://github.com/user-attachments/assets/9f59b4a4-c5ed-40e6-b5d5-46cea8b52350" />

### My USB key/HDD isn't recognized

For USB, try a FAT32 USB key first (32kb cluster) before switching to any other device. It works on my 500HDD FAT32 for information (with a cluster of 4kb)

### Does VMU work ?

It's reported working. Step 1 : go to bios. Step 2 : format VMU. Step 3 : exit properly the emulator (+ and -)

### Why this emulator ?

Because why not ? At first it was a POC (Proof Of Concept) but digging more and more, it seems somes games are achievable to 100% speed. So yeah ! Some people only have a Wii as console, especially in South America, so I'm happy to give them more games to play with their friend !

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

Maybe it's possible, but I don't have time to focus on this. CDI Files on SD Card/USB are supported since alpha 0.28 anyway.

### Can it run files Hard Drive disk ?

USB disk is currently implemented but I'm having trouble recently. Try alpha 0.63 if you have the same problem. USB key (FAT32 32kb cluster) is probably better to use

### Will WinCE games be implemented ?

That an additionnal ressources in CPU and we are limited. That may would make sense for a WiiU Port.

We are currently testing a branch with some implementation but it doesn't seem to work.

### Will Retroachievement be implemented ?

Probably not, we are super tight on RAM, and it also cost 1/2% of cpu cycle

### Will Netplay be implemented ?

Probably not, again, we are very short on RAM/CPU. And on Wii we don't have the 100% speed everytime, that would f*** up other players

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
- ZeoxGamer for heavily testing presets to make game run best as they could
- All testers and all futur testers
- People actually helping me on the wiibrew wiki

### Special no thanks

- To all people not believing in this project
- People constantly critisizing the fact AI is used in this project (this project wouldn't have existed otherwise...).
