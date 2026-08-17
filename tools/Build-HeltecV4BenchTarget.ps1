[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredVersion = 'ESP-IDF v6.0.2'
$projectRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench'
$buildRoot = Join-Path $projectRoot 'build\targets\heltec_v4_bench'
$defaultsPath = Join-Path $targetRoot 'sdkconfig.defaults'
$partitionCsvPath = Join-Path $targetRoot 'partitions.csv'
$logoPath = Join-Path $targetRoot 'main\trail_startup_logo.hpp'
$sdkconfigPath = Join-Path $buildRoot 'sdkconfig'

if (-not $env:IDF_PATH) {
    throw 'IDF_PATH is not set. Export an ESP-IDF v6.0.2 environment first.'
}

$idfPython = Get-Command python -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $idfPython) {
    throw 'Python is unavailable in the exported ESP-IDF environment.'
}

$idfTool = Join-Path $env:IDF_PATH 'tools\idf.py'
if (-not (Test-Path -LiteralPath $idfTool -PathType Leaf)) {
    throw "idf.py was not found below IDF_PATH: $idfTool"
}

$reportedVersion = (& $idfPython.Source $idfTool --version 2>&1 |
    ForEach-Object { $_.ToString().Trim() }) -join ' '
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne $requiredVersion) {
    throw "Expected $requiredVersion; received '$reportedVersion'."
}

& $idfPython.Source `
    (Join-Path $projectRoot 'tests\host\heltec_v4_bench_nimble_order_tests.py') `
    --idf-path $env:IDF_PATH
if ($LASTEXITCODE -ne 0) {
    throw "Pinned NimBLE indication/stop-order admission failed with exit code $LASTEXITCODE."
}

