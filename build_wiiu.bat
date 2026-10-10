@echo off
setlocal

call scripts\devkitpro_env.bat
if %ERRORLEVEL% NEQ 0 exit /b 1

echo Generating config...
python scripts\generate_config.py
if %ERRORLEVEL% NEQ 0 exit /b 1

:: Output goes under build\<target>\, so hosts for two games never overwrite
:: each other. The name comes from scripts\target.py, the one place that
:: decides which target is active.
set TARGET_NAME=
for /f "usebackq delims=" %%i in (`python scripts\target_value.py --name`) do set "TARGET_NAME=%%i"
if not defined TARGET_NAME exit /b 1
set "OUT=build\%TARGET_NAME%\wiiu"

:: The plugin filename lives in exactly one place - wiiu.plugin_name in
:: the active target in targets/ - and is read from there rather than repeated
:: the Makefile's TARGET (passed on the command line below) and the copy step at
:: the end; hardcoding it in either spot is how it drifts on a rename.
for /f "usebackq delims=" %%i in (`python scripts\target_value.py wiiu.plugin_name`) do set "WPS_NAME=%%i"
if not defined WPS_NAME (
    echo [WiiXLaunch] Could not read wiiu.plugin_name from this target
    exit /b 1
)
:: Makefile's TARGET is the same name without the .wps extension
set "WPS_TARGET=%WPS_NAME:.wps=%"

:: devkitPro's make rules cannot handle spaces in paths, so stage the build in
:: %TEMP% (space-free) instead of building in-place. See scripts/wiiu/Makefile.
set STAGE=%TEMP%\wiixlaunch-wiiu
echo Preparing Wii U build environment in %STAGE%...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
xcopy /s /e /y /i src "%STAGE%\src" > nul
xcopy /s /e /y /i include "%STAGE%\include" > nul
xcopy /s /e /y /i build\generated\include "%STAGE%\generated\include" > nul

:: The game module(s) this target builds, as scripts/target.py decides - see
:: build_cemu.bat. Only those are staged; scripts/wiiu/Makefile picks up
:: modules/*/include, so staging is the selection. Run once bare so its exit
:: code is seen.
python scripts\target_modules.py > nul
if %ERRORLEVEL% NEQ 0 exit /b 1
for /f "usebackq delims=" %%G in (`python scripts\target_modules.py`) do (
    xcopy /s /e /y /i "vendor\%%G\include" "%STAGE%\modules\%%G\include" > nul
)

copy /y scripts\wiiu\Makefile "%STAGE%\Makefile" > nul

:: Build via devkitPro's msys2 so DEVKITPRO/elf2rpl/wups_rules all resolve.
:: Requires: dkp-pacman packages wut + wut-tools, and WUPS + libfunctionpatcher
:: installed from vendor/ (make install into /opt/devkitpro/wups and /wums).
echo Building for Wii U (PowerPC)...
set STAGEFWD=%STAGE:\=/%
"%DKP_BASH%" -lc "cd '%STAGEFWD%' && make TARGET='%WPS_TARGET%'"
if %ERRORLEVEL% NEQ 0 exit /b 1

if not exist "%OUT%" mkdir "%OUT%"
copy /y "%STAGE%\%WPS_NAME%" "%OUT%\%WPS_NAME%" > nul
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
echo Wii U host built: %OUT%\%WPS_NAME%
exit /b 0
