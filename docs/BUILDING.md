# Building

## Inputs and dependencies

Use Python 3.11 and install `tools/requirements.txt`. Asset conversion needs
NumPy, Pillow, UnityPy and SoundFile. The source archive contains no game data.
Supply your own copy of `SlendytubbiesV1Source.unitypackage` and extract it:

```sh
python tools/extract_unitypackage.py SlendytubbiesV1Source.unitypackage original
```

The `metaData` files inside that package include the original Unity import
cache, allowing meshes and textures to be decoded without reimporting FBX files
in Unity. Conversion reads the morning/dusk/night, menu and victory scenes,
prefabs, animations, terrain data and audio references. `original/manifest.json`
records package paths and GUIDs.

The tested homebrew environment uses
[ps2dev](https://github.com/ps2dev/ps2dev),
[PS2SDK](https://github.com/ps2dev/ps2sdk) and
[gsKit](https://github.com/ps2dev/gsKit). `tools/setup_ps2dev.sh` installs the
official Ubuntu archive into `~/slendytubbies-tools/ps2dev`. It records the
download's SHA-256 locally; the default download tracks the upstream `latest`
archive and is not a pinned reproducible toolchain. Set `PS2DEV` to reuse a
specific installed toolchain.

## Windows and WSL

```powershell
python -m pip install -r tools/requirements.txt
wsl bash tools/setup_ps2dev.sh
./build.ps1
```

The wrapper exports the game, menu, victory and player animation, builds the
PS2 executable through WSL, strips debug sections and writes `dist/SLENDY.ELF`.

## Linux

Run these commands from the repository root after extraction:

```sh
python -m pip install -r tools/requirements.txt
bash tools/setup_ps2dev.sh
python tools/export_assets.py
python tools/export_menu.py
python tools/export_victory.py
bash tools/build_ps2.sh
```

Each scene exporter accepts `--source` and `--output`. For example:

```sh
python tools/export_assets.py --source /path/to/extracted/original --output ps2/generated
python tools/export_menu.py --source /path/to/extracted/original --output ps2/generated/menu
python tools/export_victory.py --source /path/to/extracted/original --output ps2/generated/victory
```

Conversion selects Consolas on Windows, DejaVu Sans Mono when available on Linux,
or Pillow's fallback for its diagnostic font. `STPS_DEBUG_FONT` selects a local
font explicitly. No font file is included in the source archive. Cross-platform
font differences can change the generated ELF hash.

## Layout

| Path | Purpose |
|---|---|
| `ps2/src/game.c` | States, player movement, collision and enemy behavior |
| `ps2/src/render.c` | Projection, clipping, terrain, models, lighting and UI |
| `ps2/src/ps2_main.c` | PS2 initialization, GS packets, pad input and audio |
| `ps2/src/steering.h` | Quaternion steering and rigid actor transforms |
| `ps2/tests/` | Host checks, asset-free math checks and native test routes |
| `tools/export_*.py` | Original asset conversion and provenance reports |
| `ps2/generated/` | Local generated banks, headers and conversion reports |
| `ps2/qa/` | Local logs, captures, test binaries and ELF hashes |
| `ps2/play/` | Local interactive PCSX2 configuration and BIOS copies |
| `dist/` | Local ELF, USB package and source archive |

The final ELF embeds all scene banks, audio, player poses and required IOP
modules. It does not read game asset files from USB after loading. Keep all
generated directories local; none belongs in the public source archive.
