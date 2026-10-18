param(
    [ValidateSet('all','sim','synth','bitstream')][string]$Step = 'all',
    [string]$Ghdl = "$PSScriptRoot/../../../../tools/ghdl/bin/ghdl.exe"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../../..").Path
$output = Join-Path $repo 'build/usb_hx4k'
$suite = if ($env:OSS_CAD_SUITE_ROOT) { $env:OSS_CAD_SUITE_ROOT } else {
    Join-Path $env:LOCALAPPDATA 'Programs/oss-cad-suite'
}
. (Join-Path $suite 'environment.ps1')
New-Item -ItemType Directory -Force -Path $output | Out-Null
$Ghdl = (Resolve-Path $Ghdl).Path
$sources = @('config.vhdl','edge_matcher.vhdl','fifo.vhdl','usb_rx_phy.vhdl',
    'usb_decoder.vhdl','ramblock.vhdl','packet_buffer.vhdl','repeat_filter.vhdl',
    'packet_filter.vhdl','addr_filter.vhdl','addr_follower.vhdl','fpga_top.vhdl')
function Invoke-Tool([string]$Exe,[string[]]$Arguments,[string]$Log) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Exe @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $output $Log)
        $status = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($status -ne 0) { throw "$Exe failed ($status); see $Log" }
}
Push-Location $output
try {
    $paths = @($sources | ForEach-Object { Join-Path $PSScriptRoot $_ })
    Invoke-Tool $Ghdl (@('-a','--std=93') + $paths) 'analyze.log'
    if ($Step -in @('sim','all')) {
        Invoke-Tool $Ghdl @('-a','--std=93',"$PSScriptRoot/std_logic_textio.vhdl") 'textio.log'
        foreach ($name in @('tb_usb_decoder','tb_fifo','tb_edge_matcher','tb_packet_filter',
            'tb_packet_buffer','tb_repeat_filter','tb_addr_filter','tb_addr_follower','tb_usb_top')) {
            Get-ChildItem "$PSScriptRoot/$($name)_*.txt" -ErrorAction SilentlyContinue | Copy-Item -Destination $output -Force
            Invoke-Tool $Ghdl @('-a','--std=93',"$PSScriptRoot/$name.vhdl") "$name.compile.log"
            Invoke-Tool $Ghdl @('-r','--std=93',$name,'--assert-level=error','--stop-time=100ms') "$name.run.log"
            if ((Test-Path (Join-Path $output "$name.run.log")) -and
                [IO.File]::ReadAllText((Join-Path $output "$name.run.log")) -match 'simulation stopped by --stop-time') {
                throw "$name did not complete before the simulation limit"
            }
            $expected = Join-Path $output "$($name)_expected.txt"
            if (Test-Path $expected) {
                $actual = Join-Path $output "$($name)_output.txt"
                if (([IO.File]::ReadAllText($expected) -replace '\s','') -ne
                    ([IO.File]::ReadAllText($actual) -replace '\s','')) { throw "$name output mismatch" }
            }
            Write-Host "PASS $name"
        }
    }
    if ($Step -in @('synth','bitstream','all')) {
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $Ghdl '--synth' '--std=93' '--no-formal' '--out=verilog' 'fpga_top' 2> (Join-Path $output 'ghdl-synth.log') |
                Set-Content -LiteralPath (Join-Path $output 'usb.v') -Encoding ascii
            $status = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousPreference }
        if ($status -ne 0) { throw 'GHDL synthesis failed' }
        Invoke-Tool "$suite/bin/yosys.exe" @('-p','read_verilog usb.v; synth_ice40 -top fpga_top -json usb.json; stat') 'synth.log'
    }
    if ($Step -in @('bitstream','all')) {
        Invoke-Tool "$suite/bin/nextpnr-ice40.exe" @('--hx4k','--package','cb132','--json','usb.json',
            '--pcf',"$PSScriptRoot/pins-hx4k.pcf",'--asc','usb.asc','--freq','72','--seed','3') 'pnr.log'
        Invoke-Tool "$suite/bin/icepack.exe" @('usb.asc','USB.FPG') 'icepack.log'
        $image = [IO.File]::ReadAllBytes((Join-Path $output 'USB.FPG'))
        if ($image.Length -ne 135100 -or
            [BitConverter]::ToString($image,0,8) -ne 'FF-00-00-FF-7E-AA-99-7E') {
            throw 'Unexpected HX4K bitstream format/length'
        }
        $target = Join-Path $repo 'bin/QuadPawn/Programs'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -LiteralPath (Join-Path $output 'USB.FPG') -Destination $target -Force
        Write-Host "Built HW 2.81 USB analyzer image: $target/USB.FPG"
    }
} finally { Pop-Location }
