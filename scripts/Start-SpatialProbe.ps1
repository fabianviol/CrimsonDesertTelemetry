# Private observer:20 CPU controls at up to2Hz. With SpatialReadback=0 it is passive.
# SpatialReadback=1 opts into ONE fenced texture/GI/exposure transaction per process.
# v2 requires observed native root bindings/Dispatch; failure never becomes paired data.
# Neither mode changes the API; CPU scene/cache data still has no GPU pairing.
#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId)
$ErrorActionPreference='Stop'
$game=Get-Process -Id $ProcessId
if($game.ProcessName -ne 'CrimsonDesert'){throw 'Target is not CrimsonDesert.'}
$health=Invoke-RestMethod 'http://127.0.0.1:27311/v1/health' -TimeoutSec 3
if($health.status -ne 'playing' -or -not $health.supportedBuild){throw 'Wait for supported live telemetry before requesting this run.'}
try {$request=[Threading.EventWaitHandle]::OpenExisting("Local\CrimsonDesertTelemetry.SpatialProbe.$ProcessId")}
catch {throw 'Spatial probe event missing. Use the new private package with Research/SpatialProbe=1; restart required.'}
try {if(-not $request.Set()){throw 'Could not signal the spatial probe.'}}
finally {$request.Dispose()}
Write-Output "Start signal sent to PID $ProcessId. Check a NEW spatial-binding-PID-*.json and native log; busy requests are discarded."
