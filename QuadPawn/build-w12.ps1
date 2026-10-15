[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9._]+$')][string]$Version = '2026.10.14.2',
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
        & $MakeExe '-C' 'QuadPawn/Runtime' '-j4' 'NAME=PAWN4W' 'BINDIR=../../bin/QuadPawnW12' "VERSION=$Version"
        if ($LASTEXITCODE -ne 0) { throw 'QuadPawn W1.2 runtime build failed.' }
        & './QuadPawn/Compiler/bin/pawncc.exe' 'QuadPawn/Programs/FPGALOAD.pawn' '-d0' '-v2' '-iQuadPawn/Compiler/include' '-obin/QuadPawnW12/FPGALOAD.AMX'
        if ($LASTEXITCODE -ne 0) { throw 'FPGALOAD compilation failed.' }
        Set-Content -LiteralPath 'bin/QuadPawnW12/VERSION.TXT' -Value $Version -Encoding ascii
        Write-Host "Built QuadPawn $Version for slot 4: bin/QuadPawnW12/"
    } finally { Pop-Location }
} finally { $env:PATH = $previousPath }
