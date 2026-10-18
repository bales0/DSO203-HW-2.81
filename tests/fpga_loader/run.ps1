$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = Join-Path $repo 'tools/host-toolchain/bin/gcc.exe'
if (Test-Path -LiteralPath $compiler) { }
elseif (Get-Command gcc.exe -ErrorAction SilentlyContinue) { $compiler = (Get-Command gcc.exe).Source }
elseif (Test-Path 'C:/msys64/ucrt64/bin/gcc.exe') { $compiler = 'C:/msys64/ucrt64/bin/gcc.exe' }
else { throw 'A native Windows GCC is required for the host tests.' }
$outdir = Join-Path $repo 'build/fpga_loader_tests'
New-Item -ItemType Directory -Path $outdir -Force | Out-Null
$executable = Join-Path $outdir 'test_loader.exe'
$oldPath = $env:PATH
$env:PATH = (Split-Path $compiler -Parent) + ';' + $env:PATH
Push-Location $repo
try {
    # Approved full W1.2 image CRC; diagnostics are not part of the author app set.
    $imageCrc = '33AFAD10'
    & $compiler '-std=c99' '-Wall' '-Wextra' '-Wno-unused-parameter' '-Itests/fpga_loader/mocks' 'tests/fpga_loader/test_loader.c' '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
    $fullBitstream = Join-Path $outdir 'full-bitstream.BIN'
    $raw = [IO.File]::ReadAllBytes((Join-Path $repo 'bin/FPGAW1_2/FPGAW1_2.BIN'))
    [IO.File]::WriteAllBytes($fullBitstream, ([byte[]]@(255,0,0,255) + $raw))
    & $executable 'tests/fpga_loader/fixtures/FPGAW1_1.BIN' $fullBitstream $imageCrc 'bin/FPGAW1_2/FPGAW1_2.BIN' | Tee-Object (Join-Path $outdir 'results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Host loader tests failed.' }
} finally {
    Pop-Location
    $env:PATH = $oldPath
}
