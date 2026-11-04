@echo off
rem Builds dist\SkillIssue-Plugins-Setup-<version>.exe from the Release VST3 builds (run build-win.bat first).
setlocal
cd /d "%~dp0"
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup 6 not found. Install it: winget install JRSoftware.InnoSetup
    exit /b 1
)
"%ISCC%" /Q SkillIssue.iss || exit /b 1
echo Installer written to dist\
