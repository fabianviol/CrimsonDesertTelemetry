#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Fixture)
$ErrorActionPreference='Stop'
$reader=Join-Path $PSScriptRoot '../scripts/Read-AmbientProbe.ps1'
$result=& $reader -Path $Fixture
if($result.Samples -ne 2 -or $result.DistinctFrames -ne 2 -or $result.Producers.Count -ne 2){throw 'Positive fixture control failed'}
$original=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Fixture))
$recordBytes=[BitConverter]::ToUInt32($original,8)
$directory=Join-Path ([IO.Path]::GetTempPath()) ('cdt-ambient-reader-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
function Reject([string]$Name,[byte[]]$Bytes) {
    $file=Join-Path $directory ($Name+'.bin')
    [IO.File]::WriteAllBytes($file,$Bytes)
    $rejected=$false
    try { & $reader -Path $file | Out-Null } catch { $rejected=$true }
    if(-not $rejected){throw "Invalid $Name accepted"}
}
Reject empty ([byte[]]@())
Reject truncated ([byte[]]$original[0..100])
foreach($offset in @(0,4,8,12,20,24,28,(64+0x20),(64+0xAC0))) {
    $bad=[byte[]]$original.Clone(); $bad[$offset]=$bad[$offset] -bxor 0x80
    Reject ('field-'+$offset) $bad
}
$bad=[byte[]]$original.Clone(); [Array]::Clear($bad,40,8); Reject missing-resource $bad
$bad=[byte[]]$original.Clone(); [Array]::Clear($bad,32,8); Reject missing-timestamp $bad
$bad=[byte[]]$original.Clone(); [BitConverter]::GetBytes([float]::NaN).CopyTo($bad,2880); Reject nonfinite $bad
$bad=[byte[]]$original.Clone(); [Array]::Copy($bad,16,$bad,$recordBytes+16,4); Reject repeated-frame $bad
$json=Join-Path $directory 'valid.json'
& $reader -Path $Fixture -OutFile $json | Out-Null
if((Get-Content -LiteralPath $json -Raw | ConvertFrom-Json).sampleCount -ne 2){throw 'JSON export mismatch'}
$refused=$false
try{& $reader -Path $Fixture -OutFile $json | Out-Null}catch{$refused=$true}
if(-not $refused){throw 'Existing output overwritten'}
'PASS: positive two-producer GPU fixture; 15 malformed-file controls; JSON export and overwrite refusal. Test artifacts: '+$directory
