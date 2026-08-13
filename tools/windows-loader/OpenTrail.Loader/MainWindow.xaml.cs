using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Input;
using Microsoft.Win32;

namespace OpenTrail.Loader;

public partial class MainWindow : Window
{
    private readonly LoaderInspectionService _inspection = new();
    private readonly LoaderRefreshAuthority _refreshAuthority = new();
    private CancellationTokenSource? _refreshCancellation;

    public MainWindow()
    {
        DataContext = ProductIdentity.Current;
        InitializeComponent();
        Loaded += async (_, _) => await RefreshAsync();
        Closed += (_, _) =>
        {
            _refreshAuthority.InvalidateAll();
            _refreshCancellation?.Cancel();
        };
    }

    private async void RefreshButton_Click(object sender, RoutedEventArgs e)
    {
        await RefreshAsync();
    }

    private async void SelectFirmwareButton_Click(object sender, RoutedEventArgs e)
    {
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

        SelectFirmwareButton.IsEnabled = false;
        SelectFirmwareButton.Content = "Inspecting bundle…";
        BundleSummaryText.Text = "Inspecting selected candidate bundle…";
        BundleDetailsText.Text = "No connected device or firmware setting is being changed.";
        RaiseLiveRegionChanged(BundleSummaryText);

        try
        {
            var result = await Task.Run(() =>
                FirmwareBundleCandidateInspector.Inspect(dialog.FileName));
            BundleSummaryText.Text = result.Summary;
            BundleDetailsText.Text = result.Details;
            BundleBlockerText.Text = result.BlockerText;
        }
        catch
        {
            BundleSummaryText.Text = "Candidate firmware bundle rejected";
            BundleDetailsText.Text = "The selected file failed bounded structure or image-digest inspection.";
            BundleBlockerText.Text = "BLOCKED: No firmware or device setting was changed.";
        }
        finally
        {
            SelectFirmwareButton.Content = "Select firmware bundle";
            SelectFirmwareButton.IsEnabled = true;
            RaiseLiveRegionChanged(BundleSummaryText);
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
            DeviceCards.ItemsSource = document.Devices;
            RaiseLiveRegionChanged(SummaryText);
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

    private static void RaiseLiveRegionChanged(UIElement element)
    {
        var peer = UIElementAutomationPeer.FromElement(element) ??
            UIElementAutomationPeer.CreatePeerForElement(element);
        peer?.RaiseAutomationEvent(AutomationEvents.LiveRegionChanged);
    }
}
