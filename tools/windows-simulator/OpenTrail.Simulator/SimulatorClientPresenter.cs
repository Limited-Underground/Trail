namespace OpenTrail.Simulator;

internal enum SimulatorUiConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Stale,
    Faulted,
}

internal sealed record SimulatorUiMessage(
    long Sequence,
    string Direction,
    string Kind,
    string Priority,
    string Text,
    string DeliveryState,
    DateTimeOffset CreatedAt);

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

internal sealed record SimulatorUiSnapshot(
    string ClientLabel,
    string PublicDeviceLabel,
    string PublicDeviceFamily,
    SimulatorUiConnectionState ConnectionState,
    string FreshnessText,
    IReadOnlyList<SimulatorUiMessage> Messages,
    IReadOnlyList<SimulatorUiAlert> Alerts,
    int OutgoingQueueCount,
    int OutgoingQueueCapacity,
    string? PublicError);

internal interface ISimulatorClientPresenter : IAsyncDisposable
{
    event EventHandler? SnapshotChanged;

    SimulatorUiSnapshot Snapshot { get; }

    Task ConnectAsync(CancellationToken cancellationToken);

    Task DisconnectAsync(CancellationToken cancellationToken);

    Task ReconnectAsync(CancellationToken cancellationToken);

    Task QueueMessageAsync(string text, bool highPriority, CancellationToken cancellationToken);

    Task InjectSyntheticAlertAsync(CancellationToken cancellationToken);

    Task AcknowledgeAlertAsync(long sequence, CancellationToken cancellationToken);
}
