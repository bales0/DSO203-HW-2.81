$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = Join-Path $repo 'tools/host-toolchain/bin/gcc.exe'
if (-not (Test-Path $compiler)) { throw '32-bit MinGW GCC required for the 32-bit Pawn VM.' }
$output = Join-Path $repo 'build/fpga_profile_tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$prefix = Join-Path $output 'empty.inc'
Set-Content -LiteralPath $prefix -Value '' -Encoding ascii
$amx = Join-Path $output 'test_profile.amx'
$runner = Join-Path $output 'runner.exe'
$previousPath = $env:PATH
$env:PATH = (Split-Path $compiler -Parent) + ';' + $env:PATH
Push-Location $repo
try {
    & './QuadPawn/Compiler/bin/pawncc.exe' 'tests/fpga_profile/test_profile.pawn' '-d2' '-v2' '-V0' "-p$prefix" '-iQuadPawn/Compiler/include' "-o$amx"
    if ($LASTEXITCODE -ne 0) { throw 'Pawn profile test compilation failed.' }
    & $compiler '-std=c99' '-O2' '-DPAWN_CELL_SIZE=32' '-DAMX_ANSIONLY' '-DAMX_NODYNALOAD' '-DHAVE_ALLOCA_H=0' '-D__WIN32__=1' '-include' 'malloc.h' '-IQuadPawn/Compiler/source/amx' 'tests/fpga_profile/runner.c' 'QuadPawn/Compiler/source/amx/amx.c' '-o' $runner
    if ($LASTEXITCODE -ne 0) { throw 'Host Pawn VM compilation failed.' }
    & $runner $amx | Tee-Object (Join-Path $output 'results.log')
    if ($LASTEXITCODE -ne 0) { throw 'FPGA profile tests failed.' }
} finally {
    Pop-Location
    $env:PATH = $previousPath
}
