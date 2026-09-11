@echo off
setlocal enabledelayedexpansion


:: EVERY GATE, AND THE EXAMPLE MODULES.
::
::   test.bat            the default target
::   test.bat totk       targets/totk.json
::
:: These used to live inside build_cemu.bat and build_switch.bat, which meant
:: "build a host" and "verify the tree" were the same command and neither could
:: be had without the other. A build script builds one host for one game now and
:: does nothing else - no gates, no example modules, no deploy, and nothing
:: copied into an emulator. This is the other half.
::
:: scripts/audit_gates.py checks that every gate below is still invoked FROM
:: HERE, so moving a gate out of this file is a build failure rather than a
:: quiet loss of coverage. That check is the reason this file exists as one
:: script instead of a list in a README.
::
:: Run a host build first if you want the two gates that need a host artifact:
:: test_host links its own, but test_switch_module reads the Switch host ELF
:: that build_switch.bat leaves in build\switch.
if not "%~1"=="" set "WIIXL_TARGET=%~1"
if "%WIIXL_TARGET%"=="" set "WIIXL_TARGET=botw"

call scripts\devkitpro_env.bat
if %ERRORLEVEL% NEQ 0 exit /b 1

:: The gates compile against build\generated\include, so the config has to
:: exist and has to be this target's.
echo Generating config...
python scripts\generate_config.py
if %ERRORLEVEL% NEQ 0 exit /b 1

if not exist build mkdir build
:: Module resources are staged fresh; a directory left from a module that has
:: been renamed or removed would otherwise sit in build\moddata forever and
:: silently join the next deploy.
if exist build\moddata rmdir /s /q build\moddata

:: Optional WiiXLaunch modules (e.g. vendor/wiixlaunch-botw) - not part of
:: base WiiXLaunch, picked up automatically if this mod added one as a
:: submodule (git submodule add <url> vendor/wiixlaunch-<name>).
set MODULE_FLAGS=
for /d %%G in (vendor\wiixlaunch-*) do (
    if exist "%%G\include" set MODULE_FLAGS=!MODULE_FLAGS! -I%%G\include
)

:: Are the gates below actually wired in, and is a failure fatal? Every gate
:: self-checks its own liveness, which is the right shape - but no gate can
:: detect that nothing calls it. This runs FIRST so a missing gate is reported
:: before the run spends time on the ones that are present.
python scripts\audit_gates.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: Host-completeness check. Links the same host from an EMPTY main.cpp and
:: asserts it is still complete - see scripts/test_host.py for why. main.cpp
:: becomes a .wxlm at stage 4, so nothing the host needs may come from it.
:: Compiled here rather than in Python because these flags live with the gate
:: that needs them; the shipping payload is built by build_cemu.bat.
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

:: WIIXL_DECLARE_PATCH_CROSS picks an architecture with the preprocessor, and a
:: macro that picked wrong would emit the other machine's bytes against the
:: other kind of address - a module that builds, packs, loads and is refused at
:: boot on somebody else's console. This compiles a fixture with both toolchains
:: and reads the emitted record back out of each ELF.
python scripts\test_patch_decl.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: The .wxlm writer and the format header have to agree; a drift between them
:: is the one failure neither side can detect at runtime. See test_wxlm.py.
python scripts\test_wxlm.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: No WIIXL_LOG line may exceed the 200-char cap. Truncation used to be
:: silent, and the half that got cut was the half saying what to do.
python scripts\test_log_lengths.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: Surface coverage. "Can a mod do what a source mod could?" used to be answered
:: by reading nineteen headers and remembering; this makes it a number, and
:: fails when a public function has neither a surface symbol nor an entry in
:: EXCLUDED saying why not. A decision and an oversight look identical until one
:: of them is written down.
python scripts\surface_coverage.py
if %ERRORLEVEL% NEQ 0 (
    echo [surface_coverage] FAILED - see above.
    exit /b 1
)

