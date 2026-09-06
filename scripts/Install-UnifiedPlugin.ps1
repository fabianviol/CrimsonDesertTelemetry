[CmdletBinding(SupportsShouldProcess = $true, DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$GameDirectory,
    [Parameter(Mandatory = $true, ParameterSetName = 'Restore')][string]$RestoreFrom
)

$ErrorActionPreference = 'Stop'
if (Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue) {
    throw 'Close Crimson Desert before installing or restoring the native plugin.'
}
$gamePath = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath.TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $gamePath 'CrimsonDesert.exe') -PathType Leaf)) {
    throw 'GameDirectory must be the bin64 directory containing CrimsonDesert.exe.'
}

function Assert-Regular([string]$Path, [bool]$Directory = $false) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -ne $Directory -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected a regular $(if ($Directory) { 'directory' } else { 'file' }): $Path"
    }
}
Assert-Regular $gamePath $true

$runtimeNames = @('CrimsonDesertTelemetry.asi', 'CrimsonDesertTelemetry.ini',
    'CrimsonDesertTelemetry.Core.dll', 'crimson-desert-telemetry.dll',
    'crimson-desert-telemetry.deps.cfg', 'crimson-desert-telemetry.runtimeconfig.cfg')
$legacyNames = @('CrimsonHueConsole.asi', 'CrimsonHueConsole.ini', 'CrimsonHueConsole-firesession.ini',
    'crimson-desert-telemetry.deps.json', 'crimson-desert-telemetry.runtimeconfig.json',
    'inject_cmd.txt', 'inject_result.txt')
$scopedNames = $runtimeNames + $legacyNames
$backupRoot = Join-Path $gamePath 'CrimsonDesertTelemetry-backups'
if (Test-Path -LiteralPath $backupRoot) { Assert-Regular $backupRoot $true }

function New-BackupDirectory([string]$Kind) {
    $stamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $path = [IO.Path]::GetFullPath((Join-Path $backupRoot "$Kind-$stamp"))
    if (-not $path.StartsWith($gamePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Backup path escaped the verified game directory.'
    }
    New-Item -ItemType Directory -Path $path | Out-Null
    return $path
}

if ($PSCmdlet.ParameterSetName -eq 'Restore') {
    $restorePath = (Resolve-Path -LiteralPath $RestoreFrom).ProviderPath.TrimEnd('\')
    if (-not $restorePath.StartsWith($backupRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Parent $restorePath) -ne $backupRoot) {
        throw 'RestoreFrom must be one direct migration backup under this game directory.'
    }
    Assert-Regular $restorePath $true
    $manifestPath = Join-Path $restorePath 'migration.cfg'
    Assert-Regular $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.version -ne 1 -or $manifest.gameDirectory -ne $gamePath -or
        @($manifest.installed).Count -ne $runtimeNames.Count -or
        @(Compare-Object $runtimeNames @($manifest.installed)).Count) {
        throw 'Migration manifest does not describe this game or this plugin.'
    }
    $originals = @{}
    foreach ($entry in $manifest.originals) {
        if ($entry.name -notin $scopedNames -or $originals.ContainsKey($entry.name) -or
            $entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
            throw 'Invalid or duplicate original file in migration manifest.'
        }
        $stored = Join-Path $restorePath ($entry.name + '.disabled')
        $target = Join-Path $gamePath $entry.name
        if (Test-Path -LiteralPath $stored) {
            Assert-Regular $stored
            if ((Get-FileHash -LiteralPath $stored -Algorithm SHA256).Hash -ne $entry.sha256) {
                throw "Backup checksum differs: $stored"
            }
            $originals[$entry.name] = $stored
        } elseif ((Test-Path -LiteralPath $target -PathType Leaf) -and
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $entry.sha256) {
            # Interrupted before this original was moved: it is already intact.
            $originals[$entry.name] = $null
        } else {
            throw "Original is absent from both backup and game: $($entry.name)"
        }
    }
    $restoreNames = @($runtimeNames + @($originals.Keys) | Select-Object -Unique)
    foreach ($name in $restoreNames) {
        $target = Join-Path $gamePath $name
        if (Test-Path -LiteralPath $target) { Assert-Regular $target }
    }
    if (-not $PSCmdlet.ShouldProcess($gamePath, "Restore originals from $restorePath; preserve current files")) { return }
    $displaced = New-BackupDirectory 'before-restore'
    foreach ($name in $restoreNames) {
        if ($originals.ContainsKey($name) -and $null -eq $originals[$name]) { continue }
        $target = Join-Path $gamePath $name
        if (Test-Path -LiteralPath $target) {
            Move-Item -LiteralPath $target -Destination (Join-Path $displaced ($name + '.disabled'))
        }
        if ($originals.ContainsKey($name)) {
            Copy-Item -LiteralPath $originals[$name] -Destination $target
        }
    }
    Write-Output "Restored original installation. Replaced files retained in: $displaced"
    return
}

$packagePath = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath.TrimEnd('\')
Assert-Regular $packagePath $true
if ($packagePath -eq $gamePath -or $packagePath.StartsWith($gamePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an expanded release package outside the game directory.'
}
$expectedNames = $runtimeNames + @('README.txt', 'THIRD-PARTY-NOTICES.txt', 'LICENSE.txt')
$packageItems = @(Get-ChildItem -LiteralPath $packagePath -Force)
if ($packageItems.Count -ne $expectedNames.Count -or @(Compare-Object $expectedNames @($packageItems.Name)).Count) {
    throw 'Package contents must match the unified single-ASI release exactly.'
}
foreach ($item in $packageItems) { Assert-Regular $item.FullName }
foreach ($name in @('CrimsonDesertTelemetry.asi', 'CrimsonDesertTelemetry.Core.dll', 'crimson-desert-telemetry.dll')) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $packagePath $name))
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "Invalid PE payload: $name" }
}
$runtime = Get-Content -LiteralPath (Join-Path $packagePath 'crimson-desert-telemetry.runtimeconfig.cfg') -Raw | ConvertFrom-Json
$deps = Get-Content -LiteralPath (Join-Path $packagePath 'crimson-desert-telemetry.deps.cfg') -Raw | ConvertFrom-Json
if ($null -eq $runtime.runtimeOptions -or -not $deps.runtimeTarget.name -or $null -eq $deps.targets) {
    throw 'Package .NET runtime metadata is invalid.'
}

