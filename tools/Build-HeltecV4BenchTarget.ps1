[CmdletBinding()]
param(
    [ValidateSet('standard', 'ot093-a', 'ot093-b')]
    [string]$BuildProfile = 'standard',
    [Parameter(DontShow)]
    [ValidateSet('none', 'success', 'failure')]
    [string]$EnvironmentIsolationProbe = 'none'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredVersion = 'ESP-IDF v6.0.2'
$acceptedFirmwareBaseCommit = '0afac6b1cf3d142aca2f2cae98264f80ee801989'
$requiredFirmwareInputManifestFileCount = 307
$requiredFirmwareInputManifestSha256 = '6738195a7da53eb3d03c4a47552f6c0b6489559a2d81c0ba068489fe9faf7bc3'
$requiredFirmwareWorkingTreeManifestSha256 = '3837dbce866a3fc7cef76fd374bf242bb0125c042e8de15273a9e44bafff3324'
$requiredGitCoreAutocrlf = 'true'
$requiredOt093ProjectVersion = 'ot093-precrypto-v0'
$requiredOt093DefaultsSha256 = '995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6'
$requiredIdfCommit = '7101770dc6db2667b3c477cc31365dd1acd6db4e'
$requiredIdfToolSha256 = '5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40'
$requiredCompiler = 'xtensa-esp32s3-elf-gcc'
$requiredCompilerVersion = '15.2.0'
$requiredCrosstool = 'esp-15.2.0_20251204'
$requiredCompilerSha256 = '20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5'
$requiredCMakeVersion = 'cmake version 4.0.3'
$requiredCMakeSha256 = '392ab4d6c3c91543fd297ed6c7e7354bf62edcd26fdf2706ad8613ad620cc45e'
$requiredNinjaVersion = '1.12.1'
$requiredNinjaSha256 = '68865c3276d449d746cea5065fdec2baf755d7813e161ab04205b0907b2629b8'
$requiredPythonVersion = 'Python 3.14.6'
$requiredPythonSha256 = '199ce15a9f0d4f9522edba59338e4879d28cf61f88e377b8164bcb716275ed22'
$projectRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench'
$buildLeaf = switch ($BuildProfile) {
    'ot093-a' { 'heltec_v4_bench_ot093_a' }
    'ot093-b' { 'heltec_v4_bench_ot093_b' }
    default { 'heltec_v4_bench' }
}
$buildRoot = Join-Path $projectRoot "build\targets\$buildLeaf"
$ot093PythonCacheRoot = Join-Path $projectRoot "build\targets\ot093-python-cache-$BuildProfile"
$defaultsPath = Join-Path $targetRoot 'sdkconfig.defaults'
$ot093DefaultsPath = Join-Path $projectRoot 'build\targets\ot093-reproducible.defaults'
$partitionCsvPath = Join-Path $targetRoot 'partitions.csv'
$logoPath = Join-Path $targetRoot 'main\trail_startup_logo.hpp'
$protectedRootRosterHeaderPath = Join-Path $targetRoot 'main\companion_protected_root_key_roster_adapter.hpp'
$protectedRootRosterSourcePath = Join-Path $targetRoot 'main\companion_protected_root_key_roster_adapter.cpp'
$configurationSecurityHeaderPath = Join-Path $targetRoot 'main\companion_protected_root_configuration_security_adapter.hpp'
$configurationSecuritySourcePath = Join-Path $targetRoot 'main\companion_protected_root_configuration_security_adapter.cpp'
$sdkconfigPath = Join-Path $buildRoot 'sdkconfig'
$publicLinkInfoHeaderPath = Join-Path $projectRoot 'firmware\components\companion\include\opentrail\companion_public_link_info.hpp'
$publicLinkInfoSourcePath = Join-Path $projectRoot 'firmware\components\companion\src\companion_public_link_info.cpp'

$ot093CleanBaseline = $BuildProfile -ne 'standard'
$ot093EnvironmentNames = @('CCACHE_DISABLE', 'PYTHONPYCACHEPREFIX', 'PYTHONNOUSERSITE')
$ot093EnvironmentSnapshot = @{}
$ot093PythonCacheOwnedByRun = $false
if (-not $ot093CleanBaseline -and $EnvironmentIsolationProbe -ne 'none') {
    throw 'The environment-isolation probe is available only to an OT-093 profile.'
}
if ($ot093CleanBaseline) {
    foreach ($name in $ot093EnvironmentNames) {
        $ot093EnvironmentSnapshot[$name] = [ordered]@{
            Exists = Test-Path -LiteralPath "Env:$name"
            Value = [System.Environment]::GetEnvironmentVariable(
                $name,
                [System.EnvironmentVariableTarget]::Process)
        }
    }
}
try {
  if ($ot093CleanBaseline) {
    if ($EnvironmentIsolationProbe -eq 'none') {
        if (Test-Path -LiteralPath $buildRoot) {
            throw 'OT-093 requires an absent, independent build directory.'
        }
        if (Test-Path -LiteralPath $ot093PythonCacheRoot) {
            throw 'OT-093 requires an absent, independent Python cache directory.'
        }
        $ot093PythonCacheOwnedByRun = $true
    }
    $env:CCACHE_DISABLE = '1'
    $env:PYTHONPYCACHEPREFIX = $ot093PythonCacheRoot
    $env:PYTHONNOUSERSITE = '1'
    if ($EnvironmentIsolationProbe -eq 'success') {
        Write-Output 'OT-093-ENVIRONMENT-ISOLATION-PROBE-ONLY'
        return
    }
    if ($EnvironmentIsolationProbe -eq 'failure') {
        throw 'OT-093 environment-isolation failure probe.'
    }
    $ot093DefaultsContent = "CONFIG_APP_REPRODUCIBLE_BUILD=y`nCONFIG_APP_COMPILE_TIME_DATE=n`n"
    $ot093DefaultsParent = Split-Path -Parent $ot093DefaultsPath
    [System.IO.Directory]::CreateDirectory($ot093DefaultsParent) | Out-Null
    [System.IO.File]::WriteAllText(
        $ot093DefaultsPath,
        $ot093DefaultsContent,
        [System.Text.UTF8Encoding]::new($false))
    $ot093DefaultsSha256 =
        (Get-FileHash -LiteralPath $ot093DefaultsPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ot093DefaultsSha256 -ne $requiredOt093DefaultsSha256) {
        throw 'The generated OT-093 reproducible-build defaults digest does not match.'
    }
}
$effectiveDefaultsPath = if ($ot093CleanBaseline) {
    "$defaultsPath;$ot093DefaultsPath"
} else {
    $defaultsPath
}
$ot093ProjectVersionArgs = if ($ot093CleanBaseline) {
    @('-D', "PROJECT_VER=$requiredOt093ProjectVersion")
} else {
    @()
}

foreach ($rosterSourcePath in @($protectedRootRosterHeaderPath, $protectedRootRosterSourcePath)) {
    if (-not (Test-Path -LiteralPath $rosterSourcePath -PathType Leaf)) {
        throw "Protected-root key-roster source is missing: $(Split-Path -Leaf $rosterSourcePath)"
    }
}
foreach ($configurationSecurityPath in @($configurationSecurityHeaderPath, $configurationSecuritySourcePath)) {
    if (-not (Test-Path -LiteralPath $configurationSecurityPath -PathType Leaf)) {
        throw "Protected-root configuration/security source is missing: $(Split-Path -Leaf $configurationSecurityPath)"
    }
}
foreach ($publicLinkInfoPath in @($publicLinkInfoHeaderPath, $publicLinkInfoSourcePath)) {
    if (-not (Test-Path -LiteralPath $publicLinkInfoPath -PathType Leaf)) {
        throw "Companion public link-info source is missing: $(Split-Path -Leaf $publicLinkInfoPath)"
    }
}

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

if ($ot093CleanBaseline) {
    $pythonIdentity = (& $idfPython.Source --version 2>&1 |
        ForEach-Object { $_.ToString().Trim() }) -join ' '
    $pythonSha256 =
        (Get-FileHash -LiteralPath $idfPython.Source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $pythonIdentity -ne $requiredPythonVersion -or
        $pythonSha256 -ne $requiredPythonSha256) {
        throw 'The ESP-IDF Python executable does not match the frozen OT-093 baseline.'
    }

    $cmakeTool = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -First 1
    $ninjaTool = Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cmakeTool -or -not $ninjaTool) {
        throw 'CMake or Ninja is unavailable in the exported ESP-IDF environment.'
    }
    $cmakeIdentity = (& $cmakeTool.Source --version 2>&1 |
        Select-Object -First 1).ToString().Trim()
    $ninjaIdentity = (& $ninjaTool.Source --version 2>&1 |
        ForEach-Object { $_.ToString().Trim() }) -join ' '
    $cmakeSha256 =
        (Get-FileHash -LiteralPath $cmakeTool.Source -Algorithm SHA256).Hash.ToLowerInvariant()
    $ninjaSha256 =
        (Get-FileHash -LiteralPath $ninjaTool.Source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($cmakeIdentity -ne $requiredCMakeVersion -or $cmakeSha256 -ne $requiredCMakeSha256) {
        throw 'CMake does not match the frozen OT-093 baseline.'
    }
    if ($ninjaIdentity -ne $requiredNinjaVersion -or $ninjaSha256 -ne $requiredNinjaSha256) {
        throw 'Ninja does not match the frozen OT-093 baseline.'
    }

    $gitTool = Get-Command git -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $gitTool) {
        throw 'Git is unavailable; the exact OT-093 source revisions cannot be verified.'
    }
    $reportedIdfCommit = (& $gitTool.Source -C $env:IDF_PATH rev-parse HEAD 2>&1 |
        ForEach-Object { $_.ToString().Trim() }) -join ''
    if ($LASTEXITCODE -ne 0 -or $reportedIdfCommit -ne $requiredIdfCommit) {
        throw "Expected ESP-IDF commit $requiredIdfCommit; received '$reportedIdfCommit'."
    }
    $idfTrackedStatus = @(& $gitTool.Source -C $env:IDF_PATH status --porcelain --untracked-files=all 2>&1 |
        ForEach-Object { $_.ToString().Trim() } |
        Where-Object { $_ })
    if ($LASTEXITCODE -ne 0) {
        throw 'The ESP-IDF tracked-source status could not be verified.'
    }
    if ($idfTrackedStatus.Count -ne 0) {
        throw 'The pinned ESP-IDF source contains tracked or untracked changes; refusing a non-reproducible build baseline.'
    }
    $idfToolSha256 =
        (Get-FileHash -LiteralPath $idfTool -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($idfToolSha256 -ne $requiredIdfToolSha256) {
        throw 'The idf.py entrypoint digest does not match the frozen OT-093 baseline.'
    }

    & $gitTool.Source -C $projectRoot diff --quiet $acceptedFirmwareBaseCommit -- firmware/components firmware/targets/heltec_v4_bench
    $sourceScopeDiffExitCode = $LASTEXITCODE
    if ($sourceScopeDiffExitCode -ne 0) {
        throw 'The firmware source scope differs from the frozen OpenTrail commit.'
    }
    $reportedGitCoreAutocrlf = (& $gitTool.Source -C $projectRoot config --get core.autocrlf 2>&1 |
        ForEach-Object { $_.ToString().Trim().ToLowerInvariant() }) -join ''
    if ($LASTEXITCODE -ne 0 -or $reportedGitCoreAutocrlf -ne $requiredGitCoreAutocrlf) {
        throw 'OT-093 requires the frozen core.autocrlf=true working-tree byte contract.'
    }
    $untrackedFirmwareInputs = @(& $gitTool.Source -C $projectRoot ls-files --others --exclude-standard -- firmware/components firmware/targets/heltec_v4_bench |
        ForEach-Object { $_.ToString().Trim() } |
        Where-Object { $_ })
    if ($LASTEXITCODE -ne 0 -or $untrackedFirmwareInputs.Count -ne 0) {
        throw 'The frozen firmware scope contains untracked build inputs.'
    }
    $trackedSourceEntries = @(& $gitTool.Source -C $projectRoot ls-files --stage -- firmware/components firmware/targets/heltec_v4_bench |
        ForEach-Object { $_.ToString() } |
        Where-Object { $_ })
    if ($LASTEXITCODE -ne 0 -or $trackedSourceEntries.Count -eq 0) {
        throw 'The frozen firmware input inventory could not be enumerated.'
    }
    $sourceManifestLines = @(
        foreach ($entry in $trackedSourceEntries) {
            if ($entry -notmatch '^(\d{6}) ([0-9a-f]{40}) 0\t(.+)$') {
                throw 'The firmware input inventory contains a non-stage-zero or malformed index entry.'
            }
            "$($Matches[1]) $($Matches[2]) $($Matches[3])"
        }
    )
    $sourceManifestText = ($sourceManifestLines -join "`n") + "`n"
    $sourceManifestBytes = [System.Text.Encoding]::UTF8.GetBytes($sourceManifestText)
    $sourceManifestHasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $sourceManifestDigest = $sourceManifestHasher.ComputeHash($sourceManifestBytes)
    } finally {
        $sourceManifestHasher.Dispose()
    }
    $sourceManifestSha256 =
        ([BitConverter]::ToString($sourceManifestDigest)).Replace('-', '').ToLowerInvariant()
    if ($trackedSourceEntries.Count -ne $requiredFirmwareInputManifestFileCount -or
        $sourceManifestSha256 -ne $requiredFirmwareInputManifestSha256) {
        throw 'The firmware input manifest does not match the frozen OT-093 baseline.'
    }
    $workingTreeManifestLines = @(
        foreach ($entry in $trackedSourceEntries) {
            if ($entry -notmatch '^(\d{6}) ([0-9a-f]{40}) 0\t(.+)$') {
                throw 'The working-tree manifest contains a malformed index entry.'
            }
            $relativePath = $Matches[3]
            $workingTreePath = Join-Path $projectRoot ($relativePath.Replace('/', '\'))
            $workingTreeSha256 =
                (Get-FileHash -LiteralPath $workingTreePath -Algorithm SHA256).Hash.ToLowerInvariant()
            "$workingTreeSha256 $relativePath"
        }
    )
    $workingTreeManifestText = ($workingTreeManifestLines -join "`n") + "`n"
    $workingTreeManifestBytes = [System.Text.Encoding]::UTF8.GetBytes($workingTreeManifestText)
    $workingTreeManifestHasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $workingTreeManifestDigest = $workingTreeManifestHasher.ComputeHash($workingTreeManifestBytes)
    } finally {
        $workingTreeManifestHasher.Dispose()
    }
    $workingTreeManifestSha256 =
        ([BitConverter]::ToString($workingTreeManifestDigest)).Replace('-', '').ToLowerInvariant()
    if ($workingTreeManifestSha256 -ne $requiredFirmwareWorkingTreeManifestSha256) {
        throw 'The raw firmware working-tree bytes do not match the frozen OT-093 compiler inputs.'
    }

    $compilerTool = Get-Command $requiredCompiler -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $compilerTool) {
        throw "The required compiler is unavailable: $requiredCompiler"
    }
    $compilerSha256 =
        (Get-FileHash -LiteralPath $compilerTool.Source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($compilerSha256 -ne $requiredCompilerSha256) {
        throw 'The compiler executable digest does not match the frozen OT-093 baseline.'
    }
    $compilerReport = @(& $compilerTool.Source --version 2>&1 |
        ForEach-Object { $_.ToString().Trim() })
    if ($LASTEXITCODE -ne 0 -or $compilerReport.Count -eq 0) {
        throw 'The compiler identity could not be verified.'
    }
    $compilerIdentity = $compilerReport[0]
    if (-not $compilerIdentity.Contains($requiredCompilerVersion) -or
        -not $compilerIdentity.Contains($requiredCrosstool)) {
        throw "Expected compiler $requiredCompilerVersion / $requiredCrosstool; received '$compilerIdentity'."
    }
}

$protectedRootMetadataSources = @(
    @{
        Path = 'components\efuse\src\efuse_controller\keys\with_key_purposes\esp_efuse_api_key.c'
        Sha256 = 'CD6C5462CB1B2ADFE7735915810461EDE96ECF0B830A0761E4CAF2E6CB982C73'
    },
    @{
        Path = 'components\efuse\src\esp_efuse_api.c'
        Sha256 = '66A12FA28B11642385C54A249CEC8EBEE139BF7A5BF562B9D5AFF29A3B8CF3F4'
    },
    @{
        Path = 'components\efuse\esp32s3\esp_efuse_table.csv'
        Sha256 = '0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E'
    },
    @{
        Path = 'components\efuse\include\esp_efuse.h'
        Sha256 = '4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7'
    },
    @{
        Path = 'components\efuse\esp32s3\include\esp_efuse_chip.h'
        Sha256 = 'B5299EE67627C912C5E7A0E4A908D1678FD0D2F12D5AFD7A58D849FC1BADAA30'
    }
)
foreach ($source in $protectedRootMetadataSources) {
    $sourcePath = Join-Path $env:IDF_PATH $source.Path
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Pinned protected-root metadata source is missing: $($source.Path)"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($actualHash -ne $source.Sha256) {
        throw "Pinned protected-root metadata source changed: $($source.Path)"
    }
}

$configurationSecuritySources = @(
    @{ Path = 'components\bootloader_support\include\esp_secure_boot.h'; Sha256 = '2589E0573A1F32C3CBF69D07AB1CF591A5A55935FBDCD76E12A59DA1DACA8B3D' },
    @{ Path = 'components\efuse\include\esp_efuse.h'; Sha256 = '4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7' },
    @{ Path = 'components\efuse\src\esp_efuse_api.c'; Sha256 = '66A12FA28B11642385C54A249CEC8EBEE139BF7A5BF562B9D5AFF29A3B8CF3F4' },
    @{ Path = 'components\efuse\src\esp_efuse_fields.c'; Sha256 = 'E7C04ACDF54CDA0EFF2F2AC7551D6B25CB782E62A2F221C0E4B31DDC37D57AB5' },
    @{ Path = 'components\efuse\esp32s3\include\esp_efuse_table.h'; Sha256 = 'B83AE97309A1AF3A7AE114C30033FDC88AB55842B0CF54ABBF64717C3AE9B8F7' },
    @{ Path = 'components\efuse\esp32s3\esp_efuse_table.c'; Sha256 = 'DA8BA0B51CEA533541E139D88F612ABECA447ACEB73F822A70C1A7A4D43E3234' },
    @{ Path = 'components\efuse\esp32s3\esp_efuse_table.csv'; Sha256 = '0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E' },
    @{ Path = 'components\hal\esp32s3\include\hal\efuse_ll.h'; Sha256 = '28C92CF756E98CDBDC31FBBE0A4C7C23E0415E620CB51323D06B66939B33EEFB' },
    @{ Path = 'components\hal\efuse_hal.c'; Sha256 = 'B73B8946370A4815391F90067C2A760466C60B68E67EB69FA725C14668430FDA' },
    @{ Path = 'components\nvs_flash\Kconfig'; Sha256 = 'ED0199B6407A1E920C9FC6169FB6E0EBA97241D01320FC52255E2AB16E1BDB06' },
    @{ Path = 'components\nvs_sec_provider\Kconfig'; Sha256 = '60C5EA67B4B957DEDF74477C3B618BE1C9B311099974EFD373E1500A58D181F9' }
)
foreach ($source in $configurationSecuritySources) {
    $sourcePath = Join-Path $env:IDF_PATH $source.Path
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Pinned configuration/security source is missing: $($source.Path)"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($actualHash -ne $source.Sha256) {
        throw "Pinned configuration/security source changed: $($source.Path)"
    }
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
        -D "SDKCONFIG_DEFAULTS=$effectiveDefaultsPath" `
        @ot093ProjectVersionArgs `
        set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32-S3 target selection failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Output 'Existing exact ESP32-S3/USB Serial-JTAG sdkconfig accepted; preserving incremental build state.'
}

