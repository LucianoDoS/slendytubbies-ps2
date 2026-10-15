# Slendytubbies V1 for PlayStation 2

An experimental native C port of Slendytubbies V1, using the original Unity
scene data through an offline conversion pipeline. The PS2 runs a standalone
ELF built with PS2SDK and gsKit; Unity is not required on the console.

**Status:** playable single-player prototype. Previous builds have booted on a
real PS2. Current changes are checked in PCSX2 and still require hardware
retesting. This is an unofficial fan project.

## Features

- Original 3D menu scenes, submenus, music, animated fire and smoke.
- Day, dusk and night, first-person movement, jumping, crouching and running.
- Original player model and flashlight, including the continuously playing
  walking animation visible when looking down.
- Original terrain layers, fog, 3D trees, foliage cutouts and nearby grass.
- Ten custards, temporary pickup messages, enemy encounters, defeat and the
  original victory scene.
- Tinky Winky turns toward the player, screams on sight and reappears nearby
  during close encounters. Distant sightings do not apply damage.
- Noo-Noo uses the original forward steering and smooth rotation around its
  single waypoint, rather than sliding back and forth without turning.
- DualShock 2 controls, embedded assets and sound in one ELF, and a nominal
  30 FPS target at NTSC 60 Hz.

Multiplayer networking is not implemented. Co-op and Versus menu pages remain
visible and show an unavailable-network notice. Green night vision has been
removed; R2 is unassigned.

## Build

The repository contains the port code and conversion tools. **Original game
assets, generated headers/banks, BIOS files and game binaries are not included
in the source archive.** Supply `SlendytubbiesV1Source.unitypackage` locally.

Windows prerequisites: Python 3.11, PowerShell, WSL Ubuntu, and a ps2dev toolchain.

```powershell
python -m pip install -r tools/requirements.txt
python tools/extract_unitypackage.py SlendytubbiesV1Source.unitypackage original
wsl bash tools/setup_ps2dev.sh
./build.ps1
```

Output: `dist/SLENDY.ELF`. To rebuild C without re-exporting assets:

```powershell
wsl bash tools/build_ps2.sh
```

See [BUILDING.md](docs/BUILDING.md) for Linux builds, custom paths and asset
provenance. The generated debug font can differ between operating systems.

## Run

Download the **ELF ZIP** or **ISO** from the
[GitHub releases](https://github.com/LucianoDoS/slendytubbies-ps2/releases).
Extract the ELF ZIP to use Free McBoot; open the ISO as a disc in PCSX2.
SHA-256 checksums and a separate source-only ZIP accompany each release.

For a visible PCSX2 window, supply your own BIOS:

```powershell
./play_pcsx2.ps1 -BiosDirectory 'C:/path/to/your/bios' -BiosFile 'your-bios.bin'
```

`-Emulator` overrides the PCSX2 executable path. The launcher keeps test
configuration and writable BIOS companions under the ignored `ps2/play/`
directory, separate from your normal emulator settings.

On a PS2 with Free McBoot, use wLaunchELF/uLaunchELF to run
`mass:/SLENDY/SLENDY.ELF` from an MBR/FAT32 USB drive. The release uses
512×448 NTSC 60 Hz. `--pal` is a diagnostic 50 Hz mode and is not certified
for the 30 FPS target.

The ISO contains an ISO9660 filesystem and `SYSTEM.CNF` that boots the same
standalone ELF. Its disc boot is tested in PCSX2. Physical ISO/disc and OPL
loading have not been validated; the documented hardware path is the ELF
through Free McBoot/wLaunchELF. No BIOS is distributed.

After completing the native checks, `tools/prepare_usb.ps1 -UsbDrive D` creates
the local USB package, checks matching ELF hashes, preserves an existing
different ELF and verifies the copy. It is separate from source packaging.

## Controls

| Action | DualShock 2 | PCSX2 launcher keyboard |
|---|---|---|
| Navigate menus / choose time of day | D-pad | Arrow keys |
| Confirm / jump | Cross | K |
| Back / crouch | Circle | L |
| Move | Left stick | W A S D |
| Look | Right stick | T F G H |
| Run | L1 or L3 | Q or 2 |
| Flashlight | R1 | E |
| Map | Triangle | I |
| Pause / resume | Start | Enter |
| Return to menu while paused | Select | Backspace |

Look down with the right stick or G to see the player model. Help retains
the original PC artwork; use the table above for PS2 controls.

## Validation and performance

```powershell
./tools/test_host.ps1
./tools/test_pcsx2.ps1 -NooNoo -Visible
./tools/test_pcsx2.ps1 -Benchmark -Capture -Require30 -Visible -TimeoutSeconds 180
./tools/test_pcsx2.ps1 -Walk -Require30 -Visible -TimeoutSeconds 180
```

An asset-free steering test and syntax checks can run without the game package;
the GitHub Actions workflow runs those checks. Full gameplay and GS checks
require local assets and a BIOS. [TESTING.md](docs/TESTING.md) explains the
remaining native checks and what each type of evidence establishes.
The latest scoped results are recorded in [VALIDATION.md](docs/VALIDATION.md).

The existing controlled PCSX2 suites target at least 29.85 measured FPS for
nominal NTSC 30 FPS. The walking suite additionally requires zero frames over
36 ms after its one-second warmup per route. These emulator measurements do
not establish the frame rate of a physical console or a complete playthrough.

Optimizations include spatial collision queries, cached mesh bounds and actor
transforms, VU0 vertex transforms, tree LODs, reusable menu geometry and simpler
fully fogged terrain. Physics remains at 60 Hz. Ground UVs are local to each
quad to reduce GS precision loss, and all four ground textures use 128×128.

## Source distribution

```powershell
./tools/prepare_source.ps1
```

This creates `dist/SLENDY_PS2_SOURCE.zip` with an allowlisted English repository,
MIT license, credits, contribution guide, CI workflow and a file-hash manifest.
The packager works locally. Release assets are kept outside Git history.
See [RELEASING.md](docs/RELEASING.md) to prepare the ELF ZIP, ISO and checksums.

## Known approximations

This is a native adaptation, not a Unity runtime. Lighting, particles and some
physics are approximated; foliage and textures have PS2-specific detail limits.
The close-attack distance is adapted to 5 m from the reference playthrough;
the original serialized scene uses 10 m. Full Rigidbody character stacking,
Po's hanging animation and Noo-Noo's separate idle hose animation are not
implemented. The source idle clip has no independent wheel rotation tracks.

The previous hardware report described clay-like ground textures. Local UVs
and higher-resolution ground textures address likely causes, but that fix and
the latest Noo-Noo changes still need confirmation on the same console.

## License and credits

Original port contributions are licensed under [MIT](LICENSE). Original game
content and third-party dependencies retain their own ownership and terms;
see [CREDITS.md](CREDITS.md). The game is not relicensed by this project.
