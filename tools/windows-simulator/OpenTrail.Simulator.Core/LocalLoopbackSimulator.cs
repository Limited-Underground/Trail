using System.Collections.Concurrent;

namespace OpenTrail.Simulator.Core;

public sealed class LocalLoopbackSimulator
{
    private readonly LoopbackNetwork _network;

    private LocalLoopbackSimulator(LoopbackNetwork network, DualClientBridge bridge)
    {
        _network = network;
        Bridge = bridge;
    }

    public DualClientBridge Bridge { get; }

    public static LocalLoopbackSimulator Create(
        ISimulatorClock? clock = null,
        TimeSpan? staleAfter = null)
    {
        var network = new LoopbackNetwork();
        var bridge = new DualClientBridge(
            network,
            network,
            clock ?? SystemSimulatorClock.Instance,
            staleAfter ?? TimeSpan.FromSeconds(10));
        return new LocalLoopbackSimulator(network, bridge);
    }

    public void SetOnline(SimulatorClientId clientId, bool online) =>
        _network.SetOnline(clientId, online);

    public void FailNextSend(SimulatorClientId clientId) =>
        _network.FailNextSend(clientId);
}

internal sealed class LoopbackNetwork : ICompanionDeviceDiscovery, ICompanionTransportFactory
{
    private const int MaximumPendingPeerObservations = 64;
    private readonly CompanionEndpoint _a = new(Guid.NewGuid());
    private readonly CompanionEndpoint _b = new(Guid.NewGuid());
    private readonly ConcurrentQueue<CompanionObservation> _aInbox = new();
    private readonly ConcurrentQueue<CompanionObservation> _bInbox = new();
    private readonly object _sync = new();
    private bool _aOnline = true;
    private bool _bOnline = true;
    private bool _aOpen;
    private bool _bOpen;
    private bool _aFailNext;
    private bool _bFailNext;

    public ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var found = new List<CompanionCandidate>(2);
        lock (_sync)
        {
            if (_aOnline)
                found.Add(new(_a, "Simulated companion A", CompanionDeviceFamily.Simulated));
            if (_bOnline)
                found.Add(new(_b, "Simulated companion B", CompanionDeviceFamily.Simulated));
        }
        return ValueTask.FromResult<IReadOnlyList<CompanionCandidate>>(found);
    }

    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_sync)
        {
            var client = candidate.Endpoint.Equals(_a)
                ? SimulatorClientId.A
                : candidate.Endpoint.Equals(_b)
                    ? SimulatorClientId.B
                    : throw new InvalidOperationException("The companion endpoint is unavailable.");
            if (!IsOnline(client) || IsOpen(client))
                throw new IOException("The companion device is unavailable or already owned.");
            SetOpen(client, true);
            return ValueTask.FromResult<ICompanionTransport>(new LoopbackTransport(this, client));
        }
    }

    internal void SetOnline(SimulatorClientId client, bool online)
    {
        lock (_sync)
        {
            if (client == SimulatorClientId.A) _aOnline = online; else _bOnline = online;
        }
    }

    internal void FailNextSend(SimulatorClientId client)
    {
        lock (_sync)
        {
            if (client == SimulatorClientId.A) _aFailNext = true; else _bFailNext = true;
        }
    }

    internal void Send(SimulatorClientId sender, CompanionCommand command)
    {
        var targetInbox = sender == SimulatorClientId.A ? _bInbox : _aInbox;
        lock (_sync)
        {
            if (!IsOnline(sender)) throw new IOException("The companion device disconnected.");
            var fail = sender == SimulatorClientId.A ? _aFailNext : _bFailNext;
            if (sender == SimulatorClientId.A) _aFailNext = false; else _bFailNext = false;
            if (fail) throw new IOException("The simulated transport rejected the command.");
            if (targetInbox.Count >= MaximumPendingPeerObservations)
                throw new IOException("The simulated peer receive queue is full.");
        }
        var observation = new CompanionObservation(
            command.Kind, command.Correlation, command.Text,
            command.Priority, command.AlertSeverity);
        targetInbox.Enqueue(observation);
    }

    internal CompanionObservation? Receive(SimulatorClientId client)
    {
        lock (_sync)
        {
            if (!IsOnline(client)) throw new IOException("The companion device disconnected.");
        }
        var inbox = client == SimulatorClientId.A ? _aInbox : _bInbox;
        return inbox.TryDequeue(out var item) ? item : null;
    }

    internal void Close(SimulatorClientId client)
    {
        lock (_sync)
        {
            SetOpen(client, false);
            var inbox = client == SimulatorClientId.A ? _aInbox : _bInbox;
            while (inbox.TryDequeue(out _)) { }
        }
    }

    private bool IsOnline(SimulatorClientId client) => client == SimulatorClientId.A ? _aOnline : _bOnline;
    private bool IsOpen(SimulatorClientId client) => client == SimulatorClientId.A ? _aOpen : _bOpen;
    private void SetOpen(SimulatorClientId client, bool value) { if (client == SimulatorClientId.A) _aOpen = value; else _bOpen = value; }
}

internal sealed class LoopbackTransport(LoopbackNetwork network, SimulatorClientId client)
    : ICompanionTransport
{
    private bool _disposed;

    public ValueTask SendAsync(CompanionCommand command, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ObjectDisposedException.ThrowIf(_disposed, this);
        network.Send(client, command);
        return ValueTask.CompletedTask;
    }

    public ValueTask<CompanionObservation?> ReceiveAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ObjectDisposedException.ThrowIf(_disposed, this);
        return ValueTask.FromResult(network.Receive(client));
    }

    public ValueTask DisposeAsync()
    {
        if (!_disposed) { _disposed = true; network.Close(client); }
        return ValueTask.CompletedTask;
    }
}
