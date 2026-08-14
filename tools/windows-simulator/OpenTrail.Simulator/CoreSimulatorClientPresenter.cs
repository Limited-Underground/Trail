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

    public IReadOnlyList<SimulatorUiDeviceChoice> DeviceChoices =>
        bridge.GetDeviceChoices().Select(MapChoice).ToArray();

    public SimulatorUiConnectionSource ConnectionSource
    {
        get
        {
            var assigned = bridge.GetDeviceChoices().FirstOrDefault(
                choice => choice.AssignedClient == clientId);
            return assigned is null
                ? SimulatorUiConnectionSource.Unassigned
                : SourceCopy(assigned.Source);
        }
    }

    public async Task RefreshDeviceChoicesAsync(CancellationToken cancellationToken)
    {
        _ = await bridge.RefreshDeviceChoicesAsync(cancellationToken);
    }

    public Task SelectDeviceAsync(
        SimulatorUiDeviceChoice choice,
        CancellationToken cancellationToken) =>
        bridge.SelectDeviceAsync(clientId, choice.NativeChoice, cancellationToken).AsTask();

    public Task ForgetDeviceAsync(CancellationToken cancellationToken) =>
        bridge.ForgetDeviceAsync(clientId, cancellationToken).AsTask();

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

    public async Task<SimulatorUiCommandAdmission> QueueMessageTemplateAsync(
        int templateId,
        string exactText,
        long expectedSessionEpoch,
        CancellationToken cancellationToken)
    {
        if (ConnectionSource != SimulatorUiConnectionSource.LocalSimulation)
            throw new InvalidOperationException(
                "Template chat is not admitted to the unauthenticated USB simulator channel.");
        var applied = await bridge.EnqueueTemplateChatForSessionAsync(
            clientId, templateId, exactText, expectedSessionEpoch,
            cancellationToken);
        return new(applied.AppliedSessionEpoch, applied.AppliedMessageSequence);
    }

    public async Task<SimulatorUiCommandAdmission> QueueQuickStatusAsync(
        int portableRequestKind,
        long expectedSessionEpoch,
        CancellationToken cancellationToken)
    {
        var status = portableRequestKind switch
        {
            1 => SimulatorQuickStatus.ImOk,
            2 => SimulatorQuickStatus.NeedAssistance,
            3 => SimulatorQuickStatus.AnyoneOnline,
            4 => SimulatorQuickStatus.AvailableToHelp,
            _ => throw new ArgumentOutOfRangeException(nameof(portableRequestKind)),
        };
        var applied = await bridge.EnqueueQuickStatusForSessionAsync(
            clientId, status, expectedSessionEpoch, cancellationToken);
        return new(applied.AppliedSessionEpoch, applied.AppliedMessageSequence);
    }

    public async Task<SimulatorUiCommandAdmission> QueueCriticalAlertAsync(
        long expectedSessionEpoch,
        CancellationToken cancellationToken)
    {
        var applied = await bridge.EnqueueCriticalAlertForSessionAsync(
            clientId, expectedSessionEpoch, cancellationToken);
        return new(applied.AppliedSessionEpoch, applied.AppliedMessageSequence);
    }

    public async Task<SimulatorUiCommandAdmission> AcknowledgeAlertAsync(
        long sequence,
        long expectedSessionEpoch,
        CancellationToken cancellationToken)
    {
        if (ConnectionSource != SimulatorUiConnectionSource.LocalSimulation)
            throw new InvalidOperationException(
                "Alert acknowledgement is not admitted to the unauthenticated USB simulator channel.");
        var applied = await bridge.AcknowledgeAlertForSessionAsync(
            clientId, sequence, expectedSessionEpoch, cancellationToken);
        return new(applied.AppliedSessionEpoch, applied.AppliedMessageSequence);
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
                message.Direction switch
                {
                    SimulatorMessageDirection.Inbound => SimulatorUiMessageDirection.Inbound,
                    SimulatorMessageDirection.Outbound => SimulatorUiMessageDirection.Outbound,
                    SimulatorMessageDirection.Local => SimulatorUiMessageDirection.Local,
                    _ => throw new InvalidOperationException("Unknown message direction."),
                },
                message.Kind switch
                {
                    SimulatorMessageKind.Chat => SimulatorUiMessageKind.Chat,
                    SimulatorMessageKind.QuickStatus => SimulatorUiMessageKind.QuickStatus,
                    SimulatorMessageKind.Alert => SimulatorUiMessageKind.Alert,
                    SimulatorMessageKind.Acknowledgement => SimulatorUiMessageKind.Acknowledgement,
                    SimulatorMessageKind.System => SimulatorUiMessageKind.System,
                    _ => throw new InvalidOperationException("Unknown message kind."),
                },
                message.Priority switch
                {
                    SimulatorMessagePriority.Normal => SimulatorUiMessagePriority.Normal,
                    SimulatorMessagePriority.Important => SimulatorUiMessagePriority.Important,
                    SimulatorMessagePriority.Critical => SimulatorUiMessagePriority.Critical,
                    _ => throw new InvalidOperationException("Unknown message priority."),
                },
                message.Text,
                message.DeliveryState switch
                {
                    SimulatorDeliveryState.Queued => SimulatorUiDeliveryState.Queued,
                    SimulatorDeliveryState.BridgeAccepted => SimulatorUiDeliveryState.BridgeAccepted,
                    SimulatorDeliveryState.BridgeObserved => SimulatorUiDeliveryState.BridgeObserved,
                    SimulatorDeliveryState.BridgeAcknowledgementObserved => SimulatorUiDeliveryState.BridgeAcknowledgementObserved,
                    SimulatorDeliveryState.Failed => SimulatorUiDeliveryState.Failed,
                    _ => throw new InvalidOperationException("Unknown delivery state."),
                },
                message.CreatedAt,
                source.Alerts.Any(alert =>
                    alert.LocalSequence == message.LocalSequence &&
                    alert.Direction == SimulatorMessageDirection.Inbound &&
                    alert.Severity == SimulatorAlertSeverity.Critical &&
                    alert.State == SimulatorAlertState.Active))).ToArray(),
            source.Alerts.Select(alert => new SimulatorUiAlert(
                alert.LocalSequence,
                alert.Direction switch
                {
                    SimulatorMessageDirection.Inbound => "Inbound",
                    SimulatorMessageDirection.Outbound => "Outbound",
                    SimulatorMessageDirection.Local => "Local",
                    _ => throw new InvalidOperationException("Unknown alert direction."),
                },
                alert.Severity switch
                {
                    SimulatorAlertSeverity.Advisory => "Advisory",
                    SimulatorAlertSeverity.Important => "Important",
                    SimulatorAlertSeverity.Critical => "Critical",
                    _ => throw new InvalidOperationException("Unknown alert severity."),
                },
                alert.Text,
                alert.State switch
                {
                    SimulatorAlertState.Active => "Active",
                    SimulatorAlertState.LocalAcknowledgementQueued =>
                        "Local acknowledgement queued",
                    SimulatorAlertState.BridgeAcknowledgementObserved =>
                        "Bridge acknowledgement observed",
                    SimulatorAlertState.Cleared => "Cleared",
                    _ => throw new InvalidOperationException("Unknown alert state."),
                },
                alert.CreatedAt,
                alert.Direction == SimulatorMessageDirection.Inbound &&
                alert.State == SimulatorAlertState.Active)).ToArray(),
            source.ConnectedSessionEpoch,
            source.OutgoingQueueCount,
            source.OutgoingQueueCapacity,
            source.LastPublicError);

    private static string FamilyCopy(CompanionDeviceFamily? family) => family switch
    {
        CompanionDeviceFamily.Simulated => "local loopback",
        CompanionDeviceFamily.Esp32S3UsbCandidate => "ESP32-S3 USB candidate",
        CompanionDeviceFamily.HeltecV4Companion => "Heltec V4 companion",
        CompanionDeviceFamily.WioTrackerL1Companion => "Wio Tracker L1 companion",
        null => string.Empty,
        _ => "compatible companion",
    };

    private SimulatorUiDeviceChoice MapChoice(SimulatorDeviceChoice choice)
    {
        var family = FamilyCopy(choice.PublicFamily);
        var assignment = choice.AssignedClient is null
            ? string.Empty
            : $" · assigned to Client {choice.AssignedClient}";
        return new SimulatorUiDeviceChoice(
            choice,
            $"{choice.PublicLabel} · {family}{assignment}",
            choice.PublicStatus,
            SourceCopy(choice.Source),
            choice.IsAvailable,
            choice.AssignedClient == clientId);
    }

    private static SimulatorUiConnectionSource SourceCopy(
        SimulatorConnectionSource source) => source switch
    {
        SimulatorConnectionSource.LocalSimulation =>
            SimulatorUiConnectionSource.LocalSimulation,
        SimulatorConnectionSource.CompatibleUsbCompanion =>
            SimulatorUiConnectionSource.UsbCandidate,
        _ => SimulatorUiConnectionSource.Unassigned,
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
            : $"Bridge observation {age} seconds ago";
    }
}
