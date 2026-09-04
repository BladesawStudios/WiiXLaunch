@echo off
setlocal
set "HERE=%~dp0"

python "%HERE%extract.py"
if errorlevel 1 exit /b 1

:: Same toolchain search as tools/ring_log_reader/build.bat.
where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [format_test] vswhere.exe not found - is Visual Studio installed?
    exit /b 2
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
    echo [format_test] No Visual Studio with the C++ tools found.
    exit /b 2
)

:: vcvarsall shells out to vswhere, so it needs the Installer directory on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 2

:build
pushd "%HERE%"
cl.exe /nologo /std:c++17 /EHsc /I. /Fe:format_test.exe /Fo:format_test.obj main.cpp
if errorlevel 1 (
    echo [format_test] COMPILE FAILED
    popd
    exit /b 1
)
echo.
"%HERE%format_test.exe"
set RC=%ERRORLEVEL%
popd
exit /b %RC%
