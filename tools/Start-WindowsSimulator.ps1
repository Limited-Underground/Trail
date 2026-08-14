[CmdletBinding()]
param(
    [switch]$NoLaunch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$appProject = Join-Path $projectRoot 'tools\windows-simulator\OpenTrail.Simulator\OpenTrail.Simulator.csproj'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$expectedRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$runtimeRoot = [System.IO.Path]::GetFullPath((Join-Path $expectedRoot 'OpenTrail.Simulator.Runtime'))
if (-not $runtimeRoot.StartsWith($expectedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The simulator runtime must remain inside the local system-temporary directory.'
}

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required to launch the local OpenTrail simulator.'
}

$intermediateRoot = Join-Path $runtimeRoot 'obj'
$outputRoot = Join-Path $runtimeRoot 'bin'
$appDataRoot = Join-Path $runtimeRoot 'appdata'
$nativeHost = Join-Path $runtimeRoot 'native\OpenTrail.PortableUiHost.exe'
New-Item -ItemType Directory -Path $appDataRoot -Force | Out-Null
$previousAppData = $env:APPDATA
$previousNativeHost = $env:OPENTRAIL_PORTABLE_UI_HOST
$isolatedBuildProperty = "/p:OpenTrailValidationIntermediateRoot=$intermediateRoot"
$isolatedOutputProperty = "/p:OpenTrailValidationOutputRoot=$outputRoot"

try {
    New-Item -ItemType Directory -Path $appDataRoot -Force | Out-Null
    $env:APPDATA = $appDataRoot
    & (Join-Path $projectRoot 'tools\Build-PortableUiHost.ps1') -OutputPath $nativeHost | Out-Null
    $env:OPENTRAIL_PORTABLE_UI_HOST = $nativeHost

    & $dotnet.Source restore $appProject --configfile $nugetConfig --nologo `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator restore failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source build $appProject --configuration Release --framework net8.0-windows `
        --no-restore --nologo $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator build failed with exit code $LASTEXITCODE."
    }
    $application = Join-Path $outputRoot 'OpenTrail.Simulator\Release\net8.0-windows\OpenTrail.Simulator.exe'
    if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
        throw 'The simulator executable was not produced at the expected local build path.'
    }

    if ($NoLaunch) {
        Write-Output $application
    }
    else {
        Start-Process -FilePath $application -WorkingDirectory $projectRoot
    }
}
finally {
    $env:APPDATA = $previousAppData
    $env:OPENTRAIL_PORTABLE_UI_HOST = $previousNativeHost
}
