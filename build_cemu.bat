@echo off
setlocal enabledelayedexpansion

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
if %ERRORLEVEL% NEQ 0 (
    echo [test_host] The host does not LINK without main.cpp.
    exit /b 1
)
python scripts\test_host.py build\wiixlaunch_cemu_hosttest
if %ERRORLEVEL% NEQ 0 exit /b 1

:: The .wxlm writer and the format header have to agree; a drift between them
:: is the one failure neither side can detect at runtime. See test_wxlm.py.
python scripts\test_wxlm.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: No WIIXL_LOG line may exceed the 200-char cap. Truncation used to be
:: silent, and the half that got cut was the half saying what to do.
python scripts\test_log_lengths.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: Are the gates below actually wired in, and is a failure fatal? Every gate
:: self-checks its own liveness, which is the right shape - but no gate can
:: detect that nothing calls it. This runs FIRST so a missing gate is reported
:: before the build spends time on the ones that are present.
python scripts\audit_gates.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: WIIXL_LOG's formatter. Every platform's logging goes through it, it cannot
:: be exercised on a console, and when it gets a conversion wrong it prints the
:: specifier and silently drops the argument - which reads as "the code under
:: test produced nothing".
::
:: THIS WAS NOT WIRED IN UNTIL 2026-09-04. It was written, it passed when run by
:: hand, and no build script had ever called it - so it could not fail, in the
:: most complete sense available. A gate nothing invokes is the limit case of
:: the fourth rule in docs/modules.md, and it is the one thing a gate cannot
:: detect about itself, which is why scripts/audit_gates.py checks the wiring.
call tools\format_test\build.bat
if errorlevel 2 (
    echo.
    echo ============================================================
    echo [format_test] SETUP PROBLEM - not a broken source tree.
    echo [format_test] This gate requires MSVC, which was not found. Install
    echo [format_test] Visual Studio with the "Desktop development with C++"
    echo [format_test] workload, or build on a machine that has it.
    echo [format_test] The formatter was NOT tested, so this build FAILS rather
    echo [format_test] than shipping an untested formatter.
    echo ============================================================
    echo.
    exit /b 1
) else if errorlevel 1 (
    echo [format_test] FAILED - the formatter is wrong; see above.
    exit /b 1
)

:: The central hook manager. Verifies a three-deep chain by DECODING the
:: instructions it emitted - call order, the prologue captured once and exactly,
:: and each Original pointing where it should. It cannot execute PowerPC; the
:: boot log proves the chain RUNS, this proves it was BUILT right.
call tools\hook_test\build.bat
if errorlevel 2 (
    echo [hook_test] SETUP PROBLEM - MSVC not found; see the loader_fuzz note below.
    exit /b 1
) else if errorlevel 1 (
    echo [hook_test] FAILED - see above.
    exit /b 1
)

:: Fuzz the loader. It reads data it did not produce and then writes to memory
:: it executes, so it runs on every build rather than on request - a check that
:: has to be remembered is a check that stops happening.
::
:: A MISSING TOOLCHAIN IS A FAILURE, NOT A WARNING. It used to print a banner
:: and let the build succeed; "skipped" is a state that has to be seen, and a
:: banner scrolls past. No gate exits 0 on a missing input.
call tools\loader_fuzz\build.bat
if errorlevel 2 (
    echo.
    echo ============================================================
    echo [loader_fuzz] SETUP PROBLEM - not a broken source tree.
    echo [loader_fuzz] This gate requires MSVC, which was not found. Install
    echo [loader_fuzz] Visual Studio with the "Desktop development with C++"
    echo [loader_fuzz] workload, or build on a machine that has it.
    echo [loader_fuzz] The loader was NOT fuzzed, so this build FAILS rather
    echo [loader_fuzz] than shipping an untested loader.
    echo ============================================================
    echo.
    exit /b 1
) else if errorlevel 1 (
    echo [loader_fuzz] FAILED - see above.
    exit /b 1
)

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
if %ERRORLEVEL% NEQ 0 exit /b 1

python scripts\wxlm.py build\sample_mod.elf build\sample.wxlm --id sample --phase load
if %ERRORLEVEL% NEQ 0 exit /b 1

:: --- the two colliding modules ------------------------------------------
:: These hook the SAME address on purpose. Two mods that did not interact
:: would prove directory enumeration and nothing else; the point is call
:: order and the conflict line, together, in one boot.
::
:: The FILENAMES decide the order - load order is lexical by filename, and
:: load order is hook install order is call order. a_first sorts before
:: b_second, which is the whole reason they are named that way.
:: Two direct calls rather than a for loop. Inside a parenthesised block
:: %ERRORLEVEL% expands when the block is PARSED, which is before the
:: command in it has run - so the check would test a stale value. Delayed
:: expansion fixes that, but not needing it at all is better.
call :build_mod a_first hook_mod_a
if %ERRORLEVEL% NEQ 0 exit /b 1
call :build_mod b_second hook_mod_b
if %ERRORLEVEL% NEQ 0 exit /b 1
call :build_mod c_patch patch_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

python scripts\deploy.py
if %ERRORLEVEL% NEQ 0 exit /b 1
echo Cemu build complete!
exit /b 0

:: --- one module, built and packed ---------------------------------------
:: %1 = mod id and output name, %2 = directory under examples/
::
:: Delegated to scripts/build_mod.py, which is the ONLY definition of how a
:: module is compiled and packed. An external mod project calls the same
:: script, so the flags cannot drift between the in-tree samples and a real
:: third-party mod - and these flags are not obvious enough to keep two
:: copies of. See the header comment there for what each one is for.
:build_mod
python scripts\build_mod.py --source examples\%2 --id %1
if %ERRORLEVEL% NEQ 0 exit /b 1
exit /b 0

