using System.Windows;

namespace OpenTrail.Loader;

public partial class MainWindow : Window
{
    private readonly LoaderInspectionService _inspection = new();
    private readonly LoaderRefreshAuthority _refreshAuthority = new();
    private CancellationTokenSource? _refreshCancellation;

    public MainWindow()
    {
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

    private async Task RefreshAsync()
    {
        var refreshRevision = _refreshAuthority.Begin();
        _refreshCancellation?.Cancel();
        _refreshCancellation?.Dispose();
        _refreshCancellation = new CancellationTokenSource(TimeSpan.FromSeconds(30));

        RefreshButton.IsEnabled = false;
        RefreshButton.Content = "Inspecting…";
        SummaryText.Text = "Looking for connected devices…";
        ErrorBanner.Visibility = Visibility.Collapsed;

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
            }
        }
    }

    private void ShowError(string message)
    {
        SummaryText.Text = "Inspection unavailable";
        DeviceCards.ItemsSource = null;
        ErrorText.Text = message;
        ErrorBanner.Visibility = Visibility.Visible;
    }
}
