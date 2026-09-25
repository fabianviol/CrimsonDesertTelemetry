#requires -Version 7.4
# One explicit private request, then return immediately; never waits for the owner.
[CmdletBinding()]
param(
    [ValidateSet('observe','replay','segment')][string]$Mode = 'observe',
    # Segment target is an EXACT current filtered ManyLights sample. Its paired
    # camera supplies the start; never substitute the authored-light vector.
    [int]$LightSampleIndex = -1,
    # Bounded downward positive control, with nonzero horizontal components
    # because the engine's zero-inverse convention is not yet established.
    [switch]$GroundControl
)
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
    id = $requestId; mode = $Mode; pid = $game.Id
    processStartFileTime = [uint64]$game.StartTime.ToUniversalTime().ToFileTimeUtc()
    player = $player; issuedTickMs = [Environment]::TickCount64
    snapshotSequence = $snapshot.sequence; snapshotCapturedAt = $snapshot.capturedAt
}
if ($Mode -eq 'segment') {
    if ($GroundControl -and $LightSampleIndex -ge 0) { throw 'Choose ground OR light, not both.' }
    if ($GroundControl) {
        $request.start = @(($player[0]+0.6), ($player[1]+1.5), ($player[2]+0.6))
        $request.end = @(($player[0]+0.7), ($player[1]-3), ($player[2]+0.67))
    } else {
        $rendered = $snapshot.lights.rendered
        if ($LightSampleIndex -lt 0 -or $rendered.status -ne 'available' -or $null -eq $rendered.camera -or
            $null -eq $rendered.ageMilliseconds -or $rendered.ageMilliseconds -gt 250) {
            throw 'Requires a fresh paired ManyLights frame and -LightSampleIndex, or explicit -GroundControl.'
        }
        $matches = @($rendered.sources | Where-Object sampleIndex -eq $LightSampleIndex)
        if ($matches.Count -ne 1) { throw 'Light sample is absent/ambiguous in the current frame. No request sent.' }
        $p = $rendered.camera.position; $light = $matches[0].position
        $request.start = @($p.x, $p.y, $p.z)
        $request.end = @($light.x, $light.y, $light.z)
        $request.lightSampleIndex = $LightSampleIndex
        $request.lightFrame = $rendered.frameNumber
    }
    # All endpoint validation is repeated natively before any extra call.
    $squared = 0.0
    for ($i=0; $i -lt 3; $i++) {
        $d = $request.end[$i]-$request.start[$i]
        if (-not [double]::IsFinite($d) -or [math]::Abs($d) -lt 0.0001) { throw 'Invalid or unverified zero-component segment.' }
        $squared += $d*$d
    }
    if ($squared -gt 2500 -or $squared -lt 0.0025) { throw 'Segment length outside 0.05..50 game units.' }
} elseif ($GroundControl -or $LightSampleIndex -ge 0) { throw 'Targets require -Mode segment.' }
$temporary = Join-Path $gameDirectory "physics-probe-request-$requestId.tmp"
$destination = Join-Path $gameDirectory 'physics-probe-request.json'
[IO.File]::WriteAllText($temporary, ($request | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
[IO.File]::Move($temporary, $destination, $true)
Write-Output "Physics $Mode requested. Result: $(Join-Path $gameDirectory "physics-probe-$($game.Id)-$requestId.json")"
