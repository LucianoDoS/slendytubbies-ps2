param(
    [string]$Emulator = 'C:/Program Files/PCSX2/pcsx2-qt.exe',
    [string]$BiosDirectory = 'C:/Program Files/PCSX2/bios',
    [string]$BiosFile = 'ps2-0170ad-20030325.bin',
    [string]$IsoPath = ''
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$playRoot = Join-Path $PSScriptRoot 'ps2/play'
. (Join-Path $PSScriptRoot 'tools/prepare_bios.ps1')
$writableBios = Initialize-WritableBios -SourceDirectory $BiosDirectory -FileName $BiosFile -Destination (Join-Path $playRoot 'bios')
$iniDirectory = Join-Path $playRoot 'PCSX2/inis'
New-Item -ItemType Directory -Force $iniDirectory | Out-Null
$iniPath = Join-Path $iniDirectory 'PCSX2.ini'
# Keep the game's test configuration separate from the user's PCSX2 settings.
# Create once so controller changes made in this instance are retained.
if (-not (Test-Path -LiteralPath $iniPath)) {
@"
[UI]
SettingsVersion = 1
SetupWizardIncomplete = false
PauseOnFocusLoss = false
StartPaused = false
[Folders]
Bios = $writableBios
[Filenames]
BIOS = $BiosFile
[EmuCore]
EnableFastBoot = true
EnableDiscordPresence = false
[EmuCore/GS]
Renderer = 12
upscale_multiplier = 1
OsdShowFPS = true
OsdShowSpeed = true
[Logging]
EnableFileLogging = true
EnableEEConsole = true
EnableIOPConsole = true
[Pad1]
Type = DualShock2
Up = Keyboard/Up
Right = Keyboard/Right
Down = Keyboard/Down
Left = Keyboard/Left
Triangle = Keyboard/I
Circle = Keyboard/L
Cross = Keyboard/K
Square = Keyboard/J
Select = Keyboard/Backspace
Start = Keyboard/Return
L1 = Keyboard/Q
L2 = Keyboard/1
R1 = Keyboard/E
R2 = Keyboard/3
L3 = Keyboard/2
R3 = Keyboard/4
LUp = Keyboard/W
LRight = Keyboard/D
LDown = Keyboard/S
LLeft = Keyboard/A
RUp = Keyboard/T
RRight = Keyboard/H
RDown = Keyboard/G
RLeft = Keyboard/F
"@ | Set-Content -LiteralPath $iniPath -Encoding utf8
}
# Repair older test configurations which pointed at read-only Program Files.
$iniText = Get-Content -LiteralPath $iniPath -Raw
$iniText = [regex]::Replace($iniText, '(?m)^Bios\s*=.*$', [System.Text.RegularExpressions.MatchEvaluator]{ param($m) "Bios = $writableBios" })
$iniText | Set-Content -LiteralPath $iniPath -Encoding utf8
$bootPath = if ($IsoPath) { (Resolve-Path -LiteralPath $IsoPath).Path } else { (Resolve-Path dist/SLENDY.ELF).Path }
$logPath = Join-Path $playRoot 'pcsx2-play.log'
$arguments = @('-nogui', '-datapath', ('"' + $playRoot + '"'), '-logfile', ('"' + $logPath + '"'), ('"' + $bootPath + '"'))
Start-Process -FilePath $Emulator -ArgumentList $arguments -WindowStyle Normal -PassThru | Select-Object Id,ProcessName
