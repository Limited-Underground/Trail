[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ArchivePath,

    [switch]$SkipLaunch,

    [switch]$RunUiAutomationAcceptance,

    [ValidateRange(1, 10)]
    [int]$ExpectedDeviceCount = 3,

    [ValidateRange(1, 10)]
    [int]$RefreshCycles = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($RunUiAutomationAcceptance -and $SkipLaunch) {
    throw 'External UI Automation acceptance requires launch verification.'
}
if ($RunUiAutomationAcceptance) {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
}

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

function Wait-ForUiAutomationCondition {
    param(
        [Parameter(Mandatory)][scriptblock]$Condition,
        [Parameter(Mandatory)][string]$FailureMessage,
        [int]$TimeoutMilliseconds = 30000
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    do {
        try {
            if (& $Condition) {
                return
            }
        }
        catch [System.Windows.Automation.ElementNotAvailableException] {
            # The packaged window can replace item peers while refresh is active.
        }
        Start-Sleep -Milliseconds 100
    }
    while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMilliseconds)

    throw $FailureMessage
}

function Get-UiAutomationElement {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Root,
        [Parameter(Mandatory)][string]$AutomationId
    )

    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty,
        $AutomationId)
    return $Root.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $condition)
}

function Test-UiAutomationPattern {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Element,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationPattern]$Pattern
    )

    $provider = $null
    return $Element.TryGetCurrentPattern($Pattern, [ref]$provider)
}

function Get-DeviceListItems {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$List
    )

    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ListItem)
    return $List.FindAll(
        [System.Windows.Automation.TreeScope]::Children,
        $condition)
}

function Get-OptionalUiAutomationLiveSetting {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Element
    )

    try {
        $value = $Element.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElementIdentifiers]::LiveSettingProperty,
            $true)
        if ($value -is [System.ArgumentException] -or
            [object]::ReferenceEquals(
                $value,
                [System.Windows.Automation.AutomationElement]::NotSupported)) {
            return $null
        }
        return $value
    }
    catch [System.ArgumentException] {
        return $null
    }
}

function Assert-UiAutomationDeviceItems {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$List,
        [Parameter(Mandatory)][int]$ExpectedCount
    )

    $items = Get-DeviceListItems -List $List
    if ($items.Count -ne $ExpectedCount) {
        throw "Expected $ExpectedCount packaged device items, found $($items.Count)."
    }

    for ($index = 0; $index -lt $items.Count; $index++) {
        $item = $items[$index]
        $name = [string]$item.Current.Name
        $help = [string]$item.Current.HelpText
        $position = $item.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElement]::PositionInSetProperty)
        $size = $item.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElement]::SizeOfSetProperty)
        if ($item.Current.ControlType -ne [System.Windows.Automation.ControlType]::ListItem -or
            -not $item.Current.IsEnabled -or
            -not $item.Current.IsKeyboardFocusable -or
            [string]::IsNullOrWhiteSpace($name) -or
            [string]::IsNullOrWhiteSpace($help) -or
            -not (Test-UiAutomationPattern -Element $item -Pattern (
                [System.Windows.Automation.SelectionItemPattern]::Pattern)) -or
            -not (Test-UiAutomationPattern -Element $item -Pattern (
                [System.Windows.Automation.ScrollItemPattern]::Pattern)) -or
            [int]$position -ne ($index + 1) -or
            [int]$size -ne $ExpectedCount) {
            throw "Packaged device item $($index + 1) violates its accessible list-item contract."
        }

        $publicCopy = "$name $help"
        if ($publicCopy -match '(?i)usb_candidate_|\bCOM\d{1,3}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
            throw "Packaged device item $($index + 1) exposed a private or internal identifier."
        }

        $flashCondition = New-Object System.Windows.Automation.AndCondition(
            (New-Object System.Windows.Automation.PropertyCondition(
                [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                [System.Windows.Automation.ControlType]::Button)),
            (New-Object System.Windows.Automation.PropertyCondition(
                [System.Windows.Automation.AutomationElement]::NameProperty,
                'Flash')))
        $cardFlash = $item.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants,
            $flashCondition)
        if ($null -eq $cardFlash -or
            $cardFlash.Current.IsEnabled -or
            [string]::IsNullOrWhiteSpace([string]$cardFlash.Current.HelpText)) {
            throw "Packaged device item $($index + 1) did not expose a disabled, explained Flash action."
        }
    }

    return $items
}

