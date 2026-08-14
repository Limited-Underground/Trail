[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ArchivePath,

    [switch]$SkipLaunch,

    [switch]$RunUiAutomationAcceptance,

    [ValidateRange(1, 10)]
    [int]$ExpectedDeviceCount = 3,

    [string]$ExpectedDeviceDisplayNames = '',

    [ValidateRange(1, 10)]
    [int]$RefreshCycles = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($RunUiAutomationAcceptance -and $SkipLaunch) {
    throw 'External UI Automation acceptance requires launch verification.'
}
$expectedDeviceDisplayNameList = @()
if (-not [string]::IsNullOrWhiteSpace($ExpectedDeviceDisplayNames)) {
    if (-not $RunUiAutomationAcceptance) {
        throw 'Expected device display names require external UI Automation acceptance.'
    }
    $expectedDeviceDisplayNameList = @($ExpectedDeviceDisplayNames.Split('|'))
    if (
        $expectedDeviceDisplayNameList.Count -ne $ExpectedDeviceCount -or
        @($expectedDeviceDisplayNameList | Where-Object {
            [string]::IsNullOrWhiteSpace($_) -or
            $_ -ne $_.Trim() -or
            $_.Length -gt 120 -or
            $_ -match '[\x00-\x1f\x7f]'
        }).Count -ne 0
    ) {
        throw 'ExpectedDeviceDisplayNames must contain one exact public display name per expected device, separated by |.'
    }
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

function Initialize-UiAutomationLiveRegionProbe {
    if ('OtLiveRegionProbe' -as [type]) {
        return
    }

    $source = @'
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Windows.Automation;

public sealed class OtLiveRegionRecord
{
    public long Sequence;
    public int ThreadId;
    public string AutomationId;
    public string Name;
    public string Error;
}

public static class OtLiveRegionProbe
{
    private static readonly ConcurrentQueue<OtLiveRegionRecord> Records =
        new ConcurrentQueue<OtLiveRegionRecord>();
    private static long sequence;

    public static readonly AutomationEventHandler Handler = OnEvent;

    public static void Reset()
    {
        OtLiveRegionRecord ignored;
        while (Records.TryDequeue(out ignored)) { }
        Interlocked.Exchange(ref sequence, 0);
    }

    private static void OnEvent(object sender, AutomationEventArgs args)
    {
        var record = new OtLiveRegionRecord
        {
            Sequence = Interlocked.Increment(ref sequence),
            ThreadId = Thread.CurrentThread.ManagedThreadId,
        };

        try
        {
            var element = sender as AutomationElement;
            if (element == null)
            {
                record.Error = "sender-not-element";
            }
            else
            {
                record.AutomationId = Convert.ToString(
                    element.GetCurrentPropertyValue(
                        AutomationElement.AutomationIdProperty,
                        true));
                record.Name = Convert.ToString(
                    element.GetCurrentPropertyValue(
                        AutomationElement.NameProperty,
                        true));
            }
        }
        catch (Exception error)
        {
            record.Error = error.GetType().FullName + ": " + error.Message;
        }

        Records.Enqueue(record);
    }

    public static OtLiveRegionRecord[] Drain()
    {
        var result = new List<OtLiveRegionRecord>();
        OtLiveRegionRecord record;
        while (Records.TryDequeue(out record))
        {
            result.Add(record);
        }
        return result.ToArray();
    }
}
'@

    $clientAssembly =
        [System.Windows.Automation.AutomationElement].Assembly.Location
    $typesAssembly =
        [System.Windows.Automation.AutomationElementIdentifiers].Assembly.Location
    Add-Type -TypeDefinition $source -ReferencedAssemblies @(
        $clientAssembly,
        $typesAssembly,
        'System.dll',
        'System.Core.dll'
    )
}

function Initialize-WindowDpiProbe {
    if ('OtWindowDpiProbe' -as [type]) {
        return
    }

    $source = @'
using System;
using System.Runtime.InteropServices;

public static class OtWindowDpiProbe
{
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr windowHandle);

    public static uint GetDpi(int nativeWindowHandle)
    {
        if (nativeWindowHandle == 0)
        {
            throw new ArgumentException(
                "A nonzero native window handle is required.",
                "nativeWindowHandle");
        }

        var dpi = GetDpiForWindow(new IntPtr(nativeWindowHandle));
        if (dpi < 48 || dpi > 768)
        {
            throw new InvalidOperationException(
                "Windows did not return a usable DPI for the packaged window.");
        }
        return dpi;
    }
}
'@

    Add-Type -TypeDefinition $source
}

function Assert-LiveRegionSequence {
    param(
        [Parameter(Mandatory)][object[]]$Actual,
        [Parameter(Mandatory)][object[]]$Expected,
        [Parameter(Mandatory)][string]$Context
    )

    $allowedIds = @(
        'inspection-summary',
        'device-selection-status',
        'bundle-summary'
    )
    $collapsed = New-Object System.Collections.Generic.List[object]
    $previousSignature = $null
    foreach ($record in @($Actual | Sort-Object Sequence)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$record.Error)) {
            throw "$Context callback failed: $($record.Error)"
        }

        $id = [string]$record.AutomationId
        $name = [string]$record.Name
        if ($allowedIds -notcontains $id) {
            throw "$Context raised unexpected live region '$id'."
        }
        if ([string]::IsNullOrWhiteSpace($name)) {
            throw "$Context raised '$id' without an accessible name."
        }
        if ($name -match '(?i)usb_candidate_|\bCOM\d{1,4}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
            throw "$Context exposed a private identifier."
        }

        $signature = "$id`0$name"
        if ($signature -ne $previousSignature) {
            $collapsed.Add([pscustomobject]@{
                Id = $id
                Name = $name
            })
            $previousSignature = $signature
        }
    }

    if ($collapsed.Count -ne $Expected.Count) {
        throw "$Context produced $($collapsed.Count) live events; expected $($Expected.Count)."
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($collapsed[$index].Id -ne $Expected[$index].Id -or
            $collapsed[$index].Name -ne $Expected[$index].Name) {
            throw "$Context live event $($index + 1) was '$($collapsed[$index].Id): $($collapsed[$index].Name)'; expected '$($Expected[$index].Id): $($Expected[$index].Name)'."
        }
    }
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

