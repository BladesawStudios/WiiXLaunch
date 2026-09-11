@echo off
setlocal enabledelayedexpansion


:: WHICH GAME THIS HOST IS FOR.
::
::   build_cemu.bat            the default target
::   build_cemu.bat totk       targets/totk.json
::
:: Set before anything else runs, so generate_config and deploy cannot disagree
:: about it - they both resolve through scripts/target.py and both print what
:: they got. A build that silently picks a target is the same class of problem
:: as a gate nothing invokes.
if not "%~1"=="" set "WIIXL_TARGET=%~1"

call scripts\devkitpro_env.bat
if %ERRORLEVEL% NEQ 0 exit /b 1

echo Generating config...
python scripts\generate_config.py
if %ERRORLEVEL% NEQ 0 exit /b 1

if not exist build mkdir build
:: Module resources are staged fresh every build; a directory left from a
:: module that has been renamed or removed would otherwise be deployed
:: forever and silently join the mods directory.
if exist build\moddata rmdir /s /q build\moddata

:: Optional WiiXLaunch modules (e.g. vendor/wiixlaunch-botw) - not part of
:: base WiiXLaunch, picked up automatically if this mod added one as a
:: submodule (git submodule add <url> vendor/wiixlaunch-<name>).
set MODULE_FLAGS=
for /d %%G in (vendor\wiixlaunch-*) do (
    if exist "%%G\include" set MODULE_FLAGS=!MODULE_FLAGS! -I%%G\include
)

:: Invoke devkitPPC directly instead of CMake: on machines with Visual Studio
:: installed, CMake defaults to the VS generator, which ignores the
:: powerpc-eabi-gcc toolchain settings and tries to compile PowerPC code with
:: MSVC. The Cemu target is one compile+link, so there is nothing CMake adds.
:: Relative paths keep the (possibly space-containing) repo root out of args.
echo Building Cemu payload (PowerPC)...
:: No -g: the payload ships as a raw binary (debug info is useless) and debug
:: sections add .rela.debug_* entries that the deploy-time relocator must not
:: see (it now filters them, but there is no reason to generate them at all).
:: -fno-pie -fno-pic, NOT -fPIE: on this machine's devkitPPC (GCC 16.1.0),
:: -fPIE makes GCC emit GOT-indirect (.got2 + R_PPC_REL32) addressing for
:: globals/statics that this project's deploy.py-based relocation patching
:: (ADDR32/ADDR16 only) doesn't handle - confirmed via two independent,
:: reproducible crashes at the exact same instruction (WiiXLaunch_Init's
:: first line, `static bool initialized`), and by directly building the
:: sibling "Actor Spawning and Weapon Detection" repo on THIS toolchain,
:: which shows the identical pattern in its own compiled output despite
:: reportedly working before - almost certainly a GCC-version-dependent
:: codegen change, not a difference in either project's own code. Revisit
:: if devkitPPC ever gets pinned/downgraded, or if deploy.py's relocation
:: scanner grows real R_PPC_REL32/.got2 support.
"%DKP_PPC_GXX%" ^
  -std=gnu++20 -fno-pie -fno-pic -msdata=none ^
  -D__CEMU__=1 -DWIIXL_CEMU=1 ^
  -I include -I build\generated\include %MODULE_FLAGS% ^
  -nostartfiles -T scripts\cemu.ld -Wl,-q ^
  src\main.cpp src\wiiu_plugin.cpp src\cemu\bootstrap.cpp ^
  -o build\wiixlaunch_cemu
if %ERRORLEVEL% NEQ 0 exit /b 1

:: THIS SCRIPT BUILDS ONE HOST FOR ONE GAME. That is all it does.
::
:: It used to also run every gate, build the six example modules and call
:: scripts/deploy.py. Three different jobs behind one command: you could not
:: build a host without also publishing one, and a deploy writes the WHOLE mods
:: directory, so building for one game could overwrite another game's modules.
::
::   gates and example modules -> test.bat
::   packaging and installing  -> python scripts\deploy.py --target <name>
echo Cemu host built: build\wiixlaunch_cemu
exit /b 0
