[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$Members = 4,

    [ValidateRange(0, 16)]
    [int]$ForwardingRelays = 0,

    [ValidateRange(1, 10080)]
    [int]$DurationMinutes = 60,

    [ValidateRange(0, 86400)]
    [int]$PositionIntervalSeconds = 60,

    [ValidateRange(0, 86400)]
    [int]$StatusIntervalSeconds = 300,

    [ValidateRange(0, 65535)]
    [int]$AlertsPerMember = 1,

    [ValidateRange(1, 4)]
    [int]$SourceAttempts = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerCandidates = @()
if ($env:OPENTRAIL_MSYS2_ROOT) {
    $compilerCandidates += @(
        (Join-Path $env:OPENTRAIL_MSYS2_ROOT 'ucrt64\bin\g++.exe'),
        (Join-Path $env:OPENTRAIL_MSYS2_ROOT 'mingw64\bin\g++.exe')
    )
}
$compilerCandidates += @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)
$pathCompiler = Get-Command g++.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($pathCompiler) {
    $compilerCandidates += $pathCompiler.Source
}
$compiler = $compilerCandidates |
    Select-Object -Unique |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $compiler) {
    throw 'A native GCC compiler was not found. Install the MSYS2 UCRT64 GCC toolchain first.'
}

$compilerDirectory = Split-Path -Parent $compiler
$env:Path = "$compilerDirectory;$env:Path"
$outputDirectory = Join-Path $projectRoot 'build\host-tests'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$output = Join-Path $outputDirectory 'group_load_cli.exe'
$arguments = @(
    '-std=c++17',
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-Werror',
    '-O2',
    '-I', (Join-Path $projectRoot 'firmware\components\radio\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\identity\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\simulation\include'),
    (Join-Path $projectRoot 'firmware\components\radio\src\lora_airtime.cpp'),
    (Join-Path $projectRoot 'firmware\components\simulation\src\group_load_model.cpp'),
    (Join-Path $projectRoot 'tools\GroupLoadCli.cpp'),
    '-o', $output
)

& $compiler @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Group-load CLI compilation failed with exit code $LASTEXITCODE."
}

& $output `
    $Members `
    $ForwardingRelays `
    $DurationMinutes `
    $PositionIntervalSeconds `
    $StatusIntervalSeconds `
    $AlertsPerMember `
    $SourceAttempts
if ($LASTEXITCODE -ne 0) {
    throw "Group-load CLI failed with exit code $LASTEXITCODE."
}
