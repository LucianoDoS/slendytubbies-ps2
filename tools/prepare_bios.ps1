function Initialize-WritableBios {
    param([string]$SourceDirectory, [string]$FileName, [string]$Destination)
    if ([IO.Path]::GetFileName($FileName) -ne $FileName) { throw 'BiosFile must be a file name' }
    $source = Join-Path $SourceDirectory $FileName
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "BIOS not found: $source" }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    $stem = [IO.Path]::GetFileNameWithoutExtension($FileName)
    $names = @($FileName)
    foreach ($suffix in @('mec','nvm','rom1','rom2','erom')) {
        $names += "$stem.$suffix"
        $names += "$FileName.$suffix"
    }
    foreach ($name in $names) {
        $from = Join-Path $SourceDirectory $name
        $to = Join-Path $Destination $name
        if ((Test-Path -LiteralPath $from -PathType Leaf) -and -not (Test-Path -LiteralPath $to)) {
            Copy-Item -LiteralPath $from -Destination $to
            (Get-Item -LiteralPath $to).IsReadOnly = $false
        }
    }
    return (Resolve-Path -LiteralPath $Destination).Path
}
