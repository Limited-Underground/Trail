[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerCandidates = @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)
$compiler = $compilerCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1

if (-not $compiler) {
    throw 'A native GCC compiler was not found. Install the MSYS2 UCRT64 GCC toolchain first.'
}

$compilerDirectory = Split-Path -Parent $compiler
$env:Path = "$compilerDirectory;$env:Path"

$buildDirectory = Join-Path $projectRoot 'build\host-tests'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$commonArguments = @(
    '-std=c++17',
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-Werror',
    '-O2',
    '-I', (Join-Path $projectRoot 'firmware\components\radio\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\radio\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\protocol\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\identity\include')
)

$builds = @(
    @{
        Name = 'radio transport'
        Output = Join-Path $buildDirectory 'radio_transport_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'tests\host\radio_transport_tests.cpp')
        )
    },
    @{
        Name = 'packet codec'
        Output = Join-Path $buildDirectory 'packet_codec_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'tests\host\packet_codec_tests.cpp')
        )
    },
    @{
        Name = 'packet transport integration'
        Output = Join-Path $buildDirectory 'packet_transport_integration_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'tests\host\packet_transport_integration_tests.cpp')
        )
    },
    @{
        Name = 'identity model'
        Output = Join-Path $buildDirectory 'identity_model_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\identity\src\identity_model.cpp'),
            (Join-Path $projectRoot 'tests\host\identity_model_tests.cpp')
        )
    },
    @{
        Name = 'packet codec CLI'
        Output = Join-Path $buildDirectory 'packet_codec_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'tools\PacketCodecCli.cpp')
        )
        Run = $false
    }
)

foreach ($build in $builds) {
    & $compiler @commonArguments @($build.Sources) '-o' $build.Output
    if ($LASTEXITCODE -ne 0) {
        throw "$($build.Name) compilation failed with exit code $LASTEXITCODE."
    }

    if (-not $build.ContainsKey('Run') -or $build.Run) {
        & $build.Output
        if ($LASTEXITCODE -ne 0) {
            throw "$($build.Name) tests failed with exit code $LASTEXITCODE."
        }
    }
}
