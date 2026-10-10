@echo off
setlocal

:: EVERY SUB-SCRIPT IS INVOKED THROUGH THIS SCRIPT'S OWN DIRECTORY.
::
:: These used to be bare names - `call build_switch.bat` - which relies on cmd
:: searching the CURRENT DIRECTORY for the command. That search is disabled on
:: any machine with NoDefaultCurrentDirectoryInExePath set, and on such a
:: machine this script died on its very first call with
::
::   'build_switch.bat' is not recognized as an internal or external command
::
:: - which means build_all.bat had never once run to completion here. The three
:: individual scripts passing is a DIFFERENT CLAIM from build_all passing, and
:: the difference is the same shape as tools/format_test sitting unwired from
:: 73596ed: something that looks like it runs and does not. scripts/audit_gates.py
:: now refuses a bare invocation here so it cannot come back.
::
:: cd /d as well as the qualified paths, because the sub-scripts use relative
:: paths of their own (scripts\deploy.py and the rest) and expect the repo root.
cd /d "%~dp0"

:: One target for all three, passed down through WIIXL_TARGET. Each host
:: lands under build\<target>\.
if not "%~1"=="" set "WIIXL_TARGET=%~1"
set TARGET_NAME=
for /f "usebackq delims=" %%i in (`python scripts\target_value.py --name`) do set "TARGET_NAME=%%i"
if not defined TARGET_NAME exit /b 1

echo ==========================================
echo Building WiiXLaunch for All Target Platforms
echo ==========================================

echo.
echo [1/3] Building for Nintendo Switch (ARM64)...
call "%~dp0build_switch.bat"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Switch build failed.
    exit /b 1
)

echo.
echo [2/3] Building for Nintendo Wii U (Aroma)...
call "%~dp0build_wiiu.bat"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Wii U build failed.
    exit /b 1
)

echo.
echo [3/3] Building for Cemu Emulator (PowerPC)...
call "%~dp0build_cemu.bat"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Cemu build failed.
    exit /b 1
)

echo.
echo ==========================================
echo All three hosts built.
echo   build\%TARGET_NAME%\switch\subsdk9
echo   build\%TARGET_NAME%\wiiu\
echo   build\%TARGET_NAME%\wiixlaunch_cemu
echo.
echo Verify with  test.bat
echo Install with python scripts\deploy.py --target ^<name^>
echo ==========================================
