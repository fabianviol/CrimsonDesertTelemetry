<# Private research format; do not confuse raw SH rows with a public ambient RGB.
Reads a bounded file snapshot (also while the producer is writing). Rejects torn
records, invalid provenance and duplicate frames. Does not access game memory.
#>
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [string]$OutFile,
    [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$recordBytes = 0
$inputPath = (Resolve-Path -LiteralPath $Path).Path
$stream = [IO.File]::Open($inputPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
try {
    $length = $stream.Length
    if ($length -lt 64 -or $length -gt 120 * 4128) { throw "Empty, incomplete or oversized probe file ($length bytes)." }
    $prefix = [byte[]]::new(12)
    $stream.ReadExactly($prefix)
    $version = [BitConverter]::ToUInt32($prefix, 4)
    $recordBytes = switch ($version) { 1 {3904} 2 {4128} default {throw "Unknown probe version $version."} }
    $stream.Position = 0
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
        [BitConverter]::ToUInt32($bytes, $offset + 4) -ne $version -or
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
    $sample = [pscustomobject]@{
        sequence=$sequence; frame=$frame; producerRva=('0x{0:X}' -f $producer); capturedTick=$tick
        resource=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+40))
        outer=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+48))
        sky=('0x{0:X}' -f [BitConverter]::ToUInt64($bytes,$offset+56))
        camera=@(0x80,0x84,0x88 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        sunDirection=@(0x2A0,0x2A4,0x2A8 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        moonDirection=@(0x2B0,0x2B4,0x2B8 | ForEach-Object { [BitConverter]::ToSingle($bytes,$sceneOffset+$_) })
        rawFloat4Rows=@($rows)
    }
    if ($version -eq 2) {
        $e = $offset + 3904
        $flags = [BitConverter]::ToUInt32($bytes, $e + 12)
        $begin = [BitConverter]::ToUInt64($bytes, $e + 16)
        $end = [BitConverter]::ToUInt64($bytes, $e + 24)
        if ([BitConverter]::ToUInt32($bytes,$e) -ne 0x58455443 -or
            [BitConverter]::ToUInt32($bytes,$e+4) -ne 1 -or
            [BitConverter]::ToUInt32($bytes,$e+8) -ne 224 -or
            [BitConverter]::ToUInt32($bytes,$e+220) -ne 0 -or
            $flags -gt 31 -or $begin -lt $tick -or $end -lt $begin -or
            (($flags -band 4) -and ($flags -band 3) -ne 3) -or
            (($flags -band 8) -and ($flags -band 7) -ne 7) -or
            (($flags -band 16) -and ($flags -band 15) -ne 15)) { throw "Invalid exposure appendix at sample $sequence." }
        $before = [byte[]]$bytes[($e+80)..($e+143)]
        $after = [byte[]]$bytes[($e+144)..($e+207)]
        $beforeHex = [Convert]::ToHexString($before)
        $afterHex = [Convert]::ToHexString($after)
        $value = [BitConverter]::ToSingle($before,0)
        if ((($flags -band 8) -and $beforeHex -ne $afterHex) -or
            (($flags -band 16) -and (-not [float]::IsFinite($value) -or $value -le 0))) {
            throw "Exposure stability/scalar flags contradict bytes at sample $sequence."
        }
        $names = @('renderer','owner','outer','inner','resource','readbackOwner')
        $provenance = [ordered]@{}
        for ($i=0; $i -lt $names.Count; $i++) {
            $p = [BitConverter]::ToUInt64($bytes,$e+32+$i*8)
            if (($flags -band 1) -and ($p -lt 0x10000 -or $p -gt 0x00007FFFFFFF0000)) { throw "Invalid exposure provenance at sample $sequence." }
            if (-not ($flags -band 1) -and $p -ne 0) { throw "Unexpected unavailable exposure provenance at sample $sequence." }
            $provenance[$names[$i]] = if ($p) {'0x{0:X}' -f $p} else {$null}
        }
        $stride = [BitConverter]::ToUInt32($bytes,$e+208)
        $count = [BitConverter]::ToUInt32($bytes,$e+212)
        $mode = [BitConverter]::ToUInt32($bytes,$e+216)
        if (($flags -band 1) -and ($stride -ne 4 -or $count -lt 20 -or $count -gt 16384 -or $mode -ne 2)) {
            throw "Invalid exposure source layout at sample $sequence."
        }
        if ((-not ($flags -band 1) -and (($before | Where-Object {$_ -ne 0}).Count -or $stride -or $count -or $mode)) -or
            (-not ($flags -band 2) -and ($after | Where-Object {$_ -ne 0}).Count)) { throw "Invalid unavailable exposure payload at sample $sequence." }
        $exposure = [ordered]@{
            source='engine-gpu-readback-cpu-cache'; gpuFramePaired=$false; sourceFrameAgeKnown=$false
            flags=$flags; beforeAvailable=[bool]($flags -band 1); afterAvailable=[bool]($flags -band 2)
            sameIdentity=[bool]($flags -band 4); unchangedDuringRecording=[bool]($flags -band 8)
            usableScalar=[bool]($flags -band 16); beginTick=$begin; endTick=$end
            provenance=$provenance; wrapperStride=$stride; wrapperCount=$count; innerMode=$mode
            exposure0x=if ($flags -band 16) {$value} else {$null}
            rawBeforeHex=$beforeHex; rawAfterHex=$afterHex
            rawBeforeUInt32=@(0..15 | ForEach-Object {[BitConverter]::ToUInt32($before,$_ * 4)})
            rawAfterUInt32=@(0..15 | ForEach-Object {[BitConverter]::ToUInt32($after,$_ * 4)})
            caveat='64-byte GPU-derived engine cache, sampled before/after ambient command recording. Stable bytes do not prove the same GPU frame or eliminate ABA/torn-read risk. Packed lanes may be NaN as floats. No exposure correction applied.'
        }
        $sample | Add-Member -NotePropertyName exposureCache -NotePropertyValue ([pscustomobject]$exposure)
    }
    $samples.Add($sample)
    $previousFrame=$frame; $previousTick=$tick
}
$report = [ordered]@{
    format="private-ambient-probe-v$version"; source=$inputPath
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
if ($PassThru) { return [pscustomobject]$report }
[pscustomobject]@{
    ProcessId=$sessionPid; Samples=$samples.Count; DistinctFrames=@($samples.frame | Sort-Object -Unique).Count
    Producers=@($samples.producerRva | Sort-Object -Unique); Resources=@($samples.resource | Sort-Object -Unique)
    FirstCamera=$samples[0].camera; LastCamera=$samples[$samples.Count-1].camera
    FirstRawRows=@($samples[0].rawFloat4Rows | Select-Object -First 8)
    LastRawRows=@($samples[$samples.Count-1].rawFloat4Rows | Select-Object -First 8)
}
