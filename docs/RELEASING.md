# Preparing a release

Source repository: https://github.com/LucianoDoS/slendytubbies-ps2

Release binaries are separate GitHub Release assets. Keep generated assets,
ELFs, ISOs, BIOS data, emulator state and local QA logs out of Git history.

1. Build and complete native checks for the exact `dist/SLENDY.ELF` being
   released. The packager checks its hash against benchmark, walking, audio,
   enemy and Noo-Noo QA records. See [TESTING.md](TESTING.md).
2. Install the optional ISO dependency and build the ELF ZIP and ISO:

   ```powershell
   python -m pip install -r tools/requirements-release.txt
   python tools/prepare_release.py
   ```

3. Validate disc boot, menu transitions/audio and gameplay from the ISO:

   ```powershell
   ./tools/test_pcsx2.ps1 -IsoPath dist/release/v0.1.0/Slendytubbies-PS2-v0.1.0.iso -Menus -Visible
   ./tools/test_pcsx2.ps1 -IsoPath dist/release/v0.1.0/Slendytubbies-PS2-v0.1.0.iso -Visible
   ```

4. Update the English validation record and release notes with the actual
   results and remaining hardware limitations. Prepare source and finalize:

   ```powershell
   python tools/prepare_source.py
   python tools/prepare_release.py --finalize
   ```

5. Commit the allowlisted source tree from `dist/source-package.json`. Publish
   an experimental prerelease tag `v0.1.0` with the ELF ZIP, ISO, source ZIP
   and `SHA256SUMS.txt` from `dist/release/v0.1.0/`. Check the public downloads
   and source-check workflow results after publishing.

The ISO is an ISO9660 data image with 2048-byte sectors, `PLAYSTATION` system
identifier, and `SYSTEM.CNF` pointing to `cdrom0:\SLENDY.ELF;1`. The packager
reads it back and checks both the boot file and embedded ELF hash. The ZIP
contains the same ELF with English instructions, credits and license notices.
Do not regenerate the ISO after native validation without testing it again;
`--finalize` checks the exact ISO hash and never rebuilds it.

The source packager uses a path/extension allowlist and verifies ZIP contents
against its hash manifest. It excludes the original package, extracted game
assets, generated banks, binaries and BIOS files. Original port contributions
are MIT; game content and dependencies retain their own terms. See
[CREDITS.md](../CREDITS.md) and [third-party notices](../third_party/NOTICES.txt).
