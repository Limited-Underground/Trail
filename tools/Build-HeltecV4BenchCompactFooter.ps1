[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('ot106-a', 'ot106-b')]
    [string]$BuildProfile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredIdfVersion = 'ESP-IDF v6.0.2'
$requiredIdfCommit = '7101770dc6db2667b3c477cc31365dd1acd6db4e'
$requiredIdfToolSha256 = '5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40'
$requiredCompiler = 'xtensa-esp32s3-elf-gcc'
$requiredCompilerVersion = '15.2.0'
$requiredCompilerCrosstool = 'esp-15.2.0_20251204'
$requiredCompilerSha256 = '20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5'
$requiredCmakeVersion = 'cmake version 4.0.3'
$requiredCmakeSha256 = '392ab4d6c3c91543fd297ed6c7e7354bf62edcd26fdf2706ad8613ad620cc45e'
$requiredNinjaVersion = '1.12.1'
$requiredNinjaSha256 = '68865c3276d449d746cea5065fdec2baf755d7813e161ab04205b0907b2629b8'
$requiredPythonVersion = 'Python 3.14.6'
$requiredPythonSha256 = '199ce15a9f0d4f9522edba59338e4879d28cf61f88e377b8164bcb716275ed22'
$requiredFirmwareFileCount = 309
$requiredIndexManifestSha256 = 'c2b731d2e3ca8031afc3679759c8768f183e4c3665877ed342981df4ef7030d8'
$requiredWorkingTreeManifestSha256 = '95a888b6f8c47509af744426e94e429040b0ef29b1947371c71fc18006dec0a9'
$requiredProjectVersion = 'ot106-footer-v0'
$requiredReproducibleDefaultsSha256 = '995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6'

$projectRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench'
$buildTargetsRoot = Join-Path $projectRoot 'build\targets'
$buildRoot = Join-Path $buildTargetsRoot "heltec_v4_bench_$BuildProfile"
$pythonCacheRoot = Join-Path $buildTargetsRoot "ot106-python-cache-$BuildProfile"
$defaultsPath = Join-Path $targetRoot 'sdkconfig.defaults'
$partitionCsvPath = Join-Path $targetRoot 'partitions.csv'
$sdkconfigPath = Join-Path $buildRoot 'sdkconfig'
$reproducibleDefaultsPath = Join-Path $buildTargetsRoot "ot106-reproducible-$BuildProfile.defaults"
$environmentNames = @('CCACHE_DISABLE', 'PYTHONPYCACHEPREFIX', 'PYTHONNOUSERSITE')
$environmentSnapshot = @{}
$pythonCacheOwnedByRun = $false
$reproducibleDefaultsOwnedByRun = $false

function Get-LowerSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = $hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))
        return ([BitConverter]::ToString($digest)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hasher.Dispose()
    }
}

