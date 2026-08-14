[CmdletBinding()]
param(
    [switch]$IncludeLocalPorts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-OpenTrailPreflightField {
    param(
        [AllowNull()]
        [object]$InputObject,

        [Parameter(Mandatory)]
        [string]$Name
    )

    if ($null -eq $InputObject) {
        return $null
    }
    if ($InputObject -is [System.Collections.IDictionary]) {
        return $InputObject[$Name]
    }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function ConvertTo-WioTrackerL1Preflight {
    [CmdletBinding()]
    param(
        [switch]$IncludeLocalPorts,
        [object[]]$SerialPorts = @(),
        [object[]]$DfuVolumes = @(),
        [bool]$PnpQuerySucceeded = $true,
        [object[]]$PnpDevices = @(),
        [AllowNull()][object]$PnpQueryError = $null,
        [bool]$PnpUtilQuerySucceeded = $true,
        [object[]]$PnpUtilOutput = @(),
        [AllowNull()][object]$PnpUtilQueryError = $null,
        [bool]$RegistryQuerySucceeded = $true,
        [object[]]$RegistryRecords = @(),
        [AllowNull()][object]$RegistryQueryError = $null
    )

    # Raw errors are accepted only so the reducer can prove they are discarded.
    # They must never become public evidence because they may contain local paths
    # or device instance identifiers.
    $null = $PnpQueryError
    $null = $PnpUtilQueryError
    $null = $RegistryQueryError

    $publicSerialPorts = @()
    $publicDfuVolumes = @()
    if ($IncludeLocalPorts) {
        $publicSerialPorts = @(
            $SerialPorts |
                Where-Object {
                    $_ -is [string] -and $_ -match '(?i)^COM[1-9]\d{0,4}$'
                } |
                Sort-Object -Unique
        )
        $publicDfuVolumes = @(
            $DfuVolumes |
                Where-Object {
                    $_ -is [string] -and $_ -match '(?i)^[A-Z]:\\$'
                } |
                Sort-Object -Unique
        )
    }

    $pnpCandidates = @()
    $exactWioPnpPresent = $false
    if ($PnpQuerySucceeded) {
        $pnpCandidates = @(
            foreach ($device in $PnpDevices) {
                $instanceId = Get-OpenTrailPreflightField $device 'InstanceId'
                $friendlyName = Get-OpenTrailPreflightField $device 'FriendlyName'
                $exactWioFamily =
                    $instanceId -is [string] -and
                    $instanceId -match
                        '(?i)^USB\\VID_2886&PID_1667(?:&|\\)'
                $possibleWio =
                    $friendlyName -is [string] -and
                    $friendlyName -match
                        '(?i)L1 Pro|TinyUSB|Tracker L1|nRF52|Nordic'
                if (-not $exactWioFamily -and -not $possibleWio) {
                    continue
                }
                if ($exactWioFamily) {
                    $exactWioPnpPresent = $true
                }

                $class = Get-OpenTrailPreflightField $device 'Class'
                $class = switch -Regex ($class) {
                    '(?i)^Ports$' { 'Ports'; break }
                    '(?i)^USB$' { 'USB'; break }
                    '(?i)^USBDevice$' { 'USBDevice'; break }
                    '(?i)^Bluetooth$' { 'Bluetooth'; break }
                    '(?i)^WPD$' { 'WPD'; break }
                    '(?i)^HIDClass$' { 'HIDClass'; break }
                    default { 'Unknown' }
                }
                $status = Get-OpenTrailPreflightField $device 'Status'
                $status = switch -Regex ($status) {
                    '(?i)^OK$' { 'OK'; break }
                    '(?i)^Error$' { 'Error'; break }
                    '(?i)^Degraded$' { 'Degraded'; break }
                    default { 'Unknown' }
                }

                [pscustomobject]@{
                    DeviceFamily = if ($exactWioFamily) {
                        'Seeed Wio Tracker L1 USB family'
                    }
                    else {
                        'Possible Wio/nRF/TinyUSB device'
                    }
                    UsbId = if ($exactWioFamily) { '2886:1667' } else { $null }
                    Class = $class
                    Status = $status
                }
            }
        )
        $pnpCandidates = @(
            $pnpCandidates |
                Sort-Object DeviceFamily, UsbId, Class, Status -Unique
        )
    }

    $exactWioPnpUtilPresent =
        $PnpUtilQuerySucceeded -and
        [bool](
            $PnpUtilOutput -match
                '(?i)VID_2886&PID_1667(?:&|\\|\s|$)'
        )

    $registryCandidates = @()
    $exactWioRegistryHistory = $false
    if ($RegistryQuerySucceeded) {
        $registryNames = [System.Collections.Generic.List[string]]::new()
        foreach ($record in $RegistryRecords) {
            $deviceTypeName =
                Get-OpenTrailPreflightField $record 'DeviceTypeName'
            $exactWioFamily =
                $deviceTypeName -is [string] -and
                $deviceTypeName -match
                    '(?i)^VID_2886&PID_1667(?:&.*)?$'
            if ($exactWioFamily) {
                $exactWioRegistryHistory = $true
                $registryNames.Add(
                    'Seeed Wio Tracker L1 USB family (2886:1667)'
                )
                continue
            }

            $displayNames = @(
                Get-OpenTrailPreflightField $record 'DisplayNames'
            )
            if (
                [bool](
                    $displayNames -match
                        '(?i)L1 Pro|TinyUSB|Tracker L1|nRF52|Nordic'
                )
            ) {
                $registryNames.Add('Possible Wio/nRF/TinyUSB device')
            }
        }
        $registryCandidates = @($registryNames | Sort-Object -Unique)
    }

    $exactWioUsbFamilyPresent =
        $exactWioPnpPresent -or $exactWioPnpUtilPresent
    $currentPnpCandidateCount = $pnpCandidates.Count
    $registryHistoryCount = $registryCandidates.Count
    $bothConnectedQueriesFailed =
        -not $PnpQuerySucceeded -and -not $PnpUtilQuerySucceeded
    $trackerDfuMounted = @($DfuVolumes).Count -gt 0

    [pscustomobject]@{
        CheckedAtUtc = [DateTime]::UtcNow.ToString('o')
        LocalPortsIncluded = [bool]$IncludeLocalPorts
        SerialPorts = $publicSerialPorts
        TrackerDfuMounted = $trackerDfuMounted
        TrackerDfuVolumes = $publicDfuVolumes
        ExactWioUsbFamilyPresent = $exactWioUsbFamilyPresent
        ExactWioUsbId = if ($exactWioUsbFamilyPresent) {
            '2886:1667'
        }
        else {
            $null
        }
        PnpQuerySucceeded = $PnpQuerySucceeded
        PnpCandidates = $pnpCandidates
        PnpQueryErrorType = if ($PnpQuerySucceeded) {
            $null
        }
        else {
            'query_failed'
        }
        PnpUtilQuerySucceeded = $PnpUtilQuerySucceeded
        PnpUtilQueryErrorType = if ($PnpUtilQuerySucceeded) {
            $null
        }
        else {
            'query_failed'
        }
        RegistryQuerySucceeded = $RegistryQuerySucceeded
        RegistryCandidates = $registryCandidates
        RegistryQueryErrorType = if ($RegistryQuerySucceeded) {
            $null
        }
        else {
            'query_failed'
        }
        StateChangesMade = $false
        Interpretation = if ($trackerDfuMounted) {
            'TRACKER L1 DFU storage is mounted; no files were read or written.'
        }
        elseif ($exactWioUsbFamilyPresent) {
            'The exact Seeed Wio Tracker L1 USB family 2886:1667 is present. Runtime identity and received revision remain separate checks.'
        }
        elseif ($currentPnpCandidateCount -gt 0) {
            'A possible current Wio/nRF/TinyUSB device is present; confirm its screen and label.'
        }
        elseif ($registryHistoryCount -gt 0) {
            'A possible Wio/nRF/TinyUSB family exists only in redacted registry history; current presence was not confirmed.'
        }
        elseif ($bothConnectedQueriesFailed) {
            'Current connected-device presence could not be checked because both connected-device queries failed; registry history is not presence evidence.'
        }
        else {
            'No current Wio-specific candidate was identified. A normal BLE-only runtime can make this inconclusive.'
        }
    }
}

function Invoke-WioTrackerL1Preflight {
    [CmdletBinding()]
    param(
        [switch]$IncludeLocalPorts
    )

    $serialPorts = @()
    if ($IncludeLocalPorts) {
        $serialPorts = @(
            [System.IO.Ports.SerialPort]::GetPortNames() |
                Sort-Object -Unique
        )
    }

    $dfuVolumes = @()
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        try {
            if ($drive.IsReady -and $drive.VolumeLabel -eq 'TRACKER L1') {
                $dfuVolumes += $drive.Name
            }
        }
        catch {
            # A removable drive may disappear between enumeration and inspection.
            # That is not evidence about the tracker, so continue read-only.
        }
    }

    $pnpQuerySucceeded = $true
    $pnpQueryError = $null
    $pnpDevices = @()
    try {
        $pnpDevices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
    }
    catch {
        $pnpQuerySucceeded = $false
        $pnpQueryError = $_.Exception.Message
    }

    $pnpUtilQuerySucceeded = $true
    $pnpUtilQueryError = $null
    $pnpUtilOutput = @()
    try {
        $pnpUtilOutput = @(
            & "$env:SystemRoot\System32\pnputil.exe" /enum-devices /connected 2>$null
        )
        if ($LASTEXITCODE -ne 0) {
            throw [System.InvalidOperationException]::new(
                'Connected-device enumeration failed.'
            )
        }
    }
    catch {
        $pnpUtilQuerySucceeded = $false
        $pnpUtilQueryError = $_.Exception.Message
        $pnpUtilOutput = @()
    }

    $registryQuerySucceeded = $true
    $registryQueryError = $null
    $registryRecords = @()
    try {
        $usbRoot =
            'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB'
        $registryRecords = @(
            foreach ($deviceType in Get-ChildItem -LiteralPath $usbRoot) {
                $displayNames = [System.Collections.Generic.List[string]]::new()
                foreach ($instance in Get-ChildItem -LiteralPath $deviceType.PSPath) {
                    $properties =
                        Get-ItemProperty -LiteralPath $instance.PSPath
                    $friendlyProperty =
                        $properties.PSObject.Properties['FriendlyName']
                    $descriptionProperty =
                        $properties.PSObject.Properties['DeviceDesc']
                    $name = if ($null -ne $friendlyProperty) {
                        $friendlyProperty.Value
                    }
                    elseif ($null -ne $descriptionProperty) {
                        $descriptionProperty.Value
                    }
                    else {
                        $null
                    }
                    if ($name -is [string]) {
                        $displayNames.Add($name)
                    }
                }
                [pscustomobject]@{
                    DeviceTypeName = $deviceType.PSChildName
                    DisplayNames = @($displayNames)
                }
            }
        )
    }
    catch {
        $registryQuerySucceeded = $false
        $registryQueryError = $_.Exception.Message
        $registryRecords = @()
    }

    ConvertTo-WioTrackerL1Preflight `
        -IncludeLocalPorts:$IncludeLocalPorts `
        -SerialPorts $serialPorts `
        -DfuVolumes $dfuVolumes `
        -PnpQuerySucceeded $pnpQuerySucceeded `
        -PnpDevices $pnpDevices `
        -PnpQueryError $pnpQueryError `
        -PnpUtilQuerySucceeded $pnpUtilQuerySucceeded `
        -PnpUtilOutput $pnpUtilOutput `
        -PnpUtilQueryError $pnpUtilQueryError `
        -RegistryQuerySucceeded $registryQuerySucceeded `
        -RegistryRecords $registryRecords `
        -RegistryQueryError $registryQueryError
}

if ($MyInvocation.InvocationName -ne '.') {
    Invoke-WioTrackerL1Preflight -IncludeLocalPorts:$IncludeLocalPorts
}
