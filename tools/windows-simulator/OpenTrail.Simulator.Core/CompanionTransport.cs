namespace OpenTrail.Simulator.Core;

public enum CompanionCommandKind { Chat, QuickStatus, Alert, Acknowledgement }

public sealed record CompanionCommand(
    CompanionCommandKind Kind,
    long Correlation,
    string Text,
    SimulatorMessagePriority Priority,
    SimulatorAlertSeverity? AlertSeverity);

public sealed record CompanionObservation(
    CompanionCommandKind Kind,
    long Correlation,
    string Text,
    SimulatorMessagePriority Priority,
    SimulatorAlertSeverity? AlertSeverity);

public sealed class CompanionEndpoint : IEquatable<CompanionEndpoint>
{
    private readonly Guid _sessionValue;

    internal CompanionEndpoint(Guid sessionValue) => _sessionValue = sessionValue;

    /// <summary>Creates an opaque process-local token; never derive it from a port or identity.</summary>
    public static CompanionEndpoint CreateSessionPrivate() => new(Guid.NewGuid());

    public bool Equals(CompanionEndpoint? other) =>
        other is not null && _sessionValue == other._sessionValue;

    public override bool Equals(object? obj) => Equals(obj as CompanionEndpoint);
    public override int GetHashCode() => _sessionValue.GetHashCode();
    public override string ToString() => "[private companion endpoint]";
}

public sealed class CompanionCandidate
{
    public CompanionCandidate(
        CompanionEndpoint endpoint,
        string publicLabel,
        CompanionDeviceFamily publicFamily,
        bool connectionReady = true)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        var labelAllowed = publicFamily switch
        {
            CompanionDeviceFamily.Simulated =>
                publicLabel is "Simulated companion A" or "Simulated companion B",
            CompanionDeviceFamily.HeltecV4Companion => publicLabel == "Heltec V4 OLED",
            CompanionDeviceFamily.WioTrackerL1Companion => publicLabel == "Wio Tracker L1",
            _ => false,
        };
        if (!labelAllowed)
            throw new ArgumentException("The public device label is not allowlisted.", nameof(publicLabel));
        if (publicFamily != CompanionDeviceFamily.Simulated && connectionReady)
            throw new ArgumentException(
                "A live USB companion transport has not passed its connection gate.",
                nameof(connectionReady));
        Endpoint = endpoint;
        PublicLabel = publicLabel;
        PublicFamily = publicFamily;
        ConnectionReady = connectionReady;
    }

    private CompanionCandidate(
        CompanionEndpoint endpoint,
        string publicLabel,
        CompanionDeviceFamily publicFamily,
        bool connectionReady,
        LiveAdmission admission)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        if (admission != LiveAdmission.ExplicitConnectCandidate)
            throw new ArgumentOutOfRangeException(nameof(admission));
        var labelAllowed = (publicFamily, publicLabel) switch
        {
            (CompanionDeviceFamily.Esp32S3UsbCandidate, "ESP32-S3 USB candidate") => true,
            (CompanionDeviceFamily.WioTrackerL1Companion, "Wio Tracker L1 USB candidate") => true,
            _ => false,
        };
        if (!labelAllowed || !connectionReady)
            throw new ArgumentException("The USB companion admission is invalid.");
        Endpoint = endpoint;
        PublicLabel = publicLabel;
        PublicFamily = publicFamily;
        ConnectionReady = true;
    }

    internal static CompanionCandidate CreateConnectableUsbCandidate(
        CompanionEndpoint endpoint,
        string publicLabel,
        CompanionDeviceFamily publicFamily) =>
        new(endpoint, publicLabel, publicFamily, connectionReady: true,
            LiveAdmission.ExplicitConnectCandidate);

    internal CompanionEndpoint Endpoint { get; }
    public string PublicLabel { get; }
    public CompanionDeviceFamily PublicFamily { get; }
    public SimulatorConnectionSource Source => PublicFamily == CompanionDeviceFamily.Simulated
        ? SimulatorConnectionSource.LocalSimulation
        : SimulatorConnectionSource.CompatibleUsbCompanion;
    public bool ConnectionReady { get; }
    public string PublicStatus => ConnectionReady
        ? Source == SimulatorConnectionSource.LocalSimulation
            ? "Local simulation ready"
            : "USB candidate ready to verify on Connect"
        : "USB companion recognized; live session adapter not ready";
    public override string ToString() => PublicLabel;

    private enum LiveAdmission { ExplicitConnectCandidate }
}

public interface ICompanionDeviceDiscovery
{
    ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken);
}

public interface ICompanionTransportFactory
{
    ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate,
        CancellationToken cancellationToken);
}

public interface ICompanionTransport : IAsyncDisposable
{
    ValueTask SendAsync(CompanionCommand command, CancellationToken cancellationToken);

    /// <summary>
    /// Performs one nonblocking poll. Implementations return null promptly when
    /// no complete observation is ready and honor cancellation before I/O.
    /// No identity-bearing background callback is permitted.
    /// </summary>
    ValueTask<CompanionObservation?> ReceiveAsync(CancellationToken cancellationToken);
}

public interface ISimulatorClock
{
    DateTimeOffset UtcNow { get; }
}

internal sealed class SystemSimulatorClock : ISimulatorClock
{
    internal static SystemSimulatorClock Instance { get; } = new();
    public DateTimeOffset UtcNow => DateTimeOffset.UtcNow;
}