foreach ($name in $environmentNames) {
    $environmentSnapshot[$name] = [ordered]@{
        Exists = Test-Path -LiteralPath "Env:$name"
        Value = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
}

try {
    if (Test-Path -LiteralPath $buildRoot) {
        throw 'OT-106 requires an absent, independent build directory.'
    }
    if (Test-Path -LiteralPath $pythonCacheRoot) {
        throw 'OT-106 requires an absent, independent Python cache directory.'
    }
    if (Test-Path -LiteralPath $reproducibleDefaultsPath) {
        throw 'OT-106 requires an absent generated reproducible-defaults file.'
    }
    $pythonCacheOwnedByRun = $true
    $env:CCACHE_DISABLE = '1'
    $env:PYTHONPYCACHEPREFIX = $pythonCacheRoot
    $env:PYTHONNOUSERSITE = '1'

    if (-not $env:IDF_PATH) {
        throw 'IDF_PATH is not set. Export the installed ESP-IDF v6.0.2 environment first.'
    }
    $pythonTool = Get-Command python -ErrorAction Stop | Select-Object -First 1
    $cmakeTool = Get-Command cmake -ErrorAction Stop | Select-Object -First 1
    $ninjaTool = Get-Command ninja -ErrorAction Stop | Select-Object -First 1
    $compilerTool = Get-Command $requiredCompiler -ErrorAction Stop | Select-Object -First 1
    $gitTool = Get-Command git -ErrorAction Stop | Select-Object -First 1
    $idfTool = Join-Path $env:IDF_PATH 'tools\idf.py'
    if (-not (Test-Path -LiteralPath $idfTool -PathType Leaf)) {
        throw 'Pinned idf.py entrypoint is unavailable.'
    }

    $reportedIdfVersion = (& $pythonTool.Source $idfTool --version 2>&1 |
        ForEach-Object { $_.ToString().Trim() }) -join ' '
    if ($LASTEXITCODE -ne 0 -or $reportedIdfVersion -ne $requiredIdfVersion) {
        throw "Expected $requiredIdfVersion; received '$reportedIdfVersion'."
    }
    $reportedIdfCommit = (& $gitTool.Source -C $env:IDF_PATH rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $reportedIdfCommit -ne $requiredIdfCommit) {
        throw 'The ESP-IDF revision does not match the OT-106 lock.'
    }
    if (@(& $gitTool.Source -C $env:IDF_PATH status --porcelain --untracked-files=all).Count -ne 0) {
        throw 'The pinned ESP-IDF source is not clean.'
    }

    $pythonVersion = (& $pythonTool.Source --version 2>&1 | Select-Object -First 1).ToString().Trim()
    $cmakeVersion = (& $cmakeTool.Source --version 2>&1 | Select-Object -First 1).ToString().Trim()
    $ninjaVersion = (& $ninjaTool.Source --version 2>&1 | Select-Object -First 1).ToString().Trim()
    $compilerVersion = (& $compilerTool.Source --version 2>&1 | Select-Object -First 1).ToString().Trim()
    if ($pythonVersion -ne $requiredPythonVersion -or (Get-LowerSha256 $pythonTool.Source) -ne $requiredPythonSha256) {
        throw 'Python does not match the OT-106 lock.'
    }
    if ($cmakeVersion -ne $requiredCmakeVersion -or (Get-LowerSha256 $cmakeTool.Source) -ne $requiredCmakeSha256) {
        throw 'CMake does not match the OT-106 lock.'
    }
    if ($ninjaVersion -ne $requiredNinjaVersion -or (Get-LowerSha256 $ninjaTool.Source) -ne $requiredNinjaSha256) {
        throw 'Ninja does not match the OT-106 lock.'
    }
    if (-not $compilerVersion.Contains($requiredCompilerVersion) -or
        -not $compilerVersion.Contains($requiredCompilerCrosstool) -or
        (Get-LowerSha256 $compilerTool.Source) -ne $requiredCompilerSha256) {
        throw 'The compiler does not match the OT-106 lock.'
    }
    if ((Get-LowerSha256 $idfTool) -ne $requiredIdfToolSha256) {
        throw 'idf.py does not match the OT-106 lock.'
    }

    & $gitTool.Source -C $projectRoot diff --quiet -- firmware/components firmware/targets/heltec_v4_bench
    if ($LASTEXITCODE -ne 0) {
        throw 'The OT-106 firmware scope has unstaged changes.'
    }
    $untracked = @(& $gitTool.Source -C $projectRoot ls-files --others --exclude-standard -- firmware/components firmware/targets/heltec_v4_bench)
    if ($LASTEXITCODE -ne 0 -or $untracked.Count -ne 0) {
        throw 'The OT-106 firmware scope contains untracked inputs.'
    }
    $autocrlf = (& $gitTool.Source -C $projectRoot config --get core.autocrlf).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $autocrlf -ne 'true') {
        throw 'OT-106 requires the accepted core.autocrlf=true working-tree byte contract.'
    }
    $trackedEntries = @(& $gitTool.Source -C $projectRoot ls-files --stage -- firmware/components firmware/targets/heltec_v4_bench)
    $indexLines = @()
    $workingLines = @()
    foreach ($entry in $trackedEntries) {
        if ($entry -notmatch '^(\d{6}) ([0-9a-f]{40}) 0\t(.+)$') {
            throw 'The OT-106 firmware index contains a malformed or non-stage-zero entry.'
        }
        $mode = $Matches[1]
        $blob = $Matches[2]
        $relativePath = $Matches[3]
        $indexLines += "$mode $blob $relativePath"
        $workingPath = Join-Path $projectRoot $relativePath.Replace('/', '\')
        $workingLines += "$(Get-LowerSha256 $workingPath) $relativePath"
    }
    $indexManifestSha256 = Get-TextSha256 (($indexLines -join "`n") + "`n")
    $workingTreeManifestSha256 = Get-TextSha256 (($workingLines -join "`n") + "`n")
    if ($trackedEntries.Count -ne $requiredFirmwareFileCount -or
        $indexManifestSha256 -ne $requiredIndexManifestSha256 -or
        $workingTreeManifestSha256 -ne $requiredWorkingTreeManifestSha256) {
        throw 'The staged OT-106 firmware input lock does not match.'
    }

    $reproducibleDefaults = "CONFIG_APP_REPRODUCIBLE_BUILD=y`nCONFIG_APP_COMPILE_TIME_DATE=n`n"
    [IO.File]::WriteAllText($reproducibleDefaultsPath, $reproducibleDefaults, [Text.UTF8Encoding]::new($false))
    $reproducibleDefaultsOwnedByRun = $true
    if ((Get-LowerSha256 $reproducibleDefaultsPath) -ne $requiredReproducibleDefaultsSha256) {
        throw 'The generated reproducible defaults do not match.'
    }
    $effectiveDefaults = "$defaultsPath;$reproducibleDefaultsPath"
    $projectVersionArgs = @('-D', "PROJECT_VER=$requiredProjectVersion")

    & $pythonTool.Source $idfTool -C $targetRoot -B $buildRoot `
        -D "SDKCONFIG=$sdkconfigPath" -D "SDKCONFIG_DEFAULTS=$effectiveDefaults" `
        @projectVersionArgs set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        throw 'ESP32-S3 target selection failed.'
    }
    $buildOutput = @(& $pythonTool.Source $idfTool -C $targetRoot -B $buildRoot `
        -D "SDKCONFIG=$sdkconfigPath" -D "SDKCONFIG_DEFAULTS=$effectiveDefaults" `
        @projectVersionArgs build 2>&1)
    $buildExitCode = $LASTEXITCODE
    $buildOutput | ForEach-Object { Write-Output $_ }
    if ($buildExitCode -ne 0) {
        throw "OT-106 build failed with exit code $buildExitCode."
    }
    $warningCount = @($buildOutput | Where-Object { $_.ToString() -match '(?i)\bwarning:' }).Count
    if ($warningCount -ne 0) {
        throw "OT-106 requires a warning-free build; observed $warningCount warning lines."
    }

    $generatedSdkconfig = @(Get-Content -LiteralPath $sdkconfigPath)
    foreach ($selection in @(
        'CONFIG_IDF_TARGET="esp32s3"',
        'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y',
        'CONFIG_APP_REPRODUCIBLE_BUILD=y',
        'CONFIG_ESPTOOLPY_FLASHMODE_QIO=y',
        'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
        'CONFIG_SPIRAM=y',
        'CONFIG_BT_NIMBLE_ENABLED=y',
        'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1'
    )) {
        if ($generatedSdkconfig -notcontains $selection) {
            throw "Generated sdkconfig is missing the OT-106 selection: $selection"
        }
    }
    $projectDescription = Get-Content -LiteralPath (Join-Path $buildRoot 'project_description.json') -Raw | ConvertFrom-Json
    if ($projectDescription.project_version -ne $requiredProjectVersion) {
        throw 'The generated project version does not match OT-106.'
    }

    $applicationPath = Join-Path $buildRoot 'opentrail_heltec_v4_bench.bin'
    $partitionPath = Join-Path $buildRoot 'partition_table\partition-table.bin'
    $linkMapPath = Join-Path $buildRoot 'opentrail_heltec_v4_bench.map'
    $applicationBytes = [IO.File]::ReadAllBytes($applicationPath)
    if ($applicationBytes.Length -lt 4 -or $applicationBytes[0] -ne 0xE9 -or
        $applicationBytes[2] -ne 0x02 -or $applicationBytes[3] -ne 0x4F) {
        throw 'The OT-106 application image header is not the locked 16MB/80MHz DIO bootstrap form.'
    }
    if (-not [Text.Encoding]::ASCII.GetString($applicationBytes).Contains($requiredProjectVersion)) {
        throw 'The application image does not embed the stable OT-106 project version.'
    }
    $linkMap = Get-Content -LiteralPath $linkMapPath -Raw
    foreach ($objectName in @('compact_status_footer.cpp.obj', 'heltec_startup_display.cpp.obj', 'heltec_v4_oled.cpp.obj')) {
        if (-not $linkMap.Contains($objectName)) {
            throw "The OT-106 linked object is absent: $objectName"
        }
    }

    $partitionTool = Join-Path $env:IDF_PATH 'components\partition_table\gen_esp32part.py'
    $decodedPartitions = @(& $pythonTool.Source $partitionTool `
        --flash-size 16MB --quiet $partitionPath 2>&1 |
        ForEach-Object { $_.ToString().Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') })
    $expectedPartitions = @(
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
        throw 'The generated OT-106 partition binary contains an unrecognized number.'
    }
    if ($LASTEXITCODE -ne 0 -or $decodedPartitions.Count -ne $expectedPartitions.Count) {
        throw 'The generated OT-106 partition binary could not be verified.'
    }
    for ($index = 0; $index -lt $expectedPartitions.Count; $index++) {
        $fields = @($decodedPartitions[$index].Split(',') | ForEach-Object { $_.Trim() })
        $expected = $expectedPartitions[$index]
        if ($fields.Count -lt 5 -or
            $fields[0] -ne $expected.Name -or
            $fields[1] -ne $expected.Type -or
            $fields[2] -ne $expected.SubType -or
            (Convert-PartitionValue $fields[3]) -ne $expected.Offset -or
            (Convert-PartitionValue $fields[4]) -ne $expected.Size) {
            throw "The generated OT-106 partition binary differs at row $index."
        }
    }
    $artifacts = @(
        @{ role = 'application'; path = $applicationPath },
        @{ role = 'application_elf'; path = (Join-Path $buildRoot 'opentrail_heltec_v4_bench.elf') },
        @{ role = 'linker_map'; path = $linkMapPath },
        @{ role = 'bootloader'; path = (Join-Path $buildRoot 'bootloader\bootloader.bin') },
        @{ role = 'partition_table'; path = $partitionPath },
        @{ role = 'generated_sdkconfig'; path = $sdkconfigPath },
        @{ role = 'partition_csv'; path = $partitionCsvPath }
    )
    $publicArtifacts = @(
        foreach ($artifact in $artifacts) {
            $item = Get-Item -LiteralPath $artifact.path
            [ordered]@{
                role = $artifact.role
                name = $item.Name
                bytes = $item.Length
                sha256 = Get-LowerSha256 $item.FullName
            }
        }
    )
    $smallestAppSlotBytes = 5177344
    $applicationImageBytes = (Get-Item -LiteralPath $applicationPath).Length
    $headroomBytes = $smallestAppSlotBytes - $applicationImageBytes
    if ($headroomBytes -le 0) {
        throw 'The OT-106 application does not fit the smallest configured application slot.'
    }

    $receipt = [ordered]@{
        schema = 'OTFBL0'
        version = 0
        record_id = 'OT-106-HELTEC-V4-COMPACT-FOOTER-BUILD-V0'
        profile = $BuildProfile
        status = 'BUILD-RUN-CAPTURED'
        public_result = 'COMPUTER-BUILD-ONLY; COMPACT-FOOTER-BUILD-LINKED; DEVICE-AND-RADIO-NOT-RUN'
        source = [ordered]@{
            firmware_input_manifest_kind = 'git-index-stage-zero-v1'
            firmware_input_manifest_file_count = $trackedEntries.Count
            firmware_input_manifest_sha256 = $indexManifestSha256
            working_tree_manifest_kind = 'sha256-raw-bytes-path-v1'
            working_tree_manifest_sha256 = $workingTreeManifestSha256
            git_core_autocrlf = $autocrlf
        }
        toolchain = [ordered]@{
            framework = $requiredIdfVersion
            framework_commit = $requiredIdfCommit
            framework_clean = $true
            idf_py_sha256 = Get-LowerSha256 $idfTool
            compiler = $requiredCompiler
            compiler_version = $requiredCompilerVersion
            compiler_crosstool = $requiredCompilerCrosstool
            compiler_sha256 = Get-LowerSha256 $compilerTool.Source
            cmake_version = $cmakeVersion
            cmake_sha256 = Get-LowerSha256 $cmakeTool.Source
            ninja_version = $ninjaVersion
            ninja_sha256 = Get-LowerSha256 $ninjaTool.Source
            python_version = $pythonVersion
            python_sha256 = Get-LowerSha256 $pythonTool.Source
        }
        isolation = [ordered]@{
            clean_independent_build_directory = $true
            independent_python_cache = $true
            python_user_site_disabled = $true
            shared_compiler_cache_disabled = $true
            reproducible_build_paths_normalized = $true
        }
        inputs = [ordered]@{
            build_helper_sha256 = Get-LowerSha256 $PSCommandPath
            project_version = $requiredProjectVersion
            reproducible_defaults_sha256 = Get-LowerSha256 $reproducibleDefaultsPath
            generated_sdkconfig_sha256 = Get-LowerSha256 $sdkconfigPath
        }
        build = [ordered]@{
            exit_code = $buildExitCode
            compiler_warning_count = $warningCount
            partition_layout = 'OTHP0/v0-verified-generated-binary'
            public_artifacts = $publicArtifacts
            application_image_bytes = $applicationImageBytes
            smallest_app_slot_bytes = $smallestAppSlotBytes
            headroom_bytes = $headroomBytes
        }
        footer = [ordered]@{
            component_object_linked = $true
            target_display_owner_linked = $true
            battery_live_source_bound = $false
            gps_live_source_bound = $false
            radio_activity_source_bound = $false
        }
        authority = [ordered]@{
            hardware_or_device_accessed = $false
            flash_or_erase_performed = $false
            radio_or_ble_executed = $false
            key_or_entropy_operation = $false
            target_support_claimed = $false
            physical_display_claimed = $false
            live_telemetry_claimed = $false
            candidate_readiness_claimed = $false
            final_configuration_selected = $false
            crypto_candidate_imported = $false
            crypto_benchmark_executed = $false
            crypto_suite_selected = $false
            score_credit_added = $false
        }
    }
    $receiptPath = Join-Path $buildRoot 'ot106-build-receipt.json'
    $receiptJson = $receipt | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText($receiptPath, "$receiptJson`n", [Text.UTF8Encoding]::new($false))
    Write-Output "OT-106 build-only profile passed: $BuildProfile"
    Write-Output "Receipt: $receiptPath"
} finally {
    try {
        if ($reproducibleDefaultsOwnedByRun -and (Test-Path -LiteralPath $reproducibleDefaultsPath -PathType Leaf)) {
            $expectedDefaults = [IO.Path]::GetFullPath($reproducibleDefaultsPath)
            $expectedDefaultsPrefix = [IO.Path]::GetFullPath((Join-Path $buildTargetsRoot 'ot106-reproducible-'))
            $defaultsItem = Get-Item -LiteralPath $reproducibleDefaultsPath -Force
            if ($defaultsItem.FullName -ne $expectedDefaults -or
                -not $defaultsItem.FullName.StartsWith($expectedDefaultsPrefix, [StringComparison]::OrdinalIgnoreCase) -or
                ($defaultsItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'Refusing unsafe OT-106 reproducible-defaults cleanup.'
            }
            Remove-Item -LiteralPath $defaultsItem.FullName -Force
        }
        if ($pythonCacheOwnedByRun -and (Test-Path -LiteralPath $pythonCacheRoot -PathType Container)) {
            $cacheItem = Get-Item -LiteralPath $pythonCacheRoot -Force
            $expected = [IO.Path]::GetFullPath($pythonCacheRoot)
            $expectedPrefix = [IO.Path]::GetFullPath((Join-Path $buildTargetsRoot 'ot106-python-cache-'))
            $reparsePoints = @(Get-ChildItem -LiteralPath $cacheItem.FullName -Recurse -Force |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
            if ($cacheItem.FullName -ne $expected -or
                -not $cacheItem.FullName.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
                ($cacheItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                $reparsePoints.Count -ne 0) {
                throw 'Refusing unsafe OT-106 Python cache cleanup.'
            }
            Remove-Item -LiteralPath $cacheItem.FullName -Recurse -Force
        }
    } finally {
        foreach ($name in $environmentNames) {
            $snapshot = $environmentSnapshot[$name]
            if ($snapshot.Exists) {
                Set-Item -LiteralPath "Env:$name" -Value $snapshot.Value
            } else {
                Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
            }
        }
    }
}
