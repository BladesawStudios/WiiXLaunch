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
cl.exe /nologo /std:c++20 /EHsc /W3 /DWIIXL_HOST_TEST=1 /I"%ROOT%\include" ^
    /Fe:hook_test.exe /Fo:hook_test.obj main.cpp
if errorlevel 1 (
    echo [hook_test] COMPILE FAILED
    popd
    exit /b 1
)
"%HERE%hook_test.exe"
set RC=%ERRORLEVEL%
popd
:: A CRASH IS A FAILURE, and used to not be one.
::
:: An access violation leaves ERRORLEVEL at -1073741819, and cmd's
:: `if errorlevel 1` means "errorlevel is >= 1" - which a NEGATIVE value is
:: not. So `exit /b %%RC%%` handed the caller a number that tested as success,
:: and a gate that segfaulted on its first case passed the build in silence.
:: Found when hook_test started crashing and the build stayed green.
::
:: Any non-zero result becomes 1, so it cannot be mistaken for either
:: success or the exit-2 "toolchain missing" case.
if not "%RC%"=="0" (
    echo [hook_test] FAILED or CRASHED - process exit code %RC%.
    exit /b 1
)
exit /b 0
