[CmdletBinding()]
param(
    [switch] $Execute,
    [string] $OutputRoot,
    [string] $EvidencePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceProject = Join-Path $projectRoot 'tests\benchmarks\crypto\esp_idf\ot121_candidate_benchmarks\monocypher_ot139_quiet'
$quietDefaults = Join-Path $sourceProject 'sdkconfig.defaults'
$application = 'ot139_monocypher_quiet_bench'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'opentrail-ot139-quiet-target'
}
if (-not $EvidencePath) {
    $EvidencePath = Join-Path $projectRoot 'tests\benchmarks\crypto\OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0.json'
}

function Assert-ExactFile([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is absent."
    }
}

function Get-LowerSha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-Artifact([string] $Role, [string] $Path) {
    Assert-ExactFile $Path $Role
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        role = $Role
        name = $item.Name
        bytes = $item.Length
        sha256 = Get-LowerSha256 $Path
    }
}

if (-not $Execute) {
    throw 'OT-139 is inert unless -Execute is supplied.'
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw 'OutputRoot must be initially absent.'
}
if (-not (Test-Path -LiteralPath $sourceProject -PathType Container)) {
    throw 'OT-139 source project is absent.'
}
Assert-ExactFile $quietDefaults 'OT-139 quiet defaults'
if (-not $env:IDF_PATH) {
    throw 'IDF_PATH is absent; activate the pinned ESP-IDF environment first.'
}

$idfRoot = (Resolve-Path -LiteralPath $env:IDF_PATH).Path
$expectedIdfCommit = '7101770dc6db2667b3c477cc31365dd1acd6db4e'
$actualIdfCommit = (& git -C $idfRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualIdfCommit -ne $expectedIdfCommit) {
    throw 'Pinned ESP-IDF commit mismatch.'
}
$idfStatus = @(& git -C $idfRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $idfStatus.Count -ne 0) {
    throw 'Pinned ESP-IDF worktree is not clean.'
}
$idfTool = Join-Path $idfRoot 'tools\idf.py'
Assert-ExactFile $idfTool 'ESP-IDF idf.py'
$python = Get-Command python -ErrorAction Stop

$requiredConfigLines = @(
    'CONFIG_ESP_CONSOLE_NONE=y',
    'CONFIG_ESP_CONSOLE_SECONDARY_NONE=y',
    'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y',
    'CONFIG_BOOTLOADER_LOG_LEVEL=0',
    'CONFIG_LOG_DEFAULT_LEVEL_NONE=y',
    'CONFIG_LOG_DEFAULT_LEVEL=0',
    'CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y',
    'CONFIG_LOG_MAXIMUM_LEVEL=0',
    'CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y',
    'CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y',
    'CONFIG_APP_REPRODUCIBLE_BUILD=y',
    'CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y',
    '# CONFIG_ESP_CONSOLE_USB_CDC is not set',
    '# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set',
    '# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set',
    '# CONFIG_ESP_CONSOLE_UART_CUSTOM is not set',
    '# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set',
    'CONFIG_ESP_CONSOLE_UART_NONE=y'
)
$forbiddenConfigLines = @(
    'CONFIG_ESP_CONSOLE_USB_CDC=y',
    'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y',
    'CONFIG_ESP_CONSOLE_UART_DEFAULT=y',
    'CONFIG_ESP_CONSOLE_UART_CUSTOM=y',
    'CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y',
    'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y',
    'CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y',
    'CONFIG_LOG_DEFAULT_LEVEL_INFO=y',
    'CONFIG_BOOT_ROM_LOG_ALWAYS_OFF=y',
    'CONFIG_BOOT_ROM_LOG_ON_GPIO_HIGH=y',
    'CONFIG_BOOT_ROM_LOG_ON_GPIO_LOW=y'
    'CONFIG_BT_ENABLED=y',
    'CONFIG_ESP_WIFI_ENABLED=y'
)

