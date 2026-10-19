@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo  ===== Monie MIDI Mimic : Windows build =====
echo.

rem ---- Git (CMake uses it to download JUCE the first time) -------------------
where git >nul 2>nul
if errorlevel 1 (
    echo  [!] Git was not found. Install it from https://git-scm.com/download/win
    echo      then run this file again.
    goto :fail
)

rem ---- Visual Studio with the C++ tools ---------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo  [!] Visual Studio was not found.
    echo      Install "Visual Studio Community" from https://visualstudio.microsoft.com/
    echo      and tick the workload "Desktop development with C++".
    goto :fail
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo  [!] Visual Studio is installed but the C++ tools are missing.
    echo      Open "Visual Studio Installer", click Modify, and tick
    echo      "Desktop development with C++".
    goto :fail
)

rem ---- CMake: use one on PATH, otherwise the copy that ships with Visual Studio -
where cmake >nul 2>nul
if errorlevel 1 set "PATH=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 (
    echo  [!] CMake was not found. Install it from https://cmake.org/download/
    goto :fail
)

echo  Visual Studio: %VSPATH%
echo.
echo  Configuring (the first run downloads JUCE, this can take a few minutes)...
cmake -S . -B build -A x64
if errorlevel 1 goto :fail

echo.
echo  Building...
cmake --build build --config Release --parallel --target MonieMidiMimic_VST3 MonieMidiMimic_Standalone
if errorlevel 1 goto :fail

if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /y /q "build\MonieMidiMimic_artefacts\Release\VST3\Monie MIDI Mimic.vst3" "dist\Monie MIDI Mimic.vst3" >nul
copy /y "build\MonieMidiMimic_artefacts\Release\Standalone\Monie MIDI Mimic.exe" "dist\" >nul
copy /y install.bat "dist\" >nul

echo.
echo  ===== Done! =====
echo  Plugin:      dist\Monie MIDI Mimic.vst3
echo  Standalone:  dist\Monie MIDI Mimic.exe
echo.
echo  Next: run install.bat to copy the plugin into your VST3 folder,
echo  then in FL Studio: Options ^> Manage plugins ^> Find installed plugins.
echo.
pause
exit /b 0

:fail
echo.
echo  Build stopped. Scroll up for the message above.
pause
exit /b 1
