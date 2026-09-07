<# Offline candidate decoding. Reads the original bounded binary through the
existing provenance validator; never accepts a hand-edited parser JSON as input.
Requires explicit layout assumption because native path A alone is not a PSO hash.
#>
#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [switch]$AssumeCsPrecomputeAmbientLayout,
    [string]$OutFile
)
$ErrorActionPreference='Stop'
if (-not $AssumeCsPrecomputeAmbientLayout) {
    throw 'Specify -AssumeCsPrecomputeAmbientLayout. Runtime shader identity is not verified; this is private candidate decoding, not API acceptance.'
}
Import-Module (Join-Path $PSScriptRoot 'AmbientSh.psm1') -Force
$raw = & (Join-Path $PSScriptRoot 'Read-AmbientProbe.ps1') -Path $Path -PassThru
if (@($raw.samples | Where-Object producerRva -ne '0x3849BB7').Count -ne 0) {
    throw 'This candidate profile is restricted to the measured A path; do not assume path B uses the same normalization.'
}
$decoded = @(
    foreach ($s in $raw.samples) {
        $d=ConvertFrom-AmbientShRows -Rows $s.rawFloat4Rows
        [pscustomobject]@{
            sequence=$s.sequence; frame=$s.frame; capturedTick=$s.capturedTick
            resource=$s.resource; camera=$s.camera
            sunDirection=$s.sunDirection; moonDirection=$s.moonDirection
            values=$d
        }
    }
)
$report=[pscustomobject]@{
    format='private-ambient-derived-v1'; isPublicApi=$false
    source=$raw.source; sourceSha256=$raw.capturedPrefixSha256
    processId=$raw.processId; sampleCount=$decoded.Count
    assumedProfile='csPrecomputeAmbient-6acf206f-8501bc6e8367adacee2edc17d1defccc'
    referenceDisassemblySha256='5D2E9CFDD7E748735EDF5FB593F037AEDDD0B8132890A39D16A883352A6E0936'
    runtimeShaderIdentityVerified=$false
    scope='upper-hemisphere sky samples; not player-local illumination'
    caveat='Relative shader units, not lux/nits or display color. Matrix reversal is algebraic; Rec.709 luminance is explicitly an input-primaries estimate. No exposure normalization, local roof occlusion or direct sun/moon disk separation has been validated. Matrix reversal follows a shader clamp and is not necessarily lossless.'
    samples=$decoded
}
if ($OutFile) {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 12))
    $stream=[IO.File]::Open([IO.Path]::GetFullPath($OutFile),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try { $stream.Write($bytes) } finally { $stream.Dispose() }
}
[pscustomobject]@{
    processId=$raw.processId; samples=$decoded.Count
    first=$decoded[0].values; last=$decoded[-1].values
    sourceSha256=$raw.capturedPrefixSha256
    caveat=$report.caveat
}
