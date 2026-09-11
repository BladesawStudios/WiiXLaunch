@echo off
setlocal
set "HERE=%~dp0"


:: Same toolchain search as tools/ring_log_reader/build.bat.
where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [nvn_swizzle_test] vswhere.exe not found - is Visual Studio installed?
    exit /b 2
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
    echo [nvn_swizzle_test] No Visual Studio with the C++ tools found.
    exit /b 2
)

:: vcvarsall shells out to vswhere, so it needs the Installer directory on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if %ERRORLEVEL% NEQ 0 exit /b 2

:build
pushd "%HERE%"
cl.exe /nologo /std:c++17 /EHsc /I. /Fe:nvn_swizzle_test.exe /Fo:nvn_swizzle_test.obj main.cpp
if %ERRORLEVEL% NEQ 0 (
    echo [nvn_swizzle_test] COMPILE FAILED
    popd
    exit /b 1
)
echo.
"%HERE%nvn_swizzle_test.exe"
set RC=%ERRORLEVEL%
popd
:: A CRASH IS A FAILURE, and used to not be one.
::
:: An access violation leaves ERRORLEVEL at -1073741819, and cmd's
:: `if errorlevel 1` means "errorlevel is >= 1" - which a NEGATIVE value is
:: not. So `exit /b %%RC%%` handed the caller a number that tested as success,
:: and a gate that segfaulted on its first case passed the build in silence.
:: Found when nvn_swizzle_test started crashing and the build stayed green.
::
:: Any non-zero result becomes 1, so it cannot be mistaken for either
:: success or the exit-2 "toolchain missing" case.
if not "%RC%"=="0" (
    echo [nvn_swizzle_test] FAILED or CRASHED - process exit code %RC%.
    exit /b 1
)
exit /b 0
