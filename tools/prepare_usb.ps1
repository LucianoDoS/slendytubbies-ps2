param([ValidatePattern('^[D-Z]$')][string]$UsbDrive = 'D')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$elf = Join-Path $repo 'dist/SLENDY.ELF'
$hash = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
$walkPath = (Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/last-walk-report.txt') -Raw).Trim()
$benchPath = (Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/last-benchmark-report.txt') -Raw).Trim()
$walk = Get-Content -LiteralPath $walkPath -Raw | ConvertFrom-Json
$bench = Get-Content -LiteralPath $benchPath -Raw | ConvertFrom-Json
$fidelity = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $benchPath) 'fidelity-check.json') -Raw | ConvertFrom-Json
$enemyElf = Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/elf-enemy.json') -Raw | ConvertFrom-Json
$enemyLog = Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/pcsx2-enemy.log') -Raw
$audioElf = Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/elf-audio.json') -Raw | ConvertFrom-Json
$audioLog = Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/pcsx2-audio.log') -Raw
$noonooPath = (Get-Content -LiteralPath (Join-Path $repo 'ps2/qa/last-noonoo-capture.txt') -Raw).Trim()
$noonoo = Get-Content -LiteralPath (Join-Path $noonooPath 'noonoo-check.json') -Raw | ConvertFrom-Json
if (-not $walk.target_met -or -not $walk.steady_target_met -or @($walk.walk_routes).Count -ne 8 -or $walk.elf_sha256 -ne $hash) { throw 'The current ELF has not passed all eight continuous walks' }
if (-not $bench.target_met -or @($bench.scenes).Count -ne 34 -or $bench.elf_sha256 -ne $hash -or -not $fidelity.passed -or $fidelity.elf_sha256 -ne $hash) { throw 'The current ELF has not passed the complete native visual benchmark' }
if ($enemyElf.Hash -ne $hash -or $enemyLog -notmatch 'STPS SMOKE PASS enemy' -or $enemyLog -notmatch 'STPS ENEMY DISTANT PASS') { throw 'The current ELF has not passed native encounters' }
if ($audioElf.Hash -ne $hash -or $audioLog -notmatch 'STPS SMOKE PASS audio[^\r\n]*flashlight=2 reserved_ignored=2') { throw 'The current ELF has not passed flashlight and retired R2 input checks' }
if (-not $noonoo.passed -or $noonoo.elf_sha256 -ne $hash) { throw 'The current ELF has not passed native Noo-Noo movement and rendering checks' }

$stage = Join-Path $repo ('dist/usb/' + $hash.Substring(0,12) + '/SLENDY')
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $elf -Destination (Join-Path $stage 'SLENDY.ELF')
$info = [ordered]@{
    game = 'Slendytubbies V1 - PS2 experimental'
    built_utc = [DateTime]::UtcNow.ToString('o')
    elf_sha256 = $hash
    elf_bytes = (Get-Item -LiteralPath $elf).Length
    video = 'NTSC 60 Hz, 512x448'
    assets_embedded = $true
    ground_textures = '128x128, local quad UVs with source repeat phase preserved'
    green_night_vision = $false
    noonoo_motion = 'Source quaternion steering and forward translation around a single waypoint'
    real_ps2_tested = $false
    hardware_retest = 'Ground UVs/detail, Noo-Noo circular steering and cached render bounds. Pending real PS2 retest.'
    walking = $walk
    fixed_views = $bench
    visual_checks = $fidelity
    noonoo_check = $noonoo
}
$info | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $stage 'VERSION.json') -Encoding utf8
"$hash  SLENDY.ELF" | Set-Content -LiteralPath (Join-Path $stage 'SHA256.txt') -Encoding ascii
@'
SLENDYTUBBIES V1 - PHYSICAL PS2 TEST

1. Safely eject the USB drive from Windows and connect it to the PS2.
2. Start Free McBoot and open wLaunchELF/uLaunchELF.
3. Browse to mass:/SLENDY/SLENDY.ELF and launch it.
4. Wait for the 3D menu and music. All game data is embedded;
   initial loading from USB may take some time.

DUALSHOCK 2 CONTROLS
D-pad: menus. Cross: confirm/jump. Circle: back/crouch.
Left stick: move. Right stick: look.
L1/L3: run. R1: flashlight. Triangle: map. R2: unassigned.
Start: pause. Select while paused: main menu.

This build uses NTSC 60 Hz (512x448), with a nominal 30 FPS target.
PCSX2 checks cover continuous walks, collisions, menus, audio,
enemy encounters and Noo-Noo movement. Physical validation is pending.

CHANGES TO RETEST ON HARDWARE
Ground uses local texture coordinates and 128x128 source textures.
Green night vision and its sound are removed.
Noo-Noo turns and travels around its original single waypoint.
Mesh bounds and actor transforms are cached to reduce repeated work.
These changes still need checking on the physical console.

Check menu/game loading, controller input, sound, forest and house movement,
ground detail, foliage transparency and Noo-Noo's circular path.
When reporting a problem, include the ELF hash from SHA256.txt,
console SCPH model, video cable/mode and a repeatable route.

Launcher documentation:
https://github.com/ps2homebrew/wLaunchELF
'@ | Set-Content -LiteralPath (Join-Path $stage 'README_PS2.txt') -Encoding utf8
$zip = Join-Path $repo 'dist/SLENDY_PS2_USB.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $zip -Force

$volume = Get-Volume -DriveLetter $UsbDrive
$disk = Get-Partition -DriveLetter $UsbDrive | Get-Disk
if ($volume.FileSystem -ne 'FAT32' -or $disk.BusType -ne 'USB' -or $disk.PartitionStyle -ne 'MBR') { throw 'The selected drive must be an MBR/FAT32 USB drive' }
$target = [IO.Path]::GetFullPath((Join-Path "${UsbDrive}:\" 'SLENDY'))
New-Item -ItemType Directory -Path $target -Force | Out-Null
$destinationElf = Join-Path $target 'SLENDY.ELF'
if (Test-Path -LiteralPath $destinationElf) {
    $previousHash = (Get-FileHash -LiteralPath $destinationElf -Algorithm SHA256).Hash
    if ($previousHash -ne $hash) { Copy-Item -LiteralPath $destinationElf -Destination (Join-Path $target ('SLENDY_PREV_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.ELF')) }
}
foreach ($name in @('SLENDY.ELF','VERSION.json','SHA256.txt','README_PS2.txt')) { Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $target $name) -Force }
$usbHash = (Get-FileHash -LiteralPath $destinationElf -Algorithm SHA256).Hash
if ($usbHash -ne $hash) { throw 'USB verification failed: the ELF differs from the tested build' }
$report = [ordered]@{ verified = $true; usb_path = $destinationElf; volume_label = $volume.FileSystemLabel; usb_model = $disk.FriendlyName; sha256 = $usbHash; bytes = (Get-Item -LiteralPath $destinationElf).Length; local_zip = $zip; real_ps2_tested = $false }
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $repo 'ps2/qa/usb-copy.json') -Encoding utf8
$report | ConvertTo-Json
