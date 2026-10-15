# Testing

## Without game assets

```sh
python -m compileall -q tools
gcc -std=c99 -O2 -Wall -Wextra -Werror -Ips2/src ps2/tests/steering.c -lm -o steering-test
./steering-test
```

The steering test runs the production quaternion and model-transform code for
60 simulated seconds. It checks unit rotations, rigid transforms, bounded
orbits and repeated turns. On Linux, CI also enables AddressSanitizer and
UndefinedBehaviorSanitizer. CI validates Python, Bash and PowerShell syntax and
builds the source-only package; it does not build or boot the game.

## Host harness

After exporting local assets, run `./tools/test_host.ps1` with Windows GCC on
PATH. It checks movement, frame-time integration, menus, terrain, collision,
enemy encounters, Noo-Noo steering, pickups and victory. It also renders reference
images through a host rasterizer. These are not PCSX2 or hardware screenshots.

`tools/check_collision_grid.ps1` compares the spatial collision index with a
complete mesh scan. `python tools/check_tree_lods.py` checks that distant leaf
cards retain original triangle positions and UVs. The terrain UV test verifies
327,680 samples, preserving repeat phase while keeping coordinate magnitudes
small enough for the GS.

## Native PCSX2 checks

Provide a BIOS with `-BiosDirectory` and `-BiosFile` if the local defaults do not
match your installation. Add `-Visible` to display an automatic test; it closes
after completion. `play_pcsx2.ps1` leaves an interactive game window open.

```powershell
./tools/test_pcsx2.ps1 -NooNoo -Visible
./tools/test_pcsx2.ps1 -Menus -Capture
./tools/test_pcsx2.ps1 -Fire -Visible
./tools/test_pcsx2.ps1 -Enemy -Capture
./tools/test_pcsx2.ps1 -Audio -Visible
./tools/test_pcsx2.ps1 -Win -Capture
./tools/test_pcsx2.ps1 -Surfaces -Capture
./tools/test_pcsx2.ps1 -Benchmark -Capture -Require30 -TimeoutSeconds 180
./tools/test_pcsx2.ps1 -Walk -Require30 -TimeoutSeconds 180
```

Tests run sequentially because they share the ignored QA emulator configuration.
Each records the exact ELF hash. Captures and reports remain under `ps2/qa/`.
The default uses native resolution and no EE cycle-rate or cycle-skip changes.

- **Noo-Noo:** eight seconds of actual simulation, orientation changes and
  visible movement from a fixed camera inside the house. The source's single
  disabled-trigger waypoint produces a circular path, with no artificial
  two-point return route.
- **Menus/Fire:** original menu navigation, music delivery and moving particles.
- **Enemy:** distant viewing without damage or teleports, then close encounters,
  repeated lateral shifts and two complete non-overlapping screams.
- **Audio:** two flashlight toggles and two ignored requests for the retired R2
  action. Source sample checksums are checked separately by the host harness.
- **Win/Surfaces:** victory transition and return, roof support, visible player
  animation and grass placement.
- **Benchmark:** 34 fixed/controlled views covering menus, environments, terrain,
  foliage, flashlight, actors, roof, pickups, defeat and victory. `-Focus N` is for
  diagnosis; final validation uses all views.
- **Walk:** eight 12-second routes with normal input, movement and collision
  between route boundaries. The first second of each route is warmup; the
  remaining frames must stay at or below 36 ms, with at least 29.85 measured FPS
  and meaningful player progress. A high average cannot hide 20 FPS frames.

After a captured benchmark, run the additional GS comparison:

```powershell
$capture = (Get-Content ps2/qa/last-benchmark-capture.txt -Raw).Trim()
python tools/check_native_fidelity.py $capture ps2/qa/pcsx2-benchmark.log
```

Optional Windows speaker-loopback QA uses `tools/requirements-audio-qa.txt` and
`tools/test_audio_output.py`. It records the selected speaker output, not a
microphone. Use `--mode switches` for flashlight and source ambience checks.

## Physical PS2

Run the tested ELF from `mass:/SLENDY/SLENDY.ELF` using your own PS2 setup.
Record the ELF hash, console SCPH model, video cable/mode, launcher, storage,
controls, observed performance and a repeatable route. Pay particular attention
to terrain detail, foliage transparency, sound, the house and Noo-Noo.

A successful emulator test cannot confirm GS precision, USB loading, video
compatibility or frame timing on every physical console. The latest local-UV,
ground-detail and Noo-Noo changes need explicit hardware retesting before a
hardware-verified release claim.
