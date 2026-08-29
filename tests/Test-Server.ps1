param(
    [string]$Executable,
    [int]$Port = 27312
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $PSScriptRoot '..\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.exe'
}
if (-not (Test-Path -LiteralPath $Executable)) { throw "Server executable is missing: $Executable" }

$server = Start-Process -FilePath $Executable -ArgumentList 'serve', $Port, 120 -PassThru -WindowStyle Hidden
try {
    $health = $null
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/health" -TimeoutSec 1
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if ($null -eq $health) { throw 'Health endpoint did not become ready.' }
    if ($health.status -ne 'waiting-for-game' -or $health.sampleRateHz -ne 120) {
        throw "Unexpected health response: $($health | ConvertTo-Json -Compress)"
    }
    if ($null -ne $health.supportedBuild) { throw 'Build support must be unknown while the game is absent.' }

    $localOrigin = Invoke-WebRequest -Uri "http://127.0.0.1:$port/v1/health" -Headers @{ Origin = 'http://127.0.0.1:8080' }
    if ($localOrigin.Headers['Access-Control-Allow-Origin'] -ne 'http://127.0.0.1:8080') {
        throw 'Loopback browser origin was not allowed.'
    }
    $remoteOrigin = Invoke-WebRequest -Uri "http://127.0.0.1:$port/v1/health" -Headers @{ Origin = 'https://example.com' }
    if ($remoteOrigin.Headers.ContainsKey('Access-Control-Allow-Origin')) {
        throw 'Remote browser origin was allowed.'
    }

    $schema = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/schema"
    if ($schema.title -ne 'Crimson Desert Telemetry snapshot v1') { throw 'Schema endpoint mismatch.' }

    try {
        Invoke-WebRequest -Uri "http://127.0.0.1:$port/v1/snapshot" | Out-Null
        throw 'Snapshot unexpectedly succeeded without a running game.'
    } catch {
        if ([int]$_.Exception.Response.StatusCode -ne 503) { throw }
    }

    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    try {
        $null = $socket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/stream",
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($socket.State -ne [System.Net.WebSockets.WebSocketState]::Open) {
            throw 'WebSocket did not open.'
        }
        $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
            'test complete', [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    } finally {
        $socket.Dispose()
    }

    $remoteSocket = [System.Net.WebSockets.ClientWebSocket]::new()
    try {
        $remoteSocket.Options.SetRequestHeader('Origin', 'https://example.com')
        try {
            $null = $remoteSocket.ConnectAsync([Uri]"ws://127.0.0.1:$port/v1/stream",
                [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            throw 'Remote WebSocket browser origin was allowed.'
        } catch [System.Net.WebSockets.WebSocketException] {
            # Expected: only loopback browser origins are accepted.
        }
    } finally {
        $remoteSocket.Dispose()
    }

    Start-Sleep -Milliseconds 100
    $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/v1/health"
    if ($health.connectedClients -ne 0) { throw 'WebSocket subscriber was not removed.' }
    Write-Output 'PASS server HTTP and WebSocket smoke test'
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
}
