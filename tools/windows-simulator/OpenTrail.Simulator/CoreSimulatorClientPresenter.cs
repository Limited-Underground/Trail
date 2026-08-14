using System.Windows.Threading;
using OpenTrail.Simulator.Core;

namespace OpenTrail.Simulator;

internal sealed class CoreSimulatorClientPresenter : ISimulatorClientPresenter
{
    private readonly DualClientBridge bridge;
    private readonly SimulatorClientId clientId;
    private readonly Dispatcher dispatcher;
    private readonly DispatcherTimer serviceTimer;
    private readonly SemaphoreSlim serviceGate = new(1, 1);
    private readonly Func<CancellationToken, Task> serviceAsync;
    private readonly object disposeSync = new();
    private SimulatorUiSnapshot snapshot;
    private volatile bool disposalStarted;
    private Task? disposalTask;

    internal CoreSimulatorClientPresenter(
        DualClientBridge bridge,
        SimulatorClientId clientId,
        Dispatcher dispatcher,
        Func<CancellationToken, Task>? serviceOverride = null,
        TimeSpan? serviceInterval = null)
    {
        this.bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
        this.clientId = clientId;
        ArgumentNullException.ThrowIfNull(dispatcher);
        this.dispatcher = dispatcher;
        serviceAsync = serviceOverride ??
            (token => bridge.ServiceAsync(clientId, token).AsTask());
        var interval = serviceInterval ?? TimeSpan.FromMilliseconds(100);
        if (interval <= TimeSpan.Zero || interval > TimeSpan.FromSeconds(10))
        {
            throw new ArgumentOutOfRangeException(nameof(serviceInterval));
        }
        snapshot = Map(bridge.GetSnapshot(clientId));
        bridge.SnapshotChanged += HandleSnapshotChanged;
        serviceTimer = new DispatcherTimer(
            interval,
            DispatcherPriority.Background,
            ServiceTimerTick,
            dispatcher);
        serviceTimer.Start();
    }

    public event EventHandler? SnapshotChanged;

    public SimulatorUiSnapshot Snapshot => snapshot;

    public Task ConnectAsync(CancellationToken cancellationToken) =>
        bridge.ConnectAsync(clientId, cancellationToken).AsTask();

    public Task DisconnectAsync(CancellationToken cancellationToken) =>
        bridge.DisconnectAsync(clientId, cancellationToken).AsTask();

    public Task ReconnectAsync(CancellationToken cancellationToken) =>
        bridge.ReconnectAsync(clientId, cancellationToken).AsTask();

    public async Task QueueMessageAsync(
        string text,
        bool highPriority,
        CancellationToken cancellationToken)
    {
        await bridge.EnqueueChatAsync(
            clientId,
            text,
            highPriority ? SimulatorMessagePriority.Important : SimulatorMessagePriority.Normal,
            cancellationToken);
        await bridge.ServiceAsync(clientId, cancellationToken);
    }

    public async Task InjectSyntheticAlertAsync(CancellationToken cancellationToken)
    {
        await bridge.EnqueueAlertAsync(
            clientId,
            "Synthetic test assistance alert",
            SimulatorAlertSeverity.Important,
            cancellationToken);
        await bridge.ServiceAsync(clientId, cancellationToken);
    }

    public async Task AcknowledgeAlertAsync(
        long sequence,
        CancellationToken cancellationToken)
    {
        await bridge.AcknowledgeAlertAsync(clientId, sequence, cancellationToken);
        await bridge.ServiceAsync(clientId, cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        lock (disposeSync)
        {
            disposalTask ??= DisposeCoreAsync();
            return new ValueTask(disposalTask);
        }
    }

    private async Task DisposeCoreAsync()
    {
        disposalStarted = true;
        bridge.SnapshotChanged -= HandleSnapshotChanged;
        if (dispatcher.CheckAccess())
        {
            serviceTimer.Stop();
        }
        else if (!dispatcher.HasShutdownStarted && !dispatcher.HasShutdownFinished)
        {
            await dispatcher.InvokeAsync(serviceTimer.Stop, DispatcherPriority.Send).Task
                .ConfigureAwait(false);
        }

        await serviceGate.WaitAsync().ConfigureAwait(false);
        serviceGate.Release();
    }

    private async void ServiceTimerTick(object? sender, EventArgs e)
    {
        if (disposalStarted)
        {
            return;
        }

        var entered = false;
        try
        {
            entered = await serviceGate.WaitAsync(0);
            if (!entered)
            {
                return;
            }
            if (!disposalStarted)
            {
                await serviceAsync(CancellationToken.None);
            }
        }
        catch (ObjectDisposedException) when (disposalStarted)
        {
        }
        catch (Exception)
        {
            // The bridge owns public failure state; no private adapter detail reaches the UI.
        }
        finally
        {
            if (entered)
            {
                serviceGate.Release();
            }
        }
    }

    private void HandleSnapshotChanged(object? sender, SimulatorClientSnapshotChangedEventArgs e)
    {
        if (disposalStarted || e.Snapshot.ClientId != clientId)
        {
            return;
        }

        snapshot = Map(e.Snapshot);
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
    }

    private static SimulatorUiSnapshot Map(SimulatorClientSnapshot source) =>
        new(
            source.ClientLabel,
            source.PublicDeviceLabel ?? "No companion assigned",
            FamilyCopy(source.PublicDeviceFamily),
            source.ConnectionState switch
            {
                SimulatorConnectionState.Disconnected => SimulatorUiConnectionState.Disconnected,
                SimulatorConnectionState.Connecting => SimulatorUiConnectionState.Connecting,
                SimulatorConnectionState.Connected => SimulatorUiConnectionState.Connected,
                SimulatorConnectionState.Stale => SimulatorUiConnectionState.Stale,
                SimulatorConnectionState.Faulted => SimulatorUiConnectionState.Faulted,
                _ => SimulatorUiConnectionState.Faulted,
            },
            FreshnessCopy(source),
            source.Messages.Select(message => new SimulatorUiMessage(
                message.LocalSequence,
                message.Direction.ToString(),
                message.Kind.ToString(),
                message.Priority.ToString(),
                message.Text,
                message.DeliveryState.ToString(),
                message.CreatedAt)).ToArray(),
            source.Alerts.Select(alert => new SimulatorUiAlert(
                alert.LocalSequence,
                alert.Direction.ToString(),
                alert.Severity.ToString(),
                alert.Text,
                alert.State.ToString(),
                alert.CreatedAt,
                alert.Direction == SimulatorMessageDirection.Inbound &&
                alert.State == SimulatorAlertState.Active)).ToArray(),
            source.OutgoingQueueCount,
            source.OutgoingQueueCapacity,
            source.LastPublicError);

    private static string FamilyCopy(CompanionDeviceFamily? family) => family switch
    {
        CompanionDeviceFamily.Simulated => "local loopback",
        CompanionDeviceFamily.HeltecV4Companion => "Heltec V4 companion",
        CompanionDeviceFamily.WioTrackerL1Companion => "Wio Tracker L1 companion",
        null => string.Empty,
        _ => "compatible companion",
    };

    private static string FreshnessCopy(SimulatorClientSnapshot snapshot)
    {
        if (snapshot.LastObservationAge is null)
        {
            return "No bridge observation";
        }

        var age = Math.Max(0, (int)Math.Floor(snapshot.LastObservationAge.Value.TotalSeconds));
        return snapshot.IsStale
            ? $"Stale for {age} seconds"
            : $"Local bridge observation {age} seconds ago";
    }
}
