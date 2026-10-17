param([string]$FirmwareDirectory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = if ($FirmwareDirectory) { (Resolve-Path -LiteralPath $FirmwareDirectory).Path } else { Join-Path $repo 'bin/WildcatV6_6' }
$verifier = Join-Path $repo 'WildcatV6_6/verify-artifacts.ps1'
$fixture = Join-Path $repo 'build/wildcat144_tests/artifact_fixture'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$files = @('app1.hex', 'APP1A.BIN', 'APP1A.ADR', 'APP1B.BIN', 'APP1B.ADR', 'APP1C.BIN', 'APP1C.ADR')
$script:checks = 0
function Reset-Fixture {
    foreach ($name in $files) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $fixture $name) -Force }
}
function Expect-Failure([string]$Name, [string]$Message) {
    try { & $verifier -FirmwareDirectory $fixture | Out-Null }
    catch {
        if ($_.Exception.Message -notlike $Message) { throw "Unexpected $Name failure: $($_.Exception.Message)" }
        $script:checks++
        Write-Output "PASS rejected $Name"
        return
    }
    throw "Verifier accepted $Name."
}
function Hex-Record([byte]$Type, [uint16]$Offset, [byte[]]$Data) {
    $bytes = New-Object 'System.Collections.Generic.List[byte]'
    $bytes.Add([byte]$Data.Length)
    $bytes.Add([byte]($Offset -shr 8)); $bytes.Add([byte]($Offset -band 255)); $bytes.Add($Type)
    foreach ($value in $Data) { $bytes.Add($value) }
    $sum = 0
    foreach ($value in $bytes) { $sum += $value }
    $bytes.Add([byte]((256 - ($sum % 256)) % 256))
    return ':' + [BitConverter]::ToString($bytes.ToArray()).Replace('-', '')
}
& $verifier -FirmwareDirectory $source
$script:checks++
$hex = [IO.File]::ReadAllLines((Join-Path $source 'app1.hex'))
$bin = [IO.File]::ReadAllBytes((Join-Path $source 'APP1A.BIN'))
# Reproduce the actual omitted ARM metadata bug using the current linker map.
$map = [IO.File]::ReadAllText((Join-Path $repo 'WildcatV6_6/build/app1.map'))
$metadata = [regex]::Match($map, '(?m)^\.ARM\.exidx\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)')
if (-not $metadata.Success) { throw 'Current ARM metadata section missing from linker map; regression was not run.' }
$offset = [Convert]::ToUInt32($metadata.Groups[1].Value, 16) - 0x0800C000
$length = [Convert]::ToUInt32($metadata.Groups[2].Value, 16)
if ($length -eq 0 -or $offset -lt 0 -or $offset + $length -gt $bin.Length) { throw 'Invalid ARM metadata extent.' }
Reset-Fixture
$changed = [byte[]]$bin.Clone()
for ($i = 0; $i -lt $length; $i++) { $changed[$offset + $i] = 0 }
[IO.File]::WriteAllBytes((Join-Path $fixture 'APP1A.BIN'), $changed)
Expect-Failure 'omitted ARM metadata' '*HEX/BIN mismatch*'
Reset-Fixture
$changed = [byte[]]$bin.Clone(); $changed[0] = $changed[0] -bxor 1
[IO.File]::WriteAllBytes((Join-Path $fixture 'APP1A.BIN'), $changed)
Expect-Failure 'corrupted BIN byte' '*HEX/BIN mismatch*'
Reset-Fixture
$changedHex = [string[]]$hex.Clone()
$checksum = [Convert]::ToByte($changedHex[0].Substring($changedHex[0].Length - 2), 16) -bxor 1
$changedHex[0] = $changedHex[0].Substring(0, $changedHex[0].Length - 2) + ('{0:X2}' -f $checksum)
[IO.File]::WriteAllLines((Join-Path $fixture 'app1.hex'), $changedHex)
Expect-Failure 'bad HEX checksum' '*checksum failure*'
Reset-Fixture
[IO.File]::WriteAllLines((Join-Path $fixture 'app1.hex'), [string[]]$hex[0..($hex.Length - 2)])
Expect-Failure 'missing EOF' '*EOF missing*'
Reset-Fixture
[IO.File]::WriteAllText((Join-Path $fixture 'APP1A.ADR'), '0x0800C004')
Expect-Failure 'wrong ADR' '*Incorrect ADR*'
Reset-Fixture
$changedHex = @($hex[0..($hex.Length - 2)]) + @(
    (Hex-Record 4 0 ([byte[]]@(8, 2))),
    (Hex-Record 0 0x4000 ([byte[]]@(123))), ':00000001FF'
)
[IO.File]::WriteAllLines((Join-Path $fixture 'app1.hex'), [string[]]$changedHex)
Expect-Failure 'HEX write into slot 4' '*outside exported regions*'
Reset-Fixture
$changedHex = @($hex[0..($hex.Length - 2)]) + @(
    (Hex-Record 4 0 ([byte[]]@(8, 0))),
    (Hex-Record 0 0xC000 ([byte[]]@($bin[0]))), ':00000001FF'
)
[IO.File]::WriteAllLines((Join-Path $fixture 'app1.hex'), [string[]]$changedHex)
Expect-Failure 'duplicate HEX address' '*Duplicate HEX data*'
Reset-Fixture
[IO.File]::WriteAllBytes((Join-Path $fixture 'APP1A.BIN'), [byte[]]($bin + [byte]0))
Expect-Failure 'extra BIN byte' '*BIN extent*'
Reset-Fixture
Write-Output "PASS artifact verifier: $script:checks checks, all corrupt fixtures rejected; actual firmware unchanged."
