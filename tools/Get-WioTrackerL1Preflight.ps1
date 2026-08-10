[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serialPorts = @(
    [System.IO.Ports.SerialPort]::GetPortNames() |
        Sort-Object -Unique
)

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
$pnpCandidates = @()
try {
    $pnpCandidates = @(
        Get-PnpDevice -PresentOnly -ErrorAction Stop |
            Where-Object {
                $_.FriendlyName -match
                    '(?i)L1 Pro|TinyUSB|Tracker L1|nRF52|Nordic'
            } |
            ForEach-Object {
                [pscustomobject]@{
                    FriendlyName = $_.FriendlyName
                    Class        = $_.Class
                    Status       = $_.Status
                }
            }
    )
}
catch {
    $pnpQuerySucceeded = $false
    $pnpQueryError = $_.Exception.Message
}

$registryQuerySucceeded = $true
$registryQueryError = $null
$registryCandidates = @()
try {
    $registryNames = [System.Collections.Generic.List[string]]::new()
    $usbRoot =
        'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB'
    foreach ($deviceType in Get-ChildItem -LiteralPath $usbRoot) {
        foreach ($instance in Get-ChildItem -LiteralPath $deviceType.PSPath) {
            $properties = Get-ItemProperty -LiteralPath $instance.PSPath
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
            if ($name -match '(?i)L1 Pro|TinyUSB|Tracker L1|nRF52|Nordic') {
                # Do not output registry key paths or instance IDs: those can
                # contain a device-specific serial number.
                $registryNames.Add(($name -replace '^.*;', ''))
            }
        }
    }
    $registryCandidates = @($registryNames | Sort-Object -Unique)
}
catch {
    $registryQuerySucceeded = $false
    $registryQueryError = $_.Exception.Message
}

$candidateCount = $pnpCandidates.Count + $registryCandidates.Count

[pscustomobject]@{
    CheckedAtUtc       = [DateTime]::UtcNow.ToString('o')
    SerialPorts        = $serialPorts
    TrackerDfuVolumes  = $dfuVolumes
    PnpQuerySucceeded  = $pnpQuerySucceeded
    PnpCandidates      = $pnpCandidates
    PnpQueryError      = $pnpQueryError
    RegistryQuerySucceeded = $registryQuerySucceeded
    RegistryCandidates = $registryCandidates
    RegistryQueryError = $registryQueryError
    StateChangesMade   = $false
    Interpretation     = if ($dfuVolumes.Count -gt 0) {
        'TRACKER L1 DFU storage is mounted; no files were read or written.'
    }
    elseif ($candidateCount -gt 0) {
        'A possible Wio/nRF/TinyUSB device is present; confirm its screen and label. Registry output is redacted.'
    }
    else {
        'No Wio-specific candidate was identified. A blocked PnP query or normal BLE-only runtime can make this inconclusive.'
    }
}
