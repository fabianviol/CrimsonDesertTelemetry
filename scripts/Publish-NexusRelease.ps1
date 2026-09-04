<#
.SYNOPSIS
    Publishes a built mod-manager package to the Nexus Mods page for this mod.

.DESCRIPTION
    Dry run by default: without -Execute the script only validates locally and
    against read-only endpoints, then prints exactly what it would upload. Add
    -Execute to actually create the upload and the mod file version.

    The mod page itself is never touched. The v3 API has no endpoint for the page
    description, so page text stays a manual edit on the site.

.EXAMPLE
    .\scripts\Publish-NexusRelease.ps1
    Dry run for the version in Directory.Build.props.

.EXAMPLE
    .\scripts\Publish-NexusRelease.ps1 -Version 1.2.1 -Execute -UpdateModVersion -ArchiveExistingFile
    Uploads a new version of the existing mod file, archives the previous one, and
    moves the mod page version to 1.2.1.

.EXAMPLE
    .\scripts\Publish-NexusRelease.ps1 -CreateModFile -Execute
    One-time bootstrap: creates the first file entry on the mod page so that later
    releases (and the GitHub workflow) have a file id to add versions to.
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+-]*$')]
    [string]$Version,

    [string]$ArchivePath,

    [string]$GameDomain = 'crimsondesert',

    [string]$GameScopedId = '3374',

    # Defaults to $env:NEXUSMODS_FILE_ID. Not needed with -CreateModFile.
    [string]$FileId,

    [string]$DisplayName = 'Crimson Desert Telemetry',

    [string]$Description,

    [ValidateSet('main', 'optional', 'miscellaneous')]
    [string]$Category = 'main',

    [string]$ChangelogPath,

    # Creates a new file entry instead of a new version of an existing file.
    [switch]$CreateModFile,

    [switch]$SkipChangelog,

    [switch]$UpdateModVersion,

    [switch]$ArchiveExistingFile,

    [switch]$PrimaryModManagerDownload,

    [bool]$AllowModManagerDownload = $true,

    # Without this the script performs read-only checks only.
    [switch]$Execute,

    [string]$ApiKey
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $repoRoot 'tools\nexus\NexusMods.psm1') -Force

if (-not $ApiKey) { $ApiKey = $env:NEXUSMODS_API_KEY }
if (-not $FileId) { $FileId = $env:NEXUSMODS_FILE_ID }

if (-not $Version) {
    [xml]$props = Get-Content -LiteralPath (Join-Path $repoRoot 'Directory.Build.props')
    $Version = $props.Project.PropertyGroup.Version
}
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $repoRoot "artifacts\mod-manager\CrimsonDesertTelemetry-v$Version-ModManagers.zip"
}
if (-not $ChangelogPath) {
    $ChangelogPath = Join-Path $repoRoot "docs\releases\v$Version.md"
}

# Fail on anything Nexus Mods would reject before a single byte moves.
Assert-NexusVersion -Version $Version
Assert-NexusFileName -Name $DisplayName
if (-not (Test-Path -LiteralPath $ArchivePath)) {
    throw "Package not found: $ArchivePath. Build it with .\scripts\Build-ModManagerPackage.ps1 -Version $Version."
}
if (-not $CreateModFile -and -not $FileId) {
    throw 'No file id. Pass -FileId, set $env:NEXUSMODS_FILE_ID, or use -CreateModFile for the very first upload. The file id is on the mod page under Files -> API Info.'
}

$changelog = $null
if (-not $SkipChangelog) {
    if (-not (Test-Path -LiteralPath $ChangelogPath)) {
        throw "Release notes not found: $ChangelogPath. Write them, or pass -SkipChangelog."
    }
    $changelog = ConvertTo-NexusChangelog -Path $ChangelogPath
}

$digest = Get-NexusFileDigest -Path $ArchivePath

