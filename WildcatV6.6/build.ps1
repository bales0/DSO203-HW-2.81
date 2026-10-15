[CmdletBinding()]
param(
    [string]$ToolchainBin,
    [string]$MakeExe,
    [ValidateRange(1, 64)][int]$Jobs = 4,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$originalPath = $env:PATH

try {
    if (-not $ToolchainBin) {
        $compiler = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
        if ($compiler) {
            $ToolchainBin = Split-Path $compiler.Source
        } else {
            # Reuse an installed Arduino ARM toolchain, without installing anything.
            $arduinoTools = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino\tools\arm-none-eabi-gcc'
            $compiler = Get-ChildItem $arduinoTools -Filter arm-none-eabi-gcc.exe -Recurse -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($compiler) { $ToolchainBin = $compiler.DirectoryName }
        }
    }
    if (-not $ToolchainBin -or -not (Test-Path -LiteralPath (Join-Path $ToolchainBin 'arm-none-eabi-gcc.exe'))) {
        throw 'ARM GCC not found. Supply -ToolchainBin <directory containing arm-none-eabi-gcc.exe>.'
    }
    $ToolchainBin = (Resolve-Path -LiteralPath $ToolchainBin).Path
    $env:PATH = "$ToolchainBin;$originalPath"

    if (-not $MakeExe) {
        foreach ($name in @('mingw32-make.exe', 'make.exe')) {
            $makeCommand = Get-Command $name -ErrorAction SilentlyContinue
            if ($makeCommand) { $MakeExe = $makeCommand.Source; break }
        }
    }
    if (-not $MakeExe) {
        foreach ($candidate in @('C:\MinGW\bin\mingw32-make.exe', 'C:\msys64\usr\bin\make.exe')) {
            if (Test-Path -LiteralPath $candidate) { $MakeExe = $candidate; break }
        }
    }
    if (-not $MakeExe) { throw 'GNU Make not found. Supply -MakeExe <path to mingw32-make.exe or make.exe>.' }

    Push-Location $PSScriptRoot
    try {
        & arm-none-eabi-gcc.exe --version
        if ($LASTEXITCODE -ne 0) { throw 'ARM GCC could not start.' }
        if ($Clean) {
            & $MakeExe clean
            if ($LASTEXITCODE -ne 0) { throw 'Cleaning failed.' }
        }
        & $MakeExe "-j$Jobs" all
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
        & (Join-Path $PSScriptRoot 'verify-artifacts.ps1')
        $firmwareDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\WildcatV6.6'
        Write-Host "Firmware: $(Join-Path $firmwareDir 'app1.hex')"
        foreach ($part in @('APP1A', 'APP1B', 'APP1C')) {
            Write-Host "Flash region $part`: $(Join-Path $firmwareDir "$part.ADR") + $(Join-Path $firmwareDir "$part.BIN")"
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $originalPath
}
