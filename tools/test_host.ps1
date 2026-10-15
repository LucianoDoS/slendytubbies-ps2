$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
New-Item -ItemType Directory -Force ps2/qa | Out-Null
gcc -std=c99 -O2 -Wall -Wextra -Ips2/src ps2/src/game.c ps2/src/render.c ps2/tests/host.c -lm -o ps2/qa/host.exe
if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
& ps2/qa/host.exe ps2/generated/world.bin ps2/qa
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
python -c "from pathlib import Path; from PIL import Image; [Image.open(p).save(p.with_suffix('.png')) for p in Path('ps2/qa').glob('*.ppm')]"
