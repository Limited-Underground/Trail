using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;

namespace OpenTrail.Loader;

public partial class MainWindow : Window
{
    private readonly LoaderInspectionService _inspection = new();
    private readonly LoaderRefreshAuthority _refreshAuthority = new();
    private readonly LoaderCandidateBundleAuthority _bundleAuthority = new();
    private readonly LoaderDeviceSelectionAuthority _deviceSelection = new();
    private CancellationTokenSource? _refreshCancellation;
    private bool _suppressSelectionChanged;

    public MainWindow()
    {
        DataContext = ProductIdentity.Current;
        InitializeComponent();
        Loaded += async (_, _) => await RefreshAsync();
        Closed += (_, _) =>
        {
            _refreshAuthority.InvalidateAll();
            _bundleAuthority.InvalidateAll();
            _deviceSelection.InvalidateAll();
            _refreshCancellation?.Cancel();
        };
    }

    private async void RefreshButton_Click(object sender, RoutedEventArgs e)
    {
        await RefreshAsync();
    }

    private async void SelectFirmwareButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_bundleAuthority.CanBeginInspection || !_deviceSelection.HasSelection)
        {
            ResetBundleDisplay(
                "Firmware bundle selection unavailable",
                "Complete a current connected-device inspection first.",
                "BLOCKED: No current device snapshot is available.");
            return;
        }

        var dialog = new OpenFileDialog
        {
            Title = "Select candidate firmware bundle",
            Filter = "Firmware bundle (*.fwbundle)|*.fwbundle",
            CheckFileExists = true,
            CheckPathExists = true,
            Multiselect = false,
            ValidateNames = true,
            DereferenceLinks = true,
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        LoaderCandidateBundlePermit permit;
        try
        {
            permit = _bundleAuthority.BeginInspection();
        }
        catch (InvalidOperationException)
        {
            ResetBundleDisplay(
                "Firmware bundle selection unavailable",
                "The connected-device snapshot changed. Inspect devices again.",
                "BLOCKED: No current device snapshot is available.");
            return;
        }

        SelectFirmwareButton.IsEnabled = false;
        SelectFirmwareButton.Content = "Inspecting bundle…";
        BundleSummaryText.Text = "Inspecting selected candidate bundle…";
        BundleDetailsText.Text = "No connected device or firmware setting is being changed.";
        RaiseLiveRegionChanged(BundleSummaryText);

        try
        {
            var result = await Task.Run(() =>
                FirmwareBundleCandidateInspector.Inspect(dialog.FileName));
            if (!_bundleAuthority.CanPublish(permit))
            {
                return;
            }
            BundleSummaryText.Text = result.Summary;
            BundleDetailsText.Text = result.Details;
            BundleBlockerText.Text = result.BlockerText;
        }
        catch
        {
            if (!_bundleAuthority.CanPublish(permit))
            {
                return;
            }
            BundleSummaryText.Text = "Candidate firmware bundle rejected";
            BundleDetailsText.Text = "The selected file failed bounded structure or image-digest inspection.";
            BundleBlockerText.Text = "BLOCKED: No firmware or device setting was changed.";
        }
        finally
        {
            if (_bundleAuthority.Complete(permit))
            {
                SelectFirmwareButton.Content = "Select firmware bundle";
                SelectFirmwareButton.IsEnabled = _bundleAuthority.CanBeginInspection;
                RaiseLiveRegionChanged(BundleSummaryText);
            }
        }
    }

    private void RefreshCommand_CanExecute(object sender, CanExecuteRoutedEventArgs e)
    {
        e.CanExecute = RefreshButton?.IsEnabled == true;
        e.Handled = true;
    }

    private async void RefreshCommand_Executed(object sender, ExecutedRoutedEventArgs e)
    {
        e.Handled = true;
        await RefreshAsync();
    }

    private async Task RefreshAsync()
    {
        var refreshRevision = _refreshAuthority.Begin();
        _deviceSelection.InvalidateForDeviceRefresh();
        _bundleAuthority.InvalidateForDeviceRefresh();
        _suppressSelectionChanged = true;
        DeviceCards.SelectedItem = null;
        DeviceCards.ItemsSource = null;
        _suppressSelectionChanged = false;
        SelectionText.Text =
            "Device selection cleared. Complete inspection, then select one current device.";
        RaiseLiveRegionChanged(SelectionText);
        SelectFirmwareButton.Content = "Select firmware bundle";
        SelectFirmwareButton.IsEnabled = false;
        ResetBundleDisplay(
            "Firmware bundle selection waiting for device inspection",
            "Any earlier candidate result was discarded before refreshing devices.",
            "BLOCKED: A current connected-device snapshot is required.");
        _refreshCancellation?.Cancel();
        _refreshCancellation?.Dispose();
        _refreshCancellation = new CancellationTokenSource(TimeSpan.FromSeconds(30));

        RefreshButton.IsEnabled = false;
        RefreshButton.Content = "Inspecting…";
        SummaryText.Text = "Looking for connected devices…";
        RaiseLiveRegionChanged(SummaryText);
        ErrorBanner.Visibility = Visibility.Collapsed;
        CommandManager.InvalidateRequerySuggested();

        try
        {
            var document = await _inspection.RefreshAsync(_refreshCancellation.Token);
            if (!_refreshAuthority.CanPublish(refreshRevision))
            {
                return;
            }

            PhaseText.Text = document.Screen.Phase;
            SummaryText.Text = document.Screen.Summary;
            NoticeText.Text = document.Screen.Notice;
            _deviceSelection.PublishSnapshot(
                document.Devices.Select(static device => device.Candidate));
            DeviceCards.ItemsSource = document.Devices;
            _bundleAuthority.PublishCurrentDeviceSnapshot();
            SelectFirmwareButton.IsEnabled = false;
            SelectionText.Text = document.Devices.Count == 0
                ? "No connected device is available for selection."
                : "Select one connected device to continue. Selection is not Flash permission.";
            RaiseLiveRegionChanged(SummaryText);
            RaiseLiveRegionChanged(SelectionText);
        }
        catch (OperationCanceledException)
        {
            if (_refreshAuthority.CanPublish(refreshRevision))
            {
                ShowError("Device inspection timed out. Disconnect browser serial sessions and try Refresh again.");
            }
        }
        catch
        {
            if (_refreshAuthority.CanPublish(refreshRevision))
            {
                ShowError("Device inspection could not complete. No device or firmware setting was changed.");
            }
        }
        finally
        {
            if (_refreshAuthority.Complete(refreshRevision))
            {
                RefreshButton.Content = "Refresh devices";
                RefreshButton.IsEnabled = true;
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    private void ShowError(string message)
    {
        SummaryText.Text = "Inspection unavailable";
        DeviceCards.ItemsSource = null;
        ErrorText.Text = message;
        ErrorBanner.Visibility = Visibility.Visible;
        RaiseLiveRegionChanged(ErrorText);
    }

    private void DeviceCards_SelectionChanged(
        object sender,
        SelectionChangedEventArgs e)
    {
        if (_suppressSelectionChanged)
        {
            return;
        }

        if (DeviceCards.SelectedItem is not LoaderDeviceCard device ||
            !_deviceSelection.TrySelect(device.Candidate))
        {
            SelectionText.Text =
                "No current device is selected. Refresh and choose one connected candidate.";
            SelectFirmwareButton.IsEnabled = false;
            RaiseLiveRegionChanged(SelectionText);
            return;
        }

        _bundleAuthority.InvalidateForDeviceSelectionChange();
        ResetBundleDisplay(
            "No firmware bundle selected for the current device",
            "Changing device selection discarded any earlier candidate result.",
            "BLOCKED: Bundle inspection, exact-device matching, and Flash permission are separate gates.");
        SelectionText.Text =
            $"Selected: {device.DisplayName}. Selection is not Flash permission.";
        SelectFirmwareButton.IsEnabled =
            _bundleAuthority.CanBeginInspection && _deviceSelection.HasSelection;
        RaiseLiveRegionChanged(SelectionText);
    }

    private void ResetBundleDisplay(string summary, string details, string blocker)
    {
        BundleSummaryText.Text = summary;
        BundleDetailsText.Text = details;
        BundleBlockerText.Text = blocker;
        RaiseLiveRegionChanged(BundleSummaryText);
    }

    private static void RaiseLiveRegionChanged(UIElement element)
    {
        var peer = UIElementAutomationPeer.FromElement(element) ??
            UIElementAutomationPeer.CreatePeerForElement(element);
        peer?.RaiseAutomationEvent(AutomationEvents.LiveRegionChanged);
    }
}
