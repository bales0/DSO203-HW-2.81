param([string]$HostCompiler,[string]$FirmwareDirectory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
if ($HostCompiler) { $compiler = (Resolve-Path -LiteralPath $HostCompiler).Path }
elseif (Test-Path (Join-Path $repo 'tools/host-toolchain/bin/gcc.exe')) { $compiler = Join-Path $repo 'tools/host-toolchain/bin/gcc.exe' }
elseif ($compiler) { $compiler = $compiler.Source }
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
    & (Join-Path $PSScriptRoot 'run_fat.ps1') -HostCompiler $compiler
    $digitalExe = Join-Path $output 'test_digital144.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' '-IWildcatV6_6/inc' 'tests/wildcat144/test_digital144.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $digitalExe
    if ($LASTEXITCODE -ne 0) { throw 'Digital C/D compilation failed.' }
    & $digitalExe | Tee-Object (Join-Path $output 'digital144-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Digital C/D tests failed.' }
    $nativeWindowExe = Join-Path $output 'test_native_window.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_native_window.c' 'WildcatV6_6/NativeWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/CaptureWindow.c' '-o' $nativeWindowExe
    if ($LASTEXITCODE -ne 0) { throw 'Native metadata compilation failed.' }
    & $nativeWindowExe | Tee-Object (Join-Path $output 'native-window-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native metadata tests failed.' }
    $nativeSource = Get-Content (Join-Path $repo 'WildcatV6_6/Process.c') -Raw
    $transferBody = [regex]::Match($nativeSource, '(?s)void TransferFIFO\(s16 i,u8 service\).*?(?=void SetOffset)').Value
    $formatBody = [regex]::Match($nativeSource, '(?s)static unsigned NativeViewFormat\(void\).*?(?=static int NativeViewUnsupported)').Value
    $bagBody = [regex]::Match($nativeSource, '(?s)u16 get_bag_max_buf\(void\).*?(?=void Beeper)').Value
    $interpolationBody = [regex]::Match($nativeSource, '(?s)        while\(j > 0 \).*?OsA_=OsA; OsB_=OsB;').Value
    if (-not $transferBody -or -not $formatBody -or -not $bagBody -or -not $interpolationBody) { throw 'Native producer source not found.' }
    $generator = 'unsigned NativeTest_Generate(unsigned origin,unsigned count,unsigned kp) { s16 i,j=0,k=(s16)origin,n=0,V[8]={0},OsA=0,OsB=0,OsA_=0,OsB_=0; u8 C_D=0; TempKp1=(u16)kp; for(i=0;i<(s16)count;++i) { V[A]=V[B]=i*4; if(i>=k) {'
    $producerSource = Join-Path $output 'native_producer.c'
    Set-Content $producerSource -Encoding ascii -Value (@('#include "BIOS.h"','#include "Menu.h"','#include "Process.h"','#include "NativeWindow.h"','extern u32 ReverseBitMask,BitMask;','extern u8 CalibrateMode,OsTransferCompleteFlag;')+$formatBody+$transferBody+$bagBody+$generator+$interpolationBody+'}}return (unsigned)n;}')
    $nativeTransferExe = Join-Path $output 'test_native_transfer.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_native_transfer.c' $producerSource 'tests/wildcat144/fixtures/native_transfer_before_view.c' 'WildcatV6_6/NativeWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/CaptureWindow.c' '-o' $nativeTransferExe
    if ($LASTEXITCODE -ne 0) { throw 'Native producer compilation failed.' }
    & $nativeTransferExe | Tee-Object (Join-Path $output 'native-transfer-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native producer baseline tests failed.' }
    $xposExecutable = Join-Path $output 'test_xpos144.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_xpos144.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $xposExecutable
    if ($LASTEXITCODE -ne 0) { throw '144 XPOS test compilation failed.' }
    & $xposExecutable | Tee-Object (Join-Path $output 'xpos144-results.log')
    if ($LASTEXITCODE -ne 0) { throw '144 XPOS tests failed.' }
    $hwExecutable = Join-Path $output 'test_hw_capture.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_hw_capture.c' 'WildcatV6_6/Capture144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $hwExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Hardware trigger test compilation failed.' }
    & $hwExecutable | Tee-Object (Join-Path $output 'hw-capture-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Hardware trigger tests failed.' }
    $frameExecutable = Join-Path $output 'test_frame144.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_frame144.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/Capture144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $frameExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Full frame test compilation failed.' }
    & $frameExecutable | Tee-Object (Join-Path $output 'frame144-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Full frame tests failed.' }
    # Compile the actual 144-only renderer without Normal acquisition/FFT.
    $processSource = Get-Content (Join-Path $repo 'WildcatV6_6/Process.c') -Raw
    $handoffBody = [regex]::Match($processSource, '(?s)static u8 native_buffer_pending;.*?(?=static unsigned NativeViewFormat)').Value
    $entryGate = [regex]::Match($processSource, '(?s)  if\(native_buffer_pending\) \{.*?(?=  if\(ChartLogic\(\) \|\| _Mode==X_Y)').Value
    if (-not $handoffBody -or -not $entryGate) { throw 'Native buffer entry guard not found.' }
    $handoffSource = Join-Path $output 'native_buffer_guard.c'
    $handoffHeaders = '#include "BIOS.h"', '#include "Menu.h"', '#include "Process.h"', '#include "NativeWindow.h"', '#include "File.h"', 'void cleardatabuf(u8);', 'extern unsigned native_processed;'
    Set-Content $handoffSource -Value ($handoffHeaders + $handoffBody + 'void NativeBuffer_TestProcessEntry(void) {' + $entryGate + '++native_processed; }') -Encoding ascii
    $handoffExecutable = Join-Path $output 'test_native_buffer.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_native_buffer.c' $handoffSource 'WildcatV6_6/NativeWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/CaptureWindow.c' '-o' $handoffExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Native buffer handoff compilation failed.' }
    & $handoffExecutable | Tee-Object (Join-Path $output 'native-buffer-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native buffer handoff tests failed.' }
    $frameBody = [regex]::Match($processSource, '(?s)void Process144_DrawFrame\(.*?(?=static u32 FftMagnitude\()').Value
    if (-not $frameBody) { throw 'Full frame renderer source not found.' }
    $frameSource = Join-Path $output 'frame_renderer.c'
    $frameHeaders = '#include "BIOS.h"', '#include "Menu.h"', '#include "Process.h"', '#include "Draw.h"', '#include "Frame144.h"', '#include <string.h>'
    Set-Content $frameSource -Value ($frameHeaders + $frameBody) -Encoding ascii
    $drawNative = Get-Content (Join-Path $repo 'WildcatV6_6/Draw.c') -Raw
    $prePattern = '(?s)if\(\(Row > MIN_X\)&&\(Row <= MAX_X\)\).*?(?=  if\(\(_Mode!=NORHLD\))'
    $pre = [regex]::Match($drawNative,$prePattern).Value
    if (-not $pre) { throw 'Digital segment preprocessing not found' }
    $preSource = Join-Path $output 'digital_segments.c'
    $preHeaders = '#include "BIOS.h"','#include "Menu.h"','#include "Process.h"','#include "Scope144.h"'
    Set-Content $preSource -Encoding ascii -Value ($preHeaders + 'void Test144_Preprocess(u16 Row,u8 *y,u8 *Dot_Hide) {u8 i; s16 Tmp,m,n;' + $pre + '}')
    $frameDrawExecutable = Join-Path $output 'test_frame_draw.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_frame_draw.c' $frameSource $preSource 'tests/wildcat144/fixtures/digital_segments_before.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $frameDrawExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Full frame renderer compilation failed.' }
    & $frameDrawExecutable | Tee-Object (Join-Path $output 'frame-draw-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Full frame renderer tests failed.' }
    $geometryExecutable = Join-Path $output 'test_view_geometry.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_view_geometry.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' '-o' $geometryExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Capture/view geometry test compilation failed.' }
    & $geometryExecutable | Tee-Object (Join-Path $output 'view-geometry-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Capture/view geometry tests failed.' }
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_interlace.c' 'WildcatV6_6/FpgaProfile.c' 'WildcatV6_6/Interlace144.c' '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat host test compilation failed.' }
    & $executable | Tee-Object (Join-Path $output 'results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat 144 processing tests failed.' }
    $captureExecutable = Join-Path $output 'test_capture.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_capture.c' 'WildcatV6_6/FpgaProfile.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/Capture144.c' 'WildcatV6_6/CaptureWindow.c' '-o' $captureExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat acquisition test compilation failed.' }
    & $captureExecutable | Tee-Object (Join-Path $output 'capture-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat acquisition tests failed.' }
    $genExecutable = Join-Path $output 'test_gen.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_gen.c' '-o' $genExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat generator test compilation failed.' }
    & $genExecutable | Tee-Object (Join-Path $output 'gen-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat generator DC tests failed.' }
    $phaseExecutable = Join-Path $output 'test_phase.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_phase.c' 'WildcatV6_6/FpgaProfile.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/Phase144.c' '-o' $phaseExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat phase test compilation failed.' }
    & $phaseExecutable | Tee-Object (Join-Path $output 'phase-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Wildcat phase/text tests failed.' }
    $scopeExecutable = Join-Path $output 'test_scope.exe'
    & $compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_scope.c' 'WildcatV6_6/FpgaProfile.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/Scope144.c' '-o' $scopeExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 test compilation failed.' }
    & $scopeExecutable | Tee-Object (Join-Path $output 'scope-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 tests failed.' }
    & python '-B' 'tests/wildcat144/prepare_ui_transition.py' $output
    if($LASTEXITCODE -ne 0){throw 'CFG source extraction failed'}
    $fileExecutable = Join-Path $output 'test_scope_files.exe'
    $fileCompiler = $compiler
    if (-not (Test-Path $fileCompiler)) { throw '32-bit MinGW GCC required for STM32 header-compatible file IO tests.' }
    $machine = & $fileCompiler '-dumpmachine'
    if ($LASTEXITCODE -ne 0 -or $machine -notmatch '^(mingw32|i686-.*mingw32)$') { throw '32-bit MinGW host compiler required.' }
    $scopeFilePath = $env:PATH
    $env:PATH = (Split-Path $fileCompiler -Parent) + ';' + $scopeFilePath
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_scope_files.c' (Join-Path $output 'cfg_save.c') 'WildcatV6_6/Files144.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $fileExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 file test compilation failed.' }
    & $fileExecutable | Tee-Object (Join-Path $output 'scope-files-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Scope144 file IO tests failed.' }
    & python '-B' 'tests/wildcat144/prepare_common.py' $output
    if ($LASTEXITCODE -ne 0) { throw 'Common GEN/FILE source extraction failed.' }
    $commonExe = Join-Path $output 'test_common.exe'
    & $fileCompiler '-std=gnu90' '-O2' '-Wall' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_common.c' (Join-Path $output 'common_engine.c') (Join-Path $output 'common_loaders.c') 'WildcatV6_6/UiCommon.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $commonExe
    if ($LASTEXITCODE -ne 0) { throw 'Common GEN/FILE compilation failed.' }
    & $commonExe | Tee-Object (Join-Path $output 'common-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Common GEN/FILE tests failed.' }
    $nativeObject = Join-Path $output 'native_scope.o'
    $nativeExecutable = Join-Path $output 'test_native.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DScope144_Crc=Scope144_RealCrc' '-IWildcatV6_6/inc' '-c' 'WildcatV6_6/Scope144.c' '-o' $nativeObject
    if ($LASTEXITCODE -ne 0) { throw 'Native service math compilation failed.' }
    & $fileCompiler '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DSCOPE144_HOST_TEST' '-IWildcatV6_6' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_native.c' 'WildcatV6_6/UnifiedCal.c' 'WildcatV6_6/UiCommon.c' 'WildcatV6_6/Wildcat144.c' 'WildcatV6_6/Frame144.c' 'WildcatV6_6/Digital144.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/Capture144.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' $nativeObject '-o' $nativeExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Native service compilation failed.' }
    & $nativeExecutable | Tee-Object (Join-Path $output 'native-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native service integration tests failed.' }
    $uiExecutable = Join-Path $output 'test_ui144.exe'
    & $fileCompiler '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_ui144.c' 'WildcatV6_6/UI144.c' 'WildcatV6_6/UiCommon.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $uiExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Shared 144 UI compilation failed.' }
    & $uiExecutable | Tee-Object (Join-Path $output 'ui144-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Shared 144 UI tests failed.' }
    $menuSource = Get-Content (Join-Path $repo 'WildcatV6_6/Menu.c') -Raw
    $triggerBody = [regex]::Match($menuSource, '(?s)int NativeTriggerMenu\(unsigned key\).*?(?=void Load_Attr\(void\))').Value
    if (-not $triggerBody) { throw 'Normal trigger menu source not found.' }
    $normalSource = Join-Path $output 'normal_trigger_menu.c'
    $normalHeaders = '#include "BIOS.h"','#include "Menu.h"','#include "Draw.h"','#include "Interrupt.h"','#include "Process.h"','#include "Function.h"','#include "Scope144.h"','#include "UiCommon.h"','extern char x10[8][6],CH_A_STR[5][10],CH_B_STR[5][10],XY_CH_A_STR[5][10],XY_CH_B_STR[5][10];extern u16 O_COLOR[2];extern const char *SubStr[1];'
    Set-Content $normalSource -Encoding ascii -Value ($normalHeaders + $triggerBody)
    $normalExe = Join-Path $output 'test_normal_trigger.exe'
    & $fileCompiler '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' '-std=gnu90' '-O2' '-Wall' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_normal_trigger.c' $normalSource 'WildcatV6_6/UI144.c' 'WildcatV6_6/UiCommon.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $normalExe
    if ($LASTEXITCODE -ne 0) { throw 'Normal trigger UI compilation failed.' }
    & $normalExe | Tee-Object (Join-Path $output 'normal-trigger-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Normal trigger UI tests failed.' }
    # Compile the actual small renderer with mock LCD calls, without the unrelated
    # acquisition/FFT functions and hardware dependencies in the rest of Draw.c.
    $drawSource = Get-Content (Join-Path $repo 'WildcatV6_6/Draw.c') -Raw
    $viewBody = [regex]::Match($drawSource, '(?s)void Update_View_Area\(void\)\s*\{.*?(?=/\*{4})').Value
    $rule = [regex]::Match($drawSource, 'uc16 RULE_BASE\[8\]\s*=\s*\{[^}]+\};').Value
    $markerBody = [regex]::Match($drawSource, '(?s)s32 ScaleXposi\(void\)\s*\{.*?(?=void ClearScreenArea)').Value
    if (-not $viewBody -or -not $rule -or -not $markerBody) { throw 'Buffer renderer/marker source not found.' }
    $viewSource = Join-Path $output 'view_renderer.c'
    $headers = '#include "BIOS.h"', '#include "Menu.h"', '#include "Process.h"', '#include "Draw.h"', '#include "Scope144.h"', '#include "NativeWindow.h"', 'void DrawPixel(u16 x,u8 y,u16 color);'
    Set-Content $viewSource -Value ($headers + $rule + $viewBody + $markerBody) -Encoding ascii
    $viewExecutable = Join-Path $output 'test_view144.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_view144.c' $viewSource 'WildcatV6_6/NativeWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/CaptureWindow.c' '-o' $viewExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Buffer waveform test compilation failed.' }
    & $viewExecutable | Tee-Object (Join-Path $output 'view144-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Buffer waveform tests failed.' }
    $calCore = Join-Path $output 'test_unified_cal.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_unified_cal.c' 'WildcatV6_6/UnifiedCal.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $calCore
    if ($LASTEXITCODE -ne 0) { throw 'Unified calibration core compilation failed.' }
    & $calCore | Tee-Object (Join-Path $output 'unified-cal-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Unified calibration core tests failed.' }
    $modelExecutable = Join-Path $output 'test_unified_model.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_unified_model.c' 'WildcatV6_6/UnifiedCal.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $modelExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Unified electrical model compilation failed.' }
    & $modelExecutable | Tee-Object (Join-Path $output 'unified-model-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Unified electrical model tests failed.' }
    $dualExecutable = Join-Path $output 'test_dual_response.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-IWildcatV6_6/inc' 'tests/wildcat144/test_dual_response.c' 'WildcatV6_6/UnifiedCal.c' 'WildcatV6_6/Scope144.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' '-o' $dualExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Dual offset response compilation failed.' }
    & $dualExecutable | Tee-Object (Join-Path $output 'dual-response-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Dual offset response tests failed.' }
    $calUi = Join-Path $output 'test_cal_ui.exe'
    & $fileCompiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-DSCOPE144_HOST_TEST' '-IWildcatV6_6' '-IWildcatV6_6/inc' '-IWildcatV6_6/FWLib/inc' 'tests/wildcat144/test_cal_ui.c' (Join-Path $output 'normal_vertical.c') 'WildcatV6_6/Calibrat.c' 'WildcatV6_6/NativeWindow.c' 'WildcatV6_6/ViewWindowGeometry.c' 'WildcatV6_6/CaptureWindow.c' 'WildcatV6_6/UnifiedCal.c' 'WildcatV6_6/Interlace144.c' 'WildcatV6_6/FpgaProfile.c' $nativeObject '-o' $calUi
    if ($LASTEXITCODE -ne 0) { throw 'Calibration UI compilation failed.' }
    & $calUi | Tee-Object (Join-Path $output 'cal-ui-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'Calibration UI tests failed.' }
    $env:PATH = $scopeFilePath
    & (Join-Path $PSScriptRoot 'test_artifacts.ps1') -FirmwareDirectory $FirmwareDirectory | Tee-Object (Join-Path $output 'artifact-results.log')
    & python '-B' 'tests/wildcat144/test_system_package.py' | Tee-Object (Join-Path $output 'system-package-results.log')
    if ($LASTEXITCODE -ne 0) { throw 'FPGA/QuadPawn system package verification failed.' }



} finally {
    Pop-Location
    $env:PATH = $previousPath
}
