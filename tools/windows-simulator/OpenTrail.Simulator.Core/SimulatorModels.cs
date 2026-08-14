namespace OpenTrail.Simulator.Core;

public enum SimulatorClientId { A = 0, B = 1 }
public enum SimulatorConnectionState { Disconnected, Connecting, Connected, Stale, Faulted }
public enum SimulatorMessageDirection { Outbound, Inbound, Local }
public enum SimulatorMessageKind { Chat, QuickStatus, Alert, Acknowledgement, System }
public enum SimulatorMessagePriority { Normal, Important, Critical }
/// <summary>Bridge-local evidence only; never proof of over-air delivery.</summary>
public enum SimulatorDeliveryState
{
    Queued,
    BridgeAccepted,
    BridgeObserved,
    Acknowledged,
    Failed,
}
public enum SimulatorAlertSeverity { Advisory, Important, Critical }
public enum SimulatorAlertState { Active, Acknowledged, Cleared }
public enum SimulatorQuickStatus { ImOk, NeedAssistance, AnyoneOnline, AvailableToHelp }
public enum CompanionDeviceFamily { Simulated, HeltecV4Companion, WioTrackerL1Companion }

public sealed record MessageSnapshot(
    long LocalSequence,
    SimulatorMessageDirection Direction,
    SimulatorMessageKind Kind,
    SimulatorMessagePriority Priority,
    string Text,
    DateTimeOffset CreatedAt,
    SimulatorDeliveryState DeliveryState);

public sealed record AlertSnapshot(
    long LocalSequence,
    SimulatorMessageDirection Direction,
    string Text,
    SimulatorAlertSeverity Severity,
    SimulatorAlertState State,
    DateTimeOffset CreatedAt);

public sealed record SimulatorClientSnapshot(
    SimulatorClientId ClientId,
    string ClientLabel,
    SimulatorConnectionState ConnectionState,
    string? PublicDeviceLabel,
    CompanionDeviceFamily? PublicDeviceFamily,
    TimeSpan? LastObservationAge,
    bool IsStale,
    IReadOnlyList<MessageSnapshot> Messages,
    IReadOnlyList<AlertSnapshot> Alerts,
    int OutgoingQueueCount,
    int OutgoingQueueCapacity,
    string? LastPublicError);

public sealed class SimulatorClientSnapshotChangedEventArgs(
    SimulatorClientSnapshot snapshot) : EventArgs
{
    public SimulatorClientSnapshot Snapshot { get; } = snapshot;
}
