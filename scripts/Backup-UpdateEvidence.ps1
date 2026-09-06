# Private local recovery copy, never part of a release. No game/process changes.
param(
    [Parameter(Mandatory)][string]$ExecutablePath
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$profile = Get-Content -Raw (Join-Path $repoRoot 'definitions/build-25116796.json') | ConvertFrom-Json
$exe = Get-Item -LiteralPath $ExecutablePath
if ($exe.PSIsContainer) { throw 'ExecutablePath must be a file.' }
$exeHash = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
if ($exeHash -ne $profile.executableSha256) { throw 'Not the validated baseline EXE; preserve separately and run check-update.' }

$research = Join-Path $repoRoot 'research'
if (-not (Test-Path -LiteralPath (Join-Path $research '.git'))) { throw 'The independent research checkout is missing.' }
$evidenceRoot = Join-Path $repoRoot 'artifacts/light-research'
$evidence = @(Get-ChildItem -LiteralPath $evidenceRoot -File | Where-Object {
    $_.Name -match '^(filtered-count-exact-20260906-2113-|light-rgb-inject-20260906-2215-)' -or
    $_.Name -eq 'crimsonforge-shader-index-20260905.json'
})
foreach ($required in @('crimsonforge-shader-index-20260905.json',
    'filtered-count-exact-20260906-2113-29588660.padxil',
    'filtered-count-exact-20260906-2113-29588660.ll',
    'light-rgb-inject-20260906-2215-b606b219.padxil',
    'light-rgb-inject-20260906-2215-05125ef9.padxil')) {
    if ($required -notin $evidence.Name) { throw "Missing baseline evidence: $required" }
}

$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$backup = Join-Path $repoRoot "artifacts/recovery/$stamp"
# Only new paths in the ignored artifacts directory. No deletion or overwrites.
if (Test-Path -LiteralPath $backup) { throw 'Backup destination already exists.' }
New-Item -ItemType Directory -Path $backup | Out-Null
New-Item -ItemType Directory -Path (Join-Path $backup 'evidence') | Out-Null
$revisions = @{}
foreach ($item in @(@{Name='product'; Path=$repoRoot}, @{Name='research'; Path=$research})) {
    $safe = 'safe.directory=' + $item.Path.Replace('\','/')
    $bundle = Join-Path $backup ($item.Name + '.bundle')
    & git -c $safe -C $item.Path bundle create $bundle --all
    if ($LASTEXITCODE -ne 0) { throw "Bundle creation failed: $($item.Name)" }
    & git -c $safe -C $item.Path bundle verify $bundle
    if ($LASTEXITCODE -ne 0) { throw "Bundle verification failed: $($item.Name)" }
    $head = & git -c $safe -C $item.Path rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read saved revision.' }
    $dirty = @(& git -c $safe -C $item.Path status --porcelain)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read working-tree status.' }
    $revisions[$item.Name] = @{head=$head; uncommittedNotInBundle=$dirty}
}
Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $backup 'CrimsonDesert.exe')
foreach ($item in $evidence) {
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $backup ('evidence/' + $item.Name))
}
$files = @(Get-ChildItem -LiteralPath $backup -File -Recurse | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($backup, $_.FullName); bytes=$_.Length;
      sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
if (($files | Where-Object path -EQ 'CrimsonDesert.exe').sha256 -ne $exeHash) { throw 'Copied EXE hash mismatch.' }
$manifest = @{
    createdUtc=[DateTime]::UtcNow.ToString('o'); executableOriginalPath=$exe.FullName
    steamBuildId=$profile.steamBuildId; revisions=$revisions; files=$files
    limitations='Private same-disk recovery copy, not an off-device backup. Game EXE/shaders must not be published. Large PIX/video/API captures remain at their original paths. Bundles contain committed Git history only.'
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8NoBOM
Write-Output "Private recovery copy verified: $backup"
