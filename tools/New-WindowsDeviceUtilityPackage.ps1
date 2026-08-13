[CmdletBinding()]
param(
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$appProject = Join-Path $projectRoot 'tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj'
$nugetConfig = Join-Path $projectRoot 'tools\windows-loader\NuGet.Config'
$runtime = 'win-x64'
$framework = 'net8.0-windows'
$assemblyName = 'DeviceUtility'
$timestamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'artifacts\windows-device-utility'
}

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'LimitedUnderground.DeviceUtility.Package.' + [System.Guid]::NewGuid().ToString('N'))))
$publishRoot = Join-Path $packageRoot 'publish'
$stageRoot = Join-Path $packageRoot 'stage'
$extractRoot = Join-Path $packageRoot 'extract'
$intermediateRoot = Join-Path $packageRoot 'obj'
$buildOutputRoot = Join-Path $packageRoot 'bin'
$appDataRoot = Join-Path $packageRoot 'appdata'

if (-not $packageRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The package workspace must remain inside the system temporary directory.'
}

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw 'The .NET 8 SDK is required to build the Windows device utility package.'
}

function Assert-PackagePayload {
    param([Parameter(Mandatory)][string]$Root)

    $allFiles = @(Get-ChildItem -LiteralPath $Root -Recurse -File)
    if ($allFiles.Count -eq 0) {
        throw 'The package payload is empty.'
    }

    $forbiddenExtensions = @(
        '.cs', '.csproj', '.py', '.pyc', '.ps1', '.pdb',
        '.bin', '.hex', '.uf2', '.elf', '.pem', '.key'
    )
    $forbiddenNames = @(
        'python.exe', 'python3.exe', 'esptool.exe', 'meshcli.exe',
        'Get-OpenTrailLoaderInspection.py'
    )

    foreach ($file in $allFiles) {
        if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant() -or
            $forbiddenNames -contains $file.Name) {
            throw "Forbidden package payload detected: $($file.Name)"
        }
    }

    $executable = Join-Path $Root "$assemblyName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Expected packaged executable is missing: $assemblyName.exe"
    }

    $stalePublicName = @($allFiles | Where-Object {
        $_.Name -match 'OpenTrail|OpenGauge'
    })
    if ($stalePublicName.Count -ne 0) {
        throw 'A package filename exposes a repository engineering name.'
    }
}

