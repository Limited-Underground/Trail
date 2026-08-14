using System.ComponentModel;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace OpenTrail.Simulator;

internal enum SimulatorScreen
{
    Home,
    Messages,
    Compose,
    Alerts,
    Status,
}

public partial class VirtualLcdWindow : Window
{
    private readonly ISimulatorClientPresenter presenter;
    private readonly CancellationTokenSource lifetime = new();
    private readonly SemaphoreSlim commandGate = new(1, 1);
    private SimulatorScreen screen = SimulatorScreen.Home;
    private volatile bool closeStarted;
    private volatile bool closeCommitted;

    internal VirtualLcdWindow(ISimulatorClientPresenter presenter)
    {
        this.presenter = presenter ?? throw new ArgumentNullException(nameof(presenter));
        InitializeComponent();
        Title = $"OpenTrail Simulator — {presenter.Snapshot.ClientLabel}";
        AutomationProperties.SetName(this, $"{presenter.Snapshot.ClientLabel} virtual touchscreen simulator");
        this.presenter.SnapshotChanged += HandleSnapshotChanged;
        RenderSnapshot(this.presenter.Snapshot);
        SetScreen(SimulatorScreen.Home);
    }

    internal SimulatorScreen CurrentScreen => screen;

    internal ISimulatorClientPresenter Presenter => presenter;

    private void HandleSnapshotChanged(object? sender, EventArgs e)
    {
        if (closeStarted)
        {
            return;
        }

        _ = Dispatcher.BeginInvoke(() =>
        {
            if (!closeStarted)
            {
                RenderSnapshot(presenter.Snapshot);
            }
        });
    }

    private void RenderSnapshot(SimulatorUiSnapshot snapshot)
    {
        ClientNameText.Text = snapshot.ClientLabel;
        DeviceLabelText.Text = string.IsNullOrWhiteSpace(snapshot.PublicDeviceFamily)
            ? snapshot.PublicDeviceLabel
            : $"{snapshot.PublicDeviceLabel} · {snapshot.PublicDeviceFamily}";
        ConnectionStateText.Text = ConnectionCopy(snapshot.ConnectionState);
        ConnectionStateText.Foreground = ConnectionBrush(snapshot.ConnectionState);
        FreshnessText.Text = snapshot.FreshnessText;

        MessagesList.ItemsSource = snapshot.Messages
            .Select(message =>
                $"{message.CreatedAt:HH:mm:ss} · {message.Direction} · {message.Priority}\n" +
                $"{message.Text}\n{DeliveryCopy(message.DeliveryState)}")
            .ToArray();
        AlertsList.ItemsSource = snapshot.Alerts.ToArray();

        StatusConnectionText.Text = $"Connection: {ConnectionCopy(snapshot.ConnectionState)}";
        StatusQueueText.Text =
            $"Outgoing queue: {snapshot.OutgoingQueueCount} of {snapshot.OutgoingQueueCapacity}";
        StatusMessagesText.Text = $"Messages: {snapshot.Messages.Count}";
        StatusAlertsText.Text = $"Alerts: {snapshot.Alerts.Count}";
        StatusErrorText.Text = string.IsNullOrWhiteSpace(snapshot.PublicError)
            ? "No current bridge error"
            : snapshot.PublicError;

        ConnectButton.IsEnabled = snapshot.ConnectionState is
            SimulatorUiConnectionState.Disconnected or SimulatorUiConnectionState.Faulted;
        DisconnectButton.IsEnabled = snapshot.ConnectionState is not
            SimulatorUiConnectionState.Disconnected;
        ReconnectButton.IsEnabled = snapshot.ConnectionState is not
            SimulatorUiConnectionState.Connecting;
        AcknowledgeAlertButton.IsEnabled = false;
    }

    private static string ConnectionCopy(SimulatorUiConnectionState state) => state switch
    {
        SimulatorUiConnectionState.Disconnected => "Disconnected",
        SimulatorUiConnectionState.Connecting => "Connecting",
        SimulatorUiConnectionState.Connected => "Connected to local bridge",
        SimulatorUiConnectionState.Stale => "Stale — awaiting bridge data",
        SimulatorUiConnectionState.Faulted => "Bridge fault",
        _ => "Unavailable",
    };