:: The import headers a .wxlm includes are GENERATED from the surface tables.
:: A generated file that has drifted from its source reads as authoritative and
:: is not - and this one would hand a mod a wrong signature, which compiles,
:: links, packs and loads before corrupting the stack at run time.
python scripts\gen_imports.py --check
if %ERRORLEVEL% NEQ 0 (
    echo [gen_imports] FAILED - see above.
    exit /b 1
)

:: The SDK, and a module built from it alone. --verify compiles a probe using
:: ONLY the assembled SDK, from a directory neither tree owns, and diffs it
:: against the same probe built from here - so "a mod can be built without the
:: framework" is a thing that happened this run rather than a design claim.
:: It is how ppc_relocs was found missing: forgetting a dependency is not
:: something you can notice from inside the tree that has it.
python scripts\make_sdk.py --check --verify
if %ERRORLEVEL% NEQ 0 (
    echo [make_sdk] FAILED - see above.
    exit /b 1
)

:: WIIXL_LOG's formatter. Every platform's logging goes through it, it cannot
:: be exercised on a console, and when it gets a conversion wrong it prints the
:: specifier and silently drops the argument - which reads as "the code under
:: test produced nothing".
call tools\format_test\build.bat
if errorlevel 2 (
    echo.
    echo ============================================================
    echo [format_test] SETUP PROBLEM - not a broken source tree.
    echo [format_test] This gate requires MSVC, which was not found. Install
    echo [format_test] Visual Studio with the "Desktop development with C++"
    echo [format_test] workload, or build on a machine that has it.
    echo [format_test] The formatter was NOT tested, so this run FAILS rather
    echo [format_test] than reporting an untested formatter as passing.
    echo ============================================================
    echo.
    exit /b 1
) else if errorlevel 1 (
    echo [format_test] FAILED - the formatter is wrong; see above.
    exit /b 1
)

:: sqrt, sin and cos for modules. A .wxlm has no libm, so include/wiixlaunch/
:: mod_math.h writes them out - and an approximation nobody measured is just a
:: wrong answer with good manners. This sweeps a million points against the
:: host's libm and asserts the bounds the header quotes.
call tools\mathtest\build.bat
if errorlevel 2 (
    echo [mathtest] SETUP PROBLEM - MSVC not found; see the format_test note above.
    exit /b 1
) else if errorlevel 1 (
    echo [mathtest] FAILED - mod_math.h is outside its stated bounds; see above.
    exit /b 1
)

:: The NVN block-linear swizzle. Derived from an artefact rather than asserted:
:: testpic_texture_bytes.hpp is a texture NVN has accepted and drawn, so this
:: swizzles it back and demands the same bytes.
call tools\nvn_swizzle_test\build.bat
if errorlevel 2 (
    echo [nvn_swizzle_test] SETUP PROBLEM - MSVC not found; see the format_test note above.
    exit /b 1
) else if errorlevel 1 (
    echo [nvn_swizzle_test] FAILED - the NVN texture layout is wrong; see above.
    exit /b 1
)

:: mod_config.h, the key = value file a module reads from its own directory.
:: The inputs are written BY HAND by people who have never seen the grammar, so
:: the cases are the ways such a file goes wrong.
call tools\config_test\build.bat
if errorlevel 2 (
    echo [config_test] SETUP PROBLEM - MSVC not found; see the format_test note above.
    exit /b 1
) else if errorlevel 1 (
    echo [config_test] FAILED - the settings parser is wrong; see above.
    exit /b 1
)

:: The central hook manager. Verifies a three-deep chain by DECODING the
:: instructions it emitted - call order, the prologue captured once and exactly,
:: and each Original pointing where it should.
call tools\hook_test\build.bat
if errorlevel 2 (
    echo [hook_test] SETUP PROBLEM - MSVC not found; see the loader_fuzz note below.
    exit /b 1
) else if errorlevel 1 (
    echo [hook_test] FAILED - see above.
    exit /b 1
)

:: Socket ownership. The transport underneath is a fake that RECYCLES file
:: descriptors, because a use-after-close is only dangerous when the number is
:: handed to somebody else - and no real platform will do that on cue.
call tools\net_test\build.bat
if errorlevel 2 (
    echo [net_test] SETUP PROBLEM - MSVC not found; see the loader_fuzz note below.
    exit /b 1
) else if errorlevel 1 (
    echo [net_test] FAILED - see above.
    exit /b 1
)

