#requires -Version 7.4
# Signal exactly one private snapshot. No sleep, polling or game-memory writes.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$games = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) { throw 'Exactly one CrimsonDesert process must be running.' }
$health = Invoke-RestMethod 'http://127.0.0.1:27311/v1/health' -TimeoutSec 3
if ($health.status -ne 'playing' -or -not $health.supportedBuild) { throw 'Loaded supported world required.' }
$event = [Threading.EventWaitHandle]::OpenExisting("Local\CrimsonDesertTelemetry.ManyLightsPair.$($games[0].Id)")
try {
    if (-not $event.Set()) { throw 'Could not signal the diagnostic event.' }
} finally { $event.Dispose() }
Write-Output 'One paired ManyLights snapshot requested. This is not confirmation of completion; check the native log and new manylights-pair-*.bin in the plugin directory.'