$env:IDF_COMPONENT_MANAGER = '0'
$runs = @()
foreach ($runLabel in @('A', 'B')) {
    $runRoot = Join-Path $OutputRoot "run-$($runLabel.ToLowerInvariant())"
    $buildRoot = Join-Path $runRoot 'build'
    $rawLog = Join-Path $runRoot 'build.log'
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $sdkconfig = Join-Path $buildRoot 'sdkconfig'
    $arguments = @(
        $idfTool,
        '--no-ccache',
        '-C', $sourceProject,
        '-B', $buildRoot,
        '-D', "SDKCONFIG=$sdkconfig",
        '-D', "SDKCONFIG_DEFAULTS=$quietDefaults",
        '-D', 'PROJECT_VER=ot139-quiet-v0',
        '-D', 'IDF_TARGET=esp32s3',
        'build'
    )
    $output = @(& $python.Source @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    [System.IO.File]::WriteAllLines(
        $rawLog, [string[]] $output, [System.Text.UTF8Encoding]::new($false))
    if ($exitCode -ne 0) {
        throw "OT-139 run $runLabel failed."
    }
    $compilerWarnings = @($output | Where-Object {
        $_ -match '(?i)\.(?:c|cc|cpp|cxx|s):[0-9]+(?::[0-9]+)?:\s+warning:'
    }).Count
    if ($compilerWarnings -ne 0) {
        throw "OT-139 run $runLabel emitted compiler warnings."
    }

    Assert-ExactFile $sdkconfig 'generated sdkconfig'
    $configLines = [string[]] (Get-Content -LiteralPath $sdkconfig)
    foreach ($line in $requiredConfigLines) {
        if ($configLines -notcontains $line) {
            throw "OT-139 run $runLabel is missing resolved config line: $line"
        }
    }
    foreach ($line in $forbiddenConfigLines) {
        if ($configLines -contains $line) {
            throw "OT-139 run $runLabel retains forbidden config line: $line"
        }
    }

    $artifacts = @(
        Get-Artifact 'application_bin' (Join-Path $buildRoot "$application.bin")
        Get-Artifact 'application_elf' (Join-Path $buildRoot "$application.elf")
        Get-Artifact 'linker_map' (Join-Path $buildRoot "$application.map")
        Get-Artifact 'bootloader_bin' (Join-Path $buildRoot 'bootloader\bootloader.bin')
        Get-Artifact 'partition_table_bin' (Join-Path $buildRoot 'partition_table\partition-table.bin')
        Get-Artifact 'generated_sdkconfig' $sdkconfig
    )
    $runs += [pscustomobject]@{
        run = $runLabel
        initial_build_directory_absent = $true
        build_exit_code = $exitCode
        compiler_warning_count = $compilerWarnings
        raw_build_log_sha256 = Get-LowerSha256 $rawLog
        artifacts = $artifacts
    }
}

$leftTuple = @($runs[0].artifacts | ForEach-Object {
    "$($_.role):$($_.name):$($_.bytes):$($_.sha256)"
})
$rightTuple = @($runs[1].artifacts | ForEach-Object {
    "$($_.role):$($_.name):$($_.bytes):$($_.sha256)"
})
if (Compare-Object $leftTuple $rightTuple) {
    throw 'OT-139 fresh-build artifact tuples differ.'
}

$inputRelativePaths = @(
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/partitions.csv',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/sdkconfig.defaults',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/main/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/app_main.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h',
    'tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c',
    'tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.h',
    'tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c',
    'tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.h',
    'tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c',
    'tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.h'
)
$inputs = @($inputRelativePaths | ForEach-Object {
    $path = Join-Path $projectRoot ($_ -replace '/', '\')
    Assert-ExactFile $path "OT-139 input $_"
    $item = Get-Item -LiteralPath $path
    [ordered]@{
        path = $_
        bytes = $item.Length
        sha256 = Get-LowerSha256 $path
    }
})

$evidence = [ordered]@{
    schema = 'OT139QTB0'
    version = 0
    artifact_kind = 'monocypher_quiet_target_build_evidence'
    evidence_id = 'OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0'
    recorded_date = '2026-08-25'
    status = 'quiet_target_build_complete_host_only'
    source_project = 'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet'
    lineage = [ordered]@{
        predecessor = 'OT-129'
        accepted_direction = 'OT-138'
        frozen_protocol_runners_modified = $false
        frozen_ot129_source_reused_by_reference = $true
    }
    configuration = [ordered]@{
        primary_console = 'none'
        secondary_console = 'none'
        bootloader_log_level = 'none'
        default_application_log_level = 'none'
        rom_log_policy = 'always_on_unchanged'
        direct_usb_serial_jtag_driver_enabled = $true
        bluetooth_enabled = $false
        wifi_enabled = $false
        required_resolved_lines = $requiredConfigLines
        forbidden_enabled_lines = $forbiddenConfigLines
    }
    toolchain = [ordered]@{
        esp_idf_version = 'v6.0.2'
        esp_idf_commit = $actualIdfCommit
        esp_idf_worktree_clean = $true
        idf_target = 'esp32s3'
        project_version = 'ot139-quiet-v0'
        ccache_allowed = $false
        component_manager_network_allowed = $false
    }
    source_inputs = $inputs
    build_reproducibility = [ordered]@{
        run_count = 2
        initial_build_directories_absent = $true
        artifact_roles = @(
            'application_bin', 'application_elf', 'linker_map',
            'bootloader_bin', 'partition_table_bin', 'generated_sdkconfig')
        required_four_artifact_roles = @(
            'application_bin', 'application_elf', 'linker_map', 'generated_sdkconfig')
        artifact_tuples_identical = $true
        canonical_artifact_tuple = $runs[0].artifacts
        runs = $runs
    }
    preserved_boundaries = [ordered]@{
        pre_ready_budget_bytes = 512
        start_retry_milliseconds = 250
        exact_ready_required = $true
        frame_before_ready_rejected = $true
        duplicate_and_post_ready_strict = $true
        privacy_safe_diagnostics_required = $true
        real_frame_count = 1014
    }
    limitations = [ordered]@{
        physical_usb_silence_proven = $false
        initial_rom_output_suppression_proven = $false
        efuse_or_strap_change_proposed = $false
        reason = 'ESP-IDF host-only configuration cannot prove physical initial ROM output behavior.'
    }
    claims = [ordered]@{
        hardware_accessed = $false
        phone_accessed = $false
        firmware_flashed = $false
        benchmark_executed = $false
        radio_used = $false
        execution_authority_created = $false
        candidate_selected = $false
        phase_two_complete = $false
        score_credit_added = $false
    }
}

$evidenceDirectory = Split-Path -Parent $EvidencePath
New-Item -ItemType Directory -Force -Path $evidenceDirectory | Out-Null
$evidenceJson = $evidence | ConvertTo-Json -Depth 12
$evidenceJson = $evidenceJson.Replace("`r`n", "`n").Replace("`r", "`n")
[System.IO.File]::WriteAllText(
    $EvidencePath,
    ($evidenceJson + "`n"),
    [System.Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    evidence_path = $EvidencePath
    output_root = $OutputRoot
    application_sha256 = $runs[0].artifacts[0].sha256
    sdkconfig_sha256 = $runs[0].artifacts[5].sha256
    artifact_tuples_identical = $true
    hardware_accessed = $false
    execution_authority_created = $false
} | ConvertTo-Json -Compress