function Assert-NoCurrentInspectionErrorPeer {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Window,
        [Parameter(Mandatory)][string]$Context
    )

    $currentError = Get-UiAutomationElement `
        -Root $Window `
        -AutomationId 'inspection-error'
    if ($null -ne $currentError) {
        throw "$Context exposed a current inspection-error peer."
    }

    $staleErrorName =
        'Device inspection could not complete. No device or firmware setting was changed.'
    $staleErrorHelp = 'Current connected-device inspection error.'
    $elements = $Window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $elements) {
        $liveSetting = Get-OptionalUiAutomationLiveSetting -Element $element
        $name = [string]$element.Current.Name
        $help = [string]$element.Current.HelpText
        if (($null -ne $liveSetting -and
                [int]$liveSetting -eq
                    [int][System.Windows.Automation.AutomationLiveSetting]::Assertive) -or
            [string]::Equals(
                $name,
                $staleErrorName,
                [System.StringComparison]::Ordinal) -or
            [string]::Equals(
                $help,
                $staleErrorHelp,
                [System.StringComparison]::Ordinal)) {
            throw "$Context exposed assertive or stale inspection-error content."
        }
    }
}

function Get-UiAutomationHeadingLevel {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Element
    )

    $value = $Element.GetCurrentPropertyValue(
        [System.Windows.Automation.AutomationElementIdentifiers]::HeadingLevelProperty,
        $true)
    if ($value -is [System.ArgumentException] -or
        [object]::ReferenceEquals(
            $value,
            [System.Windows.Automation.AutomationElement]::NotSupported)) {
        return [int][System.Windows.Automation.AutomationHeadingLevel]::None
    }
    return [int]$value
}