    private Brush ConnectionBrush(SimulatorUiConnectionState state) => state switch
    {
        SimulatorUiConnectionState.Connected => (Brush)FindResource("GoodBrush"),
        SimulatorUiConnectionState.Connecting or SimulatorUiConnectionState.Stale =>
            (Brush)FindResource("WarningBrush"),
        SimulatorUiConnectionState.Faulted => (Brush)FindResource("CriticalBrush"),
        _ => (Brush)FindResource("MutedBrush"),
    };

    private static string DeliveryCopy(string state)
    {
        if (string.Equals(state, "Acknowledged", StringComparison.OrdinalIgnoreCase))
        {
            return "Acknowledged by peer simulator";
        }
        if (string.Equals(state, "BridgeAccepted", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(state, "Accepted", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(state, "Sent", StringComparison.OrdinalIgnoreCase))
        {
            return "Accepted by local bridge; peer and radio delivery not proven";
        }
        if (string.Equals(state, "BridgeObserved", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(state, "Received", StringComparison.OrdinalIgnoreCase))
        {
            return "Observed by paired local bridge; radio delivery not proven";
        }
        if (string.Equals(state, "Failed", StringComparison.OrdinalIgnoreCase))
        {
            return "Bridge attempt failed; retained locally when retryable";
        }

        return "Queued locally";
    }

    private void SetScreen(SimulatorScreen next)
    {
        screen = next;
        HomeScreen.Visibility = next == SimulatorScreen.Home ? Visibility.Visible : Visibility.Collapsed;
        MessagesScreen.Visibility = next == SimulatorScreen.Messages ? Visibility.Visible : Visibility.Collapsed;
        ComposeScreen.Visibility = next == SimulatorScreen.Compose ? Visibility.Visible : Visibility.Collapsed;
        AlertsScreen.Visibility = next == SimulatorScreen.Alerts ? Visibility.Visible : Visibility.Collapsed;
        StatusScreen.Visibility = next == SimulatorScreen.Status ? Visibility.Visible : Visibility.Collapsed;
        BackButton.IsEnabled = next != SimulatorScreen.Home;
        ScreenNoticeText.Text = next == SimulatorScreen.Home
            ? "Ready for local simulation"
            : $"{next} screen · Esc returns home";
    }

    private void ShowMessages(object sender, RoutedEventArgs e) => SetScreen(SimulatorScreen.Messages);

    private void ShowCompose(object sender, RoutedEventArgs e)
    {
        SetScreen(SimulatorScreen.Compose);
        ComposeText.Focus();
    }

    private void ShowAlerts(object sender, RoutedEventArgs e) => SetScreen(SimulatorScreen.Alerts);

    private void ShowStatus(object sender, RoutedEventArgs e) => SetScreen(SimulatorScreen.Status);

    private void GoBack(object sender, RoutedEventArgs e) => SetScreen(SimulatorScreen.Home);

    private void GoHome(object sender, RoutedEventArgs e) => SetScreen(SimulatorScreen.Home);

    private async void QueueMessage(object sender, RoutedEventArgs e)
    {
        var text = ComposeText.Text.Trim();
        if (text.Length == 0)
        {
            ScreenNoticeText.Text = "Enter a message before queuing it.";
            ComposeText.Focus();
            return;
        }

        var succeeded = await RunCommandAsync(
            token => presenter.QueueMessageAsync(text, PriorityBox.SelectedIndex == 1, token),
            "Message queued locally.");
        if (succeeded)
        {
            await InvokeUiAsync(() =>
            {
                if (!closeStarted)
                {
                    ComposeText.Clear();
                    SetScreen(SimulatorScreen.Messages);
                }
            });
        }
    }

    private async void QueueAssistanceAlert(object sender, RoutedEventArgs e)
    {
        await RunCommandAsync(
            presenter.InjectSyntheticAlertAsync,
            "Synthetic test alert queued; held critical confirmation was not exercised.");
    }

    private async void AcknowledgeSelectedAlert(object sender, RoutedEventArgs e)
    {
        if (AlertsList.SelectedItem is not SimulatorUiAlert alert || !alert.CanAcknowledge)
        {
            ScreenNoticeText.Text = "Select an active received alert first.";
            return;
        }

        await RunCommandAsync(
            token => presenter.AcknowledgeAlertAsync(alert.Sequence, token),
            "Acknowledgement queued through the local bridge.");
    }

    private void HandleAlertSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        AcknowledgeAlertButton.IsEnabled =
            AlertsList.SelectedItem is SimulatorUiAlert { CanAcknowledge: true };
    }

    private async void ConnectClient(object sender, RoutedEventArgs e) =>
        await RunCommandAsync(presenter.ConnectAsync, "Connect requested.");

    private async void DisconnectClient(object sender, RoutedEventArgs e) =>
        await RunCommandAsync(presenter.DisconnectAsync, "Disconnected this client only.");

    private async void ReconnectClient(object sender, RoutedEventArgs e) =>
        await RunCommandAsync(presenter.ReconnectAsync, "Reconnect requested for this client only.");

    private async Task<bool> RunCommandAsync(
        Func<CancellationToken, Task> command,
        string successNotice)
    {
        if (closeStarted)
        {
            return false;
        }

        var entered = false;
        try
        {
            await commandGate.WaitAsync(lifetime.Token);
            entered = true;
            if (closeStarted)
            {
                return false;
            }
            await command(lifetime.Token).ConfigureAwait(false);
            await InvokeUiAsync(() =>
            {
                if (!closeStarted)
                {
                    ScreenNoticeText.Text = successNotice;
                }
            }).ConfigureAwait(false);
            return true;
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            return false;
        }
        catch (Exception)
        {
            await InvokeUiAsync(() =>
            {
                if (!closeStarted)
                {
                    ScreenNoticeText.Text = "The local simulator command failed; open Status for details.";
                }
            }).ConfigureAwait(false);
            return false;
        }
        finally
        {
            if (entered)
            {
                commandGate.Release();
            }
        }
    }

    private async Task InvokeUiAsync(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished)
        {
            return;
        }

        if (Dispatcher.CheckAccess())
        {
            action();
            return;
        }

        await Dispatcher.InvokeAsync(action, DispatcherPriority.Normal).Task.ConfigureAwait(false);
    }

    private void HandleWindowPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            SetScreen(SimulatorScreen.Home);
            e.Handled = true;
            return;
        }

