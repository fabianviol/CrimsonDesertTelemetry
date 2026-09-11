<#
.SYNOPSIS
    Preserves a private copy of the game executable currently on disk.

.DESCRIPTION
    Run this whenever the game is installed or updated, BEFORE Steam replaces the
    file. The copy is what makes the next update survivable.

    Why it matters, from 2026-09-11. Build 25246367 moved three sets of hardcoded
    addresses in the ASI. The two ambient hooks were recoverable from their own
    signatures, which happen to be unique in the image. The spatial probe's were
    not: its anchor is an ordinary function prologue that occurs 1390 times, so a
    signature search returns 1390 candidates and settles nothing. What resolved it
    was reading 32 bytes of context at the old address in the PREVIOUS executable
    and searching for that -- unique on the first try, and it showed both anchors
    had moved by the same +0x21C0 as the ambient pair, which is itself evidence
    that the region shifted rather than changed.

    None of that is possible without the previous binary. An executable Steam has
    already overwritten cannot be got back; a lost one has cost this project
    RTTI/provenance evidence once before.

    Copies live under artifacts/recovery/, which is git-ignored: these are the
    game's files, kept locally for compatibility work, never redistributed and
    never part of a release.

    Idempotent and non-destructive. If this exact executable is already preserved
    anywhere under artifacts/recovery, it says so and copies nothing. It never
    overwrites an existing copy, and it never touches the game.

.EXAMPLE
    .\scripts\Backup-GameExecutable.ps1
    Preserves the executable at the default install path.

.EXAMPLE
    .\scripts\Backup-GameExecutable.ps1 -ExecutablePath 'D:\Games\Crimson Desert\bin64\CrimsonDesert.exe'
#>
[CmdletBinding()]
param(
    [string]$ExecutablePath = 'C:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe',
    [string]$BuildId
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$recovery = Join-Path $repoRoot 'artifacts/recovery'

if (-not (Test-Path -LiteralPath $ExecutablePath)) {
    throw "No executable at $ExecutablePath. Pass -ExecutablePath for a non-default install."
}
$exe = Get-Item -LiteralPath $ExecutablePath
if ($exe.PSIsContainer) { throw 'ExecutablePath must be a file.' }

$hash = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
$version = $exe.VersionInfo.FileVersion
Write-Host ''
Write-Host "Executable : $($exe.FullName)"
Write-Host "Size       : $($exe.Length) bytes"
Write-Host "Version    : $version"
Write-Host "SHA256     : $hash"

# Already preserved? Compare by content, not by name: a copy filed under any
# folder still answers the question the next relocation will ask.
if (Test-Path -LiteralPath $recovery) {
    $existing = Get-ChildItem -LiteralPath $recovery -Recurse -File -Filter '*.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -eq $exe.Length } |
        Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $hash }
    if ($existing) {
        Write-Host ''
        Write-Host 'Already preserved; nothing copied:'
        foreach ($item in $existing) {
            Write-Host "  $([IO.Path]::GetRelativePath($repoRoot, $item.FullName))"
        }
        return
    }
}

# Name the folder by the build when it is known, so the recovery docs can refer
# to it; fall back to the hash prefix rather than inventing a build number.
if (-not $BuildId) {
    $definitions = Join-Path $repoRoot 'definitions'
    if (Test-Path -LiteralPath $definitions) {
        $match = Get-ChildItem -LiteralPath $definitions -Filter 'build-*.json' | ForEach-Object {
            $profile = Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json
            if ($profile.executableSha256 -eq $hash) { $profile.steamBuildId }
        } | Select-Object -First 1
        if ($match) { $BuildId = $match }
    }
}
$label = if ($BuildId) { "build-$BuildId" } else { "unknown-$($hash.Substring(0,8).ToLower())" }
$target = Join-Path $recovery ("{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $label)

New-Item -ItemType Directory -Path $target -Force | Out-Null
Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $target $exe.Name)

# A manifest beside it, so a copy is still identifiable years later.
[ordered]@{
    preservedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    sourcePath     = $exe.FullName
    fileName       = $exe.Name
    sizeBytes      = $exe.Length
    sha256         = $hash
    fileVersion    = $version
    steamBuildId   = if ($BuildId) { $BuildId } else { $null }
    note           = 'Private local copy for compatibility recovery. Never redistribute; never ship in a release.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'manifest.json') -Encoding UTF8

Write-Host ''
Write-Host "Preserved  : $([IO.Path]::GetRelativePath($repoRoot, $target))"
if (-not $BuildId) {
    Write-Host 'No build definition matches this hash yet, so the folder is named by hash.'
    Write-Host 'Run check-update against it and add a definition before promoting anything.'
}
