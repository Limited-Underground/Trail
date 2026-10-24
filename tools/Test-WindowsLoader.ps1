[CmdletBinding()]
param([switch] $PreflightOnly)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$appProject = Join-Path $projectRoot 'tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj'
$testProject = Join-Path $projectRoot 'tests\windows-loader\OpenTrail.Loader.Tests\OpenTrail.Loader.Tests.csproj'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$validationRoot = [System.IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'L-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 16))))
$intermediateRoot = Join-Path $validationRoot 'obj'
$outputRoot = Join-Path $validationRoot 'bin'
$projectedManifest = Join-Path $outputRoot 'OpenTrail.Loader.Tests\Release\net8.0-windows\app.manifest'

function Assert-NoReparseAncestors([string] $Path) {
    $current = [System.IO.Path]::GetFullPath($Path)
    while ($current) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Windows loader TEMP path contains a reparse point: $current"
        }
        $parent = [System.IO.Directory]::GetParent($current)
        if ($null -eq $parent) { break }
        $current = $parent.FullName
    }
}

function Assert-NoReparseTree([string] $Path) {
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($Path)
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Windows loader validation output contains a reparse point; preserving it for review: $current"
        }
        if ($item.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop) {
                $pending.Push($child.FullName)
            }
        }
    }
}

if (-not [System.IO.Path]::IsPathFullyQualified($temporaryRoot) -or
    -not (Test-Path -LiteralPath $temporaryRoot -PathType Container) -or
    -not $validationRoot.StartsWith($temporaryRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The Windows loader validation directory must remain inside the system temporary directory.'
}
Assert-NoReparseAncestors $temporaryRoot
if ($projectedManifest.Length -gt 240) {
    throw "The Windows loader projected manifest path is $($projectedManifest.Length) characters; shorten TEMP before the host suite (limit 240)."
}

$dotnet = Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required for the Windows loader.'
}
$sdkVersions = @(& $dotnet.Source --list-sdks 2>&1)
if ($LASTEXITCODE -ne 0 -or -not @($sdkVersions | Where-Object { $_ -match '^8\.' }).Count) {
    throw 'The Windows loader requires an installed .NET 8 SDK.'
}
$nugetProgramFiles = [Environment]::GetEnvironmentVariable('PROGRAMFILES', 'Process')
if (-not $nugetProgramFiles -or -not [System.IO.Path]::IsPathFullyQualified($nugetProgramFiles) -or
    -not (Test-Path -LiteralPath $nugetProgramFiles -PathType Container)) {
    throw 'NuGet requires PROGRAMFILES to name an existing absolute directory.'
}
$nugetProgramFilesX86 = [Environment]::GetEnvironmentVariable('PROGRAMFILES(X86)', 'Process')
if ($nugetProgramFilesX86 -and
    (-not [System.IO.Path]::IsPathFullyQualified($nugetProgramFilesX86) -or
     -not (Test-Path -LiteralPath $nugetProgramFilesX86 -PathType Container))) {
    throw 'NuGet requires PROGRAMFILES(X86), when set, to name an existing absolute directory.'
}
if ($PreflightOnly) { return }

$previousAppData = $env:APPDATA
$createdValidationRoot = $false

$isolatedBuildProperty = "/p:OpenTrailValidationIntermediateRoot=$intermediateRoot"
$isolatedOutputProperty = "/p:OpenTrailValidationOutputRoot=$outputRoot"

try {
    if (Test-Path -LiteralPath $validationRoot) { throw 'Windows loader validation directory collision.' }
    New-Item -ItemType Directory -Path $validationRoot -ErrorAction Stop | Out-Null
    $createdValidationRoot = $true
    $env:APPDATA = Join-Path $validationRoot 'appdata'
    New-Item -ItemType Directory -Path $env:APPDATA -Force | Out-Null

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
    if ($createdValidationRoot -and (Test-Path -LiteralPath $validationRoot)) {
        Assert-NoReparseTree $validationRoot
        Remove-Item -LiteralPath $validationRoot -Recurse -Force
    }
}
