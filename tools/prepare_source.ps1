param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'prepare_source.py'))
if ($OutputDirectory) { $arguments += @('--output', $OutputDirectory) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Source archive validation failed' }
