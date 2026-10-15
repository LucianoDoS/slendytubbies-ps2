# Local validation record

Date: 2026-10-15. These results apply to the following local asset-embedded ELF:

```text
SHA-256: DED6602DA75FABB05B0D965DD3CDD51238E4394DF7351951F30F2ED1EBBC368C
Size:    23,608,696 bytes
```

The ELF is not included in the public source archive. Rebuilding with a different
toolchain or diagnostic font can produce a different hash.

| Check | Result |
|---|---|
| Host gameplay and reference renderer | Passed, including source-audio checksums and 327,680 terrain UV samples |
| Asset-free steering | 60 seconds; unit quaternions, rigid transforms and bounded circular motion passed |
| Linux sanitizer run | Steering passed AddressSanitizer and UndefinedBehaviorSanitizer |
| Native Noo-Noo motion and GS frames | Passed; 12.382 turns in the eight-second route, with visible movement |
| Native benchmark | All 34 controlled views passed; minimum 29.97 FPS |
| Continuous native walks | All eight routes passed; minimum average 29.97 FPS, zero frames over 36 ms after warmup, worst measured frame 33.83 ms |
| Native visual comparisons | Fog, grass, pickup, defeat and close-only attack border passed |
| Native enemy encounters | Two complete screams, five overlapping requests ignored, six lateral shifts; distant health remained 100 |
| Native flashlight/input check | Two flashlight switches; both requests for the retired R2 action ignored |
| Clean source build | All three scene banks re-exported from separately supplied local assets; compiled ELF matched the tested hash |
| Source archive | Allowlist, UTF-8 text, ZIP entries, contents and per-file hashes verified |
| Script/workflow syntax | Python, Bash, PowerShell and workflow YAML checked locally |
| ISO filesystem | ISO9660 readback verified SYSTEM.CNF and the identical embedded ELF |
| ISO disc boot | PCSX2 loaded cdrom0:\SLENDY.ELF;1; gameplay smoke check passed, including house entry/exit and night transition |
| ISO menus and audio | All 61 scripted menu steps passed, including victory and return to menu; three music transitions passed |

Native tests used PCSX2 OpenGL at native resolution, NTSC 60 Hz, normal EE cycle
rate and no cycle skipping. Physics remained at 60 Hz. Full logs and captures
are local QA files and are excluded from the source archive.

The GitHub Actions workflow checks portable source without a BIOS or assets;
its remote results are visible in the repository's Actions tab. A previous
build booted on a physical PS2, but the latest ground-rendering and Noo-Noo
changes still require a new
hardware test. These measurements do not certify 30 FPS on real hardware or
over an entire playthrough.
