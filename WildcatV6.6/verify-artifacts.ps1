[CmdletBinding()]
param([string]$FirmwareDirectory)
$ErrorActionPreference = 'Stop'
if (-not $FirmwareDirectory) {
    $FirmwareDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin/WildcatV6.6'
}
$firmware = (Resolve-Path -LiteralPath $FirmwareDirectory).Path
$hexPath = Join-Path $firmware 'app1.hex'
$memory = New-Object 'System.Collections.Generic.Dictionary[uint32,byte]'
[uint64]$baseAddress = 0
$eof = $false
$entryPoint = $null
$lineNumber = 0
foreach ($line in [IO.File]::ReadAllLines($hexPath)) {
    $lineNumber++
    if (-not $line) { continue }
    if ($eof) { throw 'HEX record after EOF.' }
    if ($line -notmatch '^:[0-9a-fA-F]+$' -or ($line.Length % 2) -ne 1 -or $line.Length -lt 11) {
        throw "Malformed HEX record on line $lineNumber."
    }
    $bytes = New-Object byte[] (($line.Length - 1) / 2)
    [int]$sum = 0
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($line.Substring(1 + 2 * $i, 2), 16)
        $sum += $bytes[$i]
    }
    if ($bytes.Length -ne $bytes[0] + 5 -or ($sum % 256) -ne 0) {
        throw "HEX length/checksum failure on line $lineNumber."
    }
    [int]$count = $bytes[0]
    [int]$offset = 256 * $bytes[1] + $bytes[2]
    switch ($bytes[3]) {
        0 {
            if ($offset + $count -gt 65536) { throw 'HEX record crosses segment boundary.' }
            for ($i = 0; $i -lt $count; $i++) {
                [uint64]$address = $baseAddress + $offset + $i
                if ($address -gt [uint32]::MaxValue) { throw 'HEX address overflow.' }
                if ($memory.ContainsKey([uint32]$address)) { throw 'Duplicate HEX data address.' }
                $memory.Add([uint32]$address, $bytes[4 + $i])
            }
        }
        1 {
            if ($count -ne 0 -or $offset -ne 0) { throw 'Invalid HEX EOF.' }
            $eof = $true
        }
        2 {
            if ($count -ne 2 -or $offset -ne 0) { throw 'Invalid HEX segment base.' }
            $baseAddress = [uint64](256 * $bytes[4] + $bytes[5]) * 16
        }
        4 {
            if ($count -ne 2 -or $offset -ne 0) { throw 'Invalid HEX linear base.' }
            $baseAddress = [uint64](256 * $bytes[4] + $bytes[5]) * 65536
        }
        5 {
            if ($count -ne 4 -or $offset -ne 0 -or $null -ne $entryPoint) { throw 'Invalid HEX start address.' }
            $entryPoint = [Convert]::ToUInt32($line.Substring(9, 8), 16)
        }
        default { throw "Unsupported HEX record type $($bytes[3])." }
    }
}
if (-not $eof) { throw 'HEX EOF missing.' }
if ($memory.Count -eq 0) { throw 'HEX data missing.' }
$regions = @(
    @{ Name = 'APP1A'; Start = [uint32]0x0800C000; Limit = [uint32]0x0801C000 },
    @{ Name = 'APP1B'; Start = [uint32]0x0802C000; Limit = [uint32]0x08040000 },
    @{ Name = 'APP1C'; Start = [uint32]0x08047000; Limit = [uint32]0x08057400 }
)
foreach ($region in $regions) {
    $adr = ([IO.File]::ReadAllText((Join-Path $firmware ($region.Name + '.ADR')))).Trim()
    if ($adr -notmatch '^0x[0-9a-fA-F]{8}$' -or [Convert]::ToUInt32($adr.Substring(2), 16) -ne $region.Start) {
        throw "Incorrect ADR for $($region.Name)."
    }
    $region.Data = [IO.File]::ReadAllBytes((Join-Path $firmware ($region.Name + '.BIN')))
    if (-not $region.Data.Length -or $region.Data.Length -gt $region.Limit - $region.Start) {
        throw "Invalid BIN length for $($region.Name)."
    }
    $last = [uint32]($region.Start + $region.Data.Length - 1)
    if (-not $memory.ContainsKey($region.Start) -or -not $memory.ContainsKey($last)) {
        throw "BIN extent does not match HEX for $($region.Name)."
    }
    for ($i = 0; $i -lt $region.Data.Length; $i++) {
        $address = [uint32]($region.Start + $i)
        if ($memory.ContainsKey($address)) {
            if ($memory[$address] -ne $region.Data[$i]) {
                throw ('HEX/BIN mismatch for {0} at 0x{1:X8}.' -f $region.Name, $address)
            }
        } elseif ($region.Data[$i] -ne 0) {
            throw "Nonzero BIN padding absent from HEX for $($region.Name)."
        }
    }
    Write-Output ('PASS {0}: {1} B, ADR and HEX/BIN match' -f $region.Name, $region.Data.Length)
}
foreach ($address in $memory.Keys) {
    $covered = $false
    foreach ($region in $regions) {
        if ($address -ge $region.Start -and $address -lt $region.Start + $region.Data.Length) {
            $covered = $true
            break
        }
    }
    if (-not $covered) { throw ('HEX data outside exported regions at 0x{0:X8}.' -f $address) }
}
if ($null -ne $entryPoint) {
    $codeAddress = [uint32]($entryPoint -band 0xfffffffeL)
    if (-not $memory.ContainsKey($codeAddress)) { throw 'HEX entry point is outside program data.' }
}
Write-Output 'PASS HEX checksums, EOF, padding and coverage; SYS/FPGA/slot 4 excluded.'