$competingConsoleSelections = @(
    'CONFIG_ESP_CONSOLE_UART_DEFAULT=y',
    'CONFIG_ESP_CONSOLE_UART_CUSTOM=y',
    'CONFIG_ESP_CONSOLE_USB_CDC=y',
    'CONFIG_ESP_CONSOLE_NONE=y',
    'CONFIG_ESP_CONSOLE_UART_NONE=y'
)
$requiredProfileDefaultSelections = @(
    'CONFIG_ESPTOOLPY_OCT_FLASH=n',
    'CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=n',
    'CONFIG_ESPTOOLPY_FLASHMODE_QIO=y',
    'CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y',
    'CONFIG_ESPTOOLPY_FLASHFREQ_80M=y',
    'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
    'CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=n',
    'CONFIG_PARTITION_TABLE_CUSTOM=y',
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
    'CONFIG_SPIRAM=y',
    'CONFIG_SPIRAM_MODE_QUAD=y',
    'CONFIG_SPIRAM_TYPE_ESPPSRAM16=y',
    'CONFIG_SPIRAM_SPEED_80M=y',
    'CONFIG_SPIRAM_BOOT_HW_INIT=y',
    'CONFIG_SPIRAM_BOOT_INIT=y',
    'CONFIG_SPIRAM_IGNORE_NOTFOUND=n',
    'CONFIG_SPIRAM_MEMTEST=y',
    'CONFIG_SPIRAM_USE_CAPS_ALLOC=y'
)
$requiredGeneratedProfileSelections = @(
    'CONFIG_ESPTOOLPY_FLASHMODE_QIO=y',
    'CONFIG_ESPTOOLPY_FLASHMODE="dio"',
    'CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y',
    'CONFIG_ESPTOOLPY_FLASHFREQ_80M=y',
    'CONFIG_ESPTOOLPY_FLASHFREQ="80m"',
    'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
    'CONFIG_PARTITION_TABLE_CUSTOM=y',
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
    'CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"',
    'CONFIG_SPIRAM=y',
    'CONFIG_SPIRAM_MODE_QUAD=y',
    'CONFIG_SPIRAM_TYPE_ESPPSRAM16=y',
    'CONFIG_SPIRAM_SPEED_80M=y',
    'CONFIG_SPIRAM_BOOT_HW_INIT=y',
    'CONFIG_SPIRAM_BOOT_INIT=y',
    'CONFIG_SPIRAM_MEMTEST=y',
    'CONFIG_SPIRAM_USE_CAPS_ALLOC=y'
)
$requiredDisabledGeneratedProfileSelections = @(
    '# CONFIG_ESPTOOLPY_OCT_FLASH is not set',
    '# CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT is not set',
    '# CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE is not set',
    '# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set'
)
$forbiddenProfileSelections = @(
    'CONFIG_ESPTOOLPY_FLASHMODE_DIO=y',
    'CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y',
    'CONFIG_PARTITION_TABLE_SINGLE_APP=y',
    'CONFIG_SPIRAM_MODE_OCT=y',
    'CONFIG_SPIRAM_IGNORE_NOTFOUND=y'
)
$profileDefaultLines = @(Get-Content -LiteralPath $defaultsPath)
foreach ($selection in $requiredProfileDefaultSelections) {
    if ($profileDefaultLines -notcontains $selection) {
        throw "sdkconfig.defaults is missing exact OT-DEV-001 profile selection: $selection"
    }
}
foreach ($selection in $forbiddenProfileSelections) {
    if ($profileDefaultLines -contains $selection) {
        throw "sdkconfig.defaults enables a generic or unsafe profile selection: $selection"
    }
}
$requiredNimbleSelections = @(
    'CONFIG_BT_ENABLED=y',
    'CONFIG_BT_CONTROLLER_ENABLED=y',
    'CONFIG_BT_NIMBLE_ENABLED=y',
    'CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y',
    'CONFIG_BT_NIMBLE_GATT_SERVER=y',
    'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1',
    'CONFIG_BT_NIMBLE_SECURITY_ENABLE=y',
    'CONFIG_BT_NIMBLE_SM_SC=y',
    'CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION=y',
    'CONFIG_BT_NIMBLE_SM_LVL=3',
    'CONFIG_BT_NIMBLE_SM_SC_ONLY=1'
)
$forbiddenNimbleSelections = @(
    'CONFIG_BT_NIMBLE_ROLE_CENTRAL=y',
    'CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y',
    'CONFIG_BT_NIMBLE_ROLE_OBSERVER=y',
    'CONFIG_BT_NIMBLE_GATT_CLIENT=y',
    'CONFIG_BT_NIMBLE_SM_LEGACY=y',
    'CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS=y',
    'CONFIG_BT_NIMBLE_NVS_PERSIST=y'
)
$requiresTargetSelection = $true
if (Test-Path -LiteralPath $sdkconfigPath -PathType Leaf) {
    $existingSdkconfigLines = @(Get-Content -LiteralPath $sdkconfigPath)
    $existingTargetMatches =
        $existingSdkconfigLines -contains 'CONFIG_IDF_TARGET="esp32s3"'
    $existingConsoleMatches =
        $existingSdkconfigLines -contains 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y'
    $existingConsoleCompetes = @($competingConsoleSelections | Where-Object {
        $existingSdkconfigLines -contains $_
    }).Count -ne 0
    $existingProfileMatches =
        @($requiredGeneratedProfileSelections | Where-Object {
            $existingSdkconfigLines -notcontains $_
        }).Count -eq 0 -and
        @($requiredDisabledGeneratedProfileSelections | Where-Object {
            $existingSdkconfigLines -notcontains $_
        }).Count -eq 0
    $existingProfileCompetes = @($forbiddenProfileSelections | Where-Object {
        $existingSdkconfigLines -contains $_
    }).Count -ne 0
    $existingNimbleMatches = @($requiredNimbleSelections | Where-Object {
        $existingSdkconfigLines -notcontains $_
    }).Count -eq 0
    $existingNimbleCompetes = @($forbiddenNimbleSelections | Where-Object {
        $existingSdkconfigLines -contains $_
    }).Count -ne 0
    $requiresTargetSelection = -not (
        $existingTargetMatches -and
        $existingConsoleMatches -and
        -not $existingConsoleCompetes -and
        $existingProfileMatches -and
        -not $existingProfileCompetes -and
        $existingNimbleMatches -and
        -not $existingNimbleCompetes)
}

