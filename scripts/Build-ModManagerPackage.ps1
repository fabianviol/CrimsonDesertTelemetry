param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+-]*$')]
    [string]$Version = '0.1.0-preview.2'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw "CMake was not found at $cmake" }

$nativeSource = Join-Path $repoRoot 'native\CrimsonDesertTelemetry.Asi'
$nativeBuild = Join-Path $repoRoot 'build\native-package'
$managedPublish = Join-Path $repoRoot 'build\managed-package'
$artifactRoot = Join-Path $repoRoot 'artifacts\mod-manager'
$packageRoot = Join-Path $artifactRoot 'CrimsonDesertTelemetry'
$archive = Join-Path $artifactRoot "CrimsonDesertTelemetry-v$Version-ModManagers.zip"

function Assert-WithinRepo([string]$Path) {
    $resolvedRepo = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedRepo, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing filesystem mutation outside repository: $resolvedPath"
    }
}
Assert-WithinRepo $packageRoot
Assert-WithinRepo $archive

& dotnet publish (Join-Path $repoRoot 'src\CrimsonDesertTelemetry.Cli\CrimsonDesertTelemetry.Cli.csproj') `
    -c Release -r win-x64 --self-contained false -p:UseAppHost=false -o $managedPublish
if ($LASTEXITCODE -ne 0) { throw 'Managed host publish failed.' }

& $cmake -S $nativeSource -B $nativeBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native ASI configure failed.' }
& $cmake --build $nativeBuild --config Release
if ($LASTEXITCODE -ne 0) { throw 'Native ASI build failed.' }

if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
New-Item -ItemType Directory -Path $packageRoot | Out-Null

$files = @(
    @{ Source = (Join-Path $nativeBuild 'Release\CrimsonDesertTelemetry.asi'); Name = 'CrimsonDesertTelemetry.asi' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.dll'); Name = 'crimson-desert-telemetry.dll' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.deps.json'); Name = 'crimson-desert-telemetry.deps.cfg' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.runtimeconfig.json'); Name = 'crimson-desert-telemetry.runtimeconfig.cfg' },
    @{ Source = (Join-Path $managedPublish 'CrimsonDesertTelemetry.Core.dll'); Name = 'CrimsonDesertTelemetry.Core.dll' },
    @{ Source = (Join-Path $repoRoot 'packaging\mod-manager\CrimsonDesertTelemetry.ini'); Name = 'CrimsonDesertTelemetry.ini' },
    @{ Source = (Join-Path $repoRoot 'packaging\mod-manager\README.txt'); Name = 'README.txt' },
    @{ Source = (Join-Path $repoRoot 'LICENSE'); Name = 'LICENSE.txt' }
)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.Source)) { throw "Missing package input: $($file.Source)" }
    Copy-Item -LiteralPath $file.Source -Destination (Join-Path $packageRoot $file.Name)
}

if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -CompressionLevel Optimal

& (Join-Path $repoRoot 'tests\Test-ModManagerPackage.ps1') -PackageDirectory $packageRoot -ArchivePath $archive -SelfTest

$hash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
Write-Output "Package: $archive"
Write-Output "SHA-256: $($hash.Hash)"
