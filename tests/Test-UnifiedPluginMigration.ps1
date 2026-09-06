$ErrorActionPreference = 'Stop'
$migrationScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\Install-UnifiedPlugin.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('telemetry-migration-test-' + [Guid]::NewGuid().ToString('N'))
$fixtureGame = Join-Path $fixtureRoot 'game'
$fixturePackage = Join-Path $fixtureRoot 'package'
New-Item -ItemType Directory -Path $fixtureGame, $fixturePackage | Out-Null

function Check([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}
# A deterministic process fixture; the installer never targets the actual game.
$global:telemetryMigrationTestRunning = $false
function Get-Process { param([string]$Name) if ($global:telemetryMigrationTestRunning) { [pscustomobject]@{ Id = -1; Name = $Name } } }

$payload = @{
    'CrimsonDesertTelemetry.asi' = 'MZnew native'
    'CrimsonDesertTelemetry.ini' = '[Server]'
    'CrimsonDesertTelemetry.Core.dll' = 'MZnew core'
    'crimson-desert-telemetry.dll' = 'MZnew host'
    'crimson-desert-telemetry.deps.cfg' = '{"runtimeTarget":{"name":"test"},"targets":{}}'
    'crimson-desert-telemetry.runtimeconfig.cfg' = '{"runtimeOptions":{}}'
    'README.txt' = 'package documentation'
    'THIRD-PARTY-NOTICES.txt' = 'package notices'
    'LICENSE.txt' = 'package license'
}
foreach ($name in $payload.Keys) { [IO.File]::WriteAllText((Join-Path $fixturePackage $name), $payload[$name]) }
[IO.File]::WriteAllText((Join-Path $fixtureGame 'CrimsonDesert.exe'), 'MZfixture')
$originals = @{
    'CrimsonDesertTelemetry.asi' = 'old telemetry'
    'CrimsonDesertTelemetry.ini' = 'custom telemetry config'
    'CrimsonDesertTelemetry.Core.dll' = 'old core'
    'crimson-desert-telemetry.deps.json' = 'old metadata'
    'CrimsonHueConsole.asi' = 'old console'
    'CrimsonHueConsole.ini' = 'custom console config'
    'inject_cmd.txt' = 'unexecuted old command'
}
$untouched = @{ 'winmm.dll' = 'loader'; 'dinput8.dll' = 'other loader'; 'another-mod.asi' = 'unrelated mod'; 'README.txt' = 'unrelated documentation' }
foreach ($set in @($originals, $untouched)) {
    foreach ($name in $set.Keys) { [IO.File]::WriteAllText((Join-Path $fixtureGame $name), $set[$name]) }
}
$global:telemetryMigrationTestRunning = $true
Rejected { & $migrationScript -GameDirectory $fixtureGame -PackageDirectory $fixturePackage } 'Running game was accepted.'
$global:telemetryMigrationTestRunning = $false
& $migrationScript -GameDirectory $fixtureGame -PackageDirectory $fixturePackage -WhatIf | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $fixtureGame 'CrimsonDesertTelemetry-backups'))) 'Dry run mutated the installation.'
[IO.File]::WriteAllText((Join-Path $fixturePackage 'CrimsonHueConsole.asi'), 'extra')
Rejected { & $migrationScript -GameDirectory $fixtureGame -PackageDirectory $fixturePackage } 'Package with a second ASI was accepted.'
# Exact disposable fixture file, never an installation file.
Remove-Item -LiteralPath (Join-Path $fixturePackage 'CrimsonHueConsole.asi')
& $migrationScript -GameDirectory $fixtureGame -PackageDirectory $fixturePackage | Out-Null
$backup = @(Get-ChildItem -LiteralPath (Join-Path $fixtureGame 'CrimsonDesertTelemetry-backups') -Directory)[0].FullName
foreach ($name in $originals.Keys) {
    Check ([IO.File]::ReadAllText((Join-Path $backup ($name + '.disabled'))) -eq $originals[$name]) "Original not preserved: $name"
}
Check (-not (Test-Path -LiteralPath (Join-Path $fixtureGame 'CrimsonHueConsole.asi'))) 'Old console ASI remains active.'
Check (@(Get-ChildItem -LiteralPath $backup -Recurse -File -Filter '*.asi').Count -eq 0) 'Backup contains a loadable ASI.'
Check ([IO.File]::ReadAllText((Join-Path $fixtureGame 'CrimsonDesertTelemetry.asi')) -eq $payload['CrimsonDesertTelemetry.asi']) 'Unified ASI was not installed.'
foreach ($name in $untouched.Keys) {
    Check ([IO.File]::ReadAllText((Join-Path $fixtureGame $name)) -eq $untouched[$name]) "Unrelated file changed: $name"
}
& $migrationScript -GameDirectory $fixtureGame -RestoreFrom $backup | Out-Null
foreach ($set in @($originals, $untouched)) {
    foreach ($name in $set.Keys) {
        Check ([IO.File]::ReadAllText((Join-Path $fixtureGame $name)) -eq $set[$name]) "Restore lost file: $name"
    }
}
Check (-not (Test-Path -LiteralPath (Join-Path $fixtureGame 'crimson-desert-telemetry.dll'))) 'Restore left a newly introduced runtime file active.'
Check (Test-Path -LiteralPath (Join-Path $backup 'CrimsonHueConsole.asi.disabled')) 'Restore consumed the original backup.'
Write-Output 'PASS migration: running-game refusal, dry run, single ASI, backups, unrelated files, complete restore'
Write-Output "Retained isolated test fixture: $fixtureRoot"
