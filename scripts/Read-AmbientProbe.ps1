<# Private research format; do not confuse raw SH rows with a public ambient RGB.
Reads a bounded file snapshot (also while the producer is writing). Rejects torn
records, invalid provenance and duplicate frames. Does not access game memory.
#>
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [string]$OutFile
)
$ErrorActionPreference = 'Stop'
$recordBytes = 3904
$inputPath = (Resolve-Path -LiteralPath $Path).Path
$stream = [IO.File]::Open($inputPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
try {
    $length = $stream.Length
    if ($length -eq 0 -or $length -gt 120 * $recordBytes -or $length % $recordBytes -ne 0) {
        throw "Empty, incomplete or oversized probe file ($length bytes). This is not evidence of missing ambient light; wait for a complete sample."
    }
    $bytes = [byte[]]::new([int]$length)
    $stream.ReadExactly($bytes)
} finally { $stream.Dispose() }
$samples = [Collections.Generic.List[object]]::new()
$sessionPid = 0
$previousFrame = $null
$previousTick = [uint64]0
for ($offset = 0; $offset -lt $bytes.Length; $offset += $recordBytes) {
    $sequence = [BitConverter]::ToUInt32($bytes, $offset + 28)
    $frame = [BitConverter]::ToUInt32($bytes, $offset + 16)
    $producer = [BitConverter]::ToUInt32($bytes, $offset + 20)
    $processId = [BitConverter]::ToUInt32($bytes, $offset + 12)
    $tick = [BitConverter]::ToUInt64($bytes, $offset + 32)
    if ($offset -eq 0) { $sessionPid = $processId }
    if ([BitConverter]::ToUInt32($bytes, $offset) -ne 0x41445443 -or
        [BitConverter]::ToUInt32($bytes, $offset + 4) -ne 1 -or
        [BitConverter]::ToUInt32($bytes, $offset + 8) -ne $recordBytes -or
        [BitConverter]::ToUInt32($bytes, $offset + 24) -ne 7 -or
        $processId -eq 0 -or $processId -ne $sessionPid -or
        $sequence -ne $samples.Count + 1 -or $frame -eq $previousFrame -or
        $tick -eq 0 -or $tick -lt $previousTick -or
        $producer -notin @(0x3849BB7, 0x384CBA3)) { throw "Invalid probe header at $offset." }
    foreach ($field in @(40,48,56)) {
        $pointer=[BitConverter]::ToUInt64($bytes,$offset+$field)
        if ($pointer -lt 0x10000 -or $pointer -gt 0x00007FFFFFFFFFFF) { throw "Invalid provenance pointer at $offset+$field." }
    }
    $sceneOffset = $offset + 64
    if ([BitConverter]::ToUInt32($bytes, $sceneOffset + 0x20) -ne $frame -or
        [BitConverter]::ToSingle($bytes, $sceneOffset + 0xAC0) -ne 6360000.0) {
        throw "Scene control mismatch at sample $sequence."
    }
    $rows = for ($row = 0; $row -lt 64; $row++) {
        $values = for ($lane = 0; $lane -lt 4; $lane++) {
            $value = [BitConverter]::ToSingle($bytes, $offset + 2880 + $row * 16 + $lane * 4)
            if (-not [float]::IsFinite($value)) { throw "Nonfinite ambient value: sample=$sequence row=$row lane=$lane" }
            $value
        }
        [pscustomobject]@{row=$row; values=@($values)}
    }
    $samples.Add([pscustomobject]@{
        sequence=$sequence; frame=$frame; producerRva=('0x{0:X}' -f $producer); capturedTick=$tick
        resource=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+40))
        outer=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+48))
        sky=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+56))
        camera=@(0x80,0x84,0x88 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        sunDirection=@(0x2A0,0x2A4,0x2A8 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        moonDirection=@(0x2B0,0x2B4,0x2B8 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        rawFloat4Rows=@($rows)
    })
    $previousFrame=$frame; $previousTick=$tick
}
$report = [ordered]@{
    format='private-ambient-probe-v1'; source=$inputPath
    capturedPrefixSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
    bytesRead=$bytes.Length; processId=$sessionPid; sampleCount=$samples.Count
    caveat='GPU-fenced producer output with CPU scene sampled around recording. SH ordering/normalization, exposure and local indoor semantics are not validated; NOT API RGB.'
    samples=$samples
}
if ($OutFile) {
    $json=[Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 12))
    $output=[IO.File]::Open([IO.Path]::GetFullPath($OutFile),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try { $output.Write($json) } finally { $output.Dispose() }
}
[pscustomobject]@{
    ProcessId=$sessionPid; Samples=$samples.Count; DistinctFrames=@($samples.frame | Sort-Object -Unique).Count
    Producers=@($samples.producerRva | Sort-Object -Unique); Resources=@($samples.resource | Sort-Object -Unique)
    FirstCamera=$samples[0].camera; LastCamera=$samples[$samples.Count-1].camera
    FirstRawRows=@($samples[0].rawFloat4Rows | Select-Object -First 8)
    LastRawRows=@($samples[$samples.Count-1].rawFloat4Rows | Select-Object -First 8)
}
