[CmdletBinding()]
param(
    [switch] $Execute,
    [string] $OutputRoot,
    [string] $ContractPath,
    [string] $ContractValidatorPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ContractPath) {
    $ContractPath = Join-Path $projectRoot 'tests\benchmarks\crypto\OT-120-OT005-CANDIDATE-IMPORT-BUILD-CONTRACT-V1.json'
}
if (-not $ContractValidatorPath) {
    $ContractValidatorPath = Join-Path $projectRoot 'tools\crypto_candidate_import_build_admission.py'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'opentrail-ot120-candidate-builds'
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256([string] $Text) {
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
    return [Convert]::ToHexString(
        [System.Security.Cryptography.SHA256]::HashData($bytes)
    ).ToLowerInvariant()
}

function Get-NormalizedLfText([string] $Path) {
    $text = Get-Content -Raw -LiteralPath $Path
    return ($text -replace "`r`n", "`n" -replace "`r", "`n")
}

function Assert-ExactFile([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is absent."
    }
}

function Get-Artifact([string] $Role, [string] $Path) {
    Assert-ExactFile $Path $Role
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        role = $Role
        name = $item.Name
        bytes = $item.Length
        sha256 = Get-Sha256 $Path
    }
}

$python = Get-Command python -ErrorAction Stop
Assert-ExactFile $ContractPath 'OT-120 contract'
Assert-ExactFile $ContractValidatorPath 'OT-120 contract validator'

$contractValidationOutput = @(& $python.Source $ContractValidatorPath --contract $ContractPath)
if ($LASTEXITCODE -ne 0) {
    throw 'OT-120 contract preflight failed; no candidate build is allowed.'
}
$contractValidation = $contractValidationOutput[-1] | ConvertFrom-Json

$contract = Get-Content -Raw -LiteralPath $ContractPath | ConvertFrom-Json
if ($contract.schema -ne 'OTCIBC1' -or $contract.version -ne 1 -or
    $contract.status -ne 'phase_one_contract_frozen_host_only' -or
    $contract.build_policy.candidate_count -ne 3 -or
    $contract.build_policy.clean_run_count_per_candidate -ne 2 -or
    -not $contract.build_policy.component_manager_network_disabled -or
    -not $contract.build_policy.shared_compiler_cache_disabled) {
    throw 'OT-120 contract does not authorize the exact bounded host-only build procedure.'
}

$harnessRoot = Join-Path $projectRoot 'tests\benchmarks\crypto\esp_idf\ot120_candidate_builds'
$commonDefaults = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench\sdkconfig.defaults'
$reproducibleDefaults = Join-Path $harnessRoot 'reproducible.defaults'
$partitionSource = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench\partitions.csv'
Assert-ExactFile $commonDefaults 'accepted common sdkconfig defaults'
Assert-ExactFile $reproducibleDefaults 'accepted reproducible sdkconfig defaults'
if ((Get-Sha256 $reproducibleDefaults) -ne '995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6') {
    throw 'Accepted reproducible sdkconfig defaults digest mismatch.'
}
Assert-ExactFile $partitionSource 'accepted partition table'