function Assert-UiAutomationHeadingSequence {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Window,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Summary,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$DeviceList,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$BundleSummary,
        [Parameter(Mandatory)][int]$ExpectedCount,
        [Parameter(Mandatory)][string]$Context
    )

    $expected = @(
        [pscustomobject]@{
            Level = [int][System.Windows.Automation.AutomationHeadingLevel]::Level1
            Name = 'Limited Underground Trail Device Utility'
        },
        [pscustomobject]@{
            Level = [int][System.Windows.Automation.AutomationHeadingLevel]::Level2
            Name = [string]$Summary.Current.Name
        }
    )
    $items = Get-DeviceListItems -List $DeviceList
    if ($items.Count -ne $ExpectedCount) {
        throw "$Context could not derive headings for the expected device roster."
    }
    foreach ($item in $items) {
        $itemElements = $item.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        $itemHeadings = @($itemElements | Where-Object {
            (Get-UiAutomationHeadingLevel -Element $_) -eq
                [int][System.Windows.Automation.AutomationHeadingLevel]::Level3
        })
        if ($itemHeadings.Count -ne 1 -or
            [string]::IsNullOrWhiteSpace([string]$itemHeadings[0].Current.Name)) {
            throw "$Context device item did not expose exactly one named level 3 heading."
        }
        $headingName = [string]$itemHeadings[0].Current.Name
        $itemName = [string]$item.Current.Name
        if (-not $itemName.StartsWith(
                "$headingName.",
                [System.StringComparison]::Ordinal)) {
            throw "$Context device heading did not match its enclosing public item summary."
        }
        $expected += [pscustomobject]@{
            Level = [int][System.Windows.Automation.AutomationHeadingLevel]::Level3
            Name = $headingName
        }
    }
    $expected += @(
        [pscustomobject]@{
            Level = [int][System.Windows.Automation.AutomationHeadingLevel]::Level2
            Name = [string]$BundleSummary.Current.Name
        },
        [pscustomobject]@{
            Level = [int][System.Windows.Automation.AutomationHeadingLevel]::Level2
            Name = 'SAFE MODE'
        }
    )

    $allElements = $Window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    $actual = @()
    foreach ($element in $allElements) {
        $level = Get-UiAutomationHeadingLevel -Element $element
        if ($level -eq
            [int][System.Windows.Automation.AutomationHeadingLevel]::None) {
            continue
        }
        $name = [string]$element.Current.Name
        if ($element.Current.ControlType -ne
                [System.Windows.Automation.ControlType]::Text -or
            [string]::IsNullOrWhiteSpace($name) -or
            $name -match '(?i)usb_candidate_|\bCOM\d{1,4}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
            throw "$Context exposed an invalid heading."
        }
        $actual += [pscustomobject]@{
            Level = $level
            Name = $name
        }
    }

    if ($actual.Count -ne $expected.Count) {
        throw "$Context exposed $($actual.Count) headings; expected $($expected.Count)."
    }
    for ($index = 0; $index -lt $expected.Count; $index++) {
        if ($actual[$index].Level -ne $expected[$index].Level -or
            $actual[$index].Name -ne $expected[$index].Name) {
            throw "$Context heading $($index + 1) was '$($actual[$index].Level): $($actual[$index].Name)'; expected '$($expected[$index].Level): $($expected[$index].Name)'."
        }
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
        if ($publicCopy -match '(?i)usb_candidate_|\bCOM\d{1,4}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
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
        if ($publicCopy -match '(?i)usb_candidate_|\bCOM\d{1,4}\b|\bVID_[0-9a-f]{4}\b|\bPID_[0-9a-f]{4}\b|USB\\VID_|\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b') {
            throw 'The packaged accessibility tree exposed a private or internal identifier.'
        }
    }
}

function Test-UiAutomationPositiveRectangleOverlap {
    param(
        [Parameter(Mandatory)]$First,
        [Parameter(Mandatory)]$Second
    )

    if ($First.IsEmpty -or $Second.IsEmpty -or
        [double]::IsNaN([double]$First.Width) -or
        [double]::IsNaN([double]$First.Height) -or
        [double]::IsNaN([double]$Second.Width) -or
        [double]::IsNaN([double]$Second.Height) -or
        [double]::IsInfinity([double]$First.Width) -or
        [double]::IsInfinity([double]$First.Height) -or
        [double]::IsInfinity([double]$Second.Width) -or
        [double]::IsInfinity([double]$Second.Height)) {
        return $false
    }

    $overlapWidth = [Math]::Min(
        [double]$First.Right,
        [double]$Second.Right) - [Math]::Max(
            [double]$First.Left,
            [double]$Second.Left)
    $overlapHeight = [Math]::Min(
        [double]$First.Bottom,
        [double]$Second.Bottom) - [Math]::Max(
            [double]$First.Top,
            [double]$Second.Top)
    return $overlapWidth -gt 0.5 -and $overlapHeight -gt 0.5
}

