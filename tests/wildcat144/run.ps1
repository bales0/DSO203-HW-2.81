$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
if ($compiler) { $compiler = $compiler.Source }
elseif (Test-Path 'C:/msys64/ucrt64/bin/gcc.exe') { $compiler = 'C:/msys64/ucrt64/bin/gcc.exe' }
elseif (Test-Path 'C:/MinGW/bin/gcc.exe') { $compiler = 'C:/MinGW/bin/gcc.exe' }
else { throw 'Native Windows GCC required.' }
$output = Join-Path $repo 'build/wildcat144_tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$executable = Join-Path $output 'test_interlace.exe'
$previousPath = $env:PATH
$env:PATH = (Split-Path $compiler -Parent) + ';' + $env:PATH
Push-Location $repo
try {
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' 'tests/wildcat144/test_interlace.c' 'WildcatV6.6/FpgaProfile.c' 'WildcatV6.6/Interlace144.c' '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat host test compilation failed.' }
    & $executable | Tee-Object (Join-Path $output 'results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat 144 processing tests failed.' }
    $captureExecutable = Join-Path $output 'test_capture.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' 'tests/wildcat144/test_capture.c' 'WildcatV6.6/FpgaProfile.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/Capture144.c' '-o' $captureExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat acquisition test compilation failed.' }
    & $captureExecutable | Tee-Object (Join-Path $output 'capture-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat acquisition tests failed.' }
    $genExecutable = Join-Path $output 'test_gen.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' '-IWildcatV6.6/FWLib/inc' 'tests/wildcat144/test_gen.c' '-o' $genExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat generator test compilation failed.' }
    & $genExecutable | Tee-Object (Join-Path $output 'gen-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat generator DC tests failed.' }
    $phaseExecutable = Join-Path $output 'test_phase.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' 'tests/wildcat144/test_phase.c' 'WildcatV6.6/FpgaProfile.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/Phase144.c' '-o' $phaseExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat phase test compilation failed.' }
    & $phaseExecutable | Tee-Object (Join-Path $output 'phase-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat phase/text tests failed.' }
    $scopeExecutable = Join-Path $output 'test_scope.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' 'tests/wildcat144/test_scope.c' 'WildcatV6.6/FpgaProfile.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/Scope144.c' '-o' $scopeExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 test compilation failed.' }
    & $scopeExecutable | Tee-Object (Join-Path $output 'scope-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 tests failed.' }
    $fileExecutable = Join-Path $output 'test_scope_files.exe'
    $fileCompiler = 'C:/MinGW/bin/gcc.exe'
    if (-not (Test-Path $fileCompiler)) { throw '32-bit MinGW GCC required for STM32 header-compatible file IO tests.' }
    $scopeFilePath = $env:PATH
    $env:PATH = (Split-Path $fileCompiler -Parent) + ';' + $scopeFilePath
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6' '-IWildcatV6.6/inc' '-IWildcatV6.6/FWLib/inc' 'tests/wildcat144/test_scope_files.c' 'WildcatV6.6/Files144.c' 'WildcatV6.6/Scope144.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/FpgaProfile.c' '-o' $fileExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 file test compilation failed.' }
    & $fileExecutable | Tee-Object (Join-Path $output 'scope-files-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 file IO tests failed.' }
    $nativeObject = Join-Path $output 'native_scope.o'
    $nativeExecutable = Join-Path $output 'test_native.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DScope144_Crc=Scope144_RealCrc' '-IWildcatV6.6/inc' '-c' 'WildcatV6.6/Scope144.c' '-o' $nativeObject
    if ($LASTEXITCODE -ne 0) { throw 'Native service math compilation failed.' }
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DSCOPE144_HOST_TEST' '-IWildcatV6.6' '-IWildcatV6.6/inc' '-IWildcatV6.6/FWLib/inc' 'tests/wildcat144/test_native.c' 'WildcatV6.6/UnifiedCal.c' 'WildcatV6.6/Wildcat144.c' 'WildcatV6.6/Capture144.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/FpgaProfile.c' $nativeObject '-o' $nativeExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Native service compilation failed.' }
    & $nativeExecutable | Tee-Object (Join-Path $output 'native-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native service integration tests failed.' }
    $uiExecutable = Join-Path $output 'test_ui144.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6' '-IWildcatV6.6/inc' '-IWildcatV6.6/FWLib/inc' 'tests/wildcat144/test_ui144.c' 'WildcatV6.6/UI144.c' 'WildcatV6.6/Scope144.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/FpgaProfile.c' '-o' $uiExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Shared 144 UI compilation failed.' }
    & $uiExecutable | Tee-Object (Join-Path $output 'ui144-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Shared 144 UI tests failed.' }
    $calCore = Join-Path $output 'test_unified_cal.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6.6/inc' 'tests/wildcat144/test_unified_cal.c' 'WildcatV6.6/UnifiedCal.c' 'WildcatV6.6/Scope144.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/FpgaProfile.c' '-o' $calCore
    if ($LASTEXITCODE -ne 0) { throw 'Unified calibration core compilation failed.' }
    & $calCore | Tee-Object (Join-Path $output 'unified-cal-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Unified calibration core tests failed.' }
    $calUi = Join-Path $output 'test_cal_ui.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DSCOPE144_HOST_TEST' '-IWildcatV6.6' '-IWildcatV6.6/inc' '-IWildcatV6.6/FWLib/inc' 'tests/wildcat144/test_cal_ui.c' 'WildcatV6.6/Calibrat.c' 'WildcatV6.6/UnifiedCal.c' 'WildcatV6.6/Interlace144.c' 'WildcatV6.6/FpgaProfile.c' $nativeObject '-o' $calUi
    if ($LASTEXITCODE -ne 0) { throw 'Calibration UI compilation failed.' }
    & $calUi | Tee-Object (Join-Path $output 'cal-ui-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Calibration UI tests failed.' }
    $env:PATH = $scopeFilePath
    & (Join-Path $PSScriptRoot 'test_artifacts.ps1') | Tee-Object (Join-Path $output 'artifact-results.log')
    & python '-B' 'tests/wildcat144/test_system_package.py' | Tee-Object (Join-Path $output 'system-package-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'FPGA/QuadPawn system package verification failed.' }



} finally {
    Pop-Location
    $env:PATH = $previousPath
}
