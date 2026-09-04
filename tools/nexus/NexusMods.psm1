<#
.SYNOPSIS
    Windows PowerShell 5.1 client for the parts of the Nexus Mods v3 API that this
    mod needs in order to publish and maintain its files.

.DESCRIPTION
    Implements the documented upload lifecycle (https://api-docs.nexusmods.com/):

        create upload session -> PUT archive to the presigned URL -> finalise ->
        wait for state 'available' -> attach to a mod file (new file or new
        version) -> append changelog text.

    The module is read-mostly by design. It never deletes anything, never touches
    the collection endpoints, and never edits a mod page: the v3 API has no
    endpoint for mod page text, so the page description stays a manual edit on
    the site.
#>

$script:NexusApiRoot = 'https://api.nexusmods.com/v3'

# Single part uploads are capped at 100 MiB by the API; larger archives need the
# S3 multipart flow, which this module deliberately does not implement.
$script:NexusMaxSinglePartBytes = 100MB

# Mirrors the OpenAPI patterns so a bad name or version fails locally instead of
# after the archive has already been pushed to storage.
$script:NexusNamePattern = '^[a-zA-Z0-9 _''().-]+$'
$script:NexusVersionPattern = '^[a-zA-Z0-9.-]+$'

# Windows PowerShell 5.1 still negotiates TLS 1.0 first on some machines and the
# API refuses it.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

function Resolve-NexusApiKey {
    param([string]$ApiKey)

    if ([string]::IsNullOrWhiteSpace($ApiKey)) { $ApiKey = $env:NEXUSMODS_API_KEY }
    if ([string]::IsNullOrWhiteSpace($ApiKey)) {
        throw 'No Nexus Mods API key. Pass -ApiKey, or set $env:NEXUSMODS_API_KEY from https://www.nexusmods.com/settings/api-keys.'
    }
    return $ApiKey
}

function Get-NexusFailureText {
    param(
        [Management.Automation.ErrorRecord]$ErrorRecord,
        [string]$Method,
        [string]$Uri
    )

    $status = ''
    $payload = ''
    $response = $null
    if ($ErrorRecord.Exception -is [Net.WebException]) { $response = $ErrorRecord.Exception.Response }
    if ($response) {
        $status = " HTTP $([int]$response.StatusCode)"
        $stream = $response.GetResponseStream()
        if ($stream) {
            $reader = New-Object IO.StreamReader($stream)
            try { $payload = $reader.ReadToEnd() } finally { $reader.Dispose() }
        }
    }

    $detail = $ErrorRecord.Exception.Message
    if ($payload) {
        # Errors are RFC 9457 problem details; fall back to the raw body if not.
        $problem = $null
        try { $problem = $payload | ConvertFrom-Json } catch { $problem = $null }
        if ($problem -and $problem.title) {
            $detail = $problem.title
            if ($problem.detail) { $detail = "$detail - $($problem.detail)" }
        }
        else { $detail = $payload }
    }

    return "$Method $Uri failed:$status $detail"
}

function Invoke-NexusApi {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Get', 'Post', 'Put')][string]$Method,
        [Parameter(Mandatory = $true)][string]$Path,
        [object]$Body,
        [string]$ApiKey
    )

    $uri = $script:NexusApiRoot + $Path
    $arguments = @{
        Uri             = $uri
        Method          = $Method
        Headers         = @{ apikey = (Resolve-NexusApiKey -ApiKey $ApiKey); Accept = 'application/json' }
        UseBasicParsing = $true
        TimeoutSec      = 120
    }
    if ($null -ne $Body) {
        $arguments.ContentType = 'application/json'
        $arguments.Body = ($Body | ConvertTo-Json -Depth 6 -Compress)
    }

    try { return Invoke-RestMethod @arguments }
    catch { throw (Get-NexusFailureText -ErrorRecord $_ -Method $Method -Uri $uri) }
}

function Assert-NexusVersion {
    param([Parameter(Mandatory = $true)][string]$Version)

    if ($Version.Length -gt 50 -or $Version -notmatch $script:NexusVersionPattern) {
        throw "Nexus Mods rejects the version '$Version'. Allowed: up to 50 characters of A-Z, a-z, 0-9, '.' and '-' (no '+', no spaces)."
    }
}

function Assert-NexusFileName {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name.Length -gt 50 -or $Name -notmatch $script:NexusNamePattern) {
        throw "Nexus Mods rejects the file name '$Name'. Allowed: up to 50 characters of A-Z, a-z, 0-9, space, and _ ' ( ) . -"
    }
}