function Wait-ForPackagedWindowPixelSize {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Window,
        [Parameter(Mandatory)][double]$ExpectedWidth,
        [Parameter(Mandatory)][double]$ExpectedHeight,
        [Parameter(Mandatory)][double]$Tolerance,
        [Parameter(Mandatory)][string]$FailureMessage
    )

    Wait-ForUiAutomationCondition `
        -TimeoutMilliseconds 10000 `
        -FailureMessage $FailureMessage `
        -Condition {
            $bounds = $Window.Current.BoundingRectangle
            return -not $bounds.IsEmpty -and
                [Math]::Abs([double]$bounds.Width - $ExpectedWidth) -le $Tolerance -and
                [Math]::Abs([double]$bounds.Height - $ExpectedHeight) -le $Tolerance
        }
}

function Invoke-PackagedMinimumWindowScrollAcceptance {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Window,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Refresh,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Summary,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$DeviceList,
        [Parameter(Mandatory)]
        [System.Windows.Automation.SelectionPattern]$SelectionProvider,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$SelectBundle,
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$FlashSelected,
        [Parameter(Mandatory)][int]$ExpectedCount
    )

    Initialize-WindowDpiProbe
    $windowPattern = $Window.GetCurrentPattern(
        [System.Windows.Automation.WindowPattern]::Pattern) -as
        [System.Windows.Automation.WindowPattern]
    $transformPattern = $Window.GetCurrentPattern(
        [System.Windows.Automation.TransformPattern]::Pattern) -as
        [System.Windows.Automation.TransformPattern]
    if ($null -eq $windowPattern -or
        $null -eq $transformPattern -or
        -not $transformPattern.Current.CanResize) {
        throw 'The packaged window does not expose a resizable Window/Transform contract.'
    }

    $originalVisualState = $windowPattern.Current.WindowVisualState
    $restoreBounds = $null
    $readySummaryName = $null
    $primaryFailure = $null
    try {
        if ($windowPattern.Current.WindowVisualState -ne
            [System.Windows.Automation.WindowVisualState]::Normal) {
            $windowPattern.SetWindowVisualState(
                [System.Windows.Automation.WindowVisualState]::Normal)
            Wait-ForUiAutomationCondition `
                -TimeoutMilliseconds 10000 `
                -FailureMessage 'The packaged window did not enter its normal resizable state.' `
                -Condition {
                    $windowPattern.Current.WindowVisualState -eq
                        [System.Windows.Automation.WindowVisualState]::Normal
                }
        }

        $restoreBounds = $Window.Current.BoundingRectangle
        if ($restoreBounds.IsEmpty -or
            [double]$restoreBounds.Width -le 0 -or
            [double]$restoreBounds.Height -le 0) {
            throw 'The packaged window did not expose restorable bounds.'
        }

        if ($SelectionProvider.Current.GetSelection().Count -ne 0) {
            throw 'The minimum-window acceptance did not start with empty device selection.'
        }

        $nativeWindowHandle = [int]$Window.Current.NativeWindowHandle
        $resizeDpi = [double][OtWindowDpiProbe]::GetDpi($nativeWindowHandle)
        $targetPixelWidth = [Math]::Round(900.0 * $resizeDpi / 96.0)
        $targetPixelHeight = [Math]::Round(620.0 * $resizeDpi / 96.0)
        $pixelTolerance = [Math]::Max(2.0, $resizeDpi / 48.0)
        $transformPattern.Resize($targetPixelWidth, $targetPixelHeight)
        Wait-ForPackagedWindowPixelSize `
            -Window $Window `
            -ExpectedWidth $targetPixelWidth `
            -ExpectedHeight $targetPixelHeight `
            -Tolerance $pixelTolerance `
            -FailureMessage 'The packaged window did not reach its DPI-scaled 900 x 620 minimum.'

        $effectiveDpi = [double][OtWindowDpiProbe]::GetDpi(
            [int]$Window.Current.NativeWindowHandle)
        $minimumBounds = $Window.Current.BoundingRectangle
        $effectiveWidth = [double]$minimumBounds.Width * 96.0 / $effectiveDpi
        $effectiveHeight = [double]$minimumBounds.Height * 96.0 / $effectiveDpi
        if ([Math]::Abs($effectiveWidth - 900.0) -gt 1.0 -or
            [Math]::Abs($effectiveHeight - 620.0) -gt 1.0) {
            throw "The packaged window effective size was $([Math]::Round($effectiveWidth, 2)) x $([Math]::Round($effectiveHeight, 2)) DIP; expected 900 x 620 DIP."
        }

        $Refresh.SetFocus()
        Wait-ForUiAutomationCondition `
            -TimeoutMilliseconds 10000 `
            -FailureMessage 'The packaged minimum window did not focus Refresh.' `
            -Condition { $Refresh.Current.HasKeyboardFocus }

        $readySummaryName = [string]$Summary.Current.Name
        if (-not $Refresh.Current.IsEnabled -or
            $readySummaryName -notlike '*0 ready to flash*' -or
            $SelectionProvider.Current.GetSelection().Count -ne 0 -or
            $SelectBundle.Current.IsEnabled -or
            $FlashSelected.Current.IsEnabled) {
            throw 'The packaged minimum window did not begin in its zero-ready fail-closed state.'
        }

        $items = Assert-UiAutomationDeviceItems `
            -List $DeviceList `
            -ExpectedCount $ExpectedCount
        $target = $items[$items.Count - 1]
        Wait-ForUiAutomationCondition `
            -TimeoutMilliseconds 10000 `
            -FailureMessage 'The final current packaged device item was not genuinely offscreen at 900 x 620 DIP.' `
            -Condition { $target.Current.IsOffscreen }

        $targetName = [string]$target.Current.Name
        $targetHelp = [string]$target.Current.HelpText
        $targetPosition = [int]$target.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElement]::PositionInSetProperty)
        $targetSetSize = [int]$target.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElement]::SizeOfSetProperty)
        $scrollItem = $target.GetCurrentPattern(
            [System.Windows.Automation.ScrollItemPattern]::Pattern) -as
            [System.Windows.Automation.ScrollItemPattern]
        if ($null -eq $scrollItem) {
            throw 'The offscreen current packaged device item did not expose ScrollItem.'
        }

        Assert-NoPrivateUiAutomationCopy -Window $Window
        Assert-NoCurrentInspectionErrorPeer `
            -Window $Window `
            -Context 'Packaged minimum-window ready state'
        $scrollItem.ScrollIntoView()
        Wait-ForUiAutomationCondition `
            -TimeoutMilliseconds 10000 `
            -FailureMessage 'The packaged ScrollItem provider did not settle with its item visible and authority unchanged.' `
            -Condition {
                $candidateBounds = $target.Current.BoundingRectangle
                $candidateWindowBounds = $Window.Current.BoundingRectangle
                return -not $target.Current.IsOffscreen -and
                    (Test-UiAutomationPositiveRectangleOverlap `
                        -First $candidateBounds `
                        -Second $candidateWindowBounds) -and
                    [string]::Equals(
                        [string]$target.Current.Name,
                        $targetName,
                        [System.StringComparison]::Ordinal) -and
                    [string]::Equals(
                        [string]$target.Current.HelpText,
                        $targetHelp,
                        [System.StringComparison]::Ordinal) -and
                    [int]$target.GetCurrentPropertyValue(
                        [System.Windows.Automation.AutomationElement]::PositionInSetProperty) -eq
                            $targetPosition -and
                    [int]$target.GetCurrentPropertyValue(
                        [System.Windows.Automation.AutomationElement]::SizeOfSetProperty) -eq
                            $targetSetSize -and
                    $SelectionProvider.Current.GetSelection().Count -eq 0 -and
                    $Refresh.Current.IsEnabled -and
                    $Refresh.Current.HasKeyboardFocus -and
                    -not $SelectBundle.Current.IsEnabled -and
                    -not $FlashSelected.Current.IsEnabled -and
                    [string]::Equals(
                        [string]$Summary.Current.Name,
                        $readySummaryName,
                        [System.StringComparison]::Ordinal)
            }

        $settledBounds = $target.Current.BoundingRectangle
        $windowBounds = $Window.Current.BoundingRectangle
        $scrollStateFailures = @()
        if ($target.Current.IsOffscreen) {
            $scrollStateFailures += 'offscreen'
        }
        if (-not (Test-UiAutomationPositiveRectangleOverlap `
                -First $settledBounds `
                -Second $windowBounds)) {
            $scrollStateFailures += 'viewport_overlap'
        }
        if (-not [string]::Equals(
                [string]$target.Current.Name,
                $targetName,
                [System.StringComparison]::Ordinal)) {
            $scrollStateFailures += 'public_name'
        }
        if (-not [string]::Equals(
                [string]$target.Current.HelpText,
                $targetHelp,
                [System.StringComparison]::Ordinal)) {
            $scrollStateFailures += 'public_help'
        }
        if ([int]$target.GetCurrentPropertyValue(
                [System.Windows.Automation.AutomationElement]::PositionInSetProperty) -ne
                $targetPosition) {
            $scrollStateFailures += 'position_in_set'
        }
        if ([int]$target.GetCurrentPropertyValue(
                [System.Windows.Automation.AutomationElement]::SizeOfSetProperty) -ne
                $targetSetSize) {
            $scrollStateFailures += 'size_of_set'
        }
        if ($SelectionProvider.Current.GetSelection().Count -ne 0) {
            $scrollStateFailures += 'selection'
        }
        if (-not $Refresh.Current.IsEnabled) {
            $scrollStateFailures += 'refresh_enabled'
        }
        if (-not $Refresh.Current.HasKeyboardFocus) {
            $scrollStateFailures += 'refresh_focus'
        }
        if ($SelectBundle.Current.IsEnabled) {
            $scrollStateFailures += 'bundle_authority'
        }
        if ($FlashSelected.Current.IsEnabled) {
            $scrollStateFailures += 'flash_authority'
        }
        if (-not [string]::Equals(
                [string]$Summary.Current.Name,
                $readySummaryName,
                [System.StringComparison]::Ordinal)) {
            $scrollStateFailures += 'summary'
        }
        if ($scrollStateFailures.Count -ne 0) {
            throw "Packaged ScrollItem changed: $($scrollStateFailures -join ', ')."
        }

        [void](Assert-UiAutomationDeviceItems `
            -List $DeviceList `
            -ExpectedCount $ExpectedCount)
        Assert-NoPrivateUiAutomationCopy -Window $Window
        Assert-NoCurrentInspectionErrorPeer `
            -Window $Window `
            -Context 'Packaged minimum-window ScrollItem state'
    }
    catch {
        $primaryFailure = $_
        throw
    }
    finally {
        if ($null -ne $originalVisualState) {
            try {
                if ($windowPattern.Current.WindowVisualState -ne
                    [System.Windows.Automation.WindowVisualState]::Normal) {
                    $windowPattern.SetWindowVisualState(
                        [System.Windows.Automation.WindowVisualState]::Normal)
                    Wait-ForUiAutomationCondition `
                        -TimeoutMilliseconds 10000 `
                        -FailureMessage 'The packaged window could not normalize for size restoration.' `
                        -Condition {
                            $windowPattern.Current.WindowVisualState -eq
                                [System.Windows.Automation.WindowVisualState]::Normal
                        }
                }
                if ($null -ne $restoreBounds) {
                    $restoreTolerance = 4.0
                    $transformPattern.Resize(
                        [double]$restoreBounds.Width,
                        [double]$restoreBounds.Height)
                    Wait-ForPackagedWindowPixelSize `
                        -Window $Window `
                        -ExpectedWidth ([double]$restoreBounds.Width) `
                        -ExpectedHeight ([double]$restoreBounds.Height) `
                        -Tolerance $restoreTolerance `
                        -FailureMessage 'The packaged window did not restore its original normal bounds.'
                }
                if ($originalVisualState -ne
                    [System.Windows.Automation.WindowVisualState]::Normal) {
                    $windowPattern.SetWindowVisualState($originalVisualState)
                    Wait-ForUiAutomationCondition `
                        -TimeoutMilliseconds 10000 `
                        -FailureMessage 'The packaged window did not restore its original visual state.' `
                        -Condition {
                            $windowPattern.Current.WindowVisualState -eq
                                $originalVisualState
                        }
                }
            }
            catch {
                if ($null -eq $primaryFailure) {
                    throw
                }
                try {
                    $primaryFailure.Exception.Data['WindowRestoreFailure'] =
                        $_.Exception.Message
                }
                catch {
                    # Preserve the primary acceptance failure even if annotation fails.
                }
            }
        }
    }

    Wait-ForUiAutomationCondition `
        -TimeoutMilliseconds 10000 `
        -FailureMessage 'Refresh focus was not retained after packaged window restoration.' `
        -Condition { $Refresh.Current.HasKeyboardFocus }
    if (-not $Refresh.Current.IsEnabled -or
        $SelectionProvider.Current.GetSelection().Count -ne 0 -or
        $SelectBundle.Current.IsEnabled -or
        $FlashSelected.Current.IsEnabled -or
        -not [string]::Equals(
            [string]$Summary.Current.Name,
            $readySummaryName,
            [System.StringComparison]::Ordinal)) {
        throw 'Packaged window restoration changed selection, summary, or fail-closed authority.'
    }
    [void](Assert-UiAutomationDeviceItems `
        -List $DeviceList `
        -ExpectedCount $ExpectedCount)
    Assert-NoPrivateUiAutomationCopy -Window $Window
    Assert-NoCurrentInspectionErrorPeer `
        -Window $Window `
        -Context 'Restored packaged ready state'
}

function Invoke-PackagedUiAutomationAcceptance {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory)][int]$ExpectedCount,
        [string[]]$ExpectedDisplayNames = @(),
        [Parameter(Mandatory)][int]$Cycles
    )

    Initialize-UiAutomationLiveRegionProbe
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
    $lookingForDevicesName =
        'Looking for connected devices' + [char]0x2026
    $expectedPublicNames = @($initialItems | ForEach-Object {
        [string]$_.Current.Name
    } | Sort-Object)
    if ($ExpectedDisplayNames.Count -gt 0) {
        $actualDisplayNames = @(
            foreach ($item in $initialItems) {
                $headings = @($item.FindAll(
                    [System.Windows.Automation.TreeScope]::Descendants,
                    [System.Windows.Automation.Condition]::TrueCondition) |
                    Where-Object {
                        (Get-UiAutomationHeadingLevel -Element $_) -eq
                            [int][System.Windows.Automation.AutomationHeadingLevel]::Level3
                    })
                if ($headings.Count -ne 1) {
                    throw 'The packaged device roster did not expose one public display-name heading per item.'
                }
                [string]$headings[0].Current.Name
            }
        )
        $displayNameDifference = @(Compare-Object `
            -ReferenceObject @($ExpectedDisplayNames | Sort-Object) `
            -DifferenceObject @($actualDisplayNames | Sort-Object) `
            -CaseSensitive)
        if ($displayNameDifference.Count -ne 0) {
            throw 'The packaged utility did not expose the expected public device display-name roster.'
        }
    }
    Assert-NoPrivateUiAutomationCopy -Window $window
    Assert-UiAutomationHeadingSequence `
        -Window $window `
        -Summary $summary `
        -DeviceList $deviceList `
        -BundleSummary $bundleSummary `
        -ExpectedCount $ExpectedCount `
        -Context 'Initial packaged heading hierarchy'
    Assert-NoCurrentInspectionErrorPeer `
        -Window $window `
        -Context 'Initial packaged ready state'
    Invoke-PackagedMinimumWindowScrollAcceptance `
        -Window $window `
        -Refresh $refresh `
        -Summary $summary `
        -DeviceList $deviceList `
        -SelectionProvider $selectionProvider `
        -SelectBundle $selectBundle `
        -FlashSelected $flashSelected `
        -ExpectedCount $ExpectedCount

    $liveRegionSubscribed = $false
    try {
        [OtLiveRegionProbe]::Reset()
        [System.Windows.Automation.Automation]::AddAutomationEventHandler(
            [System.Windows.Automation.AutomationElementIdentifiers]::LiveRegionChangedEvent,
            $window,
            [System.Windows.Automation.TreeScope]::Subtree,
            [OtLiveRegionProbe]::Handler)
        $liveRegionSubscribed = $true

        for ($cycle = 0; $cycle -lt $Cycles; $cycle++) {
            $items = Assert-UiAutomationDeviceItems -List $deviceList -ExpectedCount $ExpectedCount
            $target = $items[$cycle % $items.Count]
            $selectionItem = $target.GetCurrentPattern(
                [System.Windows.Automation.SelectionItemPattern]::Pattern) -as
                [System.Windows.Automation.SelectionItemPattern]
            [OtLiveRegionProbe]::Reset()
            $selectionItem.Select()
            Wait-ForUiAutomationCondition -FailureMessage (
                "Packaged UI Automation selection did not settle in cycle $($cycle + 1).") -Condition {
                $selectionStatus.Current.Name -like 'Selected:*' -and
                    $selectBundle.Current.IsEnabled -and
                    -not $flashSelected.Current.IsEnabled -and
                    $selectionProvider.Current.GetSelection().Count -eq 1
            }
            $settledSelectionName = [string]$selectionStatus.Current.Name
            Assert-NoPrivateUiAutomationCopy -Window $window
            Assert-NoCurrentInspectionErrorPeer `
                -Window $window `
                -Context "Selected packaged state cycle $($cycle + 1)"
            Assert-UiAutomationHeadingSequence `
                -Window $window `
                -Summary $summary `
                -DeviceList $deviceList `
                -BundleSummary $bundleSummary `
                -ExpectedCount $ExpectedCount `
                -Context "Selected packaged heading hierarchy cycle $($cycle + 1)"
            Start-Sleep -Milliseconds 250
            Assert-LiveRegionSequence `
                -Context "Selection cycle $($cycle + 1)" `
                -Actual @([OtLiveRegionProbe]::Drain()) `
                -Expected @(
                    [pscustomobject]@{
                        Id = 'bundle-summary'
                        Name = 'No firmware bundle selected for the current device'
                    },
                    [pscustomobject]@{
                        Id = 'device-selection-status'
                        Name = $settledSelectionName
                    }
                )

            $invoke = $refresh.GetCurrentPattern(
                [System.Windows.Automation.InvokePattern]::Pattern) -as
                [System.Windows.Automation.InvokePattern]
            [OtLiveRegionProbe]::Reset()
            $invoke.Invoke()
            Wait-ForUiAutomationCondition -TimeoutMilliseconds 10000 -FailureMessage (
                "Packaged refresh did not enter its busy state in cycle $($cycle + 1).") -Condition {
                -not $refresh.Current.IsEnabled -or
                    $summary.Current.Name -eq $lookingForDevicesName
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
            $settledSummaryName = [string]$summary.Current.Name
            Start-Sleep -Milliseconds 250
            Assert-LiveRegionSequence `
                -Context "Refresh cycle $($cycle + 1)" `
                -Actual @([OtLiveRegionProbe]::Drain()) `
                -Expected @(
                    [pscustomobject]@{
                        Id = 'device-selection-status'
                        Name = 'Device selection cleared. Complete inspection, then select one current device.'
                    },
                    [pscustomobject]@{
                        Id = 'bundle-summary'
                        Name = 'Firmware bundle selection waiting for device inspection'
                    },
                    [pscustomobject]@{
                        Id = 'inspection-summary'
                        Name = $lookingForDevicesName
                    },
                    [pscustomobject]@{
                        Id = 'bundle-summary'
                        Name = 'No firmware bundle selected'
                    },
                    [pscustomobject]@{
                        Id = 'inspection-summary'
                        Name = $settledSummaryName
                    },
                    [pscustomobject]@{
                        Id = 'device-selection-status'
                        Name = 'Select one connected device to continue. Selection is not Flash permission.'
                    }
                )

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
            Assert-NoCurrentInspectionErrorPeer `
                -Window $window `
                -Context "Refreshed packaged ready state cycle $($cycle + 1)"
            Assert-UiAutomationHeadingSequence `
                -Window $window `
                -Summary $summary `
                -DeviceList $deviceList `
                -BundleSummary $bundleSummary `
                -ExpectedCount $ExpectedCount `
                -Context "Refreshed packaged heading hierarchy cycle $($cycle + 1)"
        }
    }
    finally {
        if ($liveRegionSubscribed) {
            [System.Windows.Automation.Automation]::RemoveAutomationEventHandler(
                [System.Windows.Automation.AutomationElementIdentifiers]::LiveRegionChangedEvent,
                $window,
                [OtLiveRegionProbe]::Handler)
        }
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
                -ExpectedDisplayNames $expectedDeviceDisplayNameList `
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
