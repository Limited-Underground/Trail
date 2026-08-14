namespace OpenTrail.Simulator.Core;

public enum SimulatorConnectionSource
{
    LocalSimulation,
    CompatibleUsbCompanion,
}

/// <summary>
/// Immutable public-safe choice from one exact in-process discovery revision.
/// The private endpoint and revision are deliberately non-rendering.
/// </summary>
public sealed class SimulatorDeviceChoice
{
    internal SimulatorDeviceChoice(
        CompanionCandidate candidate,
        long rosterRevision,
        object bridgeOwner,
        SimulatorClientId? assignedClient)
    {
        Candidate = candidate;
        RosterRevision = rosterRevision;
        BridgeOwner = bridgeOwner;
        AssignedClient = assignedClient;
    }

    internal CompanionCandidate Candidate { get; }
    internal long RosterRevision { get; }
    internal object BridgeOwner { get; }
    public string PublicLabel => Candidate.PublicLabel;
    public CompanionDeviceFamily PublicFamily => Candidate.PublicFamily;
    public SimulatorConnectionSource Source => Candidate.Source;
    public bool ConnectionReady => Candidate.ConnectionReady;
    public string PublicStatus => Candidate.PublicStatus;
    public SimulatorClientId? AssignedClient { get; }
    public bool IsAvailable => AssignedClient is null;

    public override string ToString() => PublicLabel;
}
