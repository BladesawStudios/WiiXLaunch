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
if "%WIIXL_TARGET%"=="" set "WIIXL_TARGET=botw"

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

:: OUR MEMORY SETTINGS, OVER EXLAUNCH'S.
::
:: generate_config has always written this file and nothing ever staged it,
:: so the Switch host compiled against exlaunch's defaults - JitSize
:: 0x1000, which is TWENTY hook trampolines - while the target asked for
:: 0x10000. A mod with twenty-six hooks aborted in AllocForTrampoline, and
:: until one had that many nothing could tell. The whole "memory" block of a
:: target was inert on this platform.
copy /y build\generated\include\program\setting.hpp "%STAGE%\source\program\setting.hpp" > nul
if %ERRORLEVEL% NEQ 0 (
    echo [WiiXLaunch] could not stage the generated setting.hpp
    exit /b 1
)

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
:: The linked ELF, kept beside the artifacts. test.bat's test_switch_module gate
:: reads it to check which nn:: symbols the host imports, and the build stage it
:: was linked in lives under %TEMP% and is deleted by the next build - so a gate
:: that reached into the stage was reading whatever the last build happened to
:: leave there.
copy /y "%STAGE%\wiixlaunch-switch.elf" build\switch\wiixlaunch-switch.elf > nul

:: THIS SCRIPT BUILDS ONE HOST FOR ONE GAME. That is all it does.
::
:: It used to also build the example modules, run scripts/deploy.py and copy the
:: result straight into Ryujinx's mods folder and virtual SD card. Three
:: different jobs behind one command: you could not build without publishing,
:: and a deploy writes the WHOLE mods directory, so building a host for one game
:: could overwrite the modules installed for another.
::
::   gates and example modules -> test.bat
::   packaging and installing  -> python scripts\deploy.py --target <name>
echo Switch host built: build\switch\subsdk9
exit /b 0
