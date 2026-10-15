$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
python tools/export_assets.py
if ($LASTEXITCODE -ne 0) { throw 'Asset export failed' }
python tools/export_menu.py
if ($LASTEXITCODE -ne 0) { throw 'Menu asset export failed' }
python tools/export_victory.py
if ($LASTEXITCODE -ne 0) { throw 'Victory asset export failed' }
$projectWsl = (wsl wslpath -a $PSScriptRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'WSL is required' }
wsl -e bash "$projectWsl/tools/build_ps2.sh"
if ($LASTEXITCODE -ne 0) { throw 'PS2 build failed' }
