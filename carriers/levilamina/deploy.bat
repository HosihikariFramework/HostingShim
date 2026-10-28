@echo off
REM Copy the built Shim and, for source deployments, the managed core into a server's plugins dir.
REM
REM   deploy.bat <server root> [shim-only]
REM
REM The managed core is taken from a sibling checkout of Hosihikari.PluginManager,
REM or from HOSIHIKARI_PLUGINMANAGER. Release packaging passes shim-only: the core
REM then comes from its own release archive. Keep this file ASCII only.
setlocal
if "%~1"=="" (
    echo usage: deploy.bat ^<server root^>
    exit /b 1
)

REM The package name is the shim's mod name in the parent's namespace (ADR-0006).
set "MODNAME=hosihikari"
set "HERE=%~dp0"
set "MANAGED=%HERE%..\..\..\PluginManager\src\bin\Release\net10.0"
set "PACKED=%HERE%bin\%MODNAME%"
set "NETHOST=%HERE%build\nethost\runtimes\win-x64\native\nethost.dll"
set "DST=%~1\plugins\%MODNAME%"

if not exist "%PACKED%\%MODNAME%.dll" (
    echo run build.bat first
    exit /b 1
)

if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"

copy /y "%PACKED%\*.dll" "%DST%" >nul
copy /y "%NETHOST%" "%DST%" >nul
copy /y "%PACKED%\manifest.json" "%DST%" >nul

if /i "%~2"=="shim-only" (
    echo deployed shim-only to %DST%
    endlocal
    exit /b 0
)

if not exist "%MANAGED%\Hosihikari.PluginManager.dll" (
    echo build Hosihikari.PluginManager in Release first
    exit /b 1
)

copy /y "%MANAGED%\*.dll" "%DST%" >nul
copy /y "%MANAGED%\*.json" "%DST%" >nul
REM PDBs too: without them the logged stacks carry no file:line.
copy /y "%MANAGED%\*.pdb" "%DST%" >nul 2>&1

echo deployed to %DST%
endlocal
