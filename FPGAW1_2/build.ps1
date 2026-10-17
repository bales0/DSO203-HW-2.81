param(
    [ValidateSet('all', 'sim', 'lint', 'synth', 'pnr', 'timing', 'bitstream')]
    [string]$Step = 'all'
)

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$repository = Split-Path $project -Parent
$output = Join-Path $repository 'build\FPGAW1_2'
$suite = if ($env:OSS_CAD_SUITE_ROOT) { $env:OSS_CAD_SUITE_ROOT } else {
    Join-Path $env:LOCALAPPDATA 'Programs\oss-cad-suite'
}
$activation = Join-Path $suite 'environment.ps1'
if (-not (Test-Path -LiteralPath $activation)) {
    throw "OSS CAD Suite not found: $activation"
}
. $activation
$env:VERILATOR_ROOT = Join-Path $suite 'share\verilator'
New-Item -ItemType Directory -Path $output -Force | Out-Null

function Invoke-FpgaTool {
    param([string]$Tool, [string[]]$Arguments, [string]$Log)
    $executable = Join-Path $suite "bin\$Tool.exe"
    if (-not (Test-Path -LiteralPath $executable)) { throw "Missing tool: $executable" }
    Write-Host "Running $Tool $($Arguments -join ' ')"
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $output $Log)
        $toolExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($toolExitCode -ne 0) { throw "$Tool failed with exit code $toolExitCode" }
}

$source = @('AddrCtrl.v', 'DP_RAM.v', 'FPGA_V25.v', 'IO_Ctrl.v', 'Signal.v')
$json = Join-Path $output 'FIFO.json'
$asc = Join-Path $output 'FIFO.asc'
$bit = Join-Path $output 'FIFO.bin'

Push-Location $project
try {
    if ($Step -in @('sim', 'all')) {
        foreach ($test in @('tb_io_ctrl', 'tb_clock_phase', 'tb_interlace', 'tb_capture_window')) {
            $vvp = Join-Path $output "$test.vvp"
            Invoke-FpgaTool 'iverilog' (@('-g2012', '-s', $test, '-o', $vvp) + $source + @("tb\$test.sv")) "$test.compile.log"
            Invoke-FpgaTool 'vvp' @($vvp) "$test.run.log"
        }
    }
    if ($Step -in @('lint', 'all')) {
        Invoke-FpgaTool 'verilator_bin' (@('--lint-only', '-Wno-fatal', '--top-module', 'FIFO') + $source) 'lint.log'
    }
    if ($Step -in @('synth', 'pnr', 'timing', 'bitstream', 'all')) {
        $script = "read_verilog $($source -join ' '); synth_ice40 -top FIFO -json $json"
        Invoke-FpgaTool 'yosys' @('-p', $script) 'synth.log'
    }
    if ($Step -in @('pnr', 'timing', 'bitstream', 'all')) {
        Invoke-FpgaTool 'nextpnr-ice40' @('--hx4k', '--package', 'cb132', '--json', $json, '--pcf', 'Pins.pcf', '--asc', $asc, '--freq', '72', '--seed', '3', '--timing-allow-fail') 'pnr.log'
    }
    if ($Step -in @('bitstream', 'all')) {
        Invoke-FpgaTool 'icepack' @($asc, $bit) 'icepack.log'
        Write-Host "FPGA W1.2 bitstream: $bit"
    }
    if ($Step -in @('timing', 'all')) {
        Invoke-FpgaTool 'icetime' @('-d', 'hx4k', '-P', 'cb132', '-p', 'Pins.pcf', '-c', '72', '-t', $asc) 'timing.log'
    }
    if ($Step -in @('bitstream', 'all')) {
        $target = Join-Path $repository 'bin/FPGAW1_2'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        $python = Join-Path $repository 'tools/python/python.exe'
        if (!(Test-Path -LiteralPath $python)) { $python = 'python' }
        & $python -B (Join-Path $project 'export-system.py')
        if ($LASTEXITCODE -ne 0) { throw 'FPGA system image export failed.' }
    }
} finally {
    Pop-Location
}