function Get-NexusMod {
    <#
    .SYNOPSIS
        Resolves a site URL such as /crimsondesert/mods/3374 to the internal mod id
        that every other endpoint expects.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$GameDomain,
        [Parameter(Mandatory = $true)][string]$GameScopedId,
        [string]$ApiKey
    )

    return (Invoke-NexusApi -Method Get -Path "/games/$GameDomain/mods/$GameScopedId" -ApiKey $ApiKey).data
}

function Get-NexusModFiles {
    param(
        [Parameter(Mandatory = $true)][string]$ModId,
        [string]$ApiKey
    )

    return (Invoke-NexusApi -Method Get -Path "/mods/$ModId/files" -ApiKey $ApiKey).data.mod_files
}

function Get-NexusModFileVersions {
    param(
        [Parameter(Mandatory = $true)][string]$FileId,
        [string]$ApiKey
    )

    return (Invoke-NexusApi -Method Get -Path "/mod-files/$FileId/versions" -ApiKey $ApiKey).data.versions
}

function Get-NexusFileDigest {
    <#
    .SYNOPSIS
        Returns the hex digest for the create-upload body and the base64 digest for
        the Content-MD5 header, which are the same bytes in two encodings.
    #>
    param([Parameter(Mandatory = $true)][string]$Path)

    $file = Get-Item -LiteralPath $Path
    $md5 = [Security.Cryptography.MD5]::Create()
    try {
        $stream = [IO.File]::OpenRead($file.FullName)
        try { $digest = $md5.ComputeHash($stream) } finally { $stream.Dispose() }
    }
    finally { $md5.Dispose() }

    return [pscustomobject]@{
        Path      = $file.FullName
        FileName  = $file.Name
        SizeBytes = $file.Length
        Md5Hex    = ([BitConverter]::ToString($digest) -replace '-', '').ToLowerInvariant()
        Md5Base64 = [Convert]::ToBase64String($digest)
    }
}

function Wait-NexusUpload {
    param(
        [Parameter(Mandatory = $true)][string]$UploadId,
        [int]$TimeoutSeconds = 300,
        [string]$ApiKey
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $upload = (Invoke-NexusApi -Method Get -Path "/uploads/$UploadId" -ApiKey $ApiKey).data
        if ($upload.state -eq 'available') { return $upload }
        if ((Get-Date) -ge $deadline) {
            throw "Upload $UploadId is still '$($upload.state)' after $TimeoutSeconds seconds."
        }
        Start-Sleep -Seconds 3
    }
}

function New-NexusUpload {
    <#
    .SYNOPSIS
        Uploads an archive and returns the finalised, available upload session.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$FileName,
        [int]$TimeoutSeconds = 600,
        [string]$ApiKey
    )

    $digest = Get-NexusFileDigest -Path $Path
    if ($digest.SizeBytes -gt $script:NexusMaxSinglePartBytes) {
        $mib = [math]::Round($digest.SizeBytes / 1MB, 1)
        throw "$($digest.FileName) is $mib MiB. Files over 100 MiB need the multipart upload flow, which this module does not implement."
    }
    if (-not $FileName) { $FileName = $digest.FileName }

    # md5 becomes mandatory on 2026-12-01; sending it now also binds the presigned
    # URL to exactly these bytes.
    $session = (Invoke-NexusApi -Method Post -Path '/uploads' -ApiKey $ApiKey -Body @{
            size_bytes = $digest.SizeBytes
            filename   = $FileName
            md5        = $digest.Md5Hex
        }).data

    Write-Verbose "Upload session $($session.id) created for $FileName ($($digest.SizeBytes) bytes)."

    # Content-Disposition and Content-MD5 are part of the presigned URL signature,
    # so storage rejects the PUT if either is missing or altered. Content-Type is
    # not signed and is left unset on purpose.
    $quote = [char]34
    $headers = @{
        'Content-Disposition' = "attachment; filename=$quote$FileName$quote"
        'Content-MD5'         = $digest.Md5Base64
    }

    # The 5.1 progress bar makes -InFile uploads an order of magnitude slower.
    $previousProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri $session.presigned_url -Method Put -InFile $digest.Path -Headers $headers -UseBasicParsing -TimeoutSec $TimeoutSeconds | Out-Null
    }
    catch { throw (Get-NexusFailureText -ErrorRecord $_ -Method 'PUT' -Uri 'presigned storage URL') }
    finally { $ProgressPreference = $previousProgress }

    Invoke-NexusApi -Method Post -Path "/uploads/$($session.id)/finalise" -ApiKey $ApiKey | Out-Null

    return (Wait-NexusUpload -UploadId $session.id -TimeoutSeconds $TimeoutSeconds -ApiKey $ApiKey)
}

