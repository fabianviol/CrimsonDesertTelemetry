#requires -Version 7.4
# One explicit private request, then return immediately; never waits for the owner.
# physics.3 rayobserve records ONE game-originated ray, not a camera/light test
# and not a replay. No target selector in that mode; inspect raw layout first.
# physics.4 rayreplay validates a same-context copy; raysegment changes its
# segment only after a matching control. Collision is NOT optical visibility.
# physics.5 rayfan uses nine same-context diagnostic rays at explicit 0/.05/.15
# gu offsets, NOT a measured source extent or an optical coverage percentage.
[CmdletBinding()]
param(
    [ValidateSet('observe','replay','segment','rayobserve','rayreplay','raysegment','rayfan')][string]$Mode = 'observe',
    # Segment target is an EXACT current filtered ManyLights sample. Its paired
    # camera supplies the start; never substitute the authored-light vector.
    [int]$LightSampleIndex = -1,
    # Select from the SAME fresh paired frame by proximity to a known light.
    # Avoid carrying a transient sample index from a previous HTTP response.
    [ValidateCount(3,3)][double[]]$NearLightPosition,
    # Diagnostic endpoint isolation ONLY, not a production occlusion tolerance.
    [ValidateRange(0,2)][double]$StopBeforeLight = 0,
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
$snapshotRequestTick = [Environment]::TickCount64
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
if ($Mode -in @('segment','raysegment','rayfan')) {
    if ($Mode -eq 'rayfan' -and ($GroundControl -or $StopBeforeLight -ne 0)) { throw 'Rayfan requires a full fresh light target.' }
    if (($GroundControl -and ($LightSampleIndex -ge 0 -or $NearLightPosition)) -or
        ($LightSampleIndex -ge 0 -and $NearLightPosition)) { throw 'Choose one target selector.' }
    if ($GroundControl) {
        if ($StopBeforeLight -ne 0) { throw 'Endpoint isolation applies only to light targets.' }
        $request.start = @(($player[0]+0.6), ($player[1]+1.5), ($player[2]+0.6))
        $request.end = @(($player[0]+0.7), ($player[1]-3), ($player[2]+0.67))
    } else {
        $rendered = $snapshot.lights.rendered
        if (($LightSampleIndex -lt 0 -and -not $NearLightPosition) -or $rendered.status -ne 'available' -or $null -eq $rendered.camera -or
            $null -eq $rendered.ageMilliseconds -or $rendered.ageMilliseconds -gt 250) {
            throw 'Requires a fresh paired ManyLights frame and -LightSampleIndex, or explicit -GroundControl.'
        }
        $matches = @($rendered.sources | Where-Object sampleIndex -eq $LightSampleIndex)
        if ($NearLightPosition) {
            foreach($coordinate in $NearLightPosition) { if(-not [double]::IsFinite($coordinate)) { throw 'Invalid target reference.' } }
            $matches = @($rendered.sources | Where-Object {
                $p=$_.position
                [math]::Pow($p.x-$NearLightPosition[0],2)+[math]::Pow($p.y-$NearLightPosition[1],2)+[math]::Pow($p.z-$NearLightPosition[2],2) -le 0.25
            } | Sort-Object {
                $p=$_.position
                [math]::Pow($p.x-$NearLightPosition[0],2)+[math]::Pow($p.y-$NearLightPosition[1],2)+[math]::Pow($p.z-$NearLightPosition[2],2)
            } | Select-Object -First 1)
        }
        if ($matches.Count -ne 1) { throw 'Light sample is absent/ambiguous in the current frame. No request sent.' }
        $p = $rendered.camera.position; $light = $matches[0].position
        $request.start = @($p.x, $p.y, $p.z)
        $request.end = @($light.x, $light.y, $light.z)
        $request.lightSampleIndex = $matches[0].sampleIndex
        $request.lightFrame = $rendered.frameNumber
        if ($Mode -eq 'rayfan') {
            $request.sourceAgeAtRequestMilliseconds = $rendered.ageMilliseconds
            # Count the HTTP round trip conservatively in the native age guard.
            $request.issuedTickMs = $snapshotRequestTick
        }
        if ($StopBeforeLight -gt 0) {
            $target = @($request.end)
            $delta = @(0..2 | ForEach-Object { $target[$_] - $request.start[$_] })
            $length = [math]::Sqrt(($delta | ForEach-Object { $_*$_ } | Measure-Object -Sum).Sum)
            if ($length -le $StopBeforeLight+0.05) { throw 'Endpoint isolation would remove the entire segment.' }
            $request.originalLightPosition = $target
            $request.stopBeforeLight = $StopBeforeLight
            $request.end = @(0..2 | ForEach-Object { $target[$_] - $delta[$_]*$StopBeforeLight/$length })
        }
    }
    # All endpoint validation is repeated natively before any extra call.
    $squared = 0.0
    for ($i=0; $i -lt 3; $i++) {
        $d = $request.end[$i]-$request.start[$i]
        if (-not [double]::IsFinite($d) -or [math]::Abs($d) -lt 0.0001) { throw 'Invalid or unverified zero-component segment.' }
        $squared += $d*$d
    }
    if ($squared -gt 2500 -or $squared -lt 0.0025) { throw 'Segment length outside 0.05..50 game units.' }
} elseif ($GroundControl -or $LightSampleIndex -ge 0 -or $NearLightPosition -or $StopBeforeLight -ne 0) { throw 'Targets require -Mode segment or raysegment.' }
$temporary = Join-Path $gameDirectory "physics-probe-request-$requestId.tmp"
$destination = Join-Path $gameDirectory 'physics-probe-request.json'
[IO.File]::WriteAllText($temporary, ($request | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
[IO.File]::Move($temporary, $destination, $true)
Write-Output "Physics $Mode requested. Result: $(Join-Path $gameDirectory "physics-probe-$($game.Id)-$requestId.json")"
