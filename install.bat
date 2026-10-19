@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Copies the plugin into the standard VST3 folder (needs admin, so it asks for it).

set "SRC=%~dp0dist\Monie MIDI Mimic.vst3"
if not exist "%SRC%" set "SRC=%~dp0Monie MIDI Mimic.vst3"
if not exist "%SRC%" (
    echo  Couldn't find "Monie MIDI Mimic.vst3" next to this file or in a "dist" folder.
    echo  Build it first with build.bat, or download it from the GitHub Actions run.
    pause
    exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
    echo  Asking Windows for permission to copy into Program Files...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
if exist "%DEST%\Monie MIDI Mimic.vst3" rmdir /s /q "%DEST%\Monie MIDI Mimic.vst3"
xcopy /e /i /y /q "%SRC%" "%DEST%\Monie MIDI Mimic.vst3" >nul
if errorlevel 1 (
    echo  Copy failed. Is FL Studio open? Close it and try again.
    pause
    exit /b 1
)

echo.
echo  Installed to: %DEST%\Monie MIDI Mimic.vst3
echo.
echo  In FL Studio: Options ^> Manage plugins ^> Find installed plugins,
echo  then add "Monie MIDI Mimic" from the Channel Rack (+) menu.
echo.
pause
