@echo off
REM Copy the built Shim and, for source deployments, the managed core into a server's plugins dir.
REM
REM   deploy.bat <server root> [shim-only] [version]
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
set "DST=%~1\mods\%MODNAME%"

if not exist "%PACKED%\%MODNAME%.dll" (
    echo run build.bat first
    exit /b 1
)

if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"

copy /y "%PACKED%\*.dll" "%DST%" >nul
copy /y "%NETHOST%" "%DST%" >nul
copy /y "%PACKED%\manifest.json" "%DST%" >nul

REM The parent's i18n reads <mod>/lang; every release carries all six catalogs.
for %%f in (ar en es fr ru zh) do (
    if not exist "%HERE%lang\%%f.json" (
        echo carrier language file %%f.json is missing under "%HERE%lang"
        exit /b 1
    )
)
xcopy /e /i /y "%HERE%lang" "%DST%\lang" >nul

REM Carrier manifest (ADR-0065): the core reads it to find carrier components and the
REM service configurator. Keep this block ASCII only, like the rest of this file.
set "CARRIERVERSION=%~3"
if "%CARRIERVERSION%"=="" set "CARRIERVERSION=0.0.0-local"
(
  echo {
  echo   "schemaVersion": 1,
  echo   "name": "hosihikari",
  echo   "version": "%CARRIERVERSION%",
  echo   "entry": "hosihikari.dll",
  echo   "loader": "assembly",
  echo   "kind": "carrier",
  echo   "apiVersion": 3,
  echo   "dependencies": [],
  echo   "carrier": { "sharedAssemblies": [], "components": [] }
  echo }
) > "%DST%\hosihikari.json"

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
REM The localized resources live in culture subdirectories (zh/).
for /d %%d in ("%MANAGED%\*") do xcopy /e /i /y "%%d" "%DST%\%%~nxd" >nul

echo deployed to %DST%
endlocal