$originalEntries = @()
foreach ($name in $scopedNames) {
    $path = Join-Path $gamePath $name
    if (Test-Path -LiteralPath $path) {
        Assert-Regular $path
        $originalEntries += [pscustomobject]@{ name = $name; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    }
}
# Known alternate loader directories are a conflict, not a reason to touch other mods.
foreach ($subdirectory in @('scripts', 'plugins')) {
    foreach ($name in @('CrimsonDesertTelemetry.asi', 'CrimsonHueConsole.asi')) {
        $otherCopy = Join-Path $gamePath "$subdirectory\$name"
        if (Test-Path -LiteralPath $otherCopy) {
            throw "A second plugin copy exists outside the root installation; resolve it first: $otherCopy"
        }
    }
}
if (-not $PSCmdlet.ShouldProcess($gamePath, 'Back up old telemetry/console files and install the unified ASI')) { return }
$backup = New-BackupDirectory 'before-install'
$manifest = [ordered]@{
    version = 1
    gameDirectory = $gamePath
    packageDirectory = $packagePath
    installed = $runtimeNames
    originals = $originalEntries
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'migration.cfg') -Encoding utf8
try {
    foreach ($entry in $originalEntries) {
        # Exact files only; .disabled suffixes prevent ASI loaders finding backups.
        Move-Item -LiteralPath (Join-Path $gamePath $entry.name) -Destination (Join-Path $backup ($entry.name + '.disabled'))
    }
    foreach ($name in $runtimeNames) {
        $source = Join-Path $packagePath $name
        $target = Join-Path $gamePath $name
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
            throw "Installed checksum differs: $name"
        }
    }
} catch {
    throw "Migration stopped: $($_.Exception.Message) Originals are retained at $backup. Use -RestoreFrom with that path."
}
Write-Output "Unified plugin installed. Existing configuration was preserved in: $backup"
Write-Output "Restore: pwsh -NoProfile -File `"$PSCommandPath`" -GameDirectory `"$gamePath`" -RestoreFrom `"$backup`""
