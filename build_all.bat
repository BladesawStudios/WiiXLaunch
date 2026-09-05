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
echo Running final deployment packager...
python scripts\deploy.py
if %ERRORLEVEL% NEQ 0 exit /b 1

echo.
echo ==========================================
echo All builds completed successfully!
echo Output artifacts ready in deploy/ folder.
echo ==========================================
