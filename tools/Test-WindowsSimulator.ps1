[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$coreTestProject = Join-Path $projectRoot 'tests\windows-simulator\OpenTrail.Simulator.Core.Tests\OpenTrail.Simulator.Core.Tests.csproj'
$windowsTestProject = Join-Path $projectRoot 'tests\windows-simulator\OpenTrail.Simulator.Windows.Tests\OpenTrail.Simulator.Windows.Tests.csproj'
$uiTestProject = Join-Path $projectRoot 'tests\windows-simulator\OpenTrail.Simulator.Tests\OpenTrail.Simulator.Tests.csproj'
$helperTest = Join-Path $projectRoot 'tests\host\windows_simulator_meshcore_bridge_tests.py'
$nativeProtocolTest = Join-Path $projectRoot 'tests\host\portable_ui_host_protocol_tests.py'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$validationRoot = [System.IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'OpenTrail.Simulator.Validation.' + [System.Guid]::NewGuid().ToString('N'))))
$intermediateRoot = Join-Path $validationRoot 'obj'
$outputRoot = Join-Path $validationRoot 'bin'
$nativeHost = Join-Path $validationRoot 'native\OpenTrail.PortableUiHost.exe'

if (-not $validationRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not ([System.IO.Path]::GetFileName($validationRoot)).StartsWith(
        'OpenTrail.Simulator.Validation.', [System.StringComparison]::Ordinal)) {
    throw 'The simulator validation directory must remain in the exact system-temporary namespace.'
}

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required for the Windows simulator.'
}
$python = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw 'Python is required for the private MeshCore simulator helper tests.'
}

$previousAppData = $env:APPDATA
$previousRenderDirectory = $env:OPENTRAIL_SIMULATOR_RENDER_DIR
$previousNativeHost = $env:OPENTRAIL_PORTABLE_UI_HOST
$isolatedBuildProperty = "/p:OpenTrailValidationIntermediateRoot=$intermediateRoot"
$isolatedOutputProperty = "/p:OpenTrailValidationOutputRoot=$outputRoot"

try {
    $env:APPDATA = Join-Path $validationRoot 'appdata'
    $env:OPENTRAIL_SIMULATOR_RENDER_DIR = $null
    New-Item -ItemType Directory -Path $env:APPDATA -Force | Out-Null
    & (Join-Path $projectRoot 'tools\Build-PortableUiHost.ps1') -OutputPath $nativeHost | Out-Null
    $env:OPENTRAIL_PORTABLE_UI_HOST = $nativeHost

    foreach ($testProject in @($coreTestProject, $windowsTestProject, $uiTestProject)) {
        & $dotnet.Source restore $testProject --configfile $nugetConfig --nologo `
            $isolatedBuildProperty $isolatedOutputProperty
        if ($LASTEXITCODE -ne 0) {
            throw "Simulator restore failed with exit code $LASTEXITCODE."
        }

        & $dotnet.Source build $testProject --configuration Release --no-restore --nologo `
            $isolatedBuildProperty $isolatedOutputProperty
        if ($LASTEXITCODE -ne 0) {
            throw "Simulator build failed with exit code $LASTEXITCODE."
        }
    }

    $coreAssembly = Join-Path $outputRoot 'OpenTrail.Simulator.Core.Tests\Release\net8.0\OpenTrail.Simulator.Core.Tests.dll'
    & $dotnet.Source $coreAssembly
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator core tests failed with exit code $LASTEXITCODE."
    }

    $windowsAssembly = Join-Path $outputRoot 'OpenTrail.Simulator.Windows.Tests\Release\net8.0-windows\OpenTrail.Simulator.Windows.Tests.dll'
    & $dotnet.Source $windowsAssembly
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator Windows bridge tests failed with exit code $LASTEXITCODE."
    }

    & $python.Source $helperTest
    if ($LASTEXITCODE -ne 0) {
        throw "Private MeshCore simulator helper tests failed with exit code $LASTEXITCODE."
    }

    & $python.Source $nativeProtocolTest
    if ($LASTEXITCODE -ne 0) {
        throw "Portable UI native protocol tests failed with exit code $LASTEXITCODE."
    }

    $uiAssembly = Join-Path $outputRoot 'OpenTrail.Simulator.Tests\Release\net8.0-windows\OpenTrail.Simulator.Tests.dll'
    & $dotnet.Source $uiAssembly
    if ($LASTEXITCODE -ne 0) {
        throw "Simulator UI tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:APPDATA = $previousAppData
    $env:OPENTRAIL_SIMULATOR_RENDER_DIR = $previousRenderDirectory
    $env:OPENTRAIL_PORTABLE_UI_HOST = $previousNativeHost
    if (Test-Path -LiteralPath $validationRoot) {
        Remove-Item -LiteralPath $validationRoot -Recurse -Force
    }
}
