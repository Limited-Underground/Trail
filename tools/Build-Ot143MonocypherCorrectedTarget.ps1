[CmdletBinding()]
param(
    [switch] $Execute,
    [string] $OutputRoot,
    [string] $EvidencePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceProject = Join-Path $projectRoot 'tests\benchmarks\crypto\esp_idf\ot121_candidate_benchmarks\monocypher_ot142_corrected'
$correctedDefaults = Join-Path $sourceProject 'sdkconfig.defaults'
$application = 'ot142_monocypher_corrected_bench'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'opentrail-ot143-corrected-target'
}
if (-not $EvidencePath) {
    $EvidencePath = Join-Path $projectRoot 'tests\benchmarks\crypto\OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json'
}

function Assert-ExactFile([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is absent."
    }
}

function Get-LowerSha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CanonicalLfBytes([string] $Path) {
    $raw = [System.IO.File]::ReadAllBytes($Path)
    if ($raw.Length -ge 3 -and $raw[0] -eq 0xef -and $raw[1] -eq 0xbb -and $raw[2] -eq 0xbf) {
        throw "OT-143 canonical input has a UTF-8 BOM: $Path"
    }
    $utf8 = [System.Text.UTF8Encoding]::new($false, $true)
    $text = $utf8.GetString($raw)
    $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    return $utf8.GetBytes($normalized)
}

function Get-BytesSha256([byte[]] $Bytes) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
}

function Assert-CanonicalLfInput([string] $RelativePath, [long] $Bytes, [string] $Sha256) {
    $path = Join-Path $projectRoot ($RelativePath -replace '/', '\')
    Assert-ExactFile $path "OT-143 canonical input $RelativePath"
    $canonical = Get-CanonicalLfBytes $path
    if ($canonical.Length -ne $Bytes -or (Get-BytesSha256 $canonical) -ne $Sha256) {
        throw "OT-143 canonical LF input differs: $RelativePath"
    }
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
    throw 'OT-143 is inert unless -Execute is supplied.'
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw 'OutputRoot must be initially absent.'
}
if (-not (Test-Path -LiteralPath $sourceProject -PathType Container)) {
    throw 'OT-143 source project is absent.'
}
Assert-ExactFile $correctedDefaults 'OT-142 corrected defaults'
$canonicalLfInputRelativePaths = @(
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/app_main.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/sdkconfig.defaults'
)
Assert-CanonicalLfInput $canonicalLfInputRelativePaths[0] 238 'a01b5298c3bbc7bbbbb72c381b991122e8aa55873e2f27f88d3ad1db45fe8c94'
Assert-CanonicalLfInput $canonicalLfInputRelativePaths[1] 877 '47bd2006a465ae18b23119314bb2385fcb47cea144f0b40191a9eaaf7b35b029'
Assert-CanonicalLfInput $canonicalLfInputRelativePaths[2] 22619 '6504cd2de51cad0af856157fa09af0904a5df61c50fc66ad1ee04acfbbab06b3'
Assert-CanonicalLfInput $canonicalLfInputRelativePaths[3] 997 'f8d20cdc61ba606e47ba76049b7be97d959441abea691deae3b85cba7fd2e404'
if ((Get-Item -LiteralPath (Join-Path $sourceProject 'partitions.csv')).Length -ne 452 -or
    (Get-LowerSha256 (Join-Path $sourceProject 'partitions.csv')) -ne
        '4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389') {
    throw 'OT-143 canonical CRLF partition input differs.'
}
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
    'CONFIG_BOOT_ROM_LOG_ON_GPIO_LOW=y',
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
        '-D', "SDKCONFIG_DEFAULTS=$correctedDefaults",
        '-D', 'PROJECT_VER=ot142-corrected-v0',
        '-D', 'IDF_TARGET=esp32s3',
        'build'
    )
    $output = @(& $python.Source @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    [System.IO.File]::WriteAllLines(
        $rawLog, [string[]] $output, [System.Text.UTF8Encoding]::new($false))
    if ($exitCode -ne 0) {
        throw "OT-143 run $runLabel failed."
    }
    $compilerWarnings = @($output | Where-Object {
        $_ -match '(?i)\.(?:c|cc|cpp|cxx|s):[0-9]+(?::[0-9]+)?:\s+warning:'
    }).Count
    if ($compilerWarnings -ne 0) {
        throw "OT-143 run $runLabel emitted compiler warnings."
    }

    Assert-ExactFile $sdkconfig 'generated sdkconfig'
    $configLines = [string[]] (Get-Content -LiteralPath $sdkconfig)
    foreach ($line in $requiredConfigLines) {
        if ($configLines -notcontains $line) {
            throw "OT-143 run $runLabel is missing resolved config line: $line"
        }
    }
    foreach ($line in $forbiddenConfigLines) {
        if ($configLines -contains $line) {
            throw "OT-143 run $runLabel retains forbidden config line: $line"
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
    throw 'OT-143 fresh-build artifact tuples differ.'
}

$expectedTuple = @(
    'application_bin:ot142_monocypher_corrected_bench.bin:149824:8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034',
    'application_elf:ot142_monocypher_corrected_bench.elf:3314668:ec74f80422a5b9722e09342d69e190282a4117b4888c96838cf915dc760466d1',
    'linker_map:ot142_monocypher_corrected_bench.map:2849996:c9a2789451305417dc2cb523ba6284a9c4ca8014724d051f829b4f5a7b31af13',
    'bootloader_bin:bootloader.bin:15216:604af9d70953d917734f45b4c1cb764a23c17c8e3e5b28e11e1f3f6a02ef1c38',
    'partition_table_bin:partition-table.bin:3072:84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab',
    'generated_sdkconfig:sdkconfig:57516:5807fe7fc6d4ef3325f06099674f07080660eabd20e0b078225247605c814817'
)
if (Compare-Object $leftTuple $expectedTuple) {
    throw 'OT-143 fresh-build artifact tuple differs from the accepted OT-142 tuple.'
}

$inputRelativePaths = @(
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/partitions.csv',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/sdkconfig.defaults',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/app_main.c',
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
    Assert-ExactFile $path "OT-143 input $_"
    if ($canonicalLfInputRelativePaths -contains $_) {
        $raw = Get-CanonicalLfBytes $path
        $bytes = $raw.Length
        $sha256 = Get-BytesSha256 $raw
    } else {
        $item = Get-Item -LiteralPath $path
        $bytes = $item.Length
        $sha256 = Get-LowerSha256 $path
    }
    [ordered]@{
        path = $_
        bytes = $bytes
        sha256 = $sha256
    }
})

$evidence = [ordered]@{
    schema = 'OT143CTB0'
    version = 0
    artifact_kind = 'monocypher_corrected_target_build_evidence'
    evidence_id = 'OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0'
    recorded_date = '2026-08-26'
    status = 'corrected_target_build_complete_host_only'
    source_project = 'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected'
    lineage = [ordered]@{
        predecessor = 'OT-129'
        accepted_direction = 'OT-142'
        frozen_protocol_runners_modified = $false
        corrected_ot142_source_bound_directly = $true
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
        project_version = 'ot142-corrected-v0'
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






