@echo off
setlocal

:: If we're already in a Developer Command Prompt (or cl.exe is otherwise on
:: PATH), skip searching entirely.
where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto :build

:: Otherwise, ask vswhere (installed alongside the VS installer since VS2017)
:: for whichever edition/version of Visual Studio actually has the C++ build
:: tools, rather than hardcoding one edition's install path.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ring_log_reader] vswhere.exe not found - is Visual Studio installed?
    echo [ring_log_reader] Install Visual Studio ^(any edition/version^) or the Build Tools, with the "Desktop development with C++" workload.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
    echo [ring_log_reader] No Visual Studio installation with the C++ build tools was found.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if %ERRORLEVEL% NEQ 0 exit /b 1

:build
cl.exe /nologo /EHsc /std:c++17 /O2 /Fe:ring_log_reader.exe main.cpp
echo BUILD_EXIT=%ERRORLEVEL%
