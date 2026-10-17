@echo off
REM Build this carrier. Works on a dev box and on a CI runner alike: the VS
REM toolchain is located with vswhere, and the proxy is left to the caller --
REM set https_proxy/http_proxy before invoking on networks that need one.
REM
REM Two things this script exists to fix:
REM   1. Do not run it from Git Bash -- the MSYS PATH makes xmake resolve cc/cxx
REM      during package install, so even header-only deps fail at detection.
REM   2. clang-cl needs the MSVC environment (INCLUDE/LIB) because it uses the
REM      MSVC STL. Without vcvars, xmake reports toolchain("clang-cl"): not found,
REM      and the failure surfaces as a confusing cascade in unrelated packages.
REM
REM Keep this file ASCII only: cmd reads it in the system codepage (GBK here),
REM and non-ASCII bytes corrupt the following lines.
setlocal

REM Locate any VS with the C++ toolset. Every VS install ships vswhere.
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo WARNING: no Visual Studio with the C++ toolset found; clang-cl needs its STL.
) else (
    call "%VCVARS%" >nul 2>&1
)

REM clang-cl lives in LLVM's bin. Only fall back to the standard install when the
REM environment does not already provide one -- CI pins a specific LLVM and must win.
where clang-cl.exe >nul 2>&1
if errorlevel 1 if exist "C:\Program Files\LLVM\bin\clang-cl.exe" set "PATH=C:\Program Files\LLVM\bin;%PATH%"

cd /d "%~dp0"
xmake f -y -p windows -a x64 -m release --toolchain=clang-cl || exit /b 1
xmake -y || exit /b 1
endlocal
