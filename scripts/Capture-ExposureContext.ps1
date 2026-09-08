<# Bounded, read-only diagnostic using the already proven Render bridge ->
filterOwner -> Renderer -> Sky/ExposureOwner chain. No scan, game calls, GPU
operations, plugin/config change or public API extension. Requires live sky.1
or another compatible Render bridge v2. The engine cache's GPU age is UNKNOWN.
#>
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][string]$OutFile,
    [ValidateRange(1,60)][int]$Seconds = 10,
    [ValidateRange(2,20)][int]$RateHz = 10
)
$ErrorActionPreference = 'Stop'
$product = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath((Join-Path $product 'artifacts')) + [IO.Path]::DirectorySeparatorChar
$outputPath = [IO.Path]::GetFullPath($OutFile)
if (-not $outputPath.StartsWith($artifacts,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $outputPath)) { throw 'Use a fresh output below product artifacts/.' }
$game = Get-Process -Id $ProcessId
if ($game.ProcessName -ne 'CrimsonDesert') { throw 'Target is not CrimsonDesert.' }
$startFileTime = $game.StartTime.ToUniversalTime().ToFileTimeUtc()
$exeHash = (Get-FileHash -LiteralPath $game.Path -Algorithm SHA256).Hash
if ($exeHash -ne '4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454') {
    throw 'Exposure layout is not validated for this executable; no memory read attempted.'
}
$health = Invoke-RestMethod 'http://127.0.0.1:27311/v1/health' -TimeoutSec 3
if ($health.status -ne 'playing' -or -not $health.supportedBuild -or
    $health.compatibility.executableSha256 -ne $exeHash) { throw 'No matching progressing telemetry control.' }

