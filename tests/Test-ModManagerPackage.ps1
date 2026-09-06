param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$ArchivePath,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$expectedNames = @(
    'CrimsonDesertTelemetry.asi',
    'CrimsonDesertTelemetry.ini',
    'CrimsonDesertTelemetry.Core.dll',
    'crimson-desert-telemetry.dll',
    'crimson-desert-telemetry.deps.cfg',
    'crimson-desert-telemetry.runtimeconfig.cfg',
    'README.txt',
    'THIRD-PARTY-NOTICES.txt',
    'LICENSE.txt'
)

function Assert-PackageNames([string[]]$Names) {
    if (@($Names | Where-Object { $_ -match '(?i)\.json$' }).Count) {
        throw 'Loose JSON is forbidden: mod managers interpret it as game-patch data.'
    }
    if (@(Compare-Object $expectedNames $Names).Count) {
        throw 'Package contents differ from the ASI companion-file contract.'
    }
}

function Assert-Payload([hashtable]$Files) {
    Assert-PackageNames @($Files.Keys)
    $deps = [Text.Encoding]::UTF8.GetString($Files['crimson-desert-telemetry.deps.cfg']) | ConvertFrom-Json
    $runtime = [Text.Encoding]::UTF8.GetString($Files['crimson-desert-telemetry.runtimeconfig.cfg']) | ConvertFrom-Json
    if (-not $deps.runtimeTarget.name -or $null -eq $deps.targets) { throw 'Invalid .NET dependency metadata.' }
    if ($null -eq $runtime.runtimeOptions) { throw 'Invalid .NET runtime configuration.' }
    $ini = [Text.Encoding]::UTF8.GetString($Files['CrimsonDesertTelemetry.ini'])
    if ($ini -notmatch '(?m)^\[Overlay\]' -or $ini -notmatch '(?m)^ToggleKey=119\r?$') { throw 'Missing overlay configuration.' }
    $overlay = [regex]::Match($ini, '(?ms)^\[Overlay\]\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
    foreach ($setting in @('Enabled=1', 'Radar3D=1')) {
        if ($overlay -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) {
            throw "Missing light HUD preview default: $setting"
        }
    }
    $worldOverlay = [regex]::Match($ini, '(?ms)^\[LightOverlay\]\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
    foreach ($setting in @('Enabled=1', 'InitiallyVisible=1', 'ToggleKey=121', 'Radius=35', 'MaxMarkers=512', 'MaxLabels=6')) {
        if ($worldOverlay -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) {
            throw "Missing independent world-light overlay default: $setting"
        }
    }
    $notifications = [regex]::Match($ini, '(?ms)^\[Notifications\]\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
    if ($notifications -notmatch '(?m)^Enabled=1\r?$' -or $notifications -notmatch '(?m)^DurationMilliseconds=6000\r?$') {
        throw 'Missing independent startup notification defaults.'
    }
    $lights = [regex]::Match($ini, '(?ms)^\[Lights\]\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
    foreach ($setting in @('Enabled=1', 'NearbyRadius=100', 'ManyLights=1', 'ManyLightsSampleRateHz=20')) {
        if ($lights -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) {
            throw "Missing unified light default: $setting"
        }
    }
    foreach ($setting in @('EnableConsole=0', 'EnableExplorer=0', 'AllowWrite=0', 'AllowCall=0',
        'AllowDebugCommands=0', 'AllowBreakpoints=0', 'AllowManyLights=0')) {
        if ($ini -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) {
            throw "Research instruments must be opt-in: $setting"
        }
    }
    $notices = [Text.Encoding]::UTF8.GetString($Files['THIRD-PARTY-NOTICES.txt'])
    foreach ($dependency in @('Dear ImGui', 'MinHook', 'JSON for Modern C++', 'Tristan Grimmer', 'Sean Barrett')) {
        if (-not $notices.Contains($dependency)) { throw "Missing third-party attribution: $dependency" }
    }
    foreach ($name in @('CrimsonDesertTelemetry.asi', 'CrimsonDesertTelemetry.Core.dll', 'crimson-desert-telemetry.dll')) {
        $bytes = $Files[$name]
        if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
            throw "Not a Windows PE binary: $name"
        }
    }
}

function Assert-Rejected([scriptblock]$Test, [string]$Message) {
    $rejected = $false
    try { & $Test } catch { $rejected = $true }
    if (-not $rejected) { throw $Message }
}

if ($SelfTest) {
    Assert-PackageNames $expectedNames
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'crimson-desert-telemetry.deps.json') } 'Old dependency JSON was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'crimson-desert-telemetry.runtimeconfig.json') } 'Old runtime JSON was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames | Where-Object { $_ -ne 'crimson-desert-telemetry.deps.cfg' }) } 'Missing dependency metadata was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'unexpected.exe') } 'A loose EXE was accepted.'
    Assert-Rejected { Assert-PackageNames ($expectedNames + 'CrimsonHueConsole.asi') } 'A second console ASI was accepted.'
    Write-Output 'PASS package validator regression cases'
}

$directoryPath = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\') + '\'
$directoryFiles = @{}
foreach ($file in Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File -Force) {
    $relative = $file.FullName.Substring($directoryPath.Length).Replace('\', '/')
    $directoryFiles[$relative] = [IO.File]::ReadAllBytes($file.FullName)
}
Assert-Payload $directoryFiles
Write-Output 'PASS expanded ASI package (no loose JSON or EXE)'

$archiveFiles = @{}
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).Path)
try {
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ($name.EndsWith('/')) { continue }
        $prefix = 'CrimsonDesertTelemetry/'
        if (-not $name.StartsWith($prefix, [StringComparison]::Ordinal)) { throw "Unexpected ZIP root: $name" }
        $relative = $name.Substring($prefix.Length)
        if ($archiveFiles.ContainsKey($relative)) { throw "Duplicate ZIP entry: $name" }
        $stream = $entry.Open()
        $buffer = New-Object IO.MemoryStream
        try { $stream.CopyTo($buffer); $archiveFiles[$relative] = $buffer.ToArray() }
        finally { $stream.Dispose(); $buffer.Dispose() }
    }
} finally { $zip.Dispose() }
Assert-Payload $archiveFiles
foreach ($name in $expectedNames) {
    if ([Convert]::ToBase64String($directoryFiles[$name]) -ne [Convert]::ToBase64String($archiveFiles[$name])) {
        throw "ZIP and expanded payload differ: $name"
    }
}
Write-Output 'PASS ZIP payload matches the validated expanded package'
