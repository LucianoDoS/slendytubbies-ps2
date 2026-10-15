$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
gcc -std=c99 -O2 -Wall -Wextra -Ips2/src ps2/src/game.c ps2/src/render.c ps2/tests/host.c -lm -o ps2/qa/host-grid.exe
if ($LASTEXITCODE -ne 0) { throw 'Grid harness compilation failed' }
gcc -std=c99 -O2 -Wall -Wextra -DSTPS_LINEAR_COLLISION -Ips2/src ps2/src/game.c ps2/src/render.c ps2/tests/host.c -lm -o ps2/qa/host-linear.exe
if ($LASTEXITCODE -ne 0) { throw 'Reference harness compilation failed' }
$grid = & ps2/qa/host-grid.exe ps2/generated/world.bin --collision-digest
if ($LASTEXITCODE -ne 0) { throw 'Grid harness failed' }
$linear = & ps2/qa/host-linear.exe ps2/generated/world.bin --collision-digest
if ($LASTEXITCODE -ne 0 -or $grid -ne $linear) { throw "Collision differs from complete scan: $grid / $linear" }
@{ passed = $true; grid = $grid; completeScan = $linear } | ConvertTo-Json | Set-Content ps2/qa/collision-grid-check.json
Write-Output $grid
