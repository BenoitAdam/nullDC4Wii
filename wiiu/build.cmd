@echo off
:: ===========================================================================
:: wiiu/build.cmd - build NullDC4WiiU (native Wii U) from a plain Windows
:: prompt: cmd.exe, PowerShell, the VS Code terminal, or a double-click.
:: Wraps wiiu/Makefile; no devkitPro MSYS2 shell needed.
::
::   build.cmd            build nulldc4wiiu.rpx + nulldc4wiiu.wuhb
::   build.cmd clean      delete build\ and the outputs
::   build.cmd rebuild    clean, then build
::   build.cmd <args>     anything else goes straight to make (e.g. -j1, V=1)
::
:: One-time setup:
::   1. wut + wut-tools:  open "devkitPro MSYS2" and run   pacman -S wiiu-dev
::   2. CafeGLSL (GLSL -> GX2 shader compiler), from
::      https://github.com/Exzap/CafeGLSL/releases  (Windows build). Either put
::      glslcompiler.exe on PATH, set CAFEGLSL=<full path to glslcompiler.exe>,
::      or unzip it to wiiu\tools\cafeglsl\ (bin\glslcompiler.exe inside).
::   3. Antivirus: Avast's Auto-Sandbox blocks glslcompiler.exe (a rarely seen
::      exe): the first run passes, then every launch fails with "Permission
::      denied" / error 126 (avast log\autosandbox.log: "Autosandbox
::      candidate"). Add an Avast exception for wiiu\tools\cafeglsl\.
::
:: Parallel jobs: 3 by default, like wii/vs_make.bat (leaves a core free on a
:: 4-core CPU). Override with  set JOBS=8  before calling, or  build.cmd -j8.
::
:: Output: wiiu\nulldc4wiiu.wuhb -> copy to sd:/wiiu/apps/ and launch it from
:: the Wii U Menu (Aroma).
:: ===========================================================================
setlocal EnableExtensions

:: --- devkitPro ---------------------------------------------------------------
:: Windows-side install folder (installer default). Override by setting
:: DEVKITPRO_WIN before calling this script.
if not defined DEVKITPRO_WIN set "DEVKITPRO_WIN=C:\devkitPro"

:: make is an MSYS program: it reads devkitPro's msys2 /etc/fstab, which maps
:: /opt/devkitpro to the install folder - the same values the devkitPro
:: installer puts in the environment. Forced here because a shell that already
:: rewrote them (Git Bash turns /opt/devkitpro into C:/Program Files/Git/...)
:: would break the build.
set "DEVKITPRO=/opt/devkitpro"
set "DEVKITPPC=/opt/devkitpro/devkitPPC"
set "PATH=%DEVKITPRO_WIN%\msys2\usr\bin;%DEVKITPRO_WIN%\devkitPPC\bin;%DEVKITPRO_WIN%\tools\bin;%PATH%"

:: Pause at the end only when started by a double-click (the window would
:: close before the result can be read): cmd was launched FOR this script, and
:: not from a VS Code terminal (TERM_PROGRAM=vscode), which stays open anyway.
set "DBLCLICK=0"
:: Windows' find.exe by full path: MSYS2 is first on PATH now, and its Unix
:: find would be picked up instead ("find: '/i': No such file or directory").
echo %CMDCMDLINE% | "%SystemRoot%\System32\find.exe" /i "%~nx0" >nul && set "DBLCLICK=1"
if defined TERM_PROGRAM set "DBLCLICK=0"

set "RC=1"

if not exist "%DEVKITPRO_WIN%\msys2\usr\bin\make.exe" (
  echo [build] ERROR: devkitPro not found in %DEVKITPRO_WIN%
  echo         Install devkitPro, or set DEVKITPRO_WIN to its folder.
  goto :end
)
if not exist "%DEVKITPRO_WIN%\devkitPPC\bin\powerpc-eabi-gcc.exe" (
  echo [build] ERROR: devkitPPC missing in %DEVKITPRO_WIN%\devkitPPC
  goto :end
)
if not exist "%DEVKITPRO_WIN%\wut\share\wut_rules" (
  echo [build] ERROR: wut ^(Wii U SDK^) is not installed.
  echo         Open "devkitPro MSYS2" from the Start menu and run:
  echo             pacman -S wiiu-dev
  goto :end
)
if not exist "%DEVKITPRO_WIN%\tools\bin\elf2rpl.exe" (
  echo [build] ERROR: elf2rpl.exe not found ^(wut-tools^). In "devkitPro MSYS2":
  echo             pacman -S wut-tools
  goto :end
)
if not exist "%DEVKITPRO_WIN%\tools\bin\wuhbtool.exe" (
  echo [build] WARNING: wuhbtool.exe not found: the .rpx will build, the .wuhb will not.
)

:: --- CafeGLSL ----------------------------------------------------------------
:: Only needed while the shaders are not built yet (wiiu\build\*.gsh) or after
:: wiiu\shaders\ changed, but checked up front so the error is readable.
if defined CAFEGLSL goto :have_glsl
if exist "%~dp0tools\cafeglsl\bin\glslcompiler.exe" set "CAFEGLSL=%~dp0tools\cafeglsl\bin\glslcompiler.exe"
if not defined CAFEGLSL if exist "%~dp0tools\cafeglsl\glslcompiler.exe" set "CAFEGLSL=%~dp0tools\cafeglsl\glslcompiler.exe"
if not defined CAFEGLSL (
  where glslcompiler >nul 2>&1 && set "CAFEGLSL=glslcompiler"
)
if not defined CAFEGLSL (
  if exist "%~dp0build\pvr.gsh" if exist "%~dp0build\blit.gsh" (
    echo [build] note: CafeGLSL not found, reusing the shaders already in build\
    set "CAFEGLSL=glslcompiler"
    goto :have_glsl
  )
  echo [build] ERROR: CafeGLSL's glslcompiler.exe not found. Get the Windows release
  echo         from https://github.com/Exzap/CafeGLSL/releases and either put it on
  echo         PATH, set CAFEGLSL=^<path to glslcompiler.exe^>, or unzip it to
  echo         %~dp0tools\cafeglsl\
  goto :end
)
:have_glsl
:: make runs its recipes through MSYS sh: backslashes would be eaten as escapes.
set "CAFEGLSL=%CAFEGLSL:\=/%"

:: --- make ----------------------------------------------------------------------
pushd "%~dp0"
if not defined JOBS set "JOBS=3"
set "JOBS=-j%JOBS%"

:: No ERRORLEVEL reads inside ( ) blocks: cmd expands them when the block is
:: parsed, i.e. before make has run.
if /i "%~1"=="clean" goto :do_clean
if /i not "%~1"=="rebuild" goto :do_build
make clean
shift

:do_build
echo [build] make %JOBS% CAFEGLSL=%CAFEGLSL%
make %JOBS% "CAFEGLSL=%CAFEGLSL%" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
  echo.
  echo [build] OK: %~dp0nulldc4wiiu.wuhb
) else (
  echo.
  echo [build] FAILED ^(make exit code %RC%^)
  echo         If the log says "glslcompiler.exe: Permission denied", the antivirus
  echo         is blocking CafeGLSL: add an exception for %~dp0tools\cafeglsl\
  echo         ^(Avast: Menu ^> Settings ^> General ^> Exceptions^).
)
goto :done

:do_clean
make clean
set "RC=%ERRORLEVEL%"

:done
popd

:end
if "%DBLCLICK%"=="1" pause
exit /b %RC%
