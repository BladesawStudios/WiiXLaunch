@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."

:: Exit codes are meaningful to the caller:
::   0  the fuzzer ran and passed
::   1  the fuzzer ran and FAILED, or would not compile
::   2  no C++ toolchain on this machine, so it did not run
::
:: 2 is separated from 1 on purpose. A test that quietly vanishes on a machine
:: without a toolchain is worse than no test, because the build still says OK -
:: so the caller turns 2 into a loud warning rather than silence.

where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" exit /b 2

:: vcvarsall shells out to vswhere, so it needs the Installer directory on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 2

:build
pushd "%HERE%"
cl.exe /nologo /std:c++20 /EHsc /W3 /I"%ROOT%\include" ^
    /Fe:loader_fuzz.exe /Fo:loader_fuzz.obj main.cpp
if errorlevel 1 (
    echo [loader_fuzz] COMPILE FAILED
    popd
    exit /b 1
)
"%HERE%loader_fuzz.exe"
set RC=%ERRORLEVEL%
popd
exit /b %RC%
