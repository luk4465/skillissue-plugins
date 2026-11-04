@echo off
rem Builds SI Reverb + SI Riser VST3 (Release, x64). Needs Visual Studio 2022 (C++ workload) and CMake.
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Kitware.CMake*") do for /d %%E in ("%%D\cmake-*") do set "PATH=%%E\bin;%PATH%"
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build --config Release --parallel --target SIReverb_VST3 SIRiser_VST3 SIEQ_VST3 SITune_VST3 SI76_VST3 SISaturn_VST3 SIEcho_VST3 SIRack_VST3 SIPitch_VST3 || goto :fail

echo.
echo Built:
echo   build\SIReverb_artefacts\Release\VST3\SI Reverb.vst3
echo   build\SIRiser_artefacts\Release\VST3\SI Riser.vst3
echo Run install-win.bat to copy them to C:\Program Files\Common Files\VST3
exit /b 0

:fail
echo Build failed.
exit /b 1
