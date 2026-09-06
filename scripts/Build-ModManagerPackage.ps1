param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+-]*$')]
    [string]$Version = '1.3.0-preview.1'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $repoRoot 'artifacts\mod-manager'
$archive = Join-Path $artifactRoot "CrimsonDesertTelemetry-v$Version-ModManagers.zip"
if (Test-Path -LiteralPath $archive) {
    throw "Release already exists; choose a new version instead of replacing it: $archive"
}
$buildStamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$stagingRoot = Join-Path $artifactRoot "v$Version-$buildStamp"
$packageRoot = Join-Path $stagingRoot 'CrimsonDesertTelemetry'
$cmake = $null
$cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        # Require the C++ CMake component so unrelated installer products are ignored.
        # Do not pin a Visual Studio generation: GitHub's windows-latest image advances.
        $cmake = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
    }
}
if (-not $cmake -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found. Install Visual Studio 2022 with C++ and CMake tools, or add CMake to PATH.'
}

$nativeSource = Join-Path $repoRoot 'native\CrimsonDesertTelemetry.Asi'
$nativeBuild = Join-Path $repoRoot 'build\native-package'
$managedPublish = Join-Path $repoRoot "build\managed-package\v$Version-$buildStamp"

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
    -c Release -r win-x64 --self-contained false -p:UseAppHost=false "-p:Version=$Version" -o $managedPublish
if ($LASTEXITCODE -ne 0) { throw 'Managed host publish failed.' }

& $cmake -S $nativeSource -B $nativeBuild -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native ASI configure failed.' }
& $cmake --build $nativeBuild --config Release
if ($LASTEXITCODE -ne 0) { throw 'Native ASI build failed.' }

New-Item -ItemType Directory -Path $packageRoot | Out-Null

$files = @(
    @{ Source = (Join-Path $nativeBuild 'Release\CrimsonDesertTelemetry.asi'); Name = 'CrimsonDesertTelemetry.asi' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.dll'); Name = 'crimson-desert-telemetry.dll' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.deps.json'); Name = 'crimson-desert-telemetry.deps.cfg' },
    @{ Source = (Join-Path $managedPublish 'crimson-desert-telemetry.runtimeconfig.json'); Name = 'crimson-desert-telemetry.runtimeconfig.cfg' },
    @{ Source = (Join-Path $managedPublish 'CrimsonDesertTelemetry.Core.dll'); Name = 'CrimsonDesertTelemetry.Core.dll' },
    @{ Source = (Join-Path $repoRoot 'packaging\mod-manager\CrimsonDesertTelemetry.ini'); Name = 'CrimsonDesertTelemetry.ini' },
    @{ Source = (Join-Path $repoRoot 'packaging\mod-manager\README.txt'); Name = 'README.txt' },
    @{ Source = (Join-Path $nativeBuild 'THIRD-PARTY-NOTICES.txt'); Name = 'THIRD-PARTY-NOTICES.txt' },
    @{ Source = (Join-Path $repoRoot 'LICENSE'); Name = 'LICENSE.txt' }
)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.Source)) { throw "Missing package input: $($file.Source)" }
    Copy-Item -LiteralPath $file.Source -Destination (Join-Path $packageRoot $file.Name)
}

# No -Force: also refuse a release created by another build while this one ran.
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -CompressionLevel Optimal

& (Join-Path $repoRoot 'tests\Test-ModManagerPackage.ps1') -PackageDirectory $packageRoot -ArchivePath $archive -SelfTest

$hash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
Write-Output "Package: $archive"
Write-Output "Expanded package: $packageRoot"
Write-Output "SHA-256: $($hash.Hash)"