function Get-PayloadRecords {
    param([Parameter(Mandatory)][string]$Root)

    return @(Get-ChildItem -LiteralPath $Root -Recurse -File |
        Where-Object { $_.Name -ne 'package-manifest.json' } |
        ForEach-Object {
            $rootPrefix = [System.IO.Path]::GetFullPath($Root)
            if (-not $rootPrefix.EndsWith('\')) {
                $rootPrefix += '\'
            }
            $fullPath = [System.IO.Path]::GetFullPath($_.FullName)
            if (-not $fullPath.StartsWith(
                    $rootPrefix,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                throw 'A package payload path escaped its staging root.'
            }
            [pscustomobject]@{
                path = $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        } |
        Sort-Object path)
}

function Stop-OwnedSmokeProcess {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process) {
        return
    }

    try {
        if (-not $Process.HasExited) {
            [void]$Process.CloseMainWindow()
            if (-not $Process.WaitForExit(5000)) {
                $Process.Kill()
                [void]$Process.WaitForExit(5000)
            }
        }
    }
    catch {
        if (-not $Process.HasExited) {
            $Process.Kill()
        }
    }
}

$previousAppData = $env:APPDATA
$smokeProcess = $null
$archivePath = $null

try {
    $env:APPDATA = $appDataRoot
    New-Item -ItemType Directory -Path $env:APPDATA -Force | Out-Null
    New-Item -ItemType Directory -Path $publishRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

    $isolatedBuildProperty = "/p:OpenTrailValidationIntermediateRoot=$intermediateRoot"
    $isolatedOutputProperty = "/p:OpenTrailValidationOutputRoot=$buildOutputRoot"

    & $dotnet.Source restore $appProject --runtime $runtime `
        --configfile $nugetConfig --nologo `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Windows device utility RID restore failed with exit code $LASTEXITCODE."
    }

    & $dotnet.Source publish $appProject --configuration Release `
        --framework $framework --runtime $runtime --self-contained true `
        --no-restore --nologo --output $publishRoot `
        "/p:AssemblyName=$assemblyName" `
        '/p:DebugSymbols=false' '/p:DebugType=None' `
        '/p:PublishSingleFile=false' '/p:UseAppHost=true' `
        $isolatedBuildProperty $isolatedOutputProperty
    if ($LASTEXITCODE -ne 0) {
        throw "Windows device utility publish failed with exit code $LASTEXITCODE."
    }

    Copy-Item -Path (Join-Path $publishRoot '*') -Destination $stageRoot -Recurse

    $guide = @'
LIMITED UNDERGROUND TRAIL - DEVICE UTILITY
WORKING NAME - ATTORNEY REVIEW PENDING

This is a local engineering package, not a public release or supported product.
It performs privacy-safe Windows USB-family discovery and fixed read-only
MeshCore runtime-identity queries. Installed runtime names are not authoritative
hardware profiles. It shows a vendor-family profile candidate, the exact
runtime evidence observed, the published family baseline, and the deliberate
maintenance step still required; those hints never authorize Flash. It can
select and structurally inspect a bounded local
candidate firmware bundle and verify its image SHA-256. Its RSA-PSS signature
verifier remains blocked without a configured production release signer. It
contains an RSA-PSS-3072/SHA-256 verifier and an immutable public-key catalog
boundary; the packaged catalog is deliberately empty. It also contains an
exact selected-device manifest matcher, but no connected device has the
authoritative received-unit profile required to pass it. A match cannot grant
release admission or Flash permission. The package contains no firmware
writer, firmware image, erase/reset/DFU/recovery adapter, Python runtime,
MeshCLI, esptool, private signing key, protected trust/revocation state,
credential, pairing data,
or arbitrary serial-command surface.
'@
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        (Join-Path $stageRoot 'README-WORKING-NAME.txt'),
        $guide,
        $utf8NoBom)

    Assert-PackagePayload -Root $stageRoot
    $payloadRecords = Get-PayloadRecords -Root $stageRoot
    $manifest = [ordered]@{
        schema = 'ot_windows_device_utility_package_v0'
        generated_utc = [DateTime]::UtcNow.ToString('o')
        display_name = 'Limited Underground Trail Device Utility'
        legal_status = 'working name - attorney review pending'
        runtime = $runtime
        framework = $framework
        self_contained = $true
        executable = "$assemblyName.exe"
        capabilities = [ordered]@{
            connected_device_inspection = $true
            windows_usb_family_discovery = $true
            meshcore_runtime_identity_query = $true
            hardware_profile_authoritative = $false
            firmware_selection = $true
            firmware_bundle_structural_inspection = $true
            firmware_bundle_image_sha256_verification = $true
            firmware_bundle_rsa_pss_3072_sha256_verifier = $true
            firmware_bundle_exact_device_match = $true
            production_trusted_signer_configured = $false
            firmware_bundle_signature_admission = $false
            protected_signer_revocation_state = $false
            firmware_write = $false
            erase_reset_dfu_recovery = $false
        }
        payload_file_count = $payloadRecords.Count
        files = $payloadRecords
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText(
        (Join-Path $stageRoot 'package-manifest.json'),
        $manifestJson,
        $utf8NoBom)

    $archiveName = "Limited-Underground-Trail-Device-Utility-WORKING-$timestamp-$runtime.zip"
    $archivePath = Join-Path $OutputDirectory $archiveName
    Compress-Archive -Path (Join-Path $stageRoot '*') `
        -DestinationPath $archivePath -CompressionLevel Optimal

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot
    Assert-PackagePayload -Root $extractRoot

    $extractedManifestPath = Join-Path $extractRoot 'package-manifest.json'
    if (-not (Test-Path -LiteralPath $extractedManifestPath -PathType Leaf)) {
        throw 'The independently extracted package manifest is missing.'
    }
    $extractedManifest = Get-Content -LiteralPath $extractedManifestPath -Raw |
        ConvertFrom-Json
    if ($extractedManifest.schema -ne 'ot_windows_device_utility_package_v0' -or
        $extractedManifest.self_contained -ne $true -or
        $extractedManifest.capabilities.windows_usb_family_discovery -ne $true -or
        $extractedManifest.capabilities.meshcore_runtime_identity_query -ne $true -or
        $extractedManifest.capabilities.hardware_profile_authoritative -ne $false -or
        $extractedManifest.capabilities.firmware_selection -ne $true -or
        $extractedManifest.capabilities.firmware_bundle_structural_inspection -ne $true -or
        $extractedManifest.capabilities.firmware_bundle_image_sha256_verification -ne $true -or
        $extractedManifest.capabilities.firmware_bundle_rsa_pss_3072_sha256_verifier -ne $true -or
        $extractedManifest.capabilities.firmware_bundle_exact_device_match -ne $true -or
        $extractedManifest.capabilities.production_trusted_signer_configured -ne $false -or
        $extractedManifest.capabilities.firmware_bundle_signature_admission -ne $false -or
        $extractedManifest.capabilities.protected_signer_revocation_state -ne $false -or
        $extractedManifest.capabilities.firmware_write -ne $false -or
        $extractedManifest.capabilities.erase_reset_dfu_recovery -ne $false) {
        throw 'The independently extracted package manifest violates the safety boundary.'
    }

    $actualRecords = Get-PayloadRecords -Root $extractRoot
    if ($actualRecords.Count -ne [int]$extractedManifest.payload_file_count -or
        $actualRecords.Count -ne @($extractedManifest.files).Count) {
        throw 'The independently extracted package file count does not match its manifest.'
    }

    $manifestByPath = @{}
    foreach ($record in @($extractedManifest.files)) {
        if ($manifestByPath.ContainsKey([string]$record.path)) {
            throw "Duplicate manifest path: $($record.path)"
        }
        $manifestByPath[[string]$record.path] = $record
    }
    foreach ($actual in $actualRecords) {
        if (-not $manifestByPath.ContainsKey($actual.path)) {
            throw "Unmanifested extracted payload: $($actual.path)"
        }
        $expected = $manifestByPath[$actual.path]
        if ([long]$expected.bytes -ne [long]$actual.bytes -or
            [string]$expected.sha256 -ne [string]$actual.sha256) {
            throw "Extracted payload hash mismatch: $($actual.path)"
        }
    }

    $extractedExecutable = Join-Path $extractRoot "$assemblyName.exe"
    $smokeProcess = Start-Process -FilePath $extractedExecutable `
        -WorkingDirectory $extractRoot -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    if ($smokeProcess.HasExited) {
        throw "The independently extracted device utility exited during launch smoke testing."
    }
    Stop-OwnedSmokeProcess -Process $smokeProcess
    $smokeProcess = $null

    $archive = Get-Item -LiteralPath $archivePath
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    [pscustomobject]@{
        Archive = $archive.FullName
        Bytes = $archive.Length
        SHA256 = $archiveHash
        PayloadFiles = $payloadRecords.Count
        Runtime = $runtime
        SelfContained = $true
        LaunchSmoke = 'passed from independent extraction'
        PublicRelease = $false
    }
}
catch {
    if ($null -ne $archivePath -and (Test-Path -LiteralPath $archivePath)) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    throw
}
finally {
    Stop-OwnedSmokeProcess -Process $smokeProcess
    $env:APPDATA = $previousAppData
    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
}
