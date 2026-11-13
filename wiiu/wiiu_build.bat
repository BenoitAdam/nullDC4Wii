:: wiiu/wiiu_build.bat - native Wii U build, mirrors wii/vs_make.bat.
:: Run from the wiiu\ folder in the devkitPro MSYS2 shell, like the Wii script.
::
:: Needs wut (dkp-pacman -S wiiu-dev) and CafeGLSL's glslcompiler on PATH,
:: or pass its location:  make CAFEGLSL=/c/tools/cafeglsl/bin/glslcompiler.exe
:: Output: nulldc4wiiu.rpx and nulldc4wiiu.wuhb in this folder. Copy the .wuhb
:: to sd:/wiiu/apps/ and launch it from the Wii U Menu (Aroma).
:: -j3: same core count as the Wii script; adjust to your CPU.

clear
make 2>&1 -j3 | sed -e "s/\([^:]*\):\([0-9][0-9]*\)\(.*\)/\1(\2) \3/"