function New-NexusModFile {
    <#
    .SYNOPSIS
        Creates an entirely new file entry on an existing mod page. Use this once,
        for the first file; every later release is a version of that file.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$ModId,
        [Parameter(Mandatory = $true)][string]$UploadId,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Version,
        [ValidateSet('main', 'optional', 'miscellaneous')][string]$Category = 'main',
        [string]$Description,
        [bool]$PrimaryModManagerDownload = $false,
        [bool]$AllowModManagerDownload = $true,
        [bool]$ShowRequirementsPopUp = $false,
        [bool]$UpdateModVersion = $false,
        [string]$ApiKey
    )

    Assert-NexusFileName -Name $Name
    Assert-NexusVersion -Version $Version

    $body = @{
        upload_id                    = $UploadId
        mod_id                       = $ModId
        name                         = $Name
        version                      = $Version
        file_category                = $Category
        primary_mod_manager_download = $PrimaryModManagerDownload
        allow_mod_manager_download   = $AllowModManagerDownload
        show_requirements_pop_up     = $ShowRequirementsPopUp
        update_mod_version           = $UpdateModVersion
    }
    if ($Description) { $body.description = $Description }

    return (Invoke-NexusApi -Method Post -Path '/mod-files' -ApiKey $ApiKey -Body $body).data
}

function New-NexusModFileVersion {
    <#
    .SYNOPSIS
        Adds a new version to an existing mod file, which is what a normal release
        does. The new version becomes the head of the update chain.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$FileId,
        [Parameter(Mandatory = $true)][string]$UploadId,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Version,
        [ValidateSet('main', 'optional', 'miscellaneous')][string]$Category = 'main',
        [string]$Description,
        [bool]$PrimaryModManagerDownload = $false,
        [bool]$AllowModManagerDownload = $true,
        [bool]$ShowRequirementsPopUp = $false,
        [bool]$UpdateModVersion = $false,
        [bool]$ArchiveExistingFile = $false,
        [string]$ApiKey
    )

    Assert-NexusFileName -Name $Name
    Assert-NexusVersion -Version $Version

    $body = @{
        upload_id                    = $UploadId
        name                         = $Name
        version                      = $Version
        file_category                = $Category
        primary_mod_manager_download = $PrimaryModManagerDownload
        allow_mod_manager_download   = $AllowModManagerDownload
        show_requirements_pop_up     = $ShowRequirementsPopUp
        update_mod_version           = $UpdateModVersion
        archive_existing_file        = $ArchiveExistingFile
    }
    if ($Description) { $body.description = $Description }

    return (Invoke-NexusApi -Method Post -Path "/mod-files/$FileId/versions" -ApiKey $ApiKey -Body $body).data
}

function Add-NexusModChangelog {
    <#
    .SYNOPSIS
        Appends changelog text for a version. Repeated calls for the same version
        append rather than replace, so never run this twice for one release.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$ModId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Changelog,
        [string]$ApiKey
    )

    Assert-NexusVersion -Version $Version
    if ([string]::IsNullOrWhiteSpace($Changelog)) { throw 'Changelog text is empty.' }
    if ($Changelog.Length -gt 65535) { throw 'Changelog text exceeds the 65535 character limit.' }

    return (Invoke-NexusApi -Method Post -Path "/mods/$ModId/changelogs" -ApiKey $ApiKey -Body @{
            version   = $Version
            changelog = $Changelog
        }).data
}

function ConvertTo-NexusChangelog {
    <#
    .SYNOPSIS
        Turns a docs/releases/vX.Y.Z.md note into the plain text the changelog
        endpoint expects: the H1 title is dropped, everything else is kept as is.
    #>
    param([Parameter(Mandatory = $true)][string]$Path)

    $lines = @(Get-Content -LiteralPath $Path -Encoding UTF8)
    if ($lines.Count -and $lines[0] -match '^#\s') { $lines = @($lines[1..($lines.Count - 1)]) }
    $text = ($lines -join "`n").Trim()
    if (-not $text) { throw "Release notes at $Path contain no body text." }
    return $text
}

Export-ModuleMember -Function Get-NexusMod, Get-NexusModFiles, Get-NexusModFileVersions, Get-NexusFileDigest, New-NexusUpload, Wait-NexusUpload, New-NexusModFile, New-NexusModFileVersion, Add-NexusModChangelog, ConvertTo-NexusChangelog, Assert-NexusVersion, Assert-NexusFileName
