namespace OpenTrail.Simulator;

internal enum SimulatorUiConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Stale,
    Faulted,
}

internal enum SimulatorUiMessageDirection { Inbound, Outbound, Local }
internal enum SimulatorUiMessageKind { Chat, QuickStatus, Alert, Acknowledgement, System }
internal enum SimulatorUiMessagePriority { Normal, Important, Critical }
internal enum SimulatorUiDeliveryState
{
    Queued,
    BridgeAccepted,
    BridgeObserved,
    BridgeAcknowledgementObserved,
    Failed,
}

internal sealed record SimulatorUiMessage(
    long Sequence,
    SimulatorUiMessageDirection Direction,
    SimulatorUiMessageKind Kind,
    SimulatorUiMessagePriority Priority,
    string Text,
    SimulatorUiDeliveryState DeliveryState,
    DateTimeOffset CreatedAt,
    bool CanAcknowledge);

internal sealed record SimulatorUiAlert(
    long Sequence,
    string Direction,
    string Severity,
    string Text,
    string State,
    DateTimeOffset CreatedAt,
    bool CanAcknowledge)
{
    public override string ToString() =>
        $"{CreatedAt:HH:mm:ss} · {Direction} · {Severity} · {Text} · {State}";
}

internal sealed record SimulatorUiCommandAdmission(
    long AppliedSessionEpoch,
    long AppliedMessageSequence);

internal sealed record SimulatorUiSnapshot(
    string ClientLabel,
    string PublicDeviceLabel,
    string PublicDeviceFamily,
    SimulatorUiConnectionState ConnectionState,
    string FreshnessText,
    IReadOnlyList<SimulatorUiMessage> Messages,
    IReadOnlyList<SimulatorUiAlert> Alerts,
    long ConnectedSessionEpoch,
    int OutgoingQueueCount,
    int OutgoingQueueCapacity,
    string? PublicError);

internal enum SimulatorUiConnectionSource
{
    Unassigned,
    LocalSimulation,
    UsbCandidate,
}

internal sealed class SimulatorUiDeviceChoice
{
    internal SimulatorUiDeviceChoice(
        OpenTrail.Simulator.Core.SimulatorDeviceChoice nativeChoice,
        string displayText,
        string publicStatus,
        SimulatorUiConnectionSource source,
        bool available,
        bool assignedToThisClient)
    {
        NativeChoice = nativeChoice;
        DisplayText = displayText;
        PublicStatus = publicStatus;
        Source = source;
        IsAvailable = available;
        IsAssignedToThisClient = assignedToThisClient;
    }

    internal OpenTrail.Simulator.Core.SimulatorDeviceChoice NativeChoice { get; }
    public string DisplayText { get; }
    public string PublicStatus { get; }
    public SimulatorUiConnectionSource Source { get; }
    public bool IsAvailable { get; }
    public bool IsAssignedToThisClient { get; }
}

internal interface ISimulatorClientPresenter : IAsyncDisposable
{
    event EventHandler? SnapshotChanged;

    SimulatorUiSnapshot Snapshot { get; }

    IReadOnlyList<SimulatorUiDeviceChoice> DeviceChoices { get; }

    SimulatorUiConnectionSource ConnectionSource { get; }

    Task RefreshDeviceChoicesAsync(CancellationToken cancellationToken);

    Task SelectDeviceAsync(
        SimulatorUiDeviceChoice choice,
        CancellationToken cancellationToken);

    Task ForgetDeviceAsync(CancellationToken cancellationToken);

    Task ConnectAsync(CancellationToken cancellationToken);

    Task DisconnectAsync(CancellationToken cancellationToken);

    Task ReconnectAsync(CancellationToken cancellationToken);

    Task QueueMessageAsync(string text, bool highPriority, CancellationToken cancellationToken);

    Task<SimulatorUiCommandAdmission> QueueMessageTemplateAsync(
        int templateId,
        string exactText,
        long expectedSessionEpoch,
        CancellationToken cancellationToken);

    Task<SimulatorUiCommandAdmission> QueueQuickStatusAsync(
        int portableRequestKind,
        long expectedSessionEpoch,
        CancellationToken cancellationToken);

    Task<SimulatorUiCommandAdmission> QueueCriticalAlertAsync(
        long expectedSessionEpoch,
        CancellationToken cancellationToken);

    Task<SimulatorUiCommandAdmission> AcknowledgeAlertAsync(
        long sequence,
        long expectedSessionEpoch,
        CancellationToken cancellationToken);
}
