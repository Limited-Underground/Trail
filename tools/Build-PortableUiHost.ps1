[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
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
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $compiler) {
    throw 'The MSYS2 GCC compiler required by the canonical portable UI host was not found.'
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$arguments = @(
    '-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2',
    '-I', (Join-Path $projectRoot 'firmware\components\diagnostics\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\integration\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\update\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\ui\include'),
    (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
    (Join-Path $projectRoot 'firmware\components\ui\src\portable_ui_render_plan.cpp'),
    (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
    (Join-Path $projectRoot 'firmware\components\integration\src\update_recovery_presentation.cpp'),
    (Join-Path $projectRoot 'firmware\components\integration\src\portable_ui_shell.cpp'),
    (Join-Path $projectRoot 'tools\native-ui-host\portable_ui_host.cpp'),
    '-o', $resolvedOutput
)
& $compiler @arguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $resolvedOutput -PathType Leaf)) {
    throw "Portable UI host build failed with exit code $LASTEXITCODE."
}
Write-Output $resolvedOutput
