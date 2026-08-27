[CmdletBinding()]
param(
    [switch] $Execute,
    [string] $OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$candidateProject = Join-Path $projectRoot 'tests\benchmarks\crypto\esp_idf\ot149_mbedtls_psa\candidate'
$controlProject = Join-Path $projectRoot 'tests\benchmarks\crypto\esp_idf\ot149_mbedtls_psa\control'
$contractRelative = 'tests/benchmarks/crypto/OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json'
$contractPath = Join-Path $projectRoot ($contractRelative -replace '/', '\')
$candidateReportRelative = 'tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json'
$controlReportRelative = 'tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json'
$resultRelative = 'tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json'
$candidateReportPath = Join-Path $projectRoot ($candidateReportRelative -replace '/', '\')
$controlReportPath = Join-Path $projectRoot ($controlReportRelative -replace '/', '\')
$resultPath = Join-Path $projectRoot ($resultRelative -replace '/', '\')
$resourceValidator = Join-Path $projectRoot 'tools\crypto_matched_resource_accounting.py'
$application = 'ot149_mbedtls_psa_bench'
$requiredProjectVersion = 'ot150-mbedtls-psa-v0'
$recordedDate = '2026-08-27'
$expectedIdfCommit = '7101770dc6db2667b3c477cc31365dd1acd6db4e'
$expectedSdkconfigBytes = 106890
$expectedSdkconfigSha256 = '00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e'
$expectedBootloaderBytes = 15216
$expectedBootloaderSha256 = '604af9d70953d917734f45b4c1cb764a23c17c8e3e5b28e11e1f3f6a02ef1c38'
$expectedPartitionTableBytes = 3072
$expectedPartitionTableSha256 = '84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab'
$expectedPartitionCsvBytes = 443
$expectedPartitionCsvSha256 = '973ce7d2d3559a792d62eacd859db1b52b7569080cb85c3f2fedeed4db6cc621'
$expectedSizeModuleSha256 = 'ba38639de2a4d1f4fa48657e94cd3f250a3744ea8595e81fc74e9da0431a15c9'
$contractRawSha256 = '575efa5700bf2d2e40cf3fb49c0b9f860815dddc3ea81bb5a54a3d91733339bd'
$contractCanonicalSha256 = '1c44d3d6f0c0d7c38ce83c51f1b79c75130455f2bbf99fed7edb4ffa1b9efaf2'
$onlyPermittedDifference = 'candidate_and_adapter_linkage_replaced_by_reviewed_no_candidate_control_bindings'

if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'opentrail-ot150-mbedtls-psa'
}

function Assert-ExactFile([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is absent."
    }
}

function Get-LowerSha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesSha256([byte[]] $Bytes) {
    return [System.Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($Bytes)
    ).ToLowerInvariant()
}

function Get-TextSha256([string] $Text) {
    return Get-BytesSha256 ([System.Text.Encoding]::UTF8.GetBytes($Text))
}

function ConvertTo-LfFinalBytes([string] $Text) {
    $normalized = $Text.Replace("`r`n", "`n").Replace("`r", "`n")
    while ($normalized.EndsWith("`n", [System.StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(0, $normalized.Length - 1)
    }
    return [System.Text.UTF8Encoding]::new($false).GetBytes($normalized + "`n")
}

function Get-FileArtifact([string] $Path) {
    Assert-ExactFile $Path 'build artifact'
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        bytes = [int64] $item.Length
        sha256 = Get-LowerSha256 $Path
    }
}

function Get-NamedFileArtifact([string] $Role, [string] $Path) {
    $artifact = Get-FileArtifact $Path
    return [ordered]@{
        role = $Role
        name = [System.IO.Path]::GetFileName($Path)
        bytes = $artifact.bytes
        sha256 = $artifact.sha256
    }
}

function Assert-Artifact(
    [string] $Path,
    [int64] $ExpectedBytes,
    [string] $ExpectedSha256,
    [string] $Label
) {
    $artifact = Get-FileArtifact $Path
    if ($artifact.bytes -ne $ExpectedBytes -or $artifact.sha256 -ne $ExpectedSha256) {
        throw "$Label identity mismatch."
    }
}

function Get-ManifestSha256([string[]] $RelativePaths) {
    $lines = @()
    foreach ($relative in $RelativePaths) {
        $path = Join-Path $projectRoot ($relative -replace '/', '\')
        Assert-ExactFile $path "manifest input $relative"
        $item = Get-Item -LiteralPath $path
        $lines += "$relative|$($item.Length)|$(Get-LowerSha256 $path)"
    }
    return Get-TextSha256 (($lines -join "`n") + "`n")
}

function Get-CanonicalObjectSha256([object] $Value) {
    $json = $Value | ConvertTo-Json -Depth 30 -Compress
    return Get-TextSha256 $json
}

function Write-JsonNoBom([string] $Path, [object] $Value) {
    $json = $Value | ConvertTo-Json -Depth 30
    [System.IO.File]::WriteAllBytes($Path, (ConvertTo-LfFinalBytes $json))
}

function Get-ReportMetrics([string] $Path) {
    $report = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    if ($report.version -ne '1.2') {
        throw 'ESP-IDF size JSON2 version mismatch.'
    }
    $diram = @($report.layout | Where-Object { $_.name -eq 'DIRAM' })
    if ($diram.Count -ne 1) {
        throw 'ESP-IDF size JSON2 requires exactly one DIRAM region.'
    }
    $staticRam = [int64] 0
    foreach ($name in @('.data', '.bss', '.noinit')) {
        $property = $diram[0].parts.PSObject.Properties[$name]
        if ($null -ne $property) {
            $staticRam += [int64] $property.Value.size
        }
    }
    $seenTls = @{}
    foreach ($region in @($report.layout)) {
        foreach ($name in @('.tdata', '.tbss')) {
            $property = $region.parts.PSObject.Properties[$name]
            if ($null -ne $property) {
                if ($seenTls.ContainsKey($name)) {
                    throw 'ESP-IDF size JSON2 contains duplicate TLS parts.'
                }
                $seenTls[$name] = $true
                $staticRam += [int64] $property.Value.size
            }
        }
    }
    return [ordered]@{
        linked_flash_bytes = [int64] $report.total_size
        static_ram_bytes = $staticRam
    }
}

function Get-NormalizedRunReceipt([object] $Artifacts) {
    return Get-CanonicalObjectSha256 ([ordered]@{
        schema = 'OT150NR0'
        project_version = $requiredProjectVersion
        artifacts = $Artifacts
    })
}

function Assert-ExactToolOutput(
    [string] $Command,
    [string[]] $Arguments,
    [string] $Pattern,
    [string] $Label
) {
    $output = @(& $Command @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0 -or (($output -join "`n") -notmatch $Pattern)) {
        throw "$Label version mismatch."
    }
}

function Invoke-BuildRun(
    [string] $Side,
    [string] $RunLabel,
    [string] $SourceProject,
    [string] $PartitionCsv,
    [string] $IdfTool,
    [string] $PythonPath
) {
    $runRoot = Join-Path $OutputRoot "$Side-$($RunLabel.ToLowerInvariant())"
    $buildRoot = Join-Path $runRoot 'build'
    $rawLog = Join-Path $runRoot 'build.log'
    $sizeReport = Join-Path $runRoot 'size-report.json'
    if (Test-Path -LiteralPath $runRoot) {
        throw "$Side run $RunLabel output already exists."
    }
    New-Item -ItemType Directory -Path $runRoot | Out-Null

    $sdkconfig = Join-Path $buildRoot 'sdkconfig'
    $arguments = @(
        $IdfTool,
        '--no-ccache',
        '-C', $SourceProject,
        '-B', $buildRoot,
        '-D', "SDKCONFIG=$sdkconfig",
        '-D', "PROJECT_VER=$requiredProjectVersion",
        '-D', 'IDF_TARGET=esp32s3',
        'build'
    )
    $buildOutput = @(& $PythonPath @arguments 2>&1)
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        [System.IO.File]::WriteAllLines(
            $rawLog,
            [string[]] $buildOutput,
            [System.Text.UTF8Encoding]::new($false)
        )
        throw "$Side run $RunLabel build failed."
    }
    $compilerWarnings = @($buildOutput | Where-Object {
        $_ -match '(?i)\.(?:c|cc|cpp|cxx|s):[0-9]+(?::[0-9]+)?:\s+warning:'
    }).Count
    if ($compilerWarnings -ne 0) {
        throw "$Side run $RunLabel emitted compiler warnings."
    }

    $applicationBin = Join-Path $buildRoot "$application.bin"
    $applicationElf = Join-Path $buildRoot "$application.elf"
    $linkerMap = Join-Path $buildRoot "$application.map"
    $bootloader = Join-Path $buildRoot 'bootloader\bootloader.bin'
    $partitionTable = Join-Path $buildRoot 'partition_table\partition-table.bin'
    $projectDescription = Join-Path $buildRoot 'project_description.json'
    foreach ($path in @(
        $applicationBin, $applicationElf, $linkerMap, $bootloader,
        $partitionTable, $sdkconfig, $projectDescription
    )) {
        Assert-ExactFile $path "$Side run $RunLabel output"
    }

    $description = Get-Content -Raw -LiteralPath $projectDescription | ConvertFrom-Json
    if (
        $description.project_name -ne $application -or
        $description.project_version -ne $requiredProjectVersion -or
        $description.target -ne 'esp32s3'
    ) {
        throw "$Side run $RunLabel project description mismatch."
    }
    $binaryText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($applicationBin)
    )
    if (-not $binaryText.Contains($requiredProjectVersion)) {
        throw "$Side run $RunLabel application descriptor lacks the fixed project version."
    }

    Assert-Artifact $sdkconfig $expectedSdkconfigBytes $expectedSdkconfigSha256 'generated sdkconfig'
    Assert-Artifact $bootloader $expectedBootloaderBytes $expectedBootloaderSha256 'bootloader'
    Assert-Artifact $partitionTable $expectedPartitionTableBytes $expectedPartitionTableSha256 'partition table'
    Assert-Artifact $PartitionCsv $expectedPartitionCsvBytes $expectedPartitionCsvSha256 'partition CSV'

    $sizeOutput = @(
        & $PythonPath -m esp_idf_size --format json2 --output-file $sizeReport $linkerMap 2>&1
    )
    $sizeExitCode = $LASTEXITCODE
    $allOutput = @($buildOutput) + @('OT-150 ESP-IDF size JSON2:') + @($sizeOutput)
    [System.IO.File]::WriteAllLines(
        $rawLog,
        [string[]] $allOutput,
        [System.Text.UTF8Encoding]::new($false)
    )
    if ($sizeExitCode -ne 0) {
        throw "$Side run $RunLabel size report failed."
    }
    Assert-ExactFile $sizeReport "$Side run $RunLabel size report"
    $sizeItem = Get-Item -LiteralPath $sizeReport
    if ($sizeItem.Length -le 0 -or $sizeItem.Length -gt 65536) {
        throw "$Side run $RunLabel size report exceeds the contract bound."
    }
    [void] (Get-ReportMetrics $sizeReport)

    $resourceArtifacts = [ordered]@{
        application_bin = Get-FileArtifact $applicationBin
        application_elf = Get-FileArtifact $applicationElf
        linker_map = Get-FileArtifact $linkerMap
        generated_sdkconfig = Get-FileArtifact $sdkconfig
        partition_csv = Get-FileArtifact $PartitionCsv
        size_report = Get-FileArtifact $sizeReport
    }
    $bundleArtifacts = [ordered]@{
        application_bin = $applicationBin
        application_elf = $applicationElf
        linker_map = $linkerMap
        bootloader_bin = $bootloader
        partition_table_bin = $partitionTable
        generated_sdkconfig = $sdkconfig
        size_report_json2 = $sizeReport
    }
    return [ordered]@{
        run = $RunLabel
        initial_build_directory_absent = $true
        compiler_warnings = 0
        raw_build_log_sha256 = Get-LowerSha256 $rawLog
        normalized_receipt_sha256 = Get-NormalizedRunReceipt $resourceArtifacts
        artifacts = $resourceArtifacts
        bundle_artifact_paths = $bundleArtifacts
        private_raw_build_log = $rawLog
    }
}

if (-not $Execute) {
    throw 'OT-150 builder is inert unless -Execute is supplied.'
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw 'OutputRoot must be initially absent.'
}
foreach ($publicPath in @($candidateReportPath, $controlReportPath, $resultPath)) {
    if (Test-Path -LiteralPath $publicPath) {
        throw 'OT-150 public output already exists.'
    }
}
foreach ($path in @(
    $candidateProject, $controlProject
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw 'OT-150 source project is absent.'
    }
}
foreach ($path in @($contractPath, $resourceValidator)) {
    Assert-ExactFile $path 'OT-150 fixed input'
}
if ((Get-LowerSha256 $contractPath) -ne $contractRawSha256) {
    throw 'OT-150 resource contract raw digest mismatch.'
}
if (-not $env:IDF_PATH) {
    throw 'IDF_PATH is absent; activate the pinned ESP-IDF environment first.'
}

$idfRoot = (Resolve-Path -LiteralPath $env:IDF_PATH).Path
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

Assert-ExactToolOutput $python.Source @('--version') '^Python 3\.14\.6$' 'Python'
$cmake = Get-Command cmake -ErrorAction Stop
Assert-ExactToolOutput $cmake.Source @('--version') 'cmake version 4\.0\.3' 'CMake'
$ninja = Get-Command ninja -ErrorAction Stop
Assert-ExactToolOutput $ninja.Source @('--version') '^1\.12\.1$' 'Ninja'
$compiler = Get-Command xtensa-esp-elf-gcc -ErrorAction Stop
Assert-ExactToolOutput $compiler.Source @('--version') '\b15\.2\.0\b' 'ESP32-S3 compiler'
$sizeModuleOutput = @(& $python.Source -c 'import pathlib, esp_idf_size; print(pathlib.Path(esp_idf_size.__file__).resolve())' 2>&1)
if ($LASTEXITCODE -ne 0 -or $sizeModuleOutput.Count -ne 1) {
    throw 'ESP-IDF size module lookup failed.'
}
$sizeModulePath = $sizeModuleOutput[0].Trim()
Assert-ExactFile $sizeModulePath 'ESP-IDF size module'
if ((Get-LowerSha256 $sizeModulePath) -ne $expectedSizeModuleSha256) {
    throw 'ESP-IDF size module digest mismatch.'
}

$candidatePartitionCsv = Join-Path $candidateProject 'partitions.csv'
$controlPartitionCsv = Join-Path $controlProject 'partitions.csv'
Assert-Artifact $candidatePartitionCsv $expectedPartitionCsvBytes $expectedPartitionCsvSha256 'candidate partition CSV'
Assert-Artifact $controlPartitionCsv $expectedPartitionCsvBytes $expectedPartitionCsvSha256 'control partition CSV'

$harnessSha256 = Get-ManifestSha256 @(
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/app_main.c',
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/ot149_candidate_api.h',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h'
)
$instrumentationSha256 = Get-ManifestSha256 @(
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h',
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/app_main.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c',
    'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h'
)
$candidateLinkageSha256 = Get-ManifestSha256 @(
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/main/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/main/mbedtls_psa_benchmark_api.c'
)
$controlLinkageSha256 = Get-ManifestSha256 @(
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/main/CMakeLists.txt',
    'tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/main/no_candidate_benchmark_api.c'
)
if ($candidateLinkageSha256 -eq $controlLinkageSha256) {
    throw 'Candidate/control linkage manifests are not distinct.'
}
$compileFlagsSha256 = Get-CanonicalObjectSha256 ([ordered]@{
    idf_target = 'esp32s3'
    project_version = $requiredProjectVersion
    no_ccache = $true
    idf_component_manager = '0'
    python_no_user_site = '1'
    generated_sdkconfig_sha256 = $expectedSdkconfigSha256
})
$matchedMetadata = [ordered]@{
    benchmark_harness_source = $harnessSha256
    frame_and_runtime_instrumentation = $instrumentationSha256
    generated_sdkconfig = $expectedSdkconfigSha256
    partition_layout = $expectedPartitionCsvSha256
    idf_target = 'esp32s3'
    esp_idf_commit = $expectedIdfCommit
    compiler = 'xtensa-esp32s3-elf-gcc 15.2.0'
    cmake = '4.0.3'
    ninja = '1.12.1'
    python = '3.14.6'
    esp_idf_size = '2.3.1'
    compile_flags = $compileFlagsSha256
    project_version = $requiredProjectVersion
    cache_policy = 'disabled'
    component_manager_network_policy = 'disabled'
}

$oldComponentManager = $env:IDF_COMPONENT_MANAGER
$oldPythonNoUserSite = $env:PYTHONNOUSERSITE
$env:IDF_COMPONENT_MANAGER = '0'
$env:PYTHONNOUSERSITE = '1'
try {
    New-Item -ItemType Directory -Path $OutputRoot | Out-Null
    $candidateRuns = @(
        Invoke-BuildRun 'candidate' 'A' $candidateProject $candidatePartitionCsv $idfTool $python.Source
        Invoke-BuildRun 'candidate' 'B' $candidateProject $candidatePartitionCsv $idfTool $python.Source
    )
    $controlRuns = @(
        Invoke-BuildRun 'control' 'A' $controlProject $controlPartitionCsv $idfTool $python.Source
        Invoke-BuildRun 'control' 'B' $controlProject $controlPartitionCsv $idfTool $python.Source
    )
}
finally {
    $env:IDF_COMPONENT_MANAGER = $oldComponentManager
    $env:PYTHONNOUSERSITE = $oldPythonNoUserSite
}

foreach ($runs in @($candidateRuns, $controlRuns)) {
    if (
        $runs[0].normalized_receipt_sha256 -ne $runs[1].normalized_receipt_sha256 -or
        (Get-CanonicalObjectSha256 $runs[0].artifacts) -ne
            (Get-CanonicalObjectSha256 $runs[1].artifacts)
    ) {
        throw 'OT-150 A/B normalized artifact equality failed.'
    }
}
$allLogHashes = @($candidateRuns + $controlRuns | ForEach-Object { $_.raw_build_log_sha256 })
if (@($allLogHashes | Select-Object -Unique).Count -ne 4) {
    throw 'OT-150 requires four distinct private raw build logs.'
}
foreach ($role in @('generated_sdkconfig')) {
    if ($candidateRuns[0].artifacts[$role].sha256 -ne $controlRuns[0].artifacts[$role].sha256) {
        throw 'OT-150 candidate/control shared metadata differs.'
    }
}

$candidatePrivateReport = $candidateRuns[0].bundle_artifact_paths.size_report_json2
$controlPrivateReport = $controlRuns[0].bundle_artifact_paths.size_report_json2
[System.IO.File]::Copy($candidatePrivateReport, $candidateReportPath, $false)
[System.IO.File]::Copy($controlPrivateReport, $controlReportPath, $false)
$candidateReportBinding = [ordered]@{
    path = $candidateReportRelative
    bytes = [int64] (Get-Item -LiteralPath $candidateReportPath).Length
    raw_sha256 = Get-LowerSha256 $candidateReportPath
}
$controlReportBinding = [ordered]@{
    path = $controlReportRelative
    bytes = [int64] (Get-Item -LiteralPath $controlReportPath).Length
    raw_sha256 = Get-LowerSha256 $controlReportPath
}
$candidateMetrics = Get-ReportMetrics $candidateReportPath
$controlMetrics = Get-ReportMetrics $controlReportPath
$measurements = [ordered]@{
    linked_flash_candidate_bytes = $candidateMetrics.linked_flash_bytes
    linked_flash_control_bytes = $controlMetrics.linked_flash_bytes
    linked_flash_delta_bytes = [int64] ($candidateMetrics.linked_flash_bytes - $controlMetrics.linked_flash_bytes)
    static_ram_candidate_bytes = $candidateMetrics.static_ram_bytes
    static_ram_control_bytes = $controlMetrics.static_ram_bytes
    static_ram_delta_bytes = [int64] ($candidateMetrics.static_ram_bytes - $controlMetrics.static_ram_bytes)
}

function Get-PublicRun([object] $Run) {
    return [ordered]@{
        run = $Run.run
        initial_build_directory_absent = $Run.initial_build_directory_absent
        compiler_warnings = $Run.compiler_warnings
        raw_build_log_sha256 = $Run.raw_build_log_sha256
        normalized_receipt_sha256 = $Run.normalized_receipt_sha256
        artifacts = $Run.artifacts
    }
}

$result = [ordered]@{
    schema = 'OTMRAR1'
    version = 1
    artifact_kind = 'matched_candidate_resource_accounting_result'
    result_id = 'OT-150-OT005-MATCHED-RESOURCE-RESULT-V1'
    recorded_date = $recordedDate
    status = 'matched_resource_result_admitted'
    contract = [ordered]@{
        path = $contractRelative
        raw_sha256 = $contractRawSha256
        canonical_sha256 = $contractCanonicalSha256
    }
    candidate_id = 'esp_idf_mbedtls_psa'
    candidate_role = 'comparison'
    selection_eligible = $false
    builds = [ordered]@{
        candidate = [ordered]@{
            matched_metadata = $matchedMetadata
            linkage_manifest_sha256 = $candidateLinkageSha256
            runs = @(
                Get-PublicRun $candidateRuns[0]
                Get-PublicRun $candidateRuns[1]
            )
        }
        control = [ordered]@{
            matched_metadata = $matchedMetadata
            linkage_manifest_sha256 = $controlLinkageSha256
            runs = @(
                Get-PublicRun $controlRuns[0]
                Get-PublicRun $controlRuns[1]
            )
        }
        only_permitted_difference = $onlyPermittedDifference
    }
    reports = [ordered]@{
        candidate = $candidateReportBinding
        control = $controlReportBinding
    }
    measurements = $measurements
    phase_two_projection = [ordered]@{
        linked_flash_delta_bytes = $measurements.linked_flash_delta_bytes
        static_ram_bytes = $measurements.static_ram_candidate_bytes
    }
    claims = [ordered]@{
        resource_delta_admitted = $true
        benchmark_executed = $false
        hardware_or_device_accessed = $false
        candidate_selected = $false
        phase_two_complete = $false
        score_credit_added = $false
    }
}

$privateResultPath = Join-Path $OutputRoot 'matched-resource-result.json'
Write-JsonNoBom $privateResultPath $result
$validation = @(& $python.Source $resourceValidator evaluate $contractPath $privateResultPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "OT-150 matched resource result failed validation: $($validation -join ' ')"
}
[System.IO.File]::Copy($privateResultPath, $resultPath, $false)

$summary = [ordered]@{
    status = 'ot150_host_bundle_build_complete'
    project_version = $requiredProjectVersion
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    candidate_report = $candidateReportRelative
    control_report = $controlReportRelative
    result = $resultRelative
    validation = ($validation -join '') | ConvertFrom-Json
    candidate_bundle_paths = $candidateRuns[0].bundle_artifact_paths
    control_bundle_paths = $controlRuns[0].bundle_artifact_paths
    hardware_accessed = $false
    execution_authority_created = $false
}
$summary | ConvertTo-Json -Depth 20