Write-Host ''
Write-Host "Mod page      : https://www.nexusmods.com/$GameDomain/mods/$GameScopedId"
Write-Host "Version       : $Version"
Write-Host "Package       : $($digest.Path)"
Write-Host "Size          : $([math]::Round($digest.SizeBytes / 1KB, 1)) KiB"
Write-Host "MD5           : $($digest.Md5Hex)"
Write-Host "File name     : $DisplayName"
Write-Host "Category      : $Category"
$changelogSummary = 'skipped'
if ($changelog) { $changelogSummary = "$ChangelogPath ($($changelog.Length) chars)" }
Write-Host "Changelog     : $changelogSummary"

# Read-only lookups. These also prove the API key works before anything mutates.
$mod = Get-NexusMod -GameDomain $GameDomain -GameScopedId $GameScopedId -ApiKey $ApiKey
Write-Host "Resolved mod  : $($mod.name) (internal id $($mod.id))"

$files = @(Get-NexusModFiles -ModId $mod.id -ApiKey $ApiKey)
if ($files.Count) {
    Write-Host 'Existing files:'
    foreach ($file in $files) {
        $marker = if ($file.id -eq $FileId) { ' <- target' } else { '' }
        Write-Host "  $($file.id)  $($file.name)  versions=$($file.versions_count)  active=$($file.is_active)$marker"
    }
}
else {
    Write-Host 'Existing files: none'
}

if ($CreateModFile) {
    if ($files.Count) {
        Write-Warning 'The mod page already has files. -CreateModFile adds another file entry rather than a new version of an existing one.'
    }
    $action = "create a new mod file '$DisplayName' at version $Version"
}
else {
    if (-not ($files | Where-Object { $_.id -eq $FileId })) {
        throw "File id $FileId is not one of this mod's files. Check Files -> API Info on the mod page."
    }
    $existing = @(Get-NexusModFileVersions -FileId $FileId -ApiKey $ApiKey)
    if ($existing | Where-Object { $_.version -eq $Version }) {
        throw "Version $Version already exists on file $FileId. Bump the version before publishing."
    }
    $latest = 'none'
    if ($existing.Count) { $latest = ($existing | Sort-Object uploaded_at -Descending | Select-Object -First 1).version }
    Write-Host "Latest version: $latest"
    $action = "add version $Version to mod file $FileId"
}

Write-Host ''
if (-not $Execute) {
    $changelogAction = 'skip the changelog'
    if ($changelog) { $changelogAction = 'append the changelog' }
    Write-Host "DRY RUN. Would $action, then $changelogAction."
    Write-Host 'Re-run with -Execute to publish.'
    return
}

Write-Host "Uploading $($digest.FileName) ..."
$upload = New-NexusUpload -Path $ArchivePath -ApiKey $ApiKey -Verbose:$VerbosePreference
Write-Host "Upload $($upload.id) is $($upload.state)."

$common = @{
    UploadId                  = $upload.id
    Name                      = $DisplayName
    Version                   = $Version
    Category                  = $Category
    PrimaryModManagerDownload = [bool]$PrimaryModManagerDownload
    AllowModManagerDownload   = [bool]$AllowModManagerDownload
    ShowRequirementsPopUp     = $false
    UpdateModVersion          = [bool]$UpdateModVersion
    ApiKey                    = $ApiKey
}
if ($Description) { $common.Description = $Description }

if ($CreateModFile) {
    $result = New-NexusModFile -ModId $mod.id @common
    Write-Host "Created mod file $($result.id). Store this as NEXUSMODS_FILE_ID for future releases."
}
else {
    $result = New-NexusModFileVersion -FileId $FileId -ArchiveExistingFile ([bool]$ArchiveExistingFile) @common
    Write-Host "Created mod file version $($result.version.id) on file $($result.file.id)."
}

if ($changelog) {
    Add-NexusModChangelog -ModId $mod.id -Version $Version -Changelog $changelog -ApiKey $ApiKey | Out-Null
    Write-Host "Appended changelog for $Version."
}

Write-Host ''
Write-Host "Done: https://www.nexusmods.com/$GameDomain/mods/$GameScopedId?tab=files"
