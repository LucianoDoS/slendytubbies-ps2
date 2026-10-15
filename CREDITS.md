# Credits and asset scope

The native PS2 port code, conversion tools and project documentation are
provided under the [MIT license](LICENSE). This license covers the port's
original contributions; it does not relicense the original game or third-party
libraries, characters, music, models, fonts or textures.

- **Original Slendytubbies V1:** [ZeoWorks](https://zeoworks.com/). Original
  in-game credits are retained by the asset conversion pipeline.
- **Native PS2 port:** Slendytubbies PS2 contributors.
- **Homebrew toolchain and SDK:** [ps2dev](https://github.com/ps2dev/ps2dev),
  [PS2SDK](https://github.com/ps2dev/ps2sdk), and
  [gsKit](https://github.com/ps2dev/gsKit). Obtain these projects separately and
  retain their license notices when distributing builds that use them.
- **Asset decoding:** [UnityPy](https://github.com/K0lb3/UnityPy).
- **Offline conversion:** NumPy, Pillow and SoundFile; dependency versions are
  listed in `tools/requirements.txt`. Optional audio QA uses SciPy and
  PyAudioWPatch.

The source archive contains the native implementation and conversion scripts.
It excludes `SlendytubbiesV1Source.unitypackage`, extracted `original/` assets,
all generated asset banks and headers, textures, audio, BIOS files, emulator
state and compiled game binaries. Supply the original package locally to build.

Original terrain coordinates and configuration are read during conversion;
their generated headers remain local. The diagnostic font is generated from a
locally installed font and is not bundled as a font file in this repository.

This is an unofficial fan port. The project does not claim ownership of
Slendytubbies or the Teletubbies characters. The downloadable ELF and ISO
contain original game resources; those resources are outside the MIT license.
The source-only archive is prepared separately and contains no game resources.
See [third-party notices](third_party/NOTICES.txt) for runtime source links and
the accompanying Academic Free License text.
