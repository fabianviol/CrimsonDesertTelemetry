#requires -Version 7.4
# One observation request, then return immediately. No live memory writes or replay.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$games = @(Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) { throw 'Exactly one CrimsonDesert process must be running.' }
$game = $games[0]
$gameDirectory = Split-Path -Parent $game.Path
$ini = Get-Content -LiteralPath (Join-Path $gameDirectory 'CrimsonDesertTelemetry.ini') -Raw
if ($ini -notmatch '(?ms)^\[Research\]\s*\r?\n(?:(?!^\[).)*?^PhysicsProbe=1\s*$') {
    throw 'Requires the private physics package with Research/PhysicsProbe=1, then a game restart.'
}
$health = Invoke-RestMethod 'http://127.0.0.1:27311/v1/health' -TimeoutSec 3
if ($health.status -ne 'playing' -or -not $health.supportedBuild) { throw 'Live supported telemetry required.' }
$snapshot = Invoke-RestMethod 'http://127.0.0.1:27311/v1/snapshot' -TimeoutSec 3
$age = ([DateTimeOffset]::UtcNow - [DateTimeOffset]$snapshot.capturedAt).TotalMilliseconds
if ($age -lt -100 -or $age -gt 500 -or $snapshot.game.state -ne 'playing' -or $null -eq $snapshot.player.position) {
    throw 'A fresh player position is required. No request sent.'
}
$point = $snapshot.player.position
$player = @([single]$point.x, [single]$point.y, [single]$point.z)
foreach ($coordinate in $player) {
    if (-not [single]::IsFinite($coordinate) -or [math]::Abs($coordinate) -gt 1000000) { throw 'Invalid player coordinate.' }
}
$requestId = [Guid]::NewGuid().ToString('N')
$request = [ordered]@{
    id = $requestId; mode = 'observe'; pid = $game.Id
    processStartFileTime = [uint64]$game.StartTime.ToUniversalTime().ToFileTimeUtc()
    player = $player; issuedTickMs = [Environment]::TickCount64
    snapshotSequence = $snapshot.sequence; snapshotCapturedAt = $snapshot.capturedAt
}
$temporary = Join-Path $gameDirectory "physics-probe-request-$requestId.tmp"
$destination = Join-Path $gameDirectory 'physics-probe-request.json'
[IO.File]::WriteAllText($temporary, ($request | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
[IO.File]::Move($temporary, $destination, $true)
Write-Output "Observation requested (no replay). Result: $(Join-Path $gameDirectory "physics-probe-$($game.Id)-$requestId.json")"
