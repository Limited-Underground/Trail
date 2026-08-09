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

$buildDirectory = Join-Path $projectRoot "build\host-tests\run-$PID"
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
    '-I', (Join-Path $projectRoot 'firmware\components\identity\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\delivery\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\diagnostics\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\diagnostics\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\location\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\location\test_support')
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
        Name = 'delivery controller'
        Output = Join-Path $buildDirectory 'delivery_controller_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'tests\host\delivery_controller_tests.cpp')
        )
    },
    @{
        Name = 'duplicate window'
        Output = Join-Path $buildDirectory 'duplicate_window_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'tests\host\duplicate_window_tests.cpp')
        )
    },
    @{
        Name = 'delivery integration'
        Output = Join-Path $buildDirectory 'delivery_integration_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'tests\host\delivery_integration_tests.cpp')
        )
    },
    @{
        Name = 'controlled forwarding'
        Output = Join-Path $buildDirectory 'forwarding_controller_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\forwarding_controller.cpp'),
            (Join-Path $projectRoot 'tests\host\forwarding_controller_tests.cpp')
        )
    },
    @{
        Name = 'priority queue'
        Output = Join-Path $buildDirectory 'priority_queue_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'tests\host\priority_queue_tests.cpp')
        )
    },
    @{
        Name = 'priority delivery integration'
        Output = Join-Path $buildDirectory 'priority_delivery_integration_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'tests\host\priority_delivery_integration_tests.cpp')
        )
    },
    @{
        Name = 'diagnostics logger'
        Output = Join-Path $buildDirectory 'logger_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\logger.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\test_support\memory_log_sink.cpp'),
            (Join-Path $projectRoot 'tests\host\logger_tests.cpp')
        )
    },
    @{
        Name = 'GPS/location abstraction'
        Output = Join-Path $buildDirectory 'location_tracker_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'tests\host\location_tracker_tests.cpp')
        )
    },
    @{
        Name = 'position payload codec'
        Output = Join-Path $buildDirectory 'position_codec_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'tests\host\position_codec_tests.cpp')
        )
    },
    @{
        Name = 'position packet integration'
        Output = Join-Path $buildDirectory 'position_packet_integration_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'tests\host\position_packet_integration_tests.cpp')
        )
    },
    @{
        Name = 'LoRa airtime'
        Output = Join-Path $buildDirectory 'lora_airtime_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\src\lora_airtime.cpp'),
            (Join-Path $projectRoot 'tests\host\lora_airtime_tests.cpp')
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
