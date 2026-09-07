#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Fixture)
$ErrorActionPreference='Stop'
$reader=Join-Path $PSScriptRoot '../scripts/Read-AmbientProbe.ps1'
$decoder=Join-Path $PSScriptRoot '../scripts/Decode-AmbientProbe.ps1'
$original=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Fixture))
if ($original.Length -ne 8256) {throw 'Expected two-record native v2 smoke fixture.'}
$raw=& $reader -Path $Fixture -PassThru
if ($raw.format -ne 'private-ambient-probe-v2' -or $raw.samples[0].exposureCache.flags -ne 31 -or
    $raw.samples[1].exposureCache.flags -ne 0 -or $raw.samples[0].exposureCache.exposure0x -ne [float]0.08 -or
    $raw.samples[0].exposureCache.gpuFramePaired -or $raw.samples[0].exposureCache.sourceFrameAgeKnown -or
    $null -ne $raw.samples[1].exposureCache.exposure0x) {throw 'Native exposure cache controls failed.'}
if ($raw.samples[0].exposureCache.rawBeforeUInt32[6] -ne [uint32]4294901493) {throw 'Packed NaN lane was not preserved.'}
$directory=Join-Path $PSScriptRoot ('../artifacts/tests/ambient-exposure-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
$script:checks=2
function Save([string]$Name,[byte[]]$Bytes) {
    $file=Join-Path $directory ($Name+'.bin'); [IO.File]::WriteAllBytes($file,$Bytes); $file
}
function Reject([string]$Name,[byte[]]$Bytes) {
    $file=Save $Name $Bytes; $rejected=$false
    try { & $reader -Path $file | Out-Null } catch {$rejected=$true}
    if (-not $rejected) {throw "Invalid $Name accepted."}; $script:checks++
}
foreach ($field in @(0,4,8,12,220)) {
    $b=[byte[]]$original.Clone(); $b[3904+$field]=$b[3904+$field] -bxor 128
    Reject ('appendix-field-'+$field) $b
}
foreach ($field in @(32,40,48,56,64,72)) {
    $b=[byte[]]$original.Clone(); [Array]::Clear($b,3904+$field,8); Reject ('pointer-'+$field) $b
}
foreach ($field in @(208,212,216)) {
    $b=[byte[]]$original.Clone(); [Array]::Clear($b,3904+$field,4); Reject ('layout-'+$field) $b
}
$b=[byte[]]$original.Clone(); [Array]::Clear($b,3920,8); Reject before-record $b
$b=[byte[]]$original.Clone(); [Array]::Clear($b,3928,8); Reject reversed-time $b
$b=[byte[]]$original.Clone(); $b[4048]=$b[4048] -bxor 1; Reject false-stability $b
$b=[byte[]]$original.Clone(); [BitConverter]::GetBytes([uint32]16).CopyTo($b,3916); Reject flag-dependencies $b
$b=[byte[]]$original.Clone(); $b[4128+3904+32]=1; Reject missing-provenance-nonzero $b
$b=[byte[]]$original.Clone(); $b[4128+3904+80]=1; Reject missing-bytes-nonzero $b
$b=[byte[]]$original.Clone(); [BitConverter]::GetBytes([uint32]1).CopyTo($b,4128+4); Reject mixed-versions $b
foreach ($value in @([float]0,[float]-1,[float]::PositiveInfinity,[float]::NaN)) {
    $b=[byte[]]$original.Clone(); [BitConverter]::GetBytes($value).CopyTo($b,3984); [BitConverter]::GetBytes($value).CopyTo($b,4048)
    Reject ('false-scalar-'+$script:checks) $b
    # Identical raw bytes with a non-usable scalar must remain diagnosable.
    [BitConverter]::GetBytes([uint32]15).CopyTo($b,3916)
    $report=& $reader -Path (Save ('raw-scalar-'+$script:checks) $b) -PassThru
    if ($report.samples[0].exposureCache.usableScalar -or $null -ne $report.samples[0].exposureCache.exposure0x) {throw 'Invalid scalar promoted.'}
    $script:checks++
}
# Changed cache is valid diagnostic evidence, never a silently repaired sample.
$b=[byte[]]$original.Clone(); $b[4048]=$b[4048] -bxor 1; [BitConverter]::GetBytes([uint32]7).CopyTo($b,3916)
$changed=& $reader -Path (Save 'changed-cache' $b) -PassThru
if ($changed.samples[0].exposureCache.unchangedDuringRecording) {throw 'Changed cache marked stable.'}; $script:checks++
# A-only synthetic edit plus valid uniform-sky low-order coefficients permits
# offline decoder passthrough. Native transport fixture RGB is not SH evidence.
$b=[byte[]]$original.Clone(); [BitConverter]::GetBytes([uint32]0x3849BB7).CopyTo($b,4128+20)
for($i=0;$i -lt 2;$i++) {
    [Array]::Clear($b,$i*4128+2880,1024)
    foreach($channel in 0..2) {
        $radiance=0.1*($channel+1)
        [BitConverter]::GetBytes([float](2*0.282095*$radiance)).CopyTo($b,$i*4128+2880+$channel*32)
        [BitConverter]::GetBytes([float](-0.488603*1.00390625*$radiance)).CopyTo($b,$i*4128+2884+$channel*32)
    }
}
$inputFile=Save 'synthetic-a-only' $b
$derived=Join-Path $directory 'derived.json'
& $decoder -Path $inputFile -AssumeCsPrecomputeAmbientLayout -OutFile $derived | Out-Null
$json=Get-Content -LiteralPath $derived -Raw | ConvertFrom-Json
if ($json.samples[0].exposureCache.rawBeforeHex -ne $raw.samples[0].exposureCache.rawBeforeHex -or
    $json.samples[0].exposureCache.gpuFramePaired -or $json.isPublicApi) {throw 'Decoder lost provenance or promoted validity.'}; $script:checks++
# Reconstruct historical v1 framing only from these synthetic records and compare
# derived sky values: adding exposure context must NOT normalize/change ambient.
$v1=[byte[]]::new(7808)
for($i=0;$i -lt 2;$i++) {
    [Array]::Copy($b,$i*4128,$v1,$i*3904,3904)
    [BitConverter]::GetBytes([uint32]1).CopyTo($v1,$i*3904+4)
    [BitConverter]::GetBytes([uint32]3904).CopyTo($v1,$i*3904+8)
}
$v1file=Save 'synthetic-v1' $v1; $v1json=Join-Path $directory 'v1-derived.json'
& $decoder -Path $v1file -AssumeCsPrecomputeAmbientLayout -OutFile $v1json | Out-Null
$prior=Get-Content -LiteralPath $v1json -Raw | ConvertFrom-Json
if (($json.samples.values | ConvertTo-Json -Depth 12 -Compress) -ne ($prior.samples.values | ConvertTo-Json -Depth 12 -Compress)) {throw 'Exposure context changed raw sky decode.'}
if ($prior.samples[0].PSObject.Properties['exposureCache']) {throw 'Old v1 output changed.'}; $script:checks+=2
"PASS $script:checks exposure-reader/decoder checks; synthetic native fixture, not live GPU pairing. Artifacts: $directory"