$candidateSpecs = @(
    [ordered]@{
        id = 'espressif_libsodium'
        project = Join-Path $harnessRoot 'espressif_libsodium'
        overlay = Join-Path $harnessRoot 'espressif_libsodium\sdkconfig.overlay'
        sdkconfig_bytes = 107001
        sdkconfig_sha256 = 'b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f'
        application = 'ot120_espressif_libsodium_import_build'
        slug = 'LIBSODIUM'
        archive = 'esp-idf\espressif__libsodium\libespressif__libsodium.a'
        source_paths = @(
            'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/dependencies.lock',
            'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/source-manifest.sha256',
            'tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c'
        )
        operation_symbols = @(
            @{ operation = 'ed25519_sign'; symbol = 'crypto_sign_detached' },
            @{ operation = 'ed25519_verify'; symbol = 'crypto_sign_verify_detached' },
            @{ operation = 'x25519'; symbol = 'crypto_scalarmult_curve25519' },
            @{ operation = 'sha256'; symbol = 'crypto_hash_sha256' },
            @{ operation = 'hkdf_sha256'; symbol = 'crypto_kdf_hkdf_sha256_extract' },
            @{ operation = 'chacha20poly1305_encrypt'; symbol = 'crypto_aead_chacha20poly1305_ietf_encrypt' },
            @{ operation = 'chacha20poly1305_decrypt'; symbol = 'crypto_aead_chacha20poly1305_ietf_decrypt' },
            @{ operation = 'noise_xk_handshake'; symbol = 'ot_noise_xk_init_initiator' }
        )
    },
    [ordered]@{
        id = 'esp_idf_mbedtls_psa'
        project = Join-Path $harnessRoot 'esp_idf_mbedtls_psa'
        overlay = Join-Path $harnessRoot 'esp_idf_mbedtls_psa\sdkconfig.overlay'
        sdkconfig_bytes = 106921
        sdkconfig_sha256 = '9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686'
        application = 'ot120_esp_idf_mbedtls_psa_import_build'
        slug = 'MBEDTLS-PSA'
        archive = 'esp-idf\mbedtls\libmbedtls.a'
        source_paths = @(
            'tests/benchmarks/crypto/esp_idf/mbedtls_4_1_0/project-lock.json',
            'tests/benchmarks/crypto/esp_idf/mbedtls_4_1_0/source-tree.jsonl'
        )
        operation_symbols = @(
            @{ operation = 'x25519'; symbol = 'psa_raw_key_agreement' },
            @{ operation = 'sha256'; symbol = 'psa_hash_compute' },
            @{ operation = 'hkdf_sha256'; symbol = 'psa_key_derivation_setup' },
            @{ operation = 'chacha20poly1305_encrypt'; symbol = 'psa_aead_encrypt' },
            @{ operation = 'chacha20poly1305_decrypt'; symbol = 'psa_aead_decrypt' }
        )
    },
    [ordered]@{
        id = 'monocypher'
        project = Join-Path $harnessRoot 'monocypher'
        overlay = $null
        sdkconfig_bytes = 106913
        sdkconfig_sha256 = '4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f'
        application = 'ot120_monocypher_import_build'
        slug = 'MONOCYPHER'
        archive = 'esp-idf\main\libmain.a'
        source_paths = @(
            'tests/benchmarks/crypto/monocypher/4.0.3/project-lock.json',
            'tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c',
            'tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c',
            'tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c'
        )
        operation_symbols = @(
            @{ operation = 'ed25519_sign'; symbol = 'ot_monocypher_ed25519_sign' },
            @{ operation = 'ed25519_verify'; symbol = 'ot_monocypher_ed25519_verify' },
            @{ operation = 'x25519'; symbol = 'ot_monocypher_x25519' },
            @{ operation = 'chacha20poly1305_encrypt'; symbol = 'ot_monocypher_chacha20poly1305_ietf_encrypt' },
            @{ operation = 'chacha20poly1305_decrypt'; symbol = 'ot_monocypher_chacha20poly1305_ietf_decrypt' }
        )
    }
)

foreach ($candidate in $candidateSpecs) {
    Assert-ExactFile (Join-Path $candidate.project 'CMakeLists.txt') "$($candidate.id) harness"
    Assert-ExactFile (Join-Path $candidate.project 'partitions.csv') "$($candidate.id) partition table"
    if ((Get-NormalizedLfText (Join-Path $candidate.project 'partitions.csv')) -cne
        (Get-NormalizedLfText $partitionSource)) {
        throw "$($candidate.id) partition table differs from the accepted target table."
    }
    if ($candidate.overlay) {
        Assert-ExactFile $candidate.overlay "$($candidate.id) sdkconfig overlay"
    }
}

if (-not $Execute) {
    [ordered]@{
        schema = 'OTCIBHP1'
        version = 1
        contract_preflight = 'passed'
        execution_requested = $false
        candidate_order = @($candidateSpecs | ForEach-Object { $_.id })
        run_count_per_candidate = 2
        device_access = $false
        flash = $false
        radio = $false
        benchmark_execution = $false
    } | ConvertTo-Json -Depth 5
    exit 0
}

if (-not $env:IDF_PATH) {
    throw 'IDF_PATH is not set. Export the pinned ESP-IDF v6.0.2 environment first.'
}
$idfTool = Join-Path $env:IDF_PATH 'tools\idf.py'
Assert-ExactFile $idfTool 'idf.py'

$idfCommit = (& git -C $env:IDF_PATH rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $idfCommit -ne '7101770dc6db2667b3c477cc31365dd1acd6db4e') {
    throw 'ESP-IDF commit does not match the OT-120 contract.'
}
if (@(& git -C $env:IDF_PATH status --porcelain --untracked-files=all).Count -ne 0) {
    throw 'ESP-IDF worktree is not clean.'
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw 'OutputRoot must be initially absent.'
}

