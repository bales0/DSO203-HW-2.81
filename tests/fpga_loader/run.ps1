$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
if ($compiler) { $compiler = $compiler.Source }
elseif (Test-Path 'C:/msys64/ucrt64/bin/gcc.exe') { $compiler = 'C:/msys64/ucrt64/bin/gcc.exe' }
else { throw 'A native Windows GCC is required for the host tests.' }
$outdir = Join-Path $repo 'build/fpga_loader_tests'
New-Item -ItemType Directory -Path $outdir -Force | Out-Null
$executable = Join-Path $outdir 'test_loader.exe'
$oldPath = $env:PATH
$env:PATH = (Split-Path $compiler -Parent) + ';' + $env:PATH
Push-Location $repo
try {
    & $compiler '-std=c99' '-Wall' '-Wextra' '-Wno-unused-parameter' '-Itests/fpga_loader/mocks' 'tests/fpga_loader/test_loader.c' '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
    & $executable 'bin/FPGAW1_1/FPGAW1_1.BIN' 'bin/FPGATest/disk/W12TEST.BIN' | Tee-Object (Join-Path $outdir 'results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Host loader tests failed.' }
} finally {
    Pop-Location
    $env:PATH = $oldPath
}
