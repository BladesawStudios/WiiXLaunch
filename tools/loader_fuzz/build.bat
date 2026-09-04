@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."

where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [loader_fuzz] vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
    echo [loader_fuzz] No Visual Studio with the C++ tools found.
    exit /b 1
)

:: vcvarsall shells out to vswhere, so it needs the Installer directory on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1

:build
pushd "%HERE%"
cl.exe /nologo /std:c++20 /EHsc /W3 /I"%ROOT%\include" ^
    /Fe:loader_fuzz.exe /Fo:loader_fuzz.obj main.cpp
if errorlevel 1 (
    echo [loader_fuzz] COMPILE FAILED
    popd
    exit /b 1
)
echo.
"%HERE%loader_fuzz.exe"
set RC=%ERRORLEVEL%
popd
exit /b %RC%
