@echo off
REM Build this carrier. Works on a dev box and on a CI runner alike: the VS toolchain
REM is located with vswhere, and the proxy is left to the caller -- set https_proxy
REM before invoking on networks that need one.
REM
REM Endstone's own example builds with clang-cl + Ninja; the default Visual Studio
REM generator (MSVC) works too and needs one less tool, so that is what this uses.
REM
REM Keep this file ASCII only: cmd reads it in the system codepage (GBK here),
REM and non-ASCII bytes corrupt the following lines.
setlocal

set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo WARNING: no Visual Studio with the C++ toolset found.
) else (
    call "%VCVARS%" >nul 2>&1
)

cd /d "%~dp0"
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
endlocal