:: Fuzz the loader. It reads data it did not produce and then writes to memory
:: it executes, so it runs on every pass rather than on request - a check that
:: has to be remembered is a check that stops happening.
::
:: A MISSING TOOLCHAIN IS A FAILURE, NOT A WARNING. "skipped" is a state that
:: has to be seen, and a banner scrolls past. No gate exits 0 on a missing input.
call tools\loader_fuzz\build.bat
if errorlevel 2 (
    echo.
    echo ============================================================
    echo [loader_fuzz] SETUP PROBLEM - not a broken source tree.
    echo [loader_fuzz] This gate requires MSVC, which was not found. Install
    echo [loader_fuzz] Visual Studio with the "Desktop development with C++"
    echo [loader_fuzz] workload, or build on a machine that has it.
    echo [loader_fuzz] The loader was NOT fuzzed, so this run FAILS rather
    echo [loader_fuzz] than reporting an untested loader as passing.
    echo ============================================================
    echo.
    exit /b 1
) else if errorlevel 1 (
    echo [loader_fuzz] FAILED - see above.
    exit /b 1
)

:: Can a MODULE be built for AArch64? The host building says nothing about that
:: - it was true for months while wxlm.py wrote MACHINE_PPC32 into every file it
:: produced.
::
:: Needs the Switch host ELF, which build_switch.bat leaves in build\switch. A
:: MISSING ONE IS A FAILURE: "no host built yet" and "the host is fine" are
:: different answers and a skip makes them look the same.
if not exist build\switch\wiixlaunch-switch.elf (
    echo [test_switch_module] build\switch\wiixlaunch-switch.elf is missing.
    echo [test_switch_module] Run build_switch.bat %WIIXL_TARGET% first - this gate
    echo [test_switch_module] reads the host ELF to check the nn:: symbols it imports.
    exit /b 1
)
python scripts\test_switch_module.py build\switch\wiixlaunch-switch.elf
if %ERRORLEVEL% NEQ 0 exit /b 1


:: --- the example modules -------------------------------------------------
:: They are the framework's own examples and they exist to be EXERCISED: the
:: two hook mods hook the same address on purpose, so load order, call order
:: and the conflict line all land in one boot. Filenames decide load order -
:: a_first sorts before b_second, which is the whole reason they are named
:: that way.
::
:: Built for both machine types, because a module is a different binary per
:: target and "the samples pass" used to be a statement about PowerPC only.
:: Building them is a check on build_mod.py, not a step towards shipping: no
:: build script builds them and no build script deploys them.
for /f "usebackq delims=" %%i in (`python scripts\target_value.py samples`) do set "WANT_SAMPLES=%%i"
if "%WANT_SAMPLES%"=="0" (
    echo [WiiXLaunch] this target does not carry the example mods - skipping
    goto :after_samples
)

call :both sample_mod
if %ERRORLEVEL% NEQ 0 exit /b 1
call :both hook_mod_a
if %ERRORLEVEL% NEQ 0 exit /b 1
call :both hook_mod_b
if %ERRORLEVEL% NEQ 0 exit /b 1
call :both patch_mod
if %ERRORLEVEL% NEQ 0 exit /b 1
call :both net_mod
if %ERRORLEVEL% NEQ 0 exit /b 1
call :both player_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

:after_samples
echo.
echo All gates passed.
exit /b 0

:: --- one example module, both machine types ------------------------------
:: %1 = directory under examples/. The id comes from that mod's mod.json.
::
:: Delegated to scripts/build_mod.py, which is the ONLY definition of how a
:: module is compiled and packed. An external mod project calls the same
:: script, so the flags cannot drift between the in-tree samples and a real
:: third-party mod.
:both
python scripts\build_mod.py --source examples\%1
if %ERRORLEVEL% NEQ 0 exit /b 1
python scripts\build_mod.py --source examples\%1 --target switch --out build\%WIIXL_TARGET%
if %ERRORLEVEL% NEQ 0 exit /b 1
exit /b 0
