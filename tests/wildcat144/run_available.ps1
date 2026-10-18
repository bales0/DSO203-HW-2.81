param([string]$HostCompiler='C:/MinGW/bin/gcc.exe')
# Available local tests only. run.ps1 remains the full suite and reports missing
# files in this checkout; this runner does not claim those tests passed.
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=Join-Path $repo 'build/wildcat144_tests'
$compiler=(Resolve-Path -LiteralPath $HostCompiler).Path
$oldPath=$env:PATH
$failed=0
function Run([string]$name,[string[]]$arguments) {
    $exe=Join-Path $out ($name+'.exe')
    & $compiler @arguments '-o' $exe
    if($LASTEXITCODE -ne 0){$script:failed++;Write-Host "FAIL $name compilation";return}
    & $exe | Tee-Object (Join-Path $out ($name+'-available.log'))
    if($LASTEXITCODE -ne 0){$script:failed++;Write-Host "FAIL $name execution"}
}
Push-Location $repo
try {
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $env:PATH=(Split-Path $compiler -Parent)+';'+$oldPath
    $nativeObject=Join-Path $out 'native_scope.o'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DScope144_Crc=Scope144_RealCrc' '-IWildcatV6_6/inc' '-c' 'WildcatV6_6/Scope144.c' '-o' $nativeObject
    if($LASTEXITCODE -ne 0){throw 'Scope144 test object compilation failed'}
    & python '-B' 'tests/wildcat144/prepare_fft144.py' $out
    if($LASTEXITCODE -ne 0){throw 'FFT source extraction failed'}
    Run 'test_interlace' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','tests/wildcat144/test_interlace.c','WildcatV6_6/FpgaProfile.c','WildcatV6_6/Interlace144.c')
    Run 'test_capture' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','tests/wildcat144/test_capture.c','WildcatV6_6/FpgaProfile.c','WildcatV6_6/Interlace144.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c')
    Run 'test_gen' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_gen.c')
    Run 'test_phase' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','tests/wildcat144/test_phase.c','WildcatV6_6/FpgaProfile.c','WildcatV6_6/Interlace144.c','WildcatV6_6/Phase144.c')
    Run 'test_scope' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','tests/wildcat144/test_scope.c','WildcatV6_6/FpgaProfile.c','WildcatV6_6/Interlace144.c','WildcatV6_6/Scope144.c')
    Run 'test_fft144' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Fft144.c','WildcatV6_6/Frame144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','-lm')
    Run 'test_scope_files' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_scope_files.c','WildcatV6_6/Files144.c','WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_native' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_native.c','WildcatV6_6/UnifiedCal.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Wildcat144.c','WildcatV6_6/Fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    Run 'test_ui144' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_ui144.c','WildcatV6_6/UI144.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_unified_cal' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','tests/wildcat144/test_unified_cal.c','WildcatV6_6/UnifiedCal.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_cal_ui' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_cal_ui.c','WildcatV6_6/Calibrat.c','WildcatV6_6/NativeWindow.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/UnifiedCal.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    & python '-B' 'tests/wildcat144/prepare_ui_transition.py' $out
    if($LASTEXITCODE -ne 0){throw 'UI source extraction failed'}
    Run 'test_fixed_percent' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_fixed_percent.c',(Join-Path $out 'normal_fixed_percent.c'),'WildcatV6_6/UI144.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_generator_title' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_generator_title.c',(Join-Path $out 'normal_generator_title.c'),'WildcatV6_6/UI144.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_ui_transition' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_ui_transition.c',(Join-Path $out 'ui_transition_engine.c'))
    Run 'test_channel_detail' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_channel_detail.c',(Join-Path $out 'normal_channel_detail.c'),'WildcatV6_6/UnifiedCal.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Wildcat144.c','WildcatV6_6/Fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    Run 'test_key1_events' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_key1_events.c',(Join-Path $out 'key1_publish.c'),'WildcatV6_6/Scope144.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c')
    Run 'test_holdoff_view' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_holdoff_view.c',(Join-Path $out 'holdoff_view.c'))
    Run 'test_k4_bounds' @('-std=c99','-O2','-Wall','-Wextra','-Werror','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_k4_bounds.c',(Join-Path $out 'k4_release_prefix.c'))
    Run 'test_detail_core' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_detail_core.c',(Join-Path $out 'normal_channel_detail.c'),'WildcatV6_6/UnifiedCal.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Wildcat144.c','WildcatV6_6/Fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    Run 'test_cursor_detail' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_cursor_detail.c',(Join-Path $out 'normal_channel_detail.c'),(Join-Path $out 'normal_cursor_detail.c'),'WildcatV6_6/UnifiedCal.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Wildcat144.c','WildcatV6_6/Fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    Run 'test_generator_dispatch' @('-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-std=c99','-O2','-Wall','-Wextra','-Werror','-DSCOPE144_HOST_TEST','-IWildcatV6_6','-IWildcatV6_6/inc','-IWildcatV6_6/FWLib/inc','tests/wildcat144/test_generator_dispatch.c',(Join-Path $out 'normal_generator_dispatch.c'),'WildcatV6_6/UnifiedCal.c','WildcatV6_6/UiCommon.c','WildcatV6_6/Wildcat144.c','WildcatV6_6/Fft144.c',(Join-Path $out 'fft_core.c'),'WildcatV6_6/Frame144.c','WildcatV6_6/Digital144.c','WildcatV6_6/ViewWindowGeometry.c','WildcatV6_6/Capture144.c','WildcatV6_6/CaptureWindow.c','WildcatV6_6/Interlace144.c','WildcatV6_6/FpgaProfile.c',$nativeObject)
    Write-Host 'NOT RUN: missing tests/wildcat144/run_fat.ps1'
    Write-Host 'NOT RUN: missing tests/wildcat144/fixtures/digital_segments_before.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/fixtures/native_transfer_before_view.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/prepare_common.py'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_common.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_digital144.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_dual_response.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_frame144.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_frame_draw.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_hw_capture.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_native_buffer.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_native_transfer.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_native_window.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_normal_trigger.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_unified_model.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_view144.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_view_geometry.c'
    Write-Host 'NOT RUN: missing tests/wildcat144/test_xpos144.c'
    if($failed){throw "$failed available tests failed"}
} finally { $env:PATH=$oldPath;Pop-Location }
