[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ArchivePath,

    [switch]$SkipLaunch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$verificationRoot = [System.IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'LimitedUnderground.DeviceUtility.Verify.' + [System.Guid]::NewGuid().ToString('N'))))
$extractRoot = Join-Path $verificationRoot 'extract'
$appDataRoot = Join-Path $verificationRoot 'appdata'

if (-not $verificationRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The verification workspace must remain inside the system temporary directory.'
}

function Get-ArchivePayloadRecords {
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
                throw 'A package payload path escaped its extraction root.'
            }
            [pscustomobject]@{
                path = $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        } |
        Sort-Object path)
}

function Stop-VerificationProcess {
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
$verificationProcess = $null

try {
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $appDataRoot -Force | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $extractRoot

    $manifestPath = Join-Path $extractRoot 'package-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'package-manifest.json is missing.'
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 'ot_windows_device_utility_package_v0' -or
        $manifest.display_name -ne 'Limited Underground Trail Device Utility' -or
        $manifest.legal_status -ne 'working name - attorney review pending' -or
        $manifest.runtime -ne 'win-x64' -or
        $manifest.framework -ne 'net8.0-windows' -or
        $manifest.self_contained -ne $true -or
        $manifest.executable -ne 'DeviceUtility.exe' -or
        $manifest.capabilities.connected_device_inspection -ne $true -or
        $manifest.capabilities.windows_usb_family_discovery -ne $true -or
        $manifest.capabilities.meshcore_runtime_identity_query -ne $true -or
        $manifest.capabilities.hardware_profile_authoritative -ne $false -or
        $manifest.capabilities.firmware_selection -ne $true -or
        $manifest.capabilities.firmware_bundle_structural_inspection -ne $true -or
        $manifest.capabilities.firmware_bundle_image_sha256_verification -ne $true -or
        $manifest.capabilities.firmware_bundle_rsa_pss_3072_sha256_verifier -ne $true -or
        $manifest.capabilities.firmware_bundle_exact_device_match -ne $true -or
        $manifest.capabilities.production_trusted_signer_configured -ne $false -or
        $manifest.capabilities.firmware_bundle_signature_admission -ne $false -or
        $manifest.capabilities.protected_signer_revocation_state -ne $false -or
        $manifest.capabilities.firmware_write -ne $false -or
        $manifest.capabilities.erase_reset_dfu_recovery -ne $false) {
        throw 'The package manifest violates the expected identity or capability boundary.'
    }

    $payloadFiles = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File |
        Where-Object { $_.Name -ne 'package-manifest.json' })
    $forbiddenExtensions = @(
        '.cs', '.csproj', '.py', '.pyc', '.ps1', '.pdb',
        '.bin', '.hex', '.uf2', '.elf', '.pem', '.key'
    )
    $forbiddenFiles = @($payloadFiles | Where-Object {
        $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() -or
        $_.Name -match '^(python3?|esptool|meshcli)\.exe$' -or
        $_.Name -match 'OpenTrail|OpenGauge'
    })
    if ($forbiddenFiles.Count -ne 0) {
        throw "Forbidden package payload detected: $($forbiddenFiles[0].Name)"
    }

    $actualRecords = Get-ArchivePayloadRecords -Root $extractRoot
    if ($actualRecords.Count -ne [int]$manifest.payload_file_count -or
        $actualRecords.Count -ne @($manifest.files).Count) {
        throw 'The extracted payload count does not match the manifest.'
    }

    $manifestByPath = @{}
    foreach ($record in @($manifest.files)) {
        $path = [string]$record.path
        if ($manifestByPath.ContainsKey($path)) {
            throw "Duplicate manifest path: $path"
        }
        $manifestByPath[$path] = $record
    }

    foreach ($actual in $actualRecords) {
        if (-not $manifestByPath.ContainsKey($actual.path)) {
            throw "Unmanifested payload: $($actual.path)"
        }
        $expected = $manifestByPath[$actual.path]
        if ([long]$expected.bytes -ne [long]$actual.bytes -or
            [string]$expected.sha256 -ne [string]$actual.sha256) {
            throw "Payload mismatch: $($actual.path)"
        }
    }

    $launchResult = 'skipped'
    if (-not $SkipLaunch) {
        $env:APPDATA = $appDataRoot
        $executable = Join-Path $extractRoot 'DeviceUtility.exe'
        $verificationProcess = Start-Process -FilePath $executable `
            -WorkingDirectory $extractRoot -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 3
        if ($verificationProcess.HasExited) {
            throw 'The independently extracted utility exited during launch verification.'
        }
        Stop-VerificationProcess -Process $verificationProcess
        $verificationProcess = $null
        $launchResult = 'passed'
    }

    [pscustomobject]@{
        Archive = $ArchivePath
        Bytes = (Get-Item -LiteralPath $ArchivePath).Length
        SHA256 = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
        PayloadFiles = $actualRecords.Count
        Runtime = [string]$manifest.runtime
        SelfContained = [bool]$manifest.self_contained
        CapabilityBoundary = 'inspection only'
        LaunchVerification = $launchResult
        PublicRelease = $false
    }
}
finally {
    Stop-VerificationProcess -Process $verificationProcess
    $env:APPDATA = $previousAppData
    if (Test-Path -LiteralPath $verificationRoot) {
        Remove-Item -LiteralPath $verificationRoot -Recurse -Force
    }
}
