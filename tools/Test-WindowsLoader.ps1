[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$appProject = Join-Path $projectRoot 'tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj'
$testProject = Join-Path $projectRoot 'tests\windows-loader\OpenTrail.Loader.Tests\OpenTrail.Loader.Tests.csproj'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$validationRoot = [System.IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'OpenTrail.Loader.Validation.' + [System.Guid]::NewGuid().ToString('N'))))
$intermediateRoot = Join-Path $validationRoot 'obj'
$outputRoot = Join-Path $validationRoot 'bin'

if (-not $validationRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The Windows loader validation directory must remain inside the system temporary directory.'
}

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required for the Windows loader.'
}

$previousAppData = $env:APPDATA
$env:APPDATA = Join-Path $validationRoot 'appdata'
New-Item -ItemType Directory -Path $env:APPDATA -Force | Out-Null

$isolatedBuildProperty = "/p:OpenTrailValidationIntermediateRoot=$intermediateRoot"
$isolatedOutputProperty = "/p:OpenTrailValidationOutputRoot=$outputRoot"

try {
    & $dotnet.Source restore $testProject --configfile $nugetConfig --nologo `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader restore failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source build $appProject --configuration Release --framework net8.0-windows --no-restore --nologo `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader build failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source build $testProject --configuration Release --framework net8.0-windows --no-restore --nologo `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader test build failed with exit code $LASTEXITCODE."
    }

    $testAssembly = Join-Path $outputRoot 'OpenTrail.Loader.Tests\Release\net8.0-windows\OpenTrail.Loader.Tests.dll'
    & $dotnet.Source $testAssembly
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:APPDATA = $previousAppData
    if (Test-Path -LiteralPath $validationRoot) {
        Remove-Item -LiteralPath $validationRoot -Recurse -Force
    }
}