if ($requiresTargetSelection) {
    & $idfPython.Source $idfTool `
        -C $targetRoot `
        -B $buildRoot `
        -D "SDKCONFIG=$sdkconfigPath" `
        -D "SDKCONFIG_DEFAULTS=$defaultsPath" `
        set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32-S3 target selection failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Output 'Existing exact ESP32-S3/USB Serial-JTAG sdkconfig accepted; preserving incremental build state.'
}

& $idfPython.Source $idfTool `
    -C $targetRoot `
    -B $buildRoot `
    -D "SDKCONFIG=$sdkconfigPath" `
    -D "SDKCONFIG_DEFAULTS=$defaultsPath" `
    build
if ($LASTEXITCODE -ne 0) {
    throw "Heltec V4 bench candidate build failed with exit code $LASTEXITCODE."
}

$generatedSdkconfigLines = @(Get-Content -LiteralPath $sdkconfigPath)
if ($generatedSdkconfigLines -notcontains 'CONFIG_IDF_TARGET="esp32s3"') {
    throw 'Generated sdkconfig did not select ESP32-S3.'
}
if ($generatedSdkconfigLines -notcontains 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y') {
    throw 'Generated sdkconfig did not select USB Serial/JTAG as the primary console.'
}
foreach ($selection in $competingConsoleSelections) {
    if ($generatedSdkconfigLines -contains $selection) {
        throw "Generated sdkconfig has a competing console selection: $selection"
    }
}
foreach ($selection in $requiredGeneratedProfileSelections) {
    if ($generatedSdkconfigLines -notcontains $selection) {
        throw "Generated sdkconfig is missing exact OT-DEV-001 profile selection: $selection"
    }
}
foreach ($selection in $requiredDisabledGeneratedProfileSelections) {
    if ($generatedSdkconfigLines -notcontains $selection) {
        throw "Generated sdkconfig did not keep the required profile option disabled: $selection"
    }
}
foreach ($selection in $forbiddenProfileSelections) {
    if ($generatedSdkconfigLines -contains $selection) {
        throw "Generated sdkconfig enables a generic or unsafe profile selection: $selection"
    }
}
foreach ($selection in $requiredNimbleSelections) {
    if ($generatedSdkconfigLines -notcontains $selection) {
        throw "Generated sdkconfig is missing required NimBLE selection: $selection"
    }
}
foreach ($selection in $forbiddenNimbleSelections) {
    if ($generatedSdkconfigLines -contains $selection) {
        throw "Generated sdkconfig enables forbidden NimBLE selection: $selection"
    }
}

& $idfPython.Source $idfTool `
    -C $targetRoot `
    -B $buildRoot `
    size
if ($LASTEXITCODE -ne 0) {
    throw "Heltec V4 bench candidate size analysis failed with exit code $LASTEXITCODE."
}

$applicationBinPath = Join-Path $buildRoot 'opentrail_heltec_v4_bench.bin'
$partitionBinPath = Join-Path $buildRoot 'partition_table\partition-table.bin'
$artifactPaths = @(
    $applicationBinPath,
    (Join-Path $buildRoot 'opentrail_heltec_v4_bench.elf'),
    (Join-Path $buildRoot 'opentrail_heltec_v4_bench.map'),
    (Join-Path $buildRoot 'bootloader\bootloader.bin'),
    $partitionBinPath,
    $sdkconfigPath,
    $partitionCsvPath
)

$imageBytes = [System.IO.File]::ReadAllBytes($applicationBinPath)
if ($imageBytes.Length -lt 4 -or $imageBytes[0] -ne 0xE9) {
    throw 'Application image does not contain a valid ESP image header.'
}
# ESP-IDF v6.0.2 deliberately emits the bootstrap header in DIO even when
# QIO is frozen in sdkconfig; the bootloader enables quad access during init.
if ($imageBytes[2] -ne 0x02) {
    throw ('Application image header flash mode mismatch: expected DIO bootstrap byte 0x02 for the frozen QIO profile; received 0x{0:X2}.' -f $imageBytes[2])
}
if ($imageBytes[3] -ne 0x4F) {
    throw ('Application image header size/frequency mismatch: expected 16MB/80MHz byte 0x4F; received 0x{0:X2}.' -f $imageBytes[3])
}

$partitionTool = Join-Path $env:IDF_PATH 'components\partition_table\gen_esp32part.py'
if (-not (Test-Path -LiteralPath $partitionTool -PathType Leaf)) {
    throw "Partition-table verifier was not found below IDF_PATH: $partitionTool"
}
$decodedPartitionLines = @(& $idfPython.Source $partitionTool `
    --flash-size 16MB --quiet $partitionBinPath 2>&1 |
    ForEach-Object { $_.ToString().Trim() } |
    Where-Object { $_ -and -not $_.StartsWith('#') })
