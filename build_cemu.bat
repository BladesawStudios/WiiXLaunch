@echo off
setlocal enabledelayedexpansion

call scripts\devkitpro_env.bat
if errorlevel 1 exit /b 1

echo Generating config...
python scripts\generate_config.py
if errorlevel 1 exit /b 1

if not exist build mkdir build

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
if errorlevel 1 exit /b 1

:: Host-completeness check. Links the same host from an EMPTY main.cpp and
:: asserts it is still complete - see scripts/test_host.py for why. main.cpp
:: becomes a .wxlm at stage 4, so nothing the host needs may come from it.
:: Compiled here rather than in Python because these flags live here and
:: duplicating them would drift.
echo. > build\empty_main.cpp
"%DKP_PPC_GXX%" ^
  -std=gnu++20 -fno-pie -fno-pic -msdata=none ^
  -D__CEMU__=1 -DWIIXL_CEMU=1 ^
  -I include -I build\generated\include %MODULE_FLAGS% ^
  -nostartfiles -T scripts\cemu.ld -Wl,-q ^
  build\empty_main.cpp src\wiiu_plugin.cpp src\cemu\bootstrap.cpp ^
  -o build\wiixlaunch_cemu_hosttest
if errorlevel 1 (
    echo [test_host] The host does not LINK without main.cpp.
    exit /b 1
)
python scripts\test_host.py build\wiixlaunch_cemu_hosttest
if errorlevel 1 exit /b 1

:: The .wxlm writer and the format header have to agree; a drift between them
:: is the one failure neither side can detect at runtime. See test_wxlm.py.
python scripts\test_wxlm.py
if errorlevel 1 exit /b 1

:: --- the sample .wxlm module ---------------------------------------------
:: Built with its own linker script: linked at 0 like the host payload, but
:: keeping .init_array, because the loader is the only thing that will ever
:: run a module's static constructors.
:: -lgcc is needed even under -nostdlib: GCC emits calls to libgcc's PowerPC
:: register save/restore helpers (_restgpr_*), and without it they stay
:: undefined and wxlm.py rejects the module.
"%DKP_PPC_GXX%" ^
  -std=gnu++20 -fno-pie -fno-pic -msdata=none -Os ^
  -ffreestanding -fno-exceptions -fno-rtti ^
  -D__CEMU__=1 -DWIIXL_CEMU=1 ^
  -nostartfiles -nostdlib -T scripts\wxlm_mod.ld -Wl,-q ^
  -Wl,--unresolved-symbols=ignore-all ^
  examples\sample_mod\mod.cpp -lgcc ^
  -o build\sample_mod.elf
if errorlevel 1 exit /b 1

python scripts\wxlm.py build\sample_mod.elf build\sample.wxlm --id sample --phase load
if errorlevel 1 exit /b 1

python scripts\deploy.py
if errorlevel 1 exit /b 1
echo Cemu build complete!