        if (e.Key == Key.F5)
        {
            ReconnectClient(sender, e);
            e.Handled = true;
            return;
        }

        if ((Keyboard.Modifiers & ModifierKeys.Control) == 0)
        {
            return;
        }

        var next = e.Key switch
        {
            Key.M => SimulatorScreen.Messages,
            Key.N => SimulatorScreen.Compose,
            Key.A => SimulatorScreen.Alerts,
            Key.S => SimulatorScreen.Status,
            _ => (SimulatorScreen?)null,
        };
        if (next is null)
        {
            return;
        }

        SetScreen(next.Value);
        if (next == SimulatorScreen.Compose)
        {
            ComposeText.Focus();
        }
        e.Handled = true;
    }

    private async void HandleWindowClosing(object? sender, CancelEventArgs e)
    {
        if (closeCommitted)
        {
            return;
        }

        if (closeStarted)
        {
            e.Cancel = true;
            return;
        }

        e.Cancel = true;
        closeStarted = true;
        IsEnabled = false;
        presenter.SnapshotChanged -= HandleSnapshotChanged;
        lifetime.Cancel();
        try
        {
            await commandGate.WaitAsync(CancellationToken.None);
            await presenter.DisconnectAsync(CancellationToken.None);
            await presenter.DisposeAsync();
        }
        catch (Exception)
        {
            // Closing is best-effort and remains scoped to this one client.
        }
        finally
        {
            commandGate.Release();
            closeCommitted = true;
            _ = Dispatcher.BeginInvoke(DispatcherPriority.Normal, Close);
        }
    }
}
