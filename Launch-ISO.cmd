@echo off
setlocal
rem Keep this launcher beside the downloaded ISO. Uses your configured PCSX2 BIOS.
set "SLENDY_EMULATOR=%ProgramFiles%\PCSX2\pcsx2-qt.exe"
if not "%~1"=="" set "SLENDY_EMULATOR=%~1"
set "SLENDY_ISO=%~dp0Slendytubbies-PS2-v0.1.0.iso"
if not exist "%SLENDY_ISO%" set "SLENDY_ISO=%~dp0dist\release\v0.1.0\Slendytubbies-PS2-v0.1.0.iso"
if not exist "%SLENDY_EMULATOR%" (
    echo PCSX2 was not found. Drag pcsx2-qt.exe onto this launcher, or install PCSX2.
    pause
    exit /b 1
)
if not exist "%SLENDY_ISO%" (
    echo Place Launch-ISO.cmd and Slendytubbies-PS2-v0.1.0.iso in the same folder.
    pause
    exit /b 1
)
start "" "%SLENDY_EMULATOR%" -fastboot -nofullscreen "%SLENDY_ISO%"
