param(
    [string]$Emulator = 'C:/Program Files/PCSX2/pcsx2-qt.exe',
    [string]$BiosDirectory = 'C:/Program Files/PCSX2/bios',
    [string]$BiosFile = 'ps2-0170ad-20030325.bin',
    [int]$TimeoutSeconds = 90,
    [switch]$Pal,
    [switch]$Menus,
    [switch]$Models,
    [switch]$Fire,
    [switch]$Enemy,
    [switch]$Audio,
    [switch]$NooNoo,
    [switch]$Win,
    [switch]$Surfaces,
    [switch]$Benchmark,
    [switch]$Walk,
    [switch]$Require30,
    [switch]$GameOnly,
    [switch]$Profile,
    [int]$Focus = -1,
    [switch]$Capture,
    [string]$IsoPath = '',
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if ($Fire -or $NooNoo) { $Capture = $true }
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
$testRoot = Join-Path $PWD 'ps2/qa/pcsx2'
. (Join-Path $PSScriptRoot 'prepare_bios.ps1')
$writableBios = Initialize-WritableBios -SourceDirectory $BiosDirectory -FileName $BiosFile -Destination (Join-Path $testRoot 'bios')
$iniDir = Join-Path $testRoot 'PCSX2/inis'
New-Item -ItemType Directory -Force $iniDir | Out-Null
$testName = if ($NooNoo) { 'noonoo' } elseif ($Walk) { 'walk' } elseif ($Audio) { 'audio' } elseif ($Surfaces) { 'surfaces' } elseif ($Win) { 'victory' } elseif ($Enemy) { 'enemy' } elseif ($Benchmark -and $Pal) { 'benchmark-pal' } elseif ($Benchmark) { 'benchmark' } elseif ($Fire -and $Pal) { 'menufire-pal' } elseif ($Fire) { 'menufire' } elseif ($Models -and $Pal) { 'models-pal' } elseif ($Models) { 'models' } elseif ($Menus -and $Pal) { 'menus-pal' } elseif ($Menus) { 'menus' } elseif ($Pal) { 'pal' } else { 'game' }
if ($IsoPath) {
    if (-not ($Menus -or $testName -eq 'game')) { throw 'ISO validation supports menu and gameplay smoke checks' }
    $testName = 'iso-' + $testName
}
$captureRoot = Join-Path $PWD ('ps2/qa/capture-' + $testName + '/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $captureRoot | Out-Null
$frameInterval = if ($Fire -or $Enemy -or $Win -or $Surfaces -or $NooNoo) { 8 } else { 30 }
$captureCount = if ($Benchmark) { 10000 } else { 3000 }
$dumpOptions = if ($Capture) { @"
DumpGSData = true
SaveFrame = true
SaveRT = false
SaveDrawCount = -1
SaveFrameStart = 0
SaveFrameCount = $captureCount
SaveFrameBy = $frameInterval
HWDumpDirectory = $captureRoot
SWDumpDirectory = $captureRoot
"@ } else { '' }
$iniText = @"
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
HostFs = true
[EmuCore/Speedhacks]
EECycleRate = 0
EECycleSkip = 0
vuThread = false
[EmuCore/GS]
Renderer = 12
upscale_multiplier = 1
$dumpOptions
[Logging]
EnableFileLogging = true
EnableEEConsole = true
EnableIOPConsole = true
"@
$iniText | Set-Content -LiteralPath (Join-Path $iniDir 'PCSX2.ini') -Encoding utf8
$elfPath = (Resolve-Path dist/SLENDY.ELF).Path
$bootPath = if ($IsoPath) { (Resolve-Path -LiteralPath $IsoPath).Path } else { $elfPath }
if ($Capture) { $captureRoot | Set-Content -LiteralPath "ps2/qa/last-$testName-capture.txt" }
Get-FileHash -LiteralPath $elfPath -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath "ps2/qa/elf-$testName.json"
$logName = if ($testName -eq 'game') { 'smoke' } else { $testName }
$logPath = Join-Path $PWD "ps2/qa/pcsx2-$logName.log"
if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath }
$mode = if ($NooNoo) { '--noonootest' } elseif ($Walk) { '--walkbench' } elseif ($Audio) { '--audiotest' } elseif ($Surfaces) { '--surfaces' } elseif ($Win) { '--wintest' } elseif ($Enemy) { '--enemytest' } elseif ($Benchmark) { '--bench' } elseif ($Fire) { '--menufire' } elseif ($Models) { '--models' } elseif ($Menus) { '--menus' } else { '--smoke' }
$gameArgs = '"' + $mode + $(if ($Pal) { ' --pal' } else { '' }) + $(if ($GameOnly) { ' --gameonly' } else { '' }) + $(if ($Profile) { ' --profile' } else { '' }) + $(if ($Focus -ge 0) { ' --benchfocus=' + $Focus } else { '' }) + '"'
$arguments = @('-nogui', '-datapath', ('"' + $testRoot + '"'), '-logfile', ('"' + $logPath + '"'), '-gameargs', $gameArgs, ('"' + $bootPath + '"'))
$windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
$testProcess = Start-Process -FilePath $Emulator -ArgumentList $arguments -WindowStyle $windowStyle -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $logPath) {
            $log = Get-Content -LiteralPath $logPath -Raw
            if ($log -match 'STPS SMOKE PASS') {
                if ($IsoPath) {
                    if ($log -notmatch 'cdrom0:[\\/]SLENDY\.ELF;1') { throw 'ISO boot path was not observed in PCSX2' }
                    [ordered]@{
                        passed = $true
                        iso_sha256 = (Get-FileHash -LiteralPath $bootPath -Algorithm SHA256).Hash
                        elf_sha256 = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash
                        mode = $mode
                        disc_boot_observed = $true
                    } | ConvertTo-Json | Set-Content -LiteralPath "ps2/qa/$testName.json" -Encoding utf8
                }
                if ($NooNoo) {
                    & python (Join-Path $PSScriptRoot 'check_noonoo.py') $captureRoot $logPath (Join-Path $PWD 'ps2/qa/elf-noonoo.json')
                    if ($LASTEXITCODE -ne 0) { throw 'Native Noo-Noo movement or rendering check failed' }
                }
                if ($Benchmark -or $Walk) {
                    $performancePath = Join-Path $captureRoot 'performance.json'
                    $performancePath | Set-Content -LiteralPath "ps2/qa/last-$testName-report.txt"
                    $checkerArgs = @($logPath, $performancePath, (Join-Path $PWD "ps2/qa/elf-$testName.json"))
                    if ($Require30) { $checkerArgs += '--require-target' }
                    & python (Join-Path $PSScriptRoot 'check_performance.py') @checkerArgs
                    if ($LASTEXITCODE -ne 0) { throw 'Native 30 FPS target check failed' }
                }
                if ($Fire) {
                    & python (Join-Path $PSScriptRoot 'check_menu_fire.py') $captureRoot
                    if ($LASTEXITCODE -ne 0) { throw 'Native menu fire animation check failed' }
                }
                if ($Models -and $Capture) {
                    & python (Join-Path $PSScriptRoot 'check_flashlight.py') $captureRoot
                    if ($LASTEXITCODE -ne 0) { throw 'Native flashlight floor projection check failed' }
                }
                if ($Menus) {
                    & python (Join-Path $PSScriptRoot 'check_menu_audio.py') $logPath
                    if ($LASTEXITCODE -ne 0) { throw 'Native menu audio delivery check failed' }
                }
                if ($Win -and $Capture) {
                    & python (Join-Path $PSScriptRoot 'check_native_victory.py') $captureRoot $logPath (Join-Path $PWD 'ps2/qa/elf-victory.json')
                    if ($LASTEXITCODE -ne 0) { throw 'Native victory flow or GS scene check failed' }
                }
                if ($Surfaces -and $Capture) {
                    & python (Join-Path $PSScriptRoot 'check_native_surfaces.py') $captureRoot $logPath (Join-Path $PWD 'ps2/qa/elf-surfaces.json')
                    if ($LASTEXITCODE -ne 0) { throw 'Native roof or body animation check failed' }
                }
                $log -split "`n" | Where-Object { $_ -match 'STPS ' }
                exit 0
            }
            if ($log -match 'STPS VRAM ERROR') { throw 'GS texture allocation failed' }
            if ($log -match 'STPS READY[\s\S]*?TLB Miss') { throw 'Native memory access failed; inspect the PCSX2 log' }
            if ($log -match 'STPS SMOKE FAIL') { throw "Native $testName test failed; inspect $logPath" }
        }
        if ($testProcess.HasExited) { throw 'PCSX2 exited before the smoke test completed' }
        Start-Sleep -Milliseconds 500
    }
    throw "PCSX2 $testName test timed out; inspect $logPath"
} finally {
    if (-not $testProcess.HasExited) { Stop-Process -Id $testProcess.Id }
}