if (-not ('CdtExposureReadOnly' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class CdtExposureReadOnly : IDisposable {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CloseHandle(IntPtr h);
    IntPtr handle;
    public CdtExposureReadOnly(int pid) {
        handle=OpenProcess(0x1010, false, pid);
        if(handle==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public byte[] Read(ulong address, int count) {
        if(handle==IntPtr.Zero || address<0x10000 || address>0x00007FFFFFFF0000UL || count<1 || count>4096)
            throw new ArgumentException("Read outside bounded pointer domain");
        var bytes=new byte[count]; UIntPtr read;
        if(!ReadProcessMemory(handle, new IntPtr((long)address), bytes, (UIntPtr)count, out read) || read.ToUInt64()!=(ulong)count)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Short/failed diagnostic read");
        return bytes;
    }
    public ulong Pointer(ulong address) {
        ulong p=BitConverter.ToUInt64(Read(address,8),0);
        if(p<0x10000 || p>0x00007FFFFFFF0000UL) throw new ArgumentException("Invalid chain pointer");
        return p;
    }
    public void Dispose() { if(handle!=IntPtr.Zero) { CloseHandle(handle); handle=IntPtr.Zero; } }
}
'@
}

function Read-Bridge {
    for($attempt=0; $attempt -lt 3; $attempt++) {
        $lockBefore = $view.ReadInt64(16)
        if ($lockBefore -band 1) { continue }
        $bytes = [byte[]]::new(3072)
        if ($view.ReadArray[byte](0,$bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Short bridge read.' }
        $lockAfter = $view.ReadInt64(16)
        if ($lockBefore -ne $lockAfter -or $lockBefore -ne [BitConverter]::ToInt64($bytes,16)) { continue }
        $age = [Environment]::TickCount64 - [BitConverter]::ToInt64($bytes,48)
        if ([BitConverter]::ToUInt32($bytes,0) -ne 0x52445443 -or
            [BitConverter]::ToUInt32($bytes,4) -ne 2 -or
            [BitConverter]::ToUInt32($bytes,8) -ne 256 -or
            [BitConverter]::ToUInt32($bytes,12) -ne 1576192 -or
            [BitConverter]::ToUInt32($bytes,24) -ne $ProcessId -or
            [BitConverter]::ToUInt32($bytes,28) -ne 1 -or
            [BitConverter]::ToInt64($bytes,32) -ne $startFileTime -or
            [BitConverter]::ToUInt32($bytes,68) -ne 2816 -or
            [BitConverter]::ToUInt32($bytes,72) -ne 32768 -or
            [BitConverter]::ToUInt32($bytes,76) -ne 48 -or
            [BitConverter]::ToUInt32($bytes,80) -ne 0 -or
            [BitConverter]::ToUInt32($bytes,84) -ne 15 -or
            [BitConverter]::ToUInt32($bytes,64) -ne [BitConverter]::ToUInt32($bytes,288) -or
            [BitConverter]::ToSingle($bytes,256+0xAC0) -ne 6360000.0 -or
            $age -lt 0 -or $age -gt 500) { throw 'Incompatible/stale Render bridge.' }
        return [pscustomobject]@{
            owner=[BitConverter]::ToUInt64($bytes,104)
            frame=[BitConverter]::ToUInt32($bytes,64)
            captureSequence=[BitConverter]::ToUInt64($bytes,40)
            camera=@(384,388,392 | ForEach-Object {[BitConverter]::ToSingle($bytes,$_ )})
            viewDirection=@(400,404,408 | ForEach-Object {[BitConverter]::ToSingle($bytes,$_ )})
        }
    }
    throw 'Render bridge changing.'
}

function Read-Cache([uint64]$filterOwner) {
    $renderer = $memory.Pointer($filterOwner+0x10)
    if ($memory.Pointer($renderer+0x660) -ne $filterOwner) { throw 'Filter backlink mismatch.' }
    $sky = $memory.Pointer($renderer+0x668)
    if ($memory.Pointer($sky+0x10) -ne $renderer) { throw 'Sky backlink mismatch.' }
    $owner = $memory.Pointer($renderer+0x690)
    if ($memory.Pointer($owner+0x10) -ne $renderer) { throw 'Exposure backlink mismatch.' }
    $outer = $memory.Pointer($owner+0xC0)
    $inner = $memory.Pointer($outer+0x30)
    $resource = $memory.Pointer($inner+0x168)
    $readback = $memory.Pointer($owner+0xD0)
    $stride = [BitConverter]::ToUInt32($memory.Read($inner+0xC0,4),0)
    $count = [BitConverter]::ToUInt32($memory.Read($inner+0xC4,4),0)
    $mode = $memory.Read($inner+0xAE,1)[0]
    if ($stride -ne 4 -or $count -lt 20 -or $count -gt 16384 -or $mode -ne 2) { throw 'Exposure wrapper mismatch.' }
    $raw = [Convert]::ToHexString($memory.Read($owner+0xD8,64))
    # Recheck EVERY link after bytes, not just the final resource.
    if ($memory.Pointer($filterOwner+0x10) -ne $renderer -or
        $memory.Pointer($renderer+0x660) -ne $filterOwner -or
        $memory.Pointer($renderer+0x668) -ne $sky -or
        $memory.Pointer($sky+0x10) -ne $renderer -or
        $memory.Pointer($renderer+0x690) -ne $owner -or
        $memory.Pointer($owner+0x10) -ne $renderer -or
        $memory.Pointer($owner+0xC0) -ne $outer -or
        $memory.Pointer($outer+0x30) -ne $inner -or
        $memory.Pointer($inner+0x168) -ne $resource -or
        $memory.Pointer($owner+0xD0) -ne $readback) { throw 'Exposure chain changed.' }
    [pscustomobject]@{ raw=$raw; stride=$stride; count=$count; mode=$mode
        identity=('{0:X}:{1:X}:{2:X}:{3:X}:{4:X}:{5:X}:{6:X}' -f $renderer,$sky,$owner,$outer,$inner,$resource,$readback) }
}

$mapping=$null; $view=$null; $memory=$null; $output=$null
try {
    $mapping=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\CrimsonDesertTelemetry.Render.$ProcessId",[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
    $view=$mapping.CreateViewAccessor(0,3072,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
    $memory=[CdtExposureReadOnly]::new($ProcessId)
    $initial=Read-Bridge
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
    $output=[IO.File]::Open($outputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $samples=[Collections.Generic.List[object]]::new()
    $started=[DateTimeOffset]::Now
    $timer=[Diagnostics.Stopwatch]::StartNew()
    for($i=0; $i -lt $Seconds*$RateHz; $i++) {
        $wait=[int]($i*1000.0/$RateHz - $timer.Elapsed.TotalMilliseconds)
        if($wait -gt 0) { Start-Sleep -Milliseconds $wait }
        $row=[ordered]@{sequence=$i+1; capturedTick=[Environment]::TickCount64; timestamp=[DateTimeOffset]::Now}
        try {
            $bridge=Read-Bridge
            $a=Read-Cache $bridge.owner
            $b=Read-Cache $bridge.owner
            $after=Read-Bridge
            if($after.owner -ne $bridge.owner -or $a.identity -ne $b.identity -or
                $a.stride -ne $b.stride -or $a.count -ne $b.count -or $a.mode -ne $b.mode) { throw 'Source changed between observations.' }
            $flags=7
            if($a.raw -eq $b.raw) {
                $flags=15
                $value=[BitConverter]::ToSingle([Convert]::FromHexString($a.raw),0)
                if([float]::IsFinite($value) -and $value -gt 0) { $flags=31 }
            }
            $row.frame=$bridge.frame; $row.camera=$bridge.camera; $row.viewDirection=$bridge.viewDirection
            $row.renderCaptureSequence=$bridge.captureSequence
            $row.exposureCache=[ordered]@{
                source='engine-gpu-readback-cpu-cache'; flags=$flags
                gpuFramePaired=$false; sourceFrameAgeKnown=$false
                provenance=$a.identity; wrapperStride=$a.stride; wrapperCount=$a.count; innerMode=$a.mode
                rawBeforeHex=$a.raw; rawAfterHex=$b.raw
            }
        } catch { $row.error=$_.Exception.Message; $row.exposureCache=$null }
        $samples.Add([pscustomobject]$row)
    }
    $finalHealth=Invoke-RestMethod 'http://127.0.0.1:27311/v1/health' -TimeoutSec 3
    $frames=@($samples | Where-Object {$null -ne $_.frame} | Select-Object -ExpandProperty frame -Unique)
    $sameProcess=(Get-Process -Id $ProcessId).StartTime.ToUniversalTime().ToFileTimeUtc() -eq $startFileTime
    $validControl=$sameProcess -and $finalHealth.status -eq 'playing' -and $finalHealth.lastSequence -gt $health.lastSequence -and $frames.Count -gt 1
    $report=[ordered]@{
        format='private-exposure-context-v1'; processId=$ProcessId; processStartFileTime=$startFileTime
        executableSha256=$exeHash; startedAt=$started; durationMilliseconds=$timer.ElapsedMilliseconds
        controlProgressed=$validControl; distinctRenderFrames=$frames.Count
        baseline='Production plugin unchanged; this recorder uses PROCESS_VM_READ only.'
        caveat='CPU readback cache age unknown. Neither sequential chain reads nor matching bytes prove a single GPU frame or exclude ABA/torn copies.'
        samples=@($samples)
    }
    $json=[Text.Encoding]::UTF8.GetBytes(($report | ConvertTo-Json -Depth 9))
    $output.Write($json)
    [pscustomobject]@{Path=$outputPath; Samples=$samples.Count; ControlProgressed=$validControl; Frames=$frames.Count}
    if(-not $validControl) { throw 'Recording saved but progressing control failed; not usable live evidence.' }
} finally {
    if($output){$output.Dispose()}; if($memory){$memory.Dispose()}; if($view){$view.Dispose()}; if($mapping){$mapping.Dispose()}
}