if ($LASTEXITCODE -ne 0) {
    throw "Partition-table binary validation failed with exit code $LASTEXITCODE."
}
$expectedPartitionRows = @(
    @{ Name = 'otadata'; Type = 'data'; SubType = 'ota'; Offset = 0x9000; Size = 0x2000 },
    @{ Name = 'factory'; Type = 'app'; SubType = 'factory'; Offset = 0x10000; Size = 0x4F0000 },
    @{ Name = 'ota_0'; Type = 'app'; SubType = 'ota_0'; Offset = 0x500000; Size = 0x500000 },
    @{ Name = 'ota_1'; Type = 'app'; SubType = 'ota_1'; Offset = 0xA00000; Size = 0x500000 },
    @{ Name = 'ot_state'; Type = '64'; SubType = '0'; Offset = 0xF00000; Size = 0x100000 }
)
function Convert-PartitionValue([string]$Value) {
    if ($Value -match '^0x[0-9a-fA-F]+$') {
        return [Convert]::ToInt64($Value.Substring(2), 16)
    }
    if ($Value -match '^(\d+)(K|M)$') {
        $multiplier = if ($Matches[2] -eq 'M') { 1024 * 1024 } else { 1024 }
        return [int64]$Matches[1] * $multiplier
    }
    throw "Unrecognized partition numeric value: $Value"
}
if ($decodedPartitionLines.Count -ne $expectedPartitionRows.Count) {
    throw "Decoded partition row count mismatch: expected $($expectedPartitionRows.Count); received $($decodedPartitionLines.Count)."
}
for ($index = 0; $index -lt $expectedPartitionRows.Count; $index++) {
    $fields = @($decodedPartitionLines[$index].Split(',') | ForEach-Object { $_.Trim() })
    $expected = $expectedPartitionRows[$index]
    if ($fields.Count -lt 5 -or
        $fields[0] -ne $expected.Name -or
        $fields[1] -ne $expected.Type -or
        $fields[2] -ne $expected.SubType -or
        (Convert-PartitionValue $fields[3]) -ne $expected.Offset -or
        (Convert-PartitionValue $fields[4]) -ne $expected.Size) {
        throw "Decoded partition mismatch at row ${index}: $($decodedPartitionLines[$index])"
    }
}
$lastPartition = $expectedPartitionRows[-1]
if (($lastPartition.Offset + $lastPartition.Size) -ne 16777216) {
    throw 'Recovery partition layout does not end at the exact 16 MB flash boundary.'
}
$partitionBinaryVerified = $true

$linkMapPath = Join-Path $buildRoot 'opentrail_heltec_v4_bench.map'
$linkMap = Get-Content -LiteralPath $linkMapPath -Raw
foreach ($requiredObject in @(
    'companion_protocol.cpp.obj',
    'companion_semantics.cpp.obj',
    'companion_request_coordinator.cpp.obj',
    'companion_gatt_session.cpp.obj',
    'companion_authorization_wire.cpp.obj',
    'companion_gatt_authorization.cpp.obj',
    'companion_gatt_authorization_adapter.cpp.obj',
    'companion_authorization.cpp.obj',
    'companion_authorization_persistence.cpp.obj',
    'companion_boot_self_check.cpp.obj',
    'companion_nimble_gatt.cpp.obj',
    'companion_nimble_runtime.cpp.obj',
    'companion_authorization_storage.cpp.obj',
    'companion_ble_runtime_owner.cpp.obj',
    'heltec_startup_display.cpp.obj',
    'heltec_v4_oled.cpp.obj'
)) {
    if (-not $linkMap.Contains($requiredObject)) {
        throw "Required companion codec object is absent from the link map: $requiredObject"
    }
}
$compiledAuthorizationBackendObjects = @(
    Get-ChildItem -LiteralPath $buildRoot -Recurse -File |
        Where-Object {
            $_.Name -eq 'companion_authorization_nvs_backend.cpp.obj' -or
            $_.Name -eq 'companion_authorization_protected_kv_media.cpp.obj' -or
            $_.Name -eq 'companion_authorization_nvs_context.cpp.obj'
        })
if (@($compiledAuthorizationBackendObjects | Select-Object -ExpandProperty Name -Unique).Count -ne 3) {
    throw 'Inactive protected-KV, ESP-IDF NVS backend, and existing-context owner objects were not all build-compiled.'
}
$artifactEvidence = @(
    foreach ($artifactPath in $artifactPaths) {
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Expected build artifact is missing: $artifactPath"
        }
        $item = Get-Item -LiteralPath $artifactPath
        $hash = Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256
        [ordered]@{
            name = $item.Name
            bytes = $item.Length
            sha256 = $hash.Hash
        }
    }
)

