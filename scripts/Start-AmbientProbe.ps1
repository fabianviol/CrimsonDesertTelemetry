# Private diagnostic control: starts one bounded run in the already loaded ASI.
# No memory patching, Explorer enablement or public API/schema change.
#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId)
$ErrorActionPreference = 'Stop'
$game = Get-Process -Id $ProcessId -ErrorAction Stop
if ($game.ProcessName -ne 'CrimsonDesert') { throw 'Target is not CrimsonDesert.' }
$eventName = "Local\CrimsonDesertTelemetry.AmbientProbe.$ProcessId"
try { $request = [Threading.EventWaitHandle]::OpenExisting($eventName) }
catch { throw "Ambient start event unavailable for PID $ProcessId. Install the manually triggered diagnostic and enable Research/AmbientProbe=1 before restarting." }
try {
    if (-not $request.Set()) { throw 'Could not signal ambient start.' }
} finally { $request.Dispose() }
Write-Output "Start signal sent to PID $ProcessId. Check the native log for acceptance and a NEW ambient-probe file; busy/faulted captures refuse the request."
