@echo off
echo ==========================================
echo Building WiiXLaunch for All Target Platforms
echo ==========================================

echo.
echo [1/3] Building for Nintendo Switch (ARM64)...
call build_switch.bat
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Switch build failed.
    exit /b 1
)

echo.
echo [2/3] Building for Nintendo Wii U (Aroma)...
call build_wiiu.bat
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Wii U build failed.
    exit /b 1
)

echo.
echo [3/3] Building for Cemu Emulator (PowerPC)...
call build_cemu.bat
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
