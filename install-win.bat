@echo off
rem Copies the built VST3 plug-ins to the system VST3 folder (asks for admin rights).
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
cd /d "%~dp0"
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
for %%P in ("SIReverb:SI Reverb" "SIRiser:SI Riser" "SIEQ:SI EQ" "SITune:SI Tune" "SI76:SI 76" "SISaturn:SI Saturn" "SIEcho:SI Echo" "SIRack:SI Rack" "SIPitch:SI Pitch") do call :copyone %%P
echo Installed to %DEST%
echo In Ableton: Settings ^> Plug-Ins ^> enable "VST3 Plug-In System Folders" ^> Rescan.
pause
exit /b 0

:copyone
for /f "tokens=1,2 delims=:" %%A in (%1) do (
    if exist "build\%%A_artefacts\Release\VST3\%%B.vst3" (
        if exist "%DEST%\%%B.vst3" rmdir /s /q "%DEST%\%%B.vst3"
        xcopy /e /i /y "build\%%A_artefacts\Release\VST3\%%B.vst3" "%DEST%\%%B.vst3" >nul
    )
)
exit /b 0