$buildOutput = @(& $idfPython.Source $idfTool `
    -C $targetRoot `
    -B $buildRoot `
    -D "SDKCONFIG=$sdkconfigPath" `
    -D "SDKCONFIG_DEFAULTS=$effectiveDefaultsPath" `
    @ot093ProjectVersionArgs `
    build 2>&1)
$buildExitCode = $LASTEXITCODE
$buildOutput | ForEach-Object { Write-Output $_ }
if ($buildExitCode -ne 0) {
    throw "Heltec V4 bench candidate build failed with exit code $buildExitCode."
}
$compilerWarningCount = @($buildOutput | Where-Object {
    $_.ToString() -match '(?i)\bwarning:'
}).Count
if ($ot093CleanBaseline -and $compilerWarningCount -ne 0) {
    throw "OT-093 requires a warning-free build; observed $compilerWarningCount warning lines."
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
    @ot093ProjectVersionArgs `
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
if ($ot093CleanBaseline) {
    $projectDescriptionPath = Join-Path $buildRoot 'project_description.json'
    $projectDescription = Get-Content -LiteralPath $projectDescriptionPath -Raw | ConvertFrom-Json
    if ($projectDescription.project_version -ne $requiredOt093ProjectVersion) {
        throw 'The generated project description does not contain the stable OT-093 project version.'
    }
    $applicationText = [System.Text.Encoding]::ASCII.GetString($imageBytes)
    if (-not $applicationText.Contains($requiredOt093ProjectVersion)) {
        throw 'The application image does not embed the stable OT-093 project version.'
    }
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
if ($ot093CleanBaseline) {
    $forbiddenOt005LinkTokens = @(
        'libsodium',
        'sodium_',
        'monocypher',
        'noise_xk',
        'secure_lora',
        'otsl0',
        'otcb0'
    )
    foreach ($token in $forbiddenOt005LinkTokens) {
        if ($linkMap.IndexOf($token, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "OT-005 candidate or secure-LoRa adapter is present in the pre-selection link map: $token"
        }
    }
    if ($generatedSdkconfigLines -notcontains 'CONFIG_APP_REPRODUCIBLE_BUILD=y') {
        throw 'OT-093 generated sdkconfig did not enable ESP-IDF reproducible-build path normalization.'
    }
}
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
    'companion_public_link_info.cpp.obj',
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
$compiledProtectedRootRosterObject = @(
    Get-ChildItem -LiteralPath $buildRoot -Recurse -File |
        Where-Object {
            $_.Name -eq 'companion_protected_root_key_roster_adapter.cpp.obj'
        })
if (@($compiledProtectedRootRosterObject).Count -ne 1) {
    throw 'Protected-root key-roster adapter was not build-compiled exactly once.'
}
$protectedRootRosterObject = @($compiledProtectedRootRosterObject)[0]
$protectedRootRosterBuildEvidence = [ordered]@{
    header = [ordered]@{
        name = Split-Path -Leaf $protectedRootRosterHeaderPath
        bytes = (Get-Item -LiteralPath $protectedRootRosterHeaderPath).Length
        sha256 = (Get-FileHash -LiteralPath $protectedRootRosterHeaderPath -Algorithm SHA256).Hash
    }
    source = [ordered]@{
        name = Split-Path -Leaf $protectedRootRosterSourcePath
        bytes = (Get-Item -LiteralPath $protectedRootRosterSourcePath).Length
        sha256 = (Get-FileHash -LiteralPath $protectedRootRosterSourcePath -Algorithm SHA256).Hash
    }
    object = [ordered]@{
        name = $protectedRootRosterObject.Name
        bytes = $protectedRootRosterObject.Length
        sha256 = (Get-FileHash -LiteralPath $protectedRootRosterObject.FullName -Algorithm SHA256).Hash
    }
}
$compiledConfigurationSecurityObject = @(
    Get-ChildItem -LiteralPath $buildRoot -Recurse -File |
        Where-Object {
            $_.Name -eq 'companion_protected_root_configuration_security_adapter.cpp.obj'
        })
if (@($compiledConfigurationSecurityObject).Count -ne 1) {
    throw 'Protected-root configuration/security adapter was not build-compiled exactly once.'
}
$configurationSecurityObject = @($compiledConfigurationSecurityObject)[0]
$configurationSecurityBuildEvidence = [ordered]@{
    header = [ordered]@{
        name = Split-Path -Leaf $configurationSecurityHeaderPath
        bytes = (Get-Item -LiteralPath $configurationSecurityHeaderPath).Length
        sha256 = (Get-FileHash -LiteralPath $configurationSecurityHeaderPath -Algorithm SHA256).Hash
    }
    source = [ordered]@{
        name = Split-Path -Leaf $configurationSecuritySourcePath
        bytes = (Get-Item -LiteralPath $configurationSecuritySourcePath).Length
        sha256 = (Get-FileHash -LiteralPath $configurationSecuritySourcePath -Algorithm SHA256).Hash
    }
    object = [ordered]@{
        name = $configurationSecurityObject.Name
        bytes = $configurationSecurityObject.Length
        sha256 = (Get-FileHash -LiteralPath $configurationSecurityObject.FullName -Algorithm SHA256).Hash
    }
}
$compiledPublicLinkInfoObject = @(
    Get-ChildItem -LiteralPath $buildRoot -Recurse -File |
        Where-Object {
            $_.Name -eq 'companion_public_link_info.cpp.obj'
        })
if (@($compiledPublicLinkInfoObject).Count -ne 1) {
    throw 'Companion public link-info source was not build-compiled exactly once.'
}
$publicLinkInfoObject = @($compiledPublicLinkInfoObject)[0]
$publicLinkInfoBuildEvidence = [ordered]@{
    header = [ordered]@{
        name = Split-Path -Leaf $publicLinkInfoHeaderPath
        bytes = (Get-Item -LiteralPath $publicLinkInfoHeaderPath).Length
        sha256 = (Get-FileHash -LiteralPath $publicLinkInfoHeaderPath -Algorithm SHA256).Hash
    }
    source = [ordered]@{
        name = Split-Path -Leaf $publicLinkInfoSourcePath
        bytes = (Get-Item -LiteralPath $publicLinkInfoSourcePath).Length
        sha256 = (Get-FileHash -LiteralPath $publicLinkInfoSourcePath -Algorithm SHA256).Hash
    }
    object = [ordered]@{
        name = $publicLinkInfoObject.Name
        bytes = $publicLinkInfoObject.Length
        sha256 = (Get-FileHash -LiteralPath $publicLinkInfoObject.FullName -Algorithm SHA256).Hash
    }
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
    build_profile = $BuildProfile
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
    protected_root_key_roster_adapter = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'
    protected_root_key_roster_execution = 'NOT-AUTHORIZED-NOT-RUN'
    protected_root_key_roster_build_evidence = $protectedRootRosterBuildEvidence
    protected_root_configuration_security_adapter = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'
    protected_root_configuration_security_execution = 'NOT-AUTHORIZED-NOT-RUN'
    protected_root_configuration_security_build_evidence = $configurationSecurityBuildEvidence
    companion_public_link_info = 'BUILD-LINKED-PUBLIC-READ-NOT-RUN'
    companion_public_link_info_build_evidence = $publicLinkInfoBuildEvidence
    bounded_public_link_window = 'HOST-TESTED-BUILD-LINKED-NOT-RUN'
    public_link_hardware_observation = 'NOT-RUN'
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
if ($ot093CleanBaseline) {
    $publicBaselineArtifactPaths = @(
        @{ Role = 'application'; Path = $applicationBinPath },
        @{ Role = 'application_elf'; Path = (Join-Path $buildRoot 'opentrail_heltec_v4_bench.elf') },
        @{ Role = 'linker_map'; Path = (Join-Path $buildRoot 'opentrail_heltec_v4_bench.map') },
        @{ Role = 'bootloader'; Path = (Join-Path $buildRoot 'bootloader\bootloader.bin') },
        @{ Role = 'partition_table'; Path = $partitionBinPath },
        @{ Role = 'generated_sdkconfig'; Path = $sdkconfigPath },
        @{ Role = 'partition_csv'; Path = $partitionCsvPath }
    )
    $publicBaselineArtifacts = @(
        foreach ($artifact in $publicBaselineArtifactPaths) {
            $item = Get-Item -LiteralPath $artifact.Path
            [ordered]@{
                role = $artifact.Role
                name = $item.Name
                bytes = $item.Length
                sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    )
    $smallestAppSlotBytes = 5177344
    $applicationImageBytes = (Get-Item -LiteralPath $applicationBinPath).Length
    $applicationHeadroomBytes = $smallestAppSlotBytes - $applicationImageBytes
    if ($applicationHeadroomBytes -le 0) {
        throw 'The application image does not fit the smallest configured application slot.'
    }
    $generatedSdkconfigSha256 =
        (Get-FileHash -LiteralPath $sdkconfigPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $evidence['ot005_pre_selection_baseline'] = [ordered]@{
        status = 'BUILD-RUN-CAPTURED'
        public_result = 'BUILD-RUN-CAPTURED; OTCBL0-RECONCILIATION-PENDING; OTCB0-EXECUTION-BLOCKED'
        linked_claim = 'NO-OT005-CANDIDATE-OR-SECURE-LORA-ADAPTER-IMPORTED-OR-EXECUTED'
        project_version = $requiredOt093ProjectVersion
        accepted_firmware_base_commit = $acceptedFirmwareBaseCommit
        firmware_input_manifest_kind = 'git-index-stage-zero-v1'
        firmware_input_manifest_file_count = $trackedSourceEntries.Count
        firmware_input_manifest_sha256 = $sourceManifestSha256
        working_tree_manifest_kind = 'sha256-raw-bytes-path-v1'
        working_tree_manifest_sha256 = $workingTreeManifestSha256
        git_core_autocrlf = $requiredGitCoreAutocrlf
        framework_commit = $requiredIdfCommit
        framework_tracked_source_clean = $true
        idf_py_sha256 = $requiredIdfToolSha256
        compiler = $requiredCompiler
        compiler_version = $requiredCompilerVersion
        compiler_crosstool = $requiredCrosstool
        compiler_sha256 = $requiredCompilerSha256
        cmake_version = $requiredCMakeVersion
        cmake_sha256 = $requiredCMakeSha256
        ninja_version = $requiredNinjaVersion
        ninja_sha256 = $requiredNinjaSha256
        python_version = $requiredPythonVersion
        python_sha256 = $requiredPythonSha256
        independent_python_cache = $true
        python_user_site_disabled = $true
        clean_independent_build_directory = $true
        shared_compiler_cache_disabled = $true
        reproducible_build_paths_normalized = $true
        reproducible_defaults_sha256 = $requiredOt093DefaultsSha256
        generated_sdkconfig_sha256 = $generatedSdkconfigSha256
        sdkconfig_role = 'PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0'
        future_ot005_candidate_builds_require_same_baseline_config = $true
        ot005_candidate_imported = $false
        secure_lora_adapter_imported = $false
        secure_lora_adapter_executed = $false
        suite_selected = $false
        handshake_implemented = $false
        packet_v1_wire_selected = $false
        radio_enabled = $false
        hardware_or_device_accessed = $false
        key_or_entropy_operation = $false
        score_credit_added = $false
        otcb0_status = 'draft_blocked'
        build_exit_code = $buildExitCode
        compiler_warning_count = $compilerWarningCount
        public_artifacts = $publicBaselineArtifacts
        app_slot_headroom = [ordered]@{
            application_image_bytes = $applicationImageBytes
            smallest_app_slot_bytes = $smallestAppSlotBytes
            headroom_bytes = $applicationHeadroomBytes
        }
    }
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
} finally {
    if ($ot093CleanBaseline) {
        try {
            if ($ot093PythonCacheOwnedByRun -and
                (Test-Path -LiteralPath $ot093PythonCacheRoot -PathType Container)) {
                $cacheItem = Get-Item -LiteralPath $ot093PythonCacheRoot -Force
                $expectedCacheRoot = [System.IO.Path]::GetFullPath($ot093PythonCacheRoot)
                $buildTargetsRoot = [System.IO.Path]::GetFullPath(
                    (Join-Path $projectRoot 'build\targets'))
                $nestedReparsePoints = @(Get-ChildItem -LiteralPath $cacheItem.FullName -Recurse -Force |
                    Where-Object {
                        $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint
                    })
                if ($cacheItem.FullName -ne $expectedCacheRoot -or
                    -not $cacheItem.FullName.StartsWith(
                        "$buildTargetsRoot\ot093-python-cache-",
                        [System.StringComparison]::OrdinalIgnoreCase) -or
                    ($cacheItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -or
                    $nestedReparsePoints.Count -ne 0) {
                    throw 'Refusing unsafe OT-093 Python cache cleanup.'
                }
                Remove-Item -LiteralPath $cacheItem.FullName -Recurse -Force
            }
        } finally {
            foreach ($name in $ot093EnvironmentNames) {
                $snapshot = $ot093EnvironmentSnapshot[$name]
                if ($snapshot.Exists) {
                    Set-Item -LiteralPath "Env:$name" -Value $snapshot.Value
                } else {
                    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
                }
            }
        }
    }
}
