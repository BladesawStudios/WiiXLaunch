@echo off
setlocal

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
set RYUJINX_MOD_EXEFS=%APPDATA%\Ryujinx\mods\contents\01007EF00011E000\WiiXLaunch\exefs
if exist "%RYUJINX_MOD_EXEFS%" (
    copy /y "deploy\switch\atmosphere\contents\01007EF00011E000\exefs\subsdk9" "%RYUJINX_MOD_EXEFS%\subsdk9" > nul
    copy /y "deploy\switch\atmosphere\contents\01007EF00011E000\exefs\main.npdm" "%RYUJINX_MOD_EXEFS%\main.npdm" > nul
    echo Copied to Ryujinx mods folder: %RYUJINX_MOD_EXEFS%
)

echo Switch build complete!