function Assert-NoPrivateUiAutomationCopy {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Window
    )

    $elements = $Window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $elements) {
        $publicCopy = "$($element.Current.Name) $($element.Current.HelpText)"
        if ($publicCopy -match '(?i)usb_candidate_|\bCOM\d{1,3}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
            throw 'The packaged accessibility tree exposed a private or internal identifier.'
        }
    }
}

function Invoke-PackagedUiAutomationAcceptance {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory)][int]$ExpectedCount,
        [Parameter(Mandatory)][int]$Cycles
    )

    $windowHolder = @{ Value = $null }
    $processCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
        $Process.Id)
    Wait-ForUiAutomationCondition -FailureMessage (
        'The packaged utility did not expose a top-level UI Automation window.') -Condition {
        $windowHolder.Value = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
            [System.Windows.Automation.TreeScope]::Children,
            $processCondition)
        return $null -ne $windowHolder.Value
    }
    $window = $windowHolder.Value

    Wait-ForUiAutomationCondition -FailureMessage (
        'The packaged utility did not publish its ready accessibility surface.') -Condition {
        $readyRefresh = Get-UiAutomationElement -Root $window -AutomationId 'refresh-devices'
        $readySummary = Get-UiAutomationElement -Root $window -AutomationId 'inspection-summary'
        $readySelection = Get-UiAutomationElement -Root $window -AutomationId 'device-selection-status'
        $readyList = Get-UiAutomationElement -Root $window -AutomationId 'connected-device-candidates'
        $readyBundle = Get-UiAutomationElement -Root $window -AutomationId 'bundle-summary'
        $readySelectBundle = Get-UiAutomationElement -Root $window -AutomationId 'select-firmware-bundle'
        $readyFlash = Get-UiAutomationElement -Root $window -AutomationId 'flash-selected-device'
        return $null -ne $readyRefresh -and $readyRefresh.Current.IsEnabled -and
            $null -ne $readySummary -and $readySummary.Current.Name -like '*0 ready to flash*' -and
            $null -ne $readySelection -and
            $null -ne $readyList -and
            $null -ne $readyBundle -and
            $null -ne $readySelectBundle -and
            $null -ne $readyFlash -and
            (Get-DeviceListItems -List $readyList).Count -eq $ExpectedCount
    }

    $refresh = Get-UiAutomationElement -Root $window -AutomationId 'refresh-devices'
    $summary = Get-UiAutomationElement -Root $window -AutomationId 'inspection-summary'
    $selectionStatus = Get-UiAutomationElement -Root $window -AutomationId 'device-selection-status'
    $deviceList = Get-UiAutomationElement -Root $window -AutomationId 'connected-device-candidates'
    $bundleSummary = Get-UiAutomationElement -Root $window -AutomationId 'bundle-summary'
    $selectBundle = Get-UiAutomationElement -Root $window -AutomationId 'select-firmware-bundle'
    $flashSelected = Get-UiAutomationElement -Root $window -AutomationId 'flash-selected-device'

    $contractFailures = @()
    if ($window.Current.ControlType -ne [System.Windows.Automation.ControlType]::Window -or
        $window.Current.AutomationId -ne 'device-utility-window' -or
        $window.Current.Name -ne 'Limited Underground Trail Device Utility' -or
        -not (Test-UiAutomationPattern -Element $window -Pattern (
            [System.Windows.Automation.WindowPattern]::Pattern))) {
        $contractFailures += 'window identity/type/pattern'
    }
    if ($refresh.Current.ControlType -ne [System.Windows.Automation.ControlType]::Button -or
        $refresh.Current.Name -ne 'Refresh connected devices' -or
        [string]::IsNullOrWhiteSpace([string]$refresh.Current.HelpText) -or
        -not (Test-UiAutomationPattern -Element $refresh -Pattern (
            [System.Windows.Automation.InvokePattern]::Pattern))) {
        $contractFailures += 'Refresh name/help/type/pattern'
    }
    if ($summary.Current.ControlType -ne [System.Windows.Automation.ControlType]::Text -or
        [string]::IsNullOrWhiteSpace([string]$summary.Current.HelpText)) {
        $contractFailures += 'summary help/type'
    }
    $summaryLiveSetting = Get-OptionalUiAutomationLiveSetting -Element $summary
    $selectionLiveSetting = Get-OptionalUiAutomationLiveSetting -Element $selectionStatus
    $bundleLiveSetting = Get-OptionalUiAutomationLiveSetting -Element $bundleSummary
    $anyLiveSetting = $null -ne $summaryLiveSetting -or
        $null -ne $selectionLiveSetting -or
        $null -ne $bundleLiveSetting
    $allLiveSettings = $null -ne $summaryLiveSetting -and
        $null -ne $selectionLiveSetting -and
        $null -ne $bundleLiveSetting
    if (($anyLiveSetting -and -not $allLiveSettings) -or
        ($allLiveSettings -and (
            [int]$summaryLiveSetting -ne [int][System.Windows.Automation.AutomationLiveSetting]::Polite -or
            [int]$selectionLiveSetting -ne [int][System.Windows.Automation.AutomationLiveSetting]::Polite -or
            [int]$bundleLiveSetting -ne [int][System.Windows.Automation.AutomationLiveSetting]::Polite))) {
        $contractFailures += "live settings ($summaryLiveSetting/$selectionLiveSetting/$bundleLiveSetting)"
    }
    if ($deviceList.Current.ControlType -ne [System.Windows.Automation.ControlType]::List -or
        $deviceList.Current.Name -ne 'Connected device candidates' -or
        [string]::IsNullOrWhiteSpace([string]$deviceList.Current.HelpText) -or
        -not (Test-UiAutomationPattern -Element $deviceList -Pattern (
            [System.Windows.Automation.SelectionPattern]::Pattern))) {
        $contractFailures += 'device-list name/help/type/pattern'
    }
    if ($selectBundle.Current.ControlType -ne [System.Windows.Automation.ControlType]::Button -or
        $selectBundle.Current.Name -ne 'Select firmware bundle' -or
        -not (Test-UiAutomationPattern -Element $selectBundle -Pattern (
            [System.Windows.Automation.InvokePattern]::Pattern)) -or
        $selectBundle.Current.IsEnabled) {
        $contractFailures += 'initial bundle-action name/state/type/pattern'
    }
    if ($flashSelected.Current.ControlType -ne [System.Windows.Automation.ControlType]::Button -or
        $flashSelected.Current.Name -ne 'Flash selected device' -or
        -not (Test-UiAutomationPattern -Element $flashSelected -Pattern (
            [System.Windows.Automation.InvokePattern]::Pattern)) -or
        $flashSelected.Current.IsEnabled -or
        [string]::IsNullOrWhiteSpace([string]$flashSelected.Current.HelpText)) {
        $contractFailures += 'Flash name/help/state/type/pattern'
    }
    if ($contractFailures.Count -ne 0) {
        throw "The packaged utility violates its external accessibility control contract: $($contractFailures -join '; ')."
    }

    $selectionProvider = $deviceList.GetCurrentPattern(
        [System.Windows.Automation.SelectionPattern]::Pattern) -as
        [System.Windows.Automation.SelectionPattern]
    if ($selectionProvider.Current.CanSelectMultiple -or
        $selectionProvider.Current.IsSelectionRequired -or
        $selectionProvider.Current.GetSelection().Count -ne 0) {
        throw 'The packaged device list did not expose empty single optional selection.'
    }

    $initialItems = Assert-UiAutomationDeviceItems `
        -List $deviceList `
        -ExpectedCount $ExpectedCount
    $expectedPublicNames = @($initialItems | ForEach-Object {
        [string]$_.Current.Name
    } | Sort-Object)
    Assert-NoPrivateUiAutomationCopy -Window $window

    for ($cycle = 0; $cycle -lt $Cycles; $cycle++) {
        $items = Assert-UiAutomationDeviceItems -List $deviceList -ExpectedCount $ExpectedCount
        $target = $items[$cycle % $items.Count]
        $selectionItem = $target.GetCurrentPattern(
            [System.Windows.Automation.SelectionItemPattern]::Pattern) -as
            [System.Windows.Automation.SelectionItemPattern]
        $selectionItem.Select()
        Wait-ForUiAutomationCondition -FailureMessage (
            "Packaged UI Automation selection did not settle in cycle $($cycle + 1).") -Condition {
            $selectionStatus.Current.Name -like 'Selected:*' -and
                $selectBundle.Current.IsEnabled -and
                -not $flashSelected.Current.IsEnabled -and
                $selectionProvider.Current.GetSelection().Count -eq 1
        }

        $invoke = $refresh.GetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern) -as
            [System.Windows.Automation.InvokePattern]
        $invoke.Invoke()
        Wait-ForUiAutomationCondition -TimeoutMilliseconds 10000 -FailureMessage (
            "Packaged refresh did not enter its busy state in cycle $($cycle + 1).") -Condition {
            -not $refresh.Current.IsEnabled -or
                $summary.Current.Name -eq 'Looking for connected devices…'
        }
        Wait-ForUiAutomationCondition -FailureMessage (
            "Packaged refresh did not restore its safe ready state in cycle $($cycle + 1).") -Condition {
            $refresh.Current.IsEnabled -and
                $summary.Current.Name -like '*0 ready to flash*' -and
                $selectionStatus.Current.Name -like 'Select one connected device*' -and
                $bundleSummary.Current.Name -eq 'No firmware bundle selected' -and
                -not $selectBundle.Current.IsEnabled -and
                -not $flashSelected.Current.IsEnabled -and
                $selectionProvider.Current.GetSelection().Count -eq 0 -and
                (Get-DeviceListItems -List $deviceList).Count -eq $ExpectedCount
        }
        $refreshedItems = Assert-UiAutomationDeviceItems `
            -List $deviceList `
            -ExpectedCount $ExpectedCount
        $refreshedPublicNames = @($refreshedItems | ForEach-Object {
            [string]$_.Current.Name
        } | Sort-Object)
        $rosterDifference = @(Compare-Object `
            -ReferenceObject $expectedPublicNames `
            -DifferenceObject $refreshedPublicNames `
            -CaseSensitive)
        if ($rosterDifference.Count -ne 0) {
            throw "Packaged refresh changed the public device roster in cycle $($cycle + 1)."
        }
        Assert-NoPrivateUiAutomationCopy -Window $window
    }

    return $Cycles
}

$previousAppData = $env:APPDATA
$verificationProcess = $null
$uiAutomationResult = 'not requested'

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
        if ($RunUiAutomationAcceptance) {
            $acceptedCycles = Invoke-PackagedUiAutomationAcceptance `
                -Process $verificationProcess `
                -ExpectedCount $ExpectedDeviceCount `
                -Cycles $RefreshCycles
            $uiAutomationResult = "passed ($acceptedCycles refresh cycles)"
        }
        else {
            Start-Sleep -Seconds 3
            if ($verificationProcess.HasExited) {
                throw 'The independently extracted utility exited during launch verification.'
            }
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
        ExternalUiAutomation = $uiAutomationResult
    }
}
finally {
    Stop-VerificationProcess -Process $verificationProcess
    $env:APPDATA = $previousAppData
    if (Test-Path -LiteralPath $verificationRoot) {
        Remove-Item -LiteralPath $verificationRoot -Recurse -Force
    }
}
