@echo off
REM Build this carrier from a plain Windows shell.
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
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
set "https_proxy=http://127.0.0.1:7890"
set "http_proxy=http://127.0.0.1:7890"

cd /d "%~dp0"
xmake f -y -p windows -a x64 -m release --toolchain=clang-cl || exit /b 1
xmake -y || exit /b 1
endlocal