$evidence = [ordered]@{
    status = 'NOT-FLASHED'
    framework = $requiredVersion
    target = 'esp32s3'
    evidence_unit = 'OT-DEV-001'
    family_profile_part = 'ESP32-S3R2'
    observed_processor_family = 'esp32s3'
    observed_processor_revision = 'v0.2'
    exact_received_revision = 'UNRESOLVED'
    rf_variant = 'UNRESOLVED'
    observed_flash_size_bytes = 16777216
    observed_flash_io_capability = 'QUAD'
    configured_flash_mode = 'QIO'
    configured_flash_frequency_mhz = 80
    physical_flash_frequency_verified = $false
    observed_psram_size_bytes = 2097152
    configured_psram_mode = 'QUAD'
    configured_psram_frequency_mhz = 80
    physical_psram_interface_and_frequency_verified = $false
    partition_layout = 'OTHP0/v0'
    factory_slot_bytes = 5177344
    ota_slot_bytes = 5242880
    reserved_ot_state_bytes = 1048576
    partition_binary_verified = $partitionBinaryVerified
    image_header_flash_mode = 'DIO-bootstrap'
    image_header_flash_size = '16MB'
    image_header_flash_frequency = '80MHz'
    updater_authority = 'NOT-IMPLEMENTED'
    ota_authority = 'NOT-IMPLEMENTED'
    storage_authority = 'NOT-IMPLEMENTED'
    recovery_authority = 'NOT-GRANTED'
    console_primary = 'USB_SERIAL_JTAG'
    application_dynamic_value = 'boot-local elapsed_ms'
    companion_codec_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_request_coordinator_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_gatt_session_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_gatt_authorization_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_gatt_authorization_adapter_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_authorization_storage_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_ble_runtime_owner_self_check = 'BUILD-LINKED-NOT-RUN'
    companion_authorization_persistence = 'BUILD-LINKED-PROTECTED-BACKEND-NOT-INJECTED'
    companion_authorization_storage_preflight = 'DENIED-NVS-ENCRYPTION-NOT-CONFIGURED'
    companion_authorization_storage_read_only_probe = 'BUILD-LINKED-NOT-RUN-CURRENT-CONFIGURATION-SHORT-CIRCUITS-BEFORE-TARGET-READS'
    companion_authorization_nvs_backend = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'
    companion_authorization_nvs_context = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'
    companion_nimble_gatt = 'BUILD-LINKED-RUNTIME-PATH-NOT-RUN'
    companion_nimble_runtime = 'CODED-BUILD-LINKED-NOT-RUN'
    companion_command_dispatch = 'BUILD-LINKED-PREFLIGHT-DENIED-NOT-RUN'
    nimble_controller = 'CODED-BUILD-LINKED-NOT-RUN'
    advertising = 'CODED-PRIVATE-SERVICE-ONLY-NOT-RUN'
    application_authorization = 'NOT-INJECTED'
    oled_startup_display = 'CODED-BUILD-LINKED-NOT-RUN'
    oled_controller_candidate = 'SSD1315-COMPATIBLE-128X64-NOT-PHYSICALLY-VERIFIED'
    oled_logo_source_sha256 = (Get-FileHash -LiteralPath $logoPath -Algorithm SHA256).Hash
    oled_ble_phase_status = 'HOST-TESTED-BUILD-LINKED-NOT-RUN'
    protected_nvs = 'NOT-INITIALIZED-NOT-VERIFIED'
    private_bond_store = 'NOT-IMPLEMENTED'
    binding_prf_key = 'NOT-PROVISIONED-NOT-VERIFIED'
    rollback_floor = 'NOT-IMPLEMENTED'
    framework_log_surface = 'UNREVIEWED-RUNTIME'
    artifacts = $artifactEvidence
}
$evidencePath = Join-Path $buildRoot 'build-evidence.json'
$evidenceJson = $evidence | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
    $evidencePath,
    "$evidenceJson`n",
    [System.Text.UTF8Encoding]::new($false))

Write-Output "Build-only target passed with $requiredVersion."
Write-Output "Output: $buildRoot"
Write-Output "Evidence: $evidencePath"
Write-Output $evidenceJson
