# One bounded, read-only pass over every public transport/feed in a running
# private integration build. No polling for the owner or for game readiness.
#requires -Version 7.4
[CmdletBinding()]
param(
    [ValidateRange(1024,65535)][int]$Port = 27311,
    [Parameter(Mandatory)][string]$OutFile
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath((Join-Path $root 'artifacts')) + [IO.Path]::DirectorySeparatorChar
$output = [IO.Path]::GetFullPath($OutFile)
if (-not $output.StartsWith($artifacts,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $output)) { throw 'Choose a new output file below artifacts/.' }

function Read-Http([string]$Path) {
    try { return Invoke-RestMethod -Uri "http://127.0.0.1:$Port$Path" -TimeoutSec 3 }
    catch { return [pscustomobject]@{status='request-failed'; reason=$_.Exception.Message} }
}
function Read-WebSocket([string]$Path) {
    $socket = [Net.WebSockets.ClientWebSocket]::new()
    $deadline = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(4))
    try {
        $socket.ConnectAsync([Uri]"ws://127.0.0.1:$Port$Path",$deadline.Token).GetAwaiter().GetResult()
        $buffer = [byte[]]::new(65536)
        $segment = [ArraySegment[byte]]::new($buffer)
        $stream = [IO.MemoryStream]::new()
        try {
            do {
                $part = $socket.ReceiveAsync($segment,$deadline.Token).GetAwaiter().GetResult()
                if ($part.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text) {
                    throw 'WebSocket did not send a text snapshot.'
                }
                if ($stream.Length + $part.Count -gt 4MB) { throw 'WebSocket snapshot exceeds 4 MiB.' }
                $stream.Write($buffer,0,$part.Count)
            } until ($part.EndOfMessage)
            $value = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            $state = if ($value.game) { $value.game.state } else { $value.status }
            return [pscustomobject]@{status='received'; state=$state; bytes=$stream.Length}
        } finally { $stream.Dispose() }
    }
    catch { return [pscustomobject]@{status='request-failed'; reason=$_.Exception.Message} }
    finally { $socket.Dispose(); $deadline.Dispose() }
}

$health = Read-Http '/v1/health'
$snapshot = Read-Http '/v1/snapshot'
$smoothed = Read-Http '/v1/lights/smoothed'
$ambient = Read-Http '/v1/ambient'
$rendered = $snapshot.lights.rendered
$sources = @($rendered.sources | Where-Object { $null -ne $_ })
$visibility = @($sources | Where-Object { $_.sourceVisibility.status -eq 'clear' -or
    $_.sourceVisibility.status -eq 'blocked' })
$report = [ordered]@{
    checkedAtUtc = [DateTimeOffset]::UtcNow
    gameProcessId = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    health = [ordered]@{ status=$health.status; compatibility=$health.compatibility.mode;
        supportedBuild=$health.supportedBuild; error=$health.error }
    snapshot = [ordered]@{ state=$snapshot.game.state; player=$null -ne $snapshot.player;
        camera=$null -ne $snapshot.camera; orientation=$null -ne $snapshot.player.orientation;
        authoredStatus=$snapshot.lights.status; renderedStatus=$rendered.status;
        renderedCount=$sources.Count; visibilityKnownCount=$visibility.Count;
        reason=$rendered.unavailableReason }
    smoothed = [ordered]@{status=$smoothed.status; groupCount=@($smoothed.sources | Where-Object { $null -ne $_ }).Count;
        reason=$smoothed.unavailableReason}
    ambient = [ordered]@{status=$ambient.status; sky=$null -ne $ambient.sky;
        cameraVisibility=$null -ne $ambient.visibility; reason=$ambient.reason}
    websocket = [ordered]@{
        raw = Read-WebSocket '/v1/stream'
        smoothed = Read-WebSocket '/v1/lights/smoothed/stream'
        ambient = Read-WebSocket '/v1/ambient/stream'
    }
}
$parent = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $output -NoNewline
$report | ConvertTo-Json -Depth 12
Write-Output "Saved integrated one-pass report: $output"
