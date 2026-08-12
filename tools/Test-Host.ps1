[CmdletBinding()]
param()

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

function Invoke-ExpectedNativeFailure {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Executable,
        [Parameter(Mandatory = $true)]
        [string] $Argument
    )

    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell converts native stderr into ErrorRecord objects.
        # Keep expected negative CLI evidence capturable under the script-wide
        # Stop preference, then restore that preference immediately.
        $ErrorActionPreference = 'Continue'
        $output = & $Executable $Argument 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    [PSCustomObject]@{
        ExitCode = $exitCode
        Text = (@($output | ForEach-Object { $_.ToString() }) -join "`n")
    }
}

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
    '-I', (Join-Path $projectRoot 'firmware\components\location\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\maps\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\integration\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\persistence\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\persistence\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\security\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\security\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\simulation\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\time\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\time\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\power\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\power\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\ui\include'),
    '-I', (Join-Path $projectRoot 'firmware\components\ui\test_support'),
    '-I', (Join-Path $projectRoot 'firmware\components\update\include'),
    '-I', (Join-Path $projectRoot 'firmware\targets\portable_client\include')
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
        Name = 'protected packet budget'
        Output = Join-Path $buildDirectory 'protected_packet_budget_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\src\lora_airtime.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\protected_packet_budget.cpp'),
            (Join-Path $projectRoot 'tests\host\protected_packet_budget_tests.cpp')
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
        Name = 'group access lifecycle'
        Output = Join-Path $buildDirectory 'group_access_controller_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\identity\src\group_access_controller.cpp'),
            (Join-Path $projectRoot 'tests\host\group_access_controller_tests.cpp')
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
        Name = 'duplicate checkpoint codec'
        Output = Join-Path $buildDirectory 'duplicate_checkpoint_codec_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_codec.cpp'),
            (Join-Path $projectRoot 'tests\host\duplicate_checkpoint_codec_tests.cpp')
        )
    },
    @{
        Name = 'duplicate checkpoint store'
        Output = Join-Path $buildDirectory 'duplicate_checkpoint_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'tests\host\duplicate_checkpoint_store_tests.cpp')
        )
    },
    @{
        Name = 'duplicate checkpoint key/value storage adapter'
        Output = Join-Path $buildDirectory 'duplicate_checkpoint_kv_storage_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_kv_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\duplicate_checkpoint_kv_storage_tests.cpp')
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
        Name = 'immutable single repeater'
        Output = Join-Path $buildDirectory 'single_repeater_forwarder_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\single_repeater_forwarder.cpp'),
            (Join-Path $projectRoot 'tests\host\single_repeater_forwarder_tests.cpp')
        )
    },
    @{
        Name = 'reboot-safe repeater replay'
        Output = Join-Path $buildDirectory 'single_repeater_replay_coordinator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_window.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\duplicate_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\single_repeater_forwarder.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\single_repeater_replay_coordinator.cpp'),
            (Join-Path $projectRoot 'tests\host\single_repeater_replay_coordinator_tests.cpp')
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
        Name = 'bounded position broadcast scheduler'
        Output = Join-Path $buildDirectory 'position_broadcast_scheduler_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'tests\host\position_broadcast_scheduler_tests.cpp')
        )
    },
    @{
        Name = 'opt-in breadcrumb archive session'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_breadcrumb_archive_transport.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_tests.cpp')
        )
    },
    @{
        Name = 'bounded breadcrumb archive outbox'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_outbox_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_outbox.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_breadcrumb_archive_remote.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_outbox_tests.cpp')
        )
    },
    @{
        Name = 'checked-time breadcrumb archive retry coordinator'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_retry_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_outbox.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_retry.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_breadcrumb_archive_remote.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_retry_tests.cpp')
        )
    },
    @{
        Name = 'privacy-safe breadcrumb archive presentation'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_presentation_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_presentation.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_presentation_tests.cpp')
        )
    },
    @{
        Name = 'single-read breadcrumb archive snapshot presentation'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_snapshot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_presentation.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_snapshot_tests.cpp')
        )
    },
    @{
        Name = 'single-owner breadcrumb archive UI coordinator'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_ui_coordinator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_presentation.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_ui_coordinator.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_ui_coordinator_tests.cpp')
        )
    },
    @{
        Name = 'serialized breadcrumb archive snapshot adapter'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_snapshot_adapter_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_outbox.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_retry.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_breadcrumb_archive_remote.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_presentation.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_ui_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_snapshot_adapter.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_snapshot_adapter_tests.cpp')
        )
    },
    @{
        Name = 'serialized breadcrumb archive runtime owner'
        Output = Join-Path $buildDirectory 'breadcrumb_archive_runtime_owner_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_outbox.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\breadcrumb_archive_retry.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_breadcrumb_archive_remote.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_presentation.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_snapshot_adapter.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\breadcrumb_archive_runtime_owner.cpp'),
            (Join-Path $projectRoot 'tests\host\breadcrumb_archive_runtime_owner_tests.cpp')
        )
    },
    @{
        Name = 'local position sharing privacy control'
        Output = Join-Path $buildDirectory 'position_sharing_control_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\position_sharing_control_tests.cpp')
        )
    },
    @{
        Name = 'experimental position packet priority admission'
        Output = Join-Path $buildDirectory 'position_packet_admission_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_packet_admission.cpp'),
            (Join-Path $projectRoot 'tests\host\position_packet_admission_tests.cpp')
        )
    },
    @{
        Name = 'loss-aware priority to delivery handoff'
        Output = Join-Path $buildDirectory 'priority_delivery_handoff_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_packet_admission.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'tests\host\priority_delivery_handoff_tests.cpp')
        )
    },
    @{
        Name = 'checked-time outbound service coordination'
        Output = Join-Path $buildDirectory 'outbound_service_coordinator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\protocol\src\packet_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_packet_admission.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_service_coordinator.cpp'),
            (Join-Path $projectRoot 'tests\host\outbound_service_coordinator_tests.cpp')
        )
    },
    @{
        Name = 'fail-visible outbound position safety'
        Output = Join-Path $buildDirectory 'outbound_position_safety_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_service_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_position_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\outbound_position_safety_tests.cpp')
        )
    },
    @{
        Name = 'checked-time outbound position commands'
        Output = Join-Path $buildDirectory 'outbound_position_command_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_service_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_position_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\outbound_position_command_tests.cpp')
        )
    },
    @{
        Name = 'single-owner position sharing UI coordination'
        Output = Join-Path $buildDirectory 'position_sharing_ui_coordinator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_service_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_position_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_ui_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\position_sharing_ui_coordinator_tests.cpp')
        )
    },
    @{
        Name = 'position sharing UI state observation'
        Output = Join-Path $buildDirectory 'position_sharing_ui_observation_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\test_support\fake_radio_transport.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\delivery_controller.cpp'),
            (Join-Path $projectRoot 'firmware\components\delivery\src\priority_queue.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\location_tracker.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_codec.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\src\position_broadcast_scheduler.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_gps_provider.cpp'),
            (Join-Path $projectRoot 'firmware\components\location\test_support\fake_position_broadcast_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\priority_delivery_handoff.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_service_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\outbound_position_control.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\position_sharing_ui_coordinator.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\position_sharing_ui_observation_tests.cpp')
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
        Name = 'group load model'
        Output = Join-Path $buildDirectory 'group_load_model_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\src\lora_airtime.cpp'),
            (Join-Path $projectRoot 'firmware\components\simulation\src\group_load_model.cpp'),
            (Join-Path $projectRoot 'tests\host\group_load_model_tests.cpp')
        )
    },
    @{
        Name = 'persistent configuration'
        Output = Join-Path $buildDirectory 'configuration_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\persistence\src\configuration_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\test_support\memory_persistent_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\configuration_store_tests.cpp')
        )
    },
    @{
        Name = 'persistent key/value storage adapter'
        Output = Join-Path $buildDirectory 'persistent_storage_kv_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\persistence\src\configuration_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\src\persistent_storage_kv.cpp'),
            (Join-Path $projectRoot 'tests\host\persistent_storage_kv_tests.cpp')
        )
    },
    @{
        Name = 'outbound counter key/value composition'
        Output = Join-Path $buildDirectory 'outbound_counter_kv_composition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\persistence\src\outbound_counter_lease_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\src\persistent_storage_kv.cpp'),
            (Join-Path $projectRoot 'tests\host\outbound_counter_kv_composition_tests.cpp')
        )
    },
    @{
        Name = 'ACK session key/value composition'
        Output = Join-Path $buildDirectory 'ack_responder_session_kv_composition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\persistence\src\ack_responder_session_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\src\persistent_storage_kv.cpp'),
            (Join-Path $projectRoot 'tests\host\ack_responder_session_kv_composition_tests.cpp')
        )
    },
    @{
        Name = 'critical alert acknowledgement codec'
        Output = Join-Path $buildDirectory 'critical_alert_ack_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
            (Join-Path $projectRoot 'tests\host\critical_alert_ack_tests.cpp')
        )
    },
    @{
        Name = 'critical alert ACK responder'
        Output = Join-Path $buildDirectory 'critical_alert_ack_responder_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_responder.cpp'),
            (Join-Path $projectRoot 'tests\host\critical_alert_ack_responder_tests.cpp')
        )
    },
    @{
        Name = 'ACK responder session store'
        Output = Join-Path $buildDirectory 'ack_responder_session_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_responder.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\src\ack_responder_session_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\test_support\memory_persistent_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\ack_responder_session_store_tests.cpp')
        )
    },
    @{
        Name = 'outbound counter lease store'
        Output = Join-Path $buildDirectory 'outbound_counter_lease_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\persistence\src\outbound_counter_lease_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\persistence\test_support\memory_persistent_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\outbound_counter_lease_store_tests.cpp')
        )
    },
    @{
        Name = 'protected reassembly'
        Output = Join-Path $buildDirectory 'protected_reassembly_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\protocol\src\protected_reassembly.cpp'),
            (Join-Path $projectRoot 'tests\host\protected_reassembly_tests.cpp')
        )
    },
    @{
        Name = 'AEAD nonce composition'
        Output = Join-Path $buildDirectory 'aead_nonce_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\security\src\aead_nonce.cpp'),
            (Join-Path $projectRoot 'tests\host\aead_nonce_tests.cpp')
        )
    },
    @{
        Name = 'traffic-key derivation context'
        Output = Join-Path $buildDirectory 'traffic_key_context_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\security\src\traffic_key_context.cpp'),
            (Join-Path $projectRoot 'tests\host\traffic_key_context_tests.cpp')
        )
    },
    @{
        Name = 'secure randomness boundary'
        Output = Join-Path $buildDirectory 'secure_random_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\security\test_support\fake_secure_random.cpp'),
            (Join-Path $projectRoot 'tests\host\secure_random_tests.cpp')
        )
    },
    @{
        Name = 'monotonic clock boundary'
        Output = Join-Path $buildDirectory 'monotonic_clock_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\time\src\monotonic_clock.cpp'),
            (Join-Path $projectRoot 'firmware\components\time\test_support\fake_monotonic_counter_source.cpp'),
            (Join-Path $projectRoot 'tests\host\monotonic_clock_tests.cpp')
        )
    },
    @{
        Name = 'power state boundary'
        Output = Join-Path $buildDirectory 'power_state_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\power\src\power_state.cpp'),
            (Join-Path $projectRoot 'firmware\components\power\test_support\fake_power_status_source.cpp'),
            (Join-Path $projectRoot 'tests\host\power_state_tests.cpp')
        )
    },
    @{
        Name = 'local display and input boundary'
        Output = Join-Path $buildDirectory 'local_interface_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\local_interface_tests.cpp')
        )
    },
    @{
        Name = 'map activation guard'
        Output = Join-Path $buildDirectory 'map_activation_guard_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'tests\host\map_activation_guard_tests.cpp')
        )
    },
    @{
        Name = 'map selector checkpoint'
        Output = Join-Path $buildDirectory 'map_selector_checkpoint_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_checkpoint_tests.cpp')
        )
    },
    @{
        Name = 'recoverable map selector store'
        Output = Join-Path $buildDirectory 'map_selector_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_store_tests.cpp')
        )
    },
    @{
        Name = 'map selector key-value storage adapter'
        Output = Join-Path $buildDirectory 'map_selector_kv_storage_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_kv_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_kv_storage_tests.cpp')
        )
    },
    @{
        Name = 'map selector boot coordinator'
        Output = Join-Path $buildDirectory 'map_selector_boot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_boot.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_boot_tests.cpp')
        )
    },
    @{
        Name = 'map selector transition coordinator'
        Output = Join-Path $buildDirectory 'map_selector_transition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_transition.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_transition_tests.cpp')
        )
    },
    @{
        Name = 'map selector candidate coordinator'
        Output = Join-Path $buildDirectory 'map_selector_candidate_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_candidate.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_candidate_tests.cpp')
        )
    },
    @{
        Name = 'map selector first baseline coordinator'
        Output = Join-Path $buildDirectory 'map_selector_baseline_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_baseline.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_baseline_tests.cpp')
        )
    },
    @{
        Name = 'map selector reseed service authorization'
        Output = Join-Path $buildDirectory 'map_selector_reseed_authorization_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reseed_authorization.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_reseed_authorization_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted-generation boundary'
        Output = Join-Path $buildDirectory 'map_selector_trusted_generation_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_generation_tests.cpp')
        )
    },
    @{
        Name = 'map selector reset and replacement policy'
        Output = Join-Path $buildDirectory 'map_selector_reset_policy_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reset_policy.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_reset_policy_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected-domain authorization'
        Output = Join-Path $buildDirectory 'map_selector_domain_authorization_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reset_policy.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_authorization.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_authorization_tests.cpp')
        )
    },
    @{
        Name = 'map selector trust-domain lifecycle record'
        Output = Join-Path $buildDirectory 'map_selector_domain_record_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_record_tests.cpp')
        )
    },
    @{
        Name = 'recoverable map selector trust-domain store'
        Output = Join-Path $buildDirectory 'map_selector_domain_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_store_tests.cpp')
        )
    },
    @{
        Name = 'map selector domain key/value storage adapter'
        Output = Join-Path $buildDirectory 'map_selector_domain_kv_storage_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_kv_storage.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_kv_storage_tests.cpp')
        )
    },
    @{
        Name = 'authorized map selector trust-domain provisioner'
        Output = Join-Path $buildDirectory 'map_selector_domain_provisioner_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reset_policy.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_authorization.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_provisioner.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_provisioner_tests.cpp')
        )
    },
    @{
        Name = 'recoverable map selector trust-domain activation'
        Output = Join-Path $buildDirectory 'map_selector_domain_activation_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_activation.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_activation_tests.cpp')
        )
    },
    @{
        Name = 'read-only active map selector trust-domain boot'
        Output = Join-Path $buildDirectory 'map_selector_domain_boot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_boot.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_boot_tests.cpp')
        )
    },
    @{
        Name = 'domain-aware map selector candidate entry'
        Output = Join-Path $buildDirectory 'map_selector_domain_candidate_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_candidate.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_candidate.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_candidate_tests.cpp')
        )
    },
    @{
        Name = 'restart-safe domain-aware map selector trial boot'
        Output = Join-Path $buildDirectory 'map_selector_domain_trial_boot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_boot.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_trial_boot.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_trial_boot_tests.cpp')
        )
    },
    @{
        Name = 'domain-aware map selector runtime transitions'
        Output = Join-Path $buildDirectory 'map_selector_domain_transition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_boot.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_transition.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_record.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_trial_boot.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_domain_transition.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_domain_transition_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted boot coordinator'
        Output = Join-Path $buildDirectory 'map_selector_trusted_boot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_boot.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_boot.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_boot_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted baseline coordinator'
        Output = Join-Path $buildDirectory 'map_selector_trusted_baseline_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_baseline.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_baseline.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_baseline_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted candidate coordinator'
        Output = Join-Path $buildDirectory 'map_selector_trusted_candidate_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_candidate.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_candidate.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_candidate_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted transition coordinator'
        Output = Join-Path $buildDirectory 'map_selector_trusted_transition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_transition.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_transition.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_transition_tests.cpp')
        )
    },
    @{
        Name = 'authorized map selector reseed coordinator'
        Output = Join-Path $buildDirectory 'map_selector_reseed_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reseed_authorization.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reseed.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_reseed_tests.cpp')
        )
    },
    @{
        Name = 'map selector protected trusted reseed coordinator'
        Output = Join-Path $buildDirectory 'map_selector_trusted_reseed_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\maps\src\map_activation_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reseed_authorization.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_reseed.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_generation.cpp'),
            (Join-Path $projectRoot 'firmware\components\maps\src\map_selector_trusted_reseed.cpp'),
            (Join-Path $projectRoot 'tests\host\map_selector_trusted_reseed_tests.cpp')
        )
    },
    @{
        Name = 'update boot guard'
        Output = Join-Path $buildDirectory 'update_boot_guard_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'tests\host\update_boot_guard_tests.cpp')
        )
    },
    @{
        Name = 'update state checkpoint'
        Output = Join-Path $buildDirectory 'update_checkpoint_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'tests\host\update_checkpoint_tests.cpp')
        )
    },
    @{
        Name = 'recoverable update checkpoint store'
        Output = Join-Path $buildDirectory 'update_checkpoint_store_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'tests\host\update_checkpoint_store_tests.cpp')
        )
    },
    @{
        Name = 'update checkpoint key/value storage adapter'
        Output = Join-Path $buildDirectory 'update_checkpoint_kv_storage_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_kv_storage.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'tests\host\update_checkpoint_kv_storage_tests.cpp')
        )
    },
    @{
        Name = 'typed update recovery boot coordinator'
        Output = Join-Path $buildDirectory 'update_recovery_boot_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_boot.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_boot_tests.cpp')
        )
    },
    @{
        Name = 'verified update recovery save coordinator'
        Output = Join-Path $buildDirectory 'update_recovery_save_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_save.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_save_tests.cpp')
        )
    },
    @{
        Name = 'durable update recovery lifecycle transitions'
        Output = Join-Path $buildDirectory 'update_recovery_transition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_save.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_transition.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_transition_tests.cpp')
        )
    },
    @{
        Name = 'redacted update recovery operator status'
        Output = Join-Path $buildDirectory 'update_recovery_status_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_checkpoint_store.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_boot.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_save.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_transition.cpp'),
            (Join-Path $projectRoot 'firmware\components\update\src\update_recovery_status.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_status_tests.cpp')
        )
    },
    @{
        Name = 'versioned update recovery diagnostic event'
        Output = Join-Path $buildDirectory 'update_recovery_diagnostics_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\logger.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\test_support\memory_log_sink.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_diagnostics_tests.cpp')
        )
    },
    @{
        Name = 'versioned position sharing UI diagnostic event'
        Output = Join-Path $buildDirectory 'position_sharing_ui_diagnostics_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\logger.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\position_sharing_ui_diagnostics.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\test_support\memory_log_sink.cpp'),
            (Join-Path $projectRoot 'tests\host\position_sharing_ui_diagnostics_tests.cpp')
        )
    },
    @{
        Name = 'update-recovery diagnostic operator decoding'
        Output = Join-Path $buildDirectory 'update_recovery_diagnostic_operator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_diagnostic_operator_tests.cpp')
        )
    },
    @{
        Name = 'bounded production ring log sink'
        Output = Join-Path $buildDirectory 'ring_log_sink_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\logger.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\ring_log_sink.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'tests\host\ring_log_sink_tests.cpp')
        )
    },
    @{
        Name = 'position-sharing UI diagnostic operator decoding'
        Output = Join-Path $buildDirectory 'position_sharing_ui_diagnostic_operator_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\position_sharing_ui_diagnostics.cpp'),
            (Join-Path $projectRoot 'tests\host\position_sharing_ui_diagnostic_operator_tests.cpp')
        )
    },
    @{
        Name = 'semantic update recovery presentation'
        Output = Join-Path $buildDirectory 'update_recovery_presentation_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\update_recovery_presentation.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\test_support\fake_local_interface.cpp'),
            (Join-Path $projectRoot 'tests\host\update_recovery_presentation_tests.cpp')
        )
    },
    @{
        Name = 'portable-client target composition'
        Output = Join-Path $buildDirectory 'portable_client_composition_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\power\src\power_state.cpp'),
            (Join-Path $projectRoot 'firmware\components\ui\src\local_interface.cpp'),
            (Join-Path $projectRoot 'firmware\targets\portable_client\src\portable_client_composition.cpp'),
            (Join-Path $projectRoot 'tests\host\portable_client_composition_tests.cpp')
        )
    },
    @{
        Name = 'critical alert ingress'
        Output = Join-Path $buildDirectory 'critical_alert_ingress_tests.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
            (Join-Path $projectRoot 'tests\host\critical_alert_ingress_tests.cpp')
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
    },
    @{
        Name = 'critical alert bridge CLI'
        Output = Join-Path $buildDirectory 'critical_alert_bridge_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
            (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_responder.cpp'),
            (Join-Path $projectRoot 'tools\CriticalAlertBridgeCli.cpp')
        )
        Run = $false
    },
    @{
        Name = 'group load CLI'
        Output = Join-Path $buildDirectory 'group_load_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\radio\src\lora_airtime.cpp'),
            (Join-Path $projectRoot 'firmware\components\simulation\src\group_load_model.cpp'),
            (Join-Path $projectRoot 'tools\GroupLoadCli.cpp')
        )
        Run = $false
    },
    @{
        Name = 'position-sharing UI diagnostic CLI'
        Output = Join-Path $buildDirectory 'position_sharing_ui_diagnostic_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\position_sharing_ui_diagnostics.cpp'),
            (Join-Path $projectRoot 'tools\PositionSharingUiDiagnosticCli.cpp')
        )
        Run = $false
    },
    @{
        Name = 'update-recovery diagnostic CLI'
        Output = Join-Path $buildDirectory 'update_recovery_diagnostic_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'tools\UpdateRecoveryDiagnosticCli.cpp')
        )
        Run = $false
    },
    @{
        Name = 'unified OpenTrail diagnostic CLI'
        Output = Join-Path $buildDirectory 'opentrail_diagnostic_cli.exe'
        Sources = @(
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\position_sharing_ui_diagnostics.cpp'),
            (Join-Path $projectRoot 'firmware\components\diagnostics\src\update_recovery_diagnostics.cpp'),
            (Join-Path $projectRoot 'tools\OpenTrailDiagnosticCli.cpp')
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

$diagnosticCli = Join-Path $buildDirectory 'position_sharing_ui_diagnostic_cli.exe'
$diagnosticOutput = & $diagnosticCli 'OTPD0=C0012040' 2>&1
if ($LASTEXITCODE -ne 0 -or
    $diagnosticOutput -ne 'event=presentation outcome=succeeded notice=stopped reason=none frame_presented=1 state_changed=0 sharing_contained=0 sensitive_detail_redacted=1') {
    throw 'Position-sharing UI diagnostic CLI canonical smoke test failed.'
}
$invalidDiagnostic = Invoke-ExpectedNativeFailure $diagnosticCli 'OTPD0=c0012040'
if ($invalidDiagnostic.ExitCode -eq 0 -or
    $invalidDiagnostic.Text -ne 'OTPD0 decode failed: invalid_message') {
    throw 'Position-sharing UI diagnostic CLI invalid-input smoke test failed.'
}

$recoveryDiagnosticCli = Join-Path $buildDirectory 'update_recovery_diagnostic_cli.exe'
$recoveryDiagnosticOutput = & $recoveryDiagnosticCli 'OTRD0=D0105084' 2>&1
if ($LASTEXITCODE -ne 0 -or
    $recoveryDiagnosticOutput -ne 'operation=boot state=operational reason=clean_baseline action=continue_operation operation_succeeded=1 normal_operation_blocked=0 attention_required=0 reboot_required=0 confirmation_required=0 cleanup_required=0 sensitive_detail_redacted=1') {
    throw 'Update-recovery diagnostic CLI canonical smoke test failed.'
}
$invalidRecovery = Invoke-ExpectedNativeFailure $recoveryDiagnosticCli 'OTRD0=d0105084'
if ($invalidRecovery.ExitCode -eq 0 -or
    $invalidRecovery.Text -ne 'OTRD0 decode failed: invalid_message') {
    throw 'Update-recovery diagnostic CLI invalid-input smoke test failed.'
}

$unifiedDiagnosticCli = Join-Path $buildDirectory 'opentrail_diagnostic_cli.exe'
$unifiedPositionOutput = & $unifiedDiagnosticCli 'OTPD0=C0012040' 2>&1
if ($LASTEXITCODE -ne 0 -or
    $unifiedPositionOutput -ne 'record=position_ui event=presentation outcome=succeeded notice=stopped reason=none frame_presented=1 state_changed=0 sharing_contained=0 sensitive_detail_redacted=1') {
    throw 'Unified diagnostic CLI position-record smoke test failed.'
}
$unifiedRecoveryOutput = & $unifiedDiagnosticCli 'OTRD0=D0105084' 2>&1
if ($LASTEXITCODE -ne 0 -or
    $unifiedRecoveryOutput -ne 'record=update_recovery operation=boot state=operational reason=clean_baseline action=continue_operation operation_succeeded=1 normal_operation_blocked=0 attention_required=0 reboot_required=0 confirmation_required=0 cleanup_required=0 sensitive_detail_redacted=1') {
    throw 'Unified diagnostic CLI recovery-record smoke test failed.'
}
$invalidUnified = Invoke-ExpectedNativeFailure $unifiedDiagnosticCli 'OTXX0=00000000'
if ($invalidUnified.ExitCode -eq 0 -or
    $invalidUnified.Text -ne 'diagnostic decode failed: unsupported_record') {
    throw 'Unified diagnostic CLI unsupported-record smoke test failed.'
}
$malformedUnified = Invoke-ExpectedNativeFailure $unifiedDiagnosticCli 'OTPD0=c0012040'
if ($malformedUnified.ExitCode -eq 0 -or
    $malformedUnified.Text -ne 'position_ui decode failed: invalid_message') {
    throw 'Unified diagnostic CLI malformed-record smoke test failed.'
}

$python = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw 'Python was not found for MeshCore channel lease host tests.'
}
& $python.Source (Join-Path $projectRoot 'tests\host\meshcore_channel_lease_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "MeshCore channel lease tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tests\host\field_test_log_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Field-test log tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tests\host\pilot_result_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Four-person pilot result tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tests\host\crypto_benchmark_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Crypto benchmark evidence tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tests\host\map_package_manifest_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Map-package manifest tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tests\host\publication_safety_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Publication-safety scanner tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tools\Test-PublicationSafety.py') --root $projectRoot
if ($LASTEXITCODE -ne 0) {
    throw "Publication-safety tracked-content scan failed with exit code $LASTEXITCODE."
}
