[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$appProject = Join-Path $projectRoot 'tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj'
$testProject = Join-Path $projectRoot 'tests\windows-loader\OpenTrail.Loader.Tests\OpenTrail.Loader.Tests.csproj'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$outputRoot = Join-Path $projectRoot 'build\windows-loader'

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required for the Windows loader.'
}

$previousAppData = $env:APPDATA
$env:APPDATA = Join-Path ([System.IO.Path]::GetTempPath()) 'OpenTrail.Loader.ValidationAppData'
New-Item -ItemType Directory -Path $env:APPDATA -Force | Out-Null

try {
    & $dotnet.Source restore $testProject --configfile $nugetConfig --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader restore failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source build $appProject --configuration Release --framework net8.0-windows --output (Join-Path $outputRoot 'app') --no-restore --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader build failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source run --project $testProject --configuration Release --framework net8.0-windows --no-restore
    if ($LASTEXITCODE -ne 0) {
        throw "Windows loader tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:APPDATA = $previousAppData
}
