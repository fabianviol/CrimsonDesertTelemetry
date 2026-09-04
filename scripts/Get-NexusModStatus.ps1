<#
.SYNOPSIS
    Read-only view of what the Nexus Mods page currently holds: files, their ids,
    and every version in each file.

.DESCRIPTION
    Nothing here mutates anything, so it is the safe way to find the file id that
    -FileId / NEXUSMODS_FILE_ID needs, and to check what a release actually did.

.EXAMPLE
    $env:NEXUSMODS_API_KEY = '<key>'
    .\scripts\Get-NexusModStatus.ps1
#>
[CmdletBinding()]
param(
    [string]$GameDomain = 'crimsondesert',
    [string]$GameScopedId = '3374',
    [bool]$IncludeVersions = $true,
    [string]$ApiKey
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $repoRoot 'tools\nexus\NexusMods.psm1') -Force

if (-not $ApiKey) { $ApiKey = $env:NEXUSMODS_API_KEY }

$mod = Get-NexusMod -GameDomain $GameDomain -GameScopedId $GameScopedId -ApiKey $ApiKey

Write-Host ''
Write-Host "Page        : https://www.nexusmods.com/$GameDomain/mods/$GameScopedId"
Write-Host "Name        : $($mod.name)"
Write-Host "Mod id      : $($mod.id)          <- NEXUSMODS_MOD_ID"
Write-Host "Game id     : $($mod.game_id)"
Write-Host ''

$files = @(Get-NexusModFiles -ModId $mod.id -ApiKey $ApiKey)
if (-not $files.Count) {
    Write-Host 'No files on this mod page yet.'
    Write-Host 'Create the first one with: .\scripts\Publish-NexusRelease.ps1 -CreateModFile -Execute'
    return
}

foreach ($file in $files) {
    Write-Host "File id     : $($file.id)          <- NEXUSMODS_FILE_ID"
    Write-Host "  name      : $($file.name)"
    Write-Host "  active    : $($file.is_active)"
    Write-Host "  versions  : $($file.versions_count) (archived $($file.archived_count), removed $($file.removed_count))"
    Write-Host "  last upload: $($file.last_file_uploaded_at)"

    if ($IncludeVersions) {
        $versions = @(Get-NexusModFileVersions -FileId $file.id -ApiKey $ApiKey)
        foreach ($version in ($versions | Sort-Object uploaded_at -Descending)) {
            Write-Host "    $($version.version.PadRight(20)) $($version.category.ToString().PadRight(14)) $($version.uploaded_at)  id=$($version.id)"
        }
    }
    Write-Host ''
}
