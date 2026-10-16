@echo off
REM Copy the built Shim and the managed core into a server's plugins dir.
REM
REM   deploy.bat <server root>
REM
REM The managed core is taken from a sibling checkout of Hosihikari.PluginManager;
REM that is where the component the shim loads is built. Keep this file ASCII only.
setlocal
if "%~1"=="" (
    echo usage: deploy.bat ^<server root^>
    exit /b 1
)

set "NAME=Hosihikari.Shim.LeviLamina"
set "HERE=%~dp0"
set "MANAGED=%HERE%..\..\..\Hosihikari.PluginManager\src\bin\Release\net10.0"
set "DST=%~1\plugins\%NAME%"

if not exist "%HERE%build\windows\x64\release\%NAME%.dll" (
    echo run build.bat first
    exit /b 1
)
if not exist "%MANAGED%\Hosihikari.PluginManager.dll" (
    echo build Hosihikari.PluginManager in Release first
    exit /b 1
)

if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"

copy /y "%HERE%build\windows\x64\release\%NAME%.dll" "%DST%" >nul
copy /y "%HERE%build\nethost\runtimes\win-x64\native\nethost.dll" "%DST%" >nul
copy /y "%MANAGED%\*.dll" "%DST%" >nul
copy /y "%MANAGED%\*.json" "%DST%" >nul
copy /y "%HERE%manifest.json" "%DST%" >nul

echo deployed to %DST%
endlocal
