# Contributing

Use English for issues, pull requests, public documentation and new code
comments. Keep game changes focused and explain the observed problem, resulting
behavior and validation in the pull request.

1. Read [the build guide](docs/BUILDING.md) and obtain the original assets locally.
2. Run the asset-free steering test and syntax checks from
   [the testing guide](docs/TESTING.md).
3. For gameplay or rendering changes, run the host harness and the relevant
   native PCSX2 checks. Performance changes require continuous walking tests;
   fixed camera averages alone can hide slow frames.
4. Record the ELF SHA-256, emulator settings and whether the result came from
   PCSX2 or real hardware. A host rasterizer image is not a GS capture.
5. Keep the original asset package, generated files, BIOS files and local
   emulator data out of commits. Use `tools/prepare_source.ps1` to create an
   allowlisted source archive.

Preserve the source's original models, audio and scene layout where practical.
Document PS2 adaptations, measured limits and remaining approximations instead
of describing them as exact Unity behavior. Measure optimizations using the
same routes, settings and quality level before and after the change.

Contributions to the port's original code and documentation are accepted under
the repository's MIT license. Do not submit third-party files without their
attribution and applicable license terms.
