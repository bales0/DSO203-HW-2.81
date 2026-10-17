[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9._]+$')][string]$Version = '2026.10.17.1',
    [string]$ToolchainBin = "$env:LOCALAPPDATA/Programs/gcc-arm-none-eabi-9-2019-q4-major/bin",
    [string]$MakeExe = 'C:/msys64/usr/bin/make.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$previousPath = $env:PATH
try {
    $env:PATH = "$ToolchainBin;C:/msys64/usr/bin;$previousPath"
    Push-Location $repo
    try {
        & $MakeExe '-C' 'QuadPawn/Runtime' '-j4' 'NAME=PAWN4W' 'BINDIR=../../bin/QuadPawn' "VERSION=$Version"
        if ($LASTEXITCODE -ne 0) { throw 'QuadPawn W1.2 runtime build failed.' }
        & $MakeExe '-C' 'QuadPawn/Programs' '-j4' 'all'
        if ($LASTEXITCODE -ne 0) { throw 'Pawn programs compilation failed.' }
        Set-Content -LiteralPath 'QuadPawn/Runtime/build/VERSION.TXT' -Value $Version -Encoding ascii
        Write-Host "Built QuadPawn $Version for slot 4: bin/QuadPawn/"
    } finally { Pop-Location }
} finally { $env:PATH = $previousPath }
