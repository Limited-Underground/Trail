Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$preflightPath =
    Join-Path $projectRoot 'tools\Get-WioTrackerL1Preflight.ps1'
. $preflightPath

function Assert-Preflight {
    param(
        [Parameter(Mandatory)]
        [bool]$Condition,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-PreflightPrivateTextAbsent {
    param(
        [Parameter(Mandatory)]
        [object]$Value,

        [Parameter(Mandatory)]
        [string[]]$PrivateTexts
    )

    $serialized = $Value | ConvertTo-Json -Depth 6 -Compress
    foreach ($privateText in $PrivateTexts) {
        if ($serialized.Contains($privateText)) {
            throw "Private preflight text leaked: $privateText"
        }
    }
}

function Test-StaleRegistryHistoryNeverMeansPresent {
    $result = ConvertTo-WioTrackerL1Preflight `
        -PnpQuerySucceeded $true `
        -PnpDevices @() `
        -PnpUtilQuerySucceeded $true `
        -PnpUtilOutput @('No matching connected device') `
        -RegistryQuerySucceeded $true `
        -RegistryRecords @(
            [pscustomobject]@{
                DeviceTypeName = 'VID_9999&PID_0001'
                DisplayNames = @('TinyUSB Serial (COM18)')
                RegistryPath = 'Registry::HKEY_PRIVATE\private-instance'
            }
        )

    Assert-Preflight `
        (-not $result.ExactWioUsbFamilyPresent) `
        'Registry history must not establish exact current presence.'
    Assert-Preflight `
        ($result.Interpretation -match 'only in redacted registry history') `
        'Registry-only matches must be described as history.'
    Assert-Preflight `
        ($result.Interpretation -notmatch 'device is present') `
        'Registry-only matches must never say a device is present.'
    Assert-PreflightPrivateTextAbsent $result @(
        'COM18',
        'HKEY_PRIVATE',
        'private-instance',
        'VID_9999&PID_0001'
    )
}

function Test-BlockedPnpWithConnectedPnpUtilEvidence {
    $result = ConvertTo-WioTrackerL1Preflight `
        -PnpQuerySucceeded $false `
        -PnpDevices @(
            [pscustomobject]@{
                InstanceId = 'USB\VID_PRIVATE&PID_PRIVATE\private-pnp-id'
                FriendlyName = 'private PnP name (COM18)'
            }
        ) `
        -PnpQueryError 'private PnP failure at HKEY_PRIVATE' `
        -PnpUtilQuerySucceeded $true `
        -PnpUtilOutput @(
            'Instance ID: USB\VID_2886&PID_1667&MI_00\private-wio-serial'
        ) `
        -RegistryQuerySucceeded $false `
        -RegistryQueryError 'private registry error and path'

    Assert-Preflight `
        $result.ExactWioUsbFamilyPresent `
        'Connected pnputil evidence must survive a blocked PnP cmdlet.'
    Assert-Preflight `
        ($result.ExactWioUsbId -eq '2886:1667') `
        'Connected pnputil evidence must reduce to the public USB family.'
    Assert-Preflight `
        ($result.PnpQueryErrorType -eq 'query_failed') `
        'PnP failures must use a bounded public category.'
    Assert-PreflightPrivateTextAbsent $result @(
        'private-wio-serial',
        'private-pnp-id',
        'private PnP failure',
        'private registry error',
        'HKEY_PRIVATE',
        'COM18',
        'USB\VID_'
    )
}

function Test-BothConnectedQueriesFailed {
    $result = ConvertTo-WioTrackerL1Preflight `
        -PnpQuerySucceeded $false `
        -PnpQueryError 'private first discovery error' `
        -PnpUtilQuerySucceeded $false `
        -PnpUtilQueryError 'private second discovery error' `
        -RegistryQuerySucceeded $false `
        -RegistryQueryError 'private registry error'

    Assert-Preflight `
        (-not $result.ExactWioUsbFamilyPresent) `
        'Failed connected-device queries must not establish presence.'
    Assert-Preflight `
        ($result.Interpretation -match 'could not be checked') `
        'Two failed connected-device queries must remain explicitly inconclusive.'
    Assert-Preflight `
        ($result.PnpQueryErrorType -eq 'query_failed' -and
            $result.PnpUtilQueryErrorType -eq 'query_failed' -and
            $result.RegistryQueryErrorType -eq 'query_failed') `
        'All query failures must use bounded public categories.'
    Assert-PreflightPrivateTextAbsent $result @(
        'private first discovery error',
        'private second discovery error',
        'private registry error'
    )
}

function Test-DefaultLocalLocationOmission {
    $result = ConvertTo-WioTrackerL1Preflight `
        -SerialPorts @('COM18') `
        -DfuVolumes @('E:\')

    Assert-Preflight `
        (-not $result.LocalPortsIncluded) `
        'Local ports must be excluded by default.'
    Assert-Preflight `
        (@($result.SerialPorts).Count -eq 0) `
        'Default preflight must omit serial-port names.'
    Assert-Preflight `
        $result.TrackerDfuMounted `
        'Default preflight may report the privacy-safe DFU-mounted boolean.'
    Assert-Preflight `
        (@($result.TrackerDfuVolumes).Count -eq 0) `
        'Default preflight must omit DFU drive roots.'
    Assert-PreflightPrivateTextAbsent $result @('COM18', 'E:\')
}

function Test-ExplicitLocalLocationInclusion {
    $result = ConvertTo-WioTrackerL1Preflight `
        -IncludeLocalPorts `
        -SerialPorts @('COM18', 'COM7', 'private-port') `
        -DfuVolumes @('E:\', 'private-drive-root')

    Assert-Preflight `
        $result.LocalPortsIncluded `
        'Explicit local-port mode must be visible in the result.'
    Assert-Preflight `
        (@($result.SerialPorts).Count -eq 2 -and
            $result.SerialPorts -contains 'COM18' -and
            $result.SerialPorts -contains 'COM7') `
        'Explicit local-port mode must retain bounded COM names.'
    Assert-Preflight `
        (@($result.TrackerDfuVolumes).Count -eq 1 -and
            $result.TrackerDfuVolumes[0] -eq 'E:\') `
        'Explicit local-port mode must retain a bounded DFU drive root.'
    Assert-PreflightPrivateTextAbsent $result @(
        'private-port',
        'private-drive-root'
    )
}

function Test-RawCurrentIdentifiersAreReduced {
    $result = ConvertTo-WioTrackerL1Preflight `
        -PnpQuerySucceeded $true `
        -PnpDevices @(
            [pscustomobject]@{
                InstanceId =
                    'USB\VID_2886&PID_1667&MI_00\private-current-serial'
                FriendlyName = 'Seeed Wio Tracker L1 (COM18) private name'
                Class = 'private class value'
                Status = 'private status value'
                DeviceLocation = 'private USB location'
            }
        ) `
        -PnpUtilQuerySucceeded $true `
        -PnpUtilOutput @('No second candidate') `
        -RegistryQuerySucceeded $true `
        -RegistryRecords @(
            [pscustomobject]@{
                DeviceTypeName = 'VID_2886&PID_1667&MI_00'
                DisplayNames = @('private registry display (COM18)')
                InstancePath = 'private-registry-instance'
            }
        )

    Assert-Preflight `
        $result.ExactWioUsbFamilyPresent `
        'Exact current PnP family must be detected.'
    Assert-Preflight `
        (@($result.PnpCandidates).Count -eq 1 -and
            $result.PnpCandidates[0].DeviceFamily -eq
                'Seeed Wio Tracker L1 USB family' -and
            $result.PnpCandidates[0].Class -eq 'Unknown' -and
            $result.PnpCandidates[0].Status -eq 'Unknown') `
        'Current PnP evidence must reduce to the public family.'
    Assert-PreflightPrivateTextAbsent $result @(
        'private-current-serial',
        'private name',
        'private class value',
        'private status value',
        'private USB location',
        'private registry display',
        'private-registry-instance',
        'COM18',
        'USB\VID_'
    )
}

function Test-PnpUtilUsbIdBoundary {
    $result = ConvertTo-WioTrackerL1Preflight `
        -PnpQuerySucceeded $true `
        -PnpUtilQuerySucceeded $true `
        -PnpUtilOutput @(
            'Instance ID: USB\VID_2886&PID_16670\private-near-match'
        ) `
        -RegistryQuerySucceeded $true

    Assert-Preflight `
        (-not $result.ExactWioUsbFamilyPresent) `
        'A pnputil PID prefix near-match must not establish Wio presence.'
    Assert-PreflightPrivateTextAbsent $result @('private-near-match', 'USB\VID_')
}

Test-StaleRegistryHistoryNeverMeansPresent
Test-BlockedPnpWithConnectedPnpUtilEvidence
Test-BothConnectedQueriesFailed
Test-DefaultLocalLocationOmission
Test-ExplicitLocalLocationInclusion
Test-RawCurrentIdentifiersAreReduced
Test-PnpUtilUsbIdBoundary

'PASS: 7 Wio Tracker L1 preflight scenario groups'
