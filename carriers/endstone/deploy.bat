@echo off
REM Copy the built Endstone plugin and the managed core into a server folder.
REM
REM   deploy.bat <bedrock server folder>
REM
REM That folder is the one holding bedrock_server.exe; Endstone launches it with
REM its working directory set there, so both `plugins/` and the search path are
REM relative to it. Keep this file ASCII only.
setlocal
if "%~1"=="" (
    echo usage: deploy.bat ^<bedrock server folder^>
    exit /b 1
)

set "NAME=hosihikari"
set "HERE=%~dp0"
set "MANAGED=%HERE%..\..\..\Hosihikari.PluginManager\src\bin\Release\net10.0"
set "PLUGIN=%HERE%build\Release\endstone_hosihikari.dll"
set "NETHOST=%HERE%build\nethost\runtimes\win-x64\native\nethost.dll"
set "DST=%~1\plugins\%NAME%"

if not exist "%PLUGIN%" (
    echo run build.bat first
    exit /b 1
)
if not exist "%MANAGED%\Hosihikari.PluginManager.dll" (
    echo build Hosihikari.PluginManager in Release first
    exit /b 1
)

if not exist "%~1\plugins" mkdir "%~1\plugins"
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"

REM The plugin itself has to sit flat in plugins/: that is where Endstone looks.
copy /y "%PLUGIN%" "%~1\plugins\" >nul

REM nethost.dll goes next to bedrock_server.exe. Endstone loads plugins with a
REM plain LoadLibrary, so the shim's own dependency is resolved from the
REM application directory -- there is no per-mod directory on PATH here, unlike
REM on LeviLamina.
copy /y "%NETHOST%" "%~1\" >nul

copy /y "%MANAGED%\*.dll" "%DST%" >nul
copy /y "%MANAGED%\*.json" "%DST%" >nul

echo deployed to %~1
endlocal