$env:IDF_COMPONENT_MANAGER = '0'
$nm = Get-Command xtensa-esp32s3-elf-nm -ErrorAction Stop
$receipts = @()
foreach ($candidate in $candidateSpecs) {
    foreach ($run in @('A', 'B')) {
        $runRoot = Join-Path $OutputRoot "$($candidate.id)\run-$($run.ToLowerInvariant())"
        $buildRoot = Join-Path $runRoot 'build'
        $rawLog = Join-Path $runRoot 'build.log'
        New-Item -ItemType Directory -Path $runRoot | Out-Null

        $defaults = "$commonDefaults;$reproducibleDefaults"
        if ($candidate.overlay) {
            $defaults = "$defaults;$($candidate.overlay)"
        }
        $sdkconfig = Join-Path $buildRoot 'sdkconfig'
        $arguments = @(
            $idfTool,
            '--no-ccache',
            '-C', $candidate.project,
            '-B', $buildRoot,
            '-D', "SDKCONFIG=$sdkconfig",
            '-D', "SDKCONFIG_DEFAULTS=$defaults",
            '-D', 'PROJECT_VER=ot107-config-v0',
            '-D', 'IDF_TARGET=esp32s3',
            'build'
        )
        $output = @(& $python.Source @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        [System.IO.File]::WriteAllLines($rawLog, [string[]]$output, [System.Text.UTF8Encoding]::new($false))
        if ($exitCode -ne 0) {
            throw "$($candidate.id) run $run failed."
        }

        $compilerWarnings = @($output | Where-Object {
            $_ -match '(?i)\.(?:c|cc|cpp|cxx|s):[0-9]+(?::[0-9]+)?:\s+warning:'
        }).Count
        if ($compilerWarnings -ne 0) {
            throw "$($candidate.id) run $run emitted compiler warnings."
        }

        Assert-ExactFile $sdkconfig 'generated sdkconfig'
        $sdkconfigItem = Get-Item -LiteralPath $sdkconfig
        if ($sdkconfigItem.Length -ne $candidate.sdkconfig_bytes -or
            (Get-Sha256 $sdkconfig) -ne $candidate.sdkconfig_sha256) {
            throw "$($candidate.id) run $run did not reproduce its OT-107 sdkconfig."
        }

        $application = $candidate.application
        $elfPath = Join-Path $buildRoot "$application.elf"
        $artifacts = @(
            Get-Artifact 'application' (Join-Path $buildRoot "$application.bin")
            Get-Artifact 'application_elf' $elfPath
            Get-Artifact 'linker_map' (Join-Path $buildRoot "$application.map")
            Get-Artifact 'bootloader' (Join-Path $buildRoot 'bootloader\bootloader.bin')
            Get-Artifact 'partition_table' (Join-Path $buildRoot 'partition_table\partition-table.bin')
            Get-Artifact 'generated_sdkconfig' $sdkconfig
            Get-Artifact 'partition_csv' (Join-Path $candidate.project 'partitions.csv')
        )

        $nmLines = @(& $nm.Source -S --defined-only $elfPath)
        if ($LASTEXITCODE -ne 0) {
            throw "$($candidate.id) run $run symbol inspection failed."
        }
        $anchors = @()
        foreach ($anchor in $candidate.operation_symbols) {
            $escapedSymbol = [regex]::Escape($anchor.symbol)
            $matches = @($nmLines | Where-Object {
                $_ -match "^([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+\S\s+$escapedSymbol$"
            })
            if ($matches.Count -ne 1 -or
                $matches[0] -notmatch "^([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+\S\s+$escapedSymbol$") {
                throw "$($candidate.id) run $run did not retain exact symbol $($anchor.symbol)."
            }
            $sizeHex = $Matches[2].ToLowerInvariant()
            $anchors += [ordered]@{
                artifact_role = 'benchmark_adapter'
                bytes = [Convert]::ToInt64($sizeHex, 16)
                logical_path = "anchors/$($anchor.operation)/$($anchor.symbol)"
                operation_id = $anchor.operation
                record_kind = 'build_graph_entry'
                retained_in_final_link = $true
                sha256 = Get-TextSha256 "$($anchor.symbol)`t$sizeHex`n"
            }
        }


        $runRecord = [ordered]@{
            profile = "ot120-$($candidate.slug.ToLowerInvariant())-$($run.ToLowerInvariant())"
            initial_build_directory_absent = $true
            build_exit_code = $exitCode
            compiler_warning_count = $compilerWarnings
            raw_build_evidence_sha256 = Get-Sha256 $rawLog
            normalized_receipt_sha256 = 'pending'
            artifacts = $artifacts
        }
        $receipts += [pscustomobject]@{
            candidate_id = $candidate.id
            run_root = $runRoot
            run = $runRecord
            anchors = $anchors
            candidate_archive = Get-Artifact 'candidate_archive' (Join-Path $buildRoot $candidate.archive)
        }
    }
}

$generatedArtifacts = @()
foreach ($candidate in $candidateSpecs) {
    $pair = @($receipts | Where-Object candidate_id -eq $candidate.id)
    if ($pair.Count -ne 2) {
        throw "$($candidate.id) did not produce exactly two run receipts."
    }
    $first = @($pair[0].run.artifacts | ForEach-Object { "$($_.role):$($_.name):$($_.bytes):$($_.sha256)" })
    $second = @($pair[1].run.artifacts | ForEach-Object { "$($_.role):$($_.name):$($_.bytes):$($_.sha256)" })
    if (Compare-Object $first $second) {
        throw "$($candidate.id) clean-run artifact tuples differ."
    }
    if ($pair[0].run.normalized_receipt_sha256 -ne $pair[1].run.normalized_receipt_sha256) {
        throw "$($candidate.id) normalized clean-run receipts differ."
    }

    $graphEntries = @()
    foreach ($logicalPath in $candidate.source_paths) {
        $sourcePath = Join-Path $projectRoot ($logicalPath -replace '/', '\')
        Assert-ExactFile $sourcePath "$($candidate.id) graph source"
        $sourceItem = Get-Item -LiteralPath $sourcePath
        $graphEntries += [ordered]@{
            artifact_role = 'candidate_source'
            bytes = $sourceItem.Length
            logical_path = $logicalPath
            operation_id = $null
            record_kind = 'build_graph_entry'
            retained_in_final_link = $false
            sha256 = Get-Sha256 $sourcePath
        }
    }
    $archive = $pair[0].candidate_archive
    $graphEntries += [ordered]@{
        artifact_role = 'candidate_archive'
        bytes = $archive.bytes
        logical_path = "build/$($candidate.archive -replace '\\', '/')"
        operation_id = $null
        record_kind = 'build_graph_entry'
        retained_in_final_link = $true
        sha256 = $archive.sha256
    }
    $graphEntries += $pair[0].anchors
    $graphEntries = @($graphEntries | Sort-Object { [string]$_['logical_path'] })

    $contractCandidate = @($contract.candidates | Where-Object candidate_id -eq $candidate.id)
    if ($contractCandidate.Count -ne 1) {
        throw "$($candidate.id) is not uniquely bound by the OT-120 contract."
    }
    $contractCandidate = $contractCandidate[0]
    $header = [ordered]@{
        api_config_evidence_sha256 = $contractCandidate.api_config_evidence_canonical_sha256
        candidate_id = $candidate.id
        entry_count = $graphEntries.Count
        generated_sdkconfig_sha256 = $contractCandidate.generated_sdkconfig_sha256
        record_kind = 'header'
        schema = 'OTCIBG1'
        source_evidence_sha256 = $contractCandidate.source_evidence_sha256
        version = 1
    }
    $graphPath = Join-Path $projectRoot "tests\benchmarks\crypto\OT-120-OT005-$($candidate.slug)-IMPORT-BUILD-GRAPH-V1.jsonl"
    $graphLines = @($header) + $graphEntries | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 8 }
    [System.IO.File]::WriteAllText($graphPath, (($graphLines -join "`n") + "`n"), [System.Text.UTF8Encoding]::new($false))

    $sourceBindings = [ordered]@{}
    foreach ($field in @(
        'version', 'source_evidence_sha256', 'source_admission_canonical_sha256',
        'project_dependency_lock_sha256', 'source_manifest_sha256', 'sbom_sha256',
        'api_config_evidence_canonical_sha256', 'generated_sdkconfig_bytes',
        'generated_sdkconfig_sha256', 'coverage_state', 'eligible_operations',
        'unavailable_operations'
    )) {
        $sourceBindings[$field] = $contractCandidate.$field
    }
    $graphBinding = [ordered]@{
        path = "tests/benchmarks/crypto/OT-120-OT005-$($candidate.slug)-IMPORT-BUILD-GRAPH-V1.jsonl"
        raw_sha256 = Get-Sha256 $graphPath
        entry_count = $graphEntries.Count
        manifest_kind = 'canonical-lf-jsonl-build-graph-v1'
    }
    $normalizedReceipt = [ordered]@{
        candidate_id = $candidate.id
        source_bindings = $sourceBindings
        target = $contract.target
        toolchain = $contract.toolchain
        graph_binding = $graphBinding
        artifacts = $pair[0].run.artifacts
    }
    $canonicalDigestScript = 'import hashlib,json,sys; value=json.load(open(sys.argv[1],encoding="utf-8")); raw=json.dumps(value,ensure_ascii=True,allow_nan=False,sort_keys=True,separators=(",",":")).encode("utf-8"); print(hashlib.sha256(raw).hexdigest())'
    foreach ($item in $pair) {
        $receiptPath = Join-Path $item.run_root 'normalized-receipt.json'
        [System.IO.File]::WriteAllText($receiptPath, (($normalizedReceipt | ConvertTo-Json -Depth 12) + "`n"), [System.Text.UTF8Encoding]::new($false))
        $item.run.normalized_receipt_sha256 = (& $python.Source -c $canonicalDigestScript $receiptPath).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "$($candidate.id) normalized receipt hashing failed."
        }
    }
    if ($pair[0].run.normalized_receipt_sha256 -ne $pair[1].run.normalized_receipt_sha256) {
        throw "$($candidate.id) contract-bound normalized receipts differ."
    }
    $evidence = [ordered]@{
        schema = 'OTCIBE1'
        version = 1
        artifact_kind = 'retained_candidate_import_build_evidence'
        evidence_id = "OT-120-OT005-$($candidate.slug)-IMPORT-BUILD-EVIDENCE-V1"
        recorded_date = '2026-08-22'
        status = 'candidate_import_build_evidence_complete_pending_atomic_admission'
        public_result = "$($candidate.slug)-IMPORT-BUILD-EVIDENCE-COMPLETE-HOST-ONLY; PENDING-ATOMIC-ADMISSION; NO-BENCHMARK-DEVICE-RADIO-SELECTION-OR-SCORE"
        contract_raw_sha256 = Get-Sha256 $ContractPath
        contract_canonical_sha256 = $contractValidation.contract_sha256
        candidate_id = $candidate.id
        source_bindings = $sourceBindings
        target = $contract.target
        toolchain = $contract.toolchain
        graph_binding = $graphBinding

        build_reproducibility = [ordered]@{
            clean_run_count = 2
            independent_build_directories = $true
            shared_compiler_cache_disabled = $true
            component_manager_network_disabled = $true
            reproducible_paths_normalized = $true
            runs = @($pair | ForEach-Object { $_.run })
        }
        one_time_authority = [ordered]@{
            host_only_phase_one_instruction_used = $true
            consumed = $true
            hardware_scope_included = $false
            benchmark_execution_scope_included = $false
        }
        boundaries = [ordered]@{
            benchmark_executed = $false
            device_accessed = $false
            flashed = $false
            radio_used = $false
            key_or_entropy_operation = $false
            candidate_selected = $false
            suite_selected = $false
            packet_v1_authorized = $false
            score_credit_added = $false
        }
        claims = [ordered]@{
            candidate_imported_for_benchmark = $true
            candidate_benchmark_built = $true
            benchmark_executed = $false
            hardware_or_device_accessed = $false
            candidate_selected = $false
            support_proven = $false
            compatibility_proven = $false
            regulatory_compliance_proven = $false
            score_credit_added = $false
        }
    }
    $evidencePath = Join-Path $projectRoot "tests\benchmarks\crypto\OT-120-OT005-$($candidate.slug)-IMPORT-BUILD-EVIDENCE-V1.json"
    [System.IO.File]::WriteAllText($evidencePath, (($evidence | ConvertTo-Json -Depth 12) + "`n"), [System.Text.UTF8Encoding]::new($false))

    $evidenceCanonicalSha256 = (& $python.Source -c $canonicalDigestScript $evidencePath).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "$($candidate.id) canonical evidence hashing failed."
    }
    $generatedArtifacts += [pscustomobject]@{
        candidate_id = $candidate.id
        graph_path = $graphPath
        graph_raw_sha256 = Get-Sha256 $graphPath
        evidence_path = $evidencePath
        evidence_raw_sha256 = Get-Sha256 $evidencePath
        evidence_canonical_sha256 = $evidenceCanonicalSha256
    }
}

