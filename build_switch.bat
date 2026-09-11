@echo off
setlocal


:: WHICH GAME THIS HOST IS FOR.
::
::   build_switch.bat            the default target
::   build_switch.bat totk     targets/totk.json
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

:: devkitPro's make rules cannot handle spaces in paths, so stage the build in
:: %TEMP% (space-free) instead of building in-place.
set STAGE=%TEMP%\wiixlaunch-switch
echo Preparing Switch build environment in %STAGE%...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"

:: Copy exlaunch template and generated configs into the stage
xcopy /s /e /y /i vendor\exlaunch "%STAGE%" > nul
copy /y build\generated\switch\config.json "%STAGE%\config.json" > nul
copy /y build\generated\switch\config.mk "%STAGE%\config.mk" > nul

:: Copy our source files into the exlaunch source tree
if not exist "%STAGE%\source\wiixlaunch" mkdir "%STAGE%\source\wiixlaunch"
xcopy /s /e /y /i src\* "%STAGE%\source\wiixlaunch" > nul

:: Delete exlaunch template main.cpp to avoid multiple definition conflict with our src/main.cpp
del /f /q "%STAGE%\source\program\main.cpp"

:: Fix GCC anonymous struct typedef error in exlaunch
powershell -Command "(Get-Content '%STAGE%\source\lib\hook\nx64\hook_impl.cpp') -replace 'typedef struct \{', 'struct context {' -replace '\} context;', '};' | Set-Content '%STAGE%\source\lib\hook\nx64\hook_impl.cpp'"

:: Build via devkitPro's msys2 so DEVKITA64 and the switch rules resolve
echo Building for Switch (ARM64)...
set STAGEFWD=%STAGE:\=/%
"%DKP_BASH%" -lc "cd '%STAGEFWD%' && make"
if %ERRORLEVEL% NEQ 0 exit /b 1

:: Extract artifacts
if not exist build\switch mkdir build\switch
copy /y "%STAGE%\deploy\subsdk9" build\switch\subsdk9 > nul
copy /y "%STAGE%\deploy\main.npdm" build\switch\main.npdm > nul

:: Can a MODULE be built for this platform? The host building says nothing
:: about that - it was true for months while wxlm.py wrote MACHINE_PPC32 into
:: every file it produced. Run here rather than in build_cemu because this is
:: where devkitA64 is already a hard requirement.
python scripts\test_switch_module.py "%STAGE%\wiixlaunch-switch.elf"
if %ERRORLEVEL% NEQ 0 exit /b 1


:: The same sample modules build_cemu builds, for THIS machine.
::
:: They were Cemu-only, and not by decision - nothing here built them, so
:: build/switch-mods held whatever had been produced by hand. A module is a
:: different binary per target (aarch64, little-endian, ABS64 relocations), so
:: "the samples pass" was a statement about PowerPC and nothing else, and the
:: one platform where module loading is newest had the least coverage.

:: the smallest complete module
call :build_mod sample_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

:: two modules hooking the same function, in load order
call :build_mod hook_mod_a
if %ERRORLEVEL% NEQ 0 exit /b 1

:: the second half of that pair
call :build_mod hook_mod_b
if %ERRORLEVEL% NEQ 0 exit /b 1

:: a declared patch rather than a hook
call :build_mod patch_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

:: the wiixl.net demonstration
call :build_mod net_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

:: the botw.player v1.1 demonstration
call :build_mod player_mod
if %ERRORLEVEL% NEQ 0 exit /b 1

python scripts\deploy.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: deploy.py only writes deploy\switch\atmosphere\contents\... - it never
:: touches Ryujinx's actual mods folder. That gap meant every test this
:: session after the mods copy was last done by hand kept re-running the
:: SAME stale subsdk9 no matter what changed in source, which cost a lot of
:: debugging time chasing phantom "identical behavior across different code"
:: symptoms that were really just "never rebuilt." Copy straight into the
:: mods folder here so `main.npdm`/`subsdk9` in Ryujinx are always what was
:: just compiled.
:: The mod folder name is ours to pick - Ryujinx reads every subfolder of
:: the title id. This pointed at NVNInjectionTest, a folder from an older
:: setup that no longer exists, so the guard below was false on every
:: build and the copy silently never happened. That is precisely the
:: staleness the comment above was written about, one level up.
:: THE TARGET'S TITLE ID, NOT A CONSTANT.
::
:: This was hardcoded to BotW. The first build of a second target therefore
:: copied a TOTK subsdk9 over the BotW host, and the next BotW boot would have
:: run it - a value repeated in two places is a value that will disagree with
:: itself. It comes from the resolver the rest of the build used.
for /f "usebackq delims=" %%i in (`python scripts\target_value.py switch.title_id`) do set "TITLE_ID=%%i"
if "%TITLE_ID%"=="" (
    echo [WiiXLaunch] Could not read switch.title_id for this target
    exit /b 1
)
set RYUJINX_MOD_EXEFS=%APPDATA%\Ryujinx\mods\contents\%TITLE_ID%\WiiXLaunch\exefs
if exist "%RYUJINX_MOD_EXEFS%" (
    copy /y "deploy\switch\atmosphere\contents\%TITLE_ID%\exefs\subsdk9" "%RYUJINX_MOD_EXEFS%\subsdk9" > nul
    copy /y "deploy\switch\atmosphere\contents\%TITLE_ID%\exefs\main.npdm" "%RYUJINX_MOD_EXEFS%\main.npdm" > nul
    echo Copied to Ryujinx mods folder: %RYUJINX_MOD_EXEFS%
)

:: And the modules, onto Ryujinx's virtual SD card. Same gap as the exefs copy
:: above and the same consequence: the loader reads sd:/WiiXLaunch/mods, nothing
:: put anything there, and the modules that ran in the last Switch test were
:: copied in by hand - so that test proved the loader worked and proved nothing
:: about the build.
:: The delete is not tidiness. The loader enumerates the directory, so a module
:: left from an older build is one the next boot LOADS.
set RYUJINX_SD=%APPDATA%\Ryujinx\sdcard
set RYUJINX_SD_MODS=%RYUJINX_SD%\WiiXLaunch\mods\%TITLE_ID%
if exist "%RYUJINX_SD%" (
    if not exist "%RYUJINX_SD_MODS%" mkdir "%RYUJINX_SD_MODS%"
    del /q "%RYUJINX_SD_MODS%\*.wxlm" 2>nul
    rem xcopy, not copy: modules have resource DIRECTORIES beside them
    rem (mods/<id>/), and copying only *.wxlm shipped the code without the
    rem files it reads. "rem" and not "::" because a :: label inside a
    rem parenthesised if block is a cmd parse error, which is how this
    rem announced itself: "and was unexpected at this time".
    xcopy /e /i /y /q "deploy\switch\WiiXLaunch\mods\%TITLE_ID%" "%RYUJINX_SD_MODS%" > nul
    echo Copied modules to Ryujinx SD card: %RYUJINX_SD_MODS%
)


echo Switch build complete!
exit /b 0

:: --- one module, built for aarch64 --------------------------------------
:: %1 = directory under examples/. Same script and same manifest as the Cemu
:: build; only --target differs, which is the whole point of it being a flag.
:build_mod
python scripts\build_mod.py --source examples\%1 --target switch
if %ERRORLEVEL% NEQ 0 exit /b 1
exit /b 0
