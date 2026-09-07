# Bounded, read-only live API evidence. Does not modify the game or lamps.
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(2,30)][int]$Seconds=12,
    [ValidateRange(1024,65535)][int]$Port=27311
)
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Evidence directory already exists; choose a new run.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;
public static class LightStreamEvidence {
    public static async Task<int> Capture(string url, string output, int seconds) {
        using var socket = new ClientWebSocket();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
        using var file = new FileStream(output, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        using var writer = new StreamWriter(file, new UTF8Encoding(false));
        byte[] chunk = new byte[65536]; long total=0; int messages=0;
        try {
            await socket.ConnectAsync(new Uri(url), timeout.Token);
            while (!timeout.IsCancellationRequested) {
                using var message = new MemoryStream(); WebSocketReceiveResult result;
                do {
                    result = await socket.ReceiveAsync(new ArraySegment<byte>(chunk), timeout.Token);
                    if (result.MessageType == WebSocketMessageType.Close) return messages;
                    if (result.MessageType != WebSocketMessageType.Text) throw new InvalidDataException("Non-text API frame");
                    message.Write(chunk,0,result.Count); total += result.Count;
                    if (message.Length > 8*1024*1024 || total > 128*1024*1024) throw new InvalidDataException("Capture size bound exceeded");
                } while (!result.EndOfMessage);
                await writer.WriteLineAsync(Encoding.UTF8.GetString(message.ToArray()));
                ++messages;
            }
        } catch (OperationCanceledException) when (timeout.IsCancellationRequested) { }
        finally { socket.Abort(); }
        return messages;
    }
}
'@
function Write-Evidence([string]$Name,$Data) {
    $file=[IO.File]::Open((Join-Path $OutputDirectory $Name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try { $bytes=[Text.Encoding]::UTF8.GetBytes(($Data | ConvertTo-Json -Depth 12)); $file.Write($bytes) }
    finally { $file.Dispose() }
}
$base="http://127.0.0.1:$Port"
Write-Evidence 'before.json' ([ordered]@{at=[DateTimeOffset]::Now; health=(Invoke-RestMethod "$base/v1/health"); snapshot=(Invoke-RestMethod "$base/v1/snapshot")})
$raw=[LightStreamEvidence]::Capture("ws://127.0.0.1:$Port/v1/stream",(Join-Path $OutputDirectory 'raw.jsonl'),$Seconds)
$smoothed=[LightStreamEvidence]::Capture("ws://127.0.0.1:$Port/v1/lights/smoothed/stream",(Join-Path $OutputDirectory 'smoothed.jsonl'),$Seconds)
[Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($raw,$smoothed)).GetAwaiter().GetResult() | Out-Null
Write-Evidence 'after.json' ([ordered]@{at=[DateTimeOffset]::Now; health=(Invoke-RestMethod "$base/v1/health"); snapshot=(Invoke-RestMethod "$base/v1/snapshot"); rawMessages=$raw.Result; smoothedMessages=$smoothed.Result})
[pscustomobject]@{OutputDirectory=$OutputDirectory; RawMessages=$raw.Result; SmoothedMessages=$smoothed.Result; Seconds=$Seconds}