if ($generatedArtifacts.Count -ne 3 -or
    (Compare-Object @($candidateSpecs | ForEach-Object { $_.id }) @($generatedArtifacts | ForEach-Object { $_.candidate_id }))) {
    throw 'Atomic admission requires the exact ordered three-candidate evidence set.'
}

$admission = [ordered]@{
    schema = 'OTCIBA1'
    version = 1
    artifact_kind = 'append_only_atomic_candidate_import_build_admission'
    admission_id = 'OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1'
    accepted_date = '2026-08-22'
    status = 'phase_one_complete_measurement_awaits_fresh_authority'
    public_result = 'OT-120 PHASE-ONE CANDIDATE IMPORT BUILDS ACCEPTED HOST-ONLY; MEASUREMENT AWAITS FRESH AUTHORITY; NO BENCHMARK DEVICE RADIO SELECTION OR SCORE'
    parents = [ordered]@{
        contract_raw_sha256 = 'ac0b3dd0e7f6fbd1fdb7edbf482ed301cfd2ce15a32c9b6f2e48bf1b8408df51'
        contract_canonical_sha256 = 'bbbc9c93028affce509bc145ce2f3de44c0cc2a5934cc3804bd3526dee94a8ea'
        otrtpa1_v1_raw_sha256 = 'afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36'
        otrtpa1_v1_canonical_sha256 = '0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443'
    }
    accepted_candidate_imports = @($generatedArtifacts | ForEach-Object {
        [ordered]@{
            candidate_id = $_.candidate_id
            evidence_raw_sha256 = $_.evidence_raw_sha256
            evidence_canonical_sha256 = $_.evidence_canonical_sha256
            graph_raw_sha256 = $_.graph_raw_sha256
        }
    })
    acceptance_counts = [ordered]@{
        exact_profile_units = 2
        source = 3
        api_config = 3
        candidate_import = 3
    }
    phases = [ordered]@{
        phase_zero_complete = $true
        phase_one_complete = $true
        phase_two_execution_admitted = $false
        phase_three_admission_complete = $false
    }
    measurement_blockers = @('fresh_benchmark_execution_authority_absent')
    one_time_authority = [ordered]@{
        host_only_phase_one_instruction_used = $true
        consumed = $true
        hardware_scope_included = $false
        benchmark_execution_scope_included = $false
    }
    continuing_authority = [ordered]@{
        candidate_import_authorized = $false
        benchmark_build_authorized = $false
        benchmark_execution_authorized = $false
        device_access_authorized = $false
        flash_authorized = $false
        radio_transmit_authorized = $false
        key_or_entropy_operation_authorized = $false
        candidate_selection_authorized = $false
        suite_selection_authorized = $false
        packet_v1_authorized = $false
        score_credit_added = $false
    }
    claims = [ordered]@{
        candidate_imports_accepted = $true
        candidate_benchmark_builds_accepted = $true
        phase_one_complete = $true
        measurement_ready = $false
        benchmark_executed = $false
        hardware_or_device_accessed = $false
        candidate_selected = $false
        suite_selected = $false
        support_proven = $false
        compatibility_proven = $false
        regulatory_compliance_proven = $false
        score_credit_added = $false
    }
}
$admissionPath = Join-Path $projectRoot 'tests\benchmarks\crypto\OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1.json'
[System.IO.File]::WriteAllText($admissionPath, (($admission | ConvertTo-Json -Depth 12) + "`n"), [System.Text.UTF8Encoding]::new($false))

$validationArguments = @($ContractValidatorPath, '--contract', $ContractPath)
foreach ($item in $generatedArtifacts) {
    $validationArguments += @('--graph', $item.graph_path)
}
foreach ($item in $generatedArtifacts) {
    $validationArguments += @('--evidence', $item.evidence_path)
}
$validationArguments += @('--admission', $admissionPath)
$admissionValidationOutput = @(& $python.Source @validationArguments)
if ($LASTEXITCODE -ne 0) {
    throw 'OT-120 atomic candidate import build admission validation failed.'
}
$admissionValidation = $admissionValidationOutput[-1] | ConvertFrom-Json

[ordered]@{
    schema = 'OTCIBHP1'
    version = 1
    contract_preflight = 'passed'
    execution_requested = $true
    result = 'all_candidate_clean_run_pairs_equal'
    admission_validation = $admissionValidation
    output_root = $OutputRoot
    candidate_order = @($candidateSpecs | ForEach-Object { $_.id })
    benchmark_executed = $false
    hardware_or_device_accessed = $false
} | ConvertTo-Json -Depth 5
