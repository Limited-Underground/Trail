namespace OpenTrail.Simulator.Core;

public sealed class DualClientBridge : IAsyncDisposable
{
    public const int QueueCapacity = 32;
    public const int MessageHistoryCapacity = 128;
    public const int AlertHistoryCapacity = 32;
    private const int MaximumDiscoveries = 16;
    private const int MaximumReceivesPerService = 8;

    private readonly ICompanionDeviceDiscovery _discovery;
    private readonly ICompanionTransportFactory _transportFactory;
    private readonly ISimulatorClock _clock;
    private readonly TimeSpan _staleAfter;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ClientState[] _clients = [new(SimulatorClientId.A), new(SimulatorClientId.B)];
    private bool _disposed;

    public DualClientBridge(
        ICompanionDeviceDiscovery discovery,
        ICompanionTransportFactory transportFactory,
        ISimulatorClock clock,
        TimeSpan staleAfter)
    {
        _discovery = discovery ?? throw new ArgumentNullException(nameof(discovery));
        _transportFactory = transportFactory ?? throw new ArgumentNullException(nameof(transportFactory));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        if (staleAfter <= TimeSpan.Zero || staleAfter > TimeSpan.FromMinutes(5))
            throw new ArgumentOutOfRangeException(nameof(staleAfter));
        _staleAfter = staleAfter;
    }

    public event EventHandler<SimulatorClientSnapshotChangedEventArgs>? SnapshotChanged;

    public SimulatorClientSnapshot GetSnapshot(SimulatorClientId clientId)
    {
        _gate.Wait();
        try { ThrowIfDisposed(); return CreateSnapshot(State(clientId)); }
        finally { _gate.Release(); }
    }

    public async ValueTask ConnectAsync(
        SimulatorClientId clientId,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            if (state.Transport is not null) return;
            state.Connection = SimulatorConnectionState.Connecting;
            state.LastPublicError = null;
            try
            {
                var candidates = await _discovery.DiscoverAsync(cancellationToken).ConfigureAwait(false);
                if (candidates.Count > MaximumDiscoveries)
                    throw new InvalidDataException("Too many compatible companion devices were discovered.");
                AssignAvailableCandidates(candidates);
                if (state.Candidate is null)
                {
                    state.Connection = SimulatorConnectionState.Disconnected;
                    state.LastPublicError = "No unassigned compatible companion device is available.";
                    return;
                }
                state.Transport = await _transportFactory.OpenAsync(
                    state.Candidate, cancellationToken).ConfigureAwait(false);
                state.Connection = SimulatorConnectionState.Connected;
                state.LastObservationAt = _clock.UtcNow;
                AddSystem(state, "Connected to assigned companion.");
            }
            catch (OperationCanceledException)
            {
                state.Connection = SimulatorConnectionState.Disconnected;
                state.LastPublicError = null;
                throw;
            }
            catch
            {
                state.Connection = SimulatorConnectionState.Faulted;
                state.LastPublicError = "Compatible companion discovery or connection failed.";
            }
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    public async ValueTask DisconnectAsync(
        SimulatorClientId clientId,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            if (state.Transport is not null)
            {
                var transport = state.Transport;
                state.Transport = null;
                try { await transport.DisposeAsync().ConfigureAwait(false); }
                catch
                {
                    state.Connection = SimulatorConnectionState.Faulted;
                    state.LastPublicError = "The companion session did not close cleanly.";
                    return;
                }
            }
            state.Connection = SimulatorConnectionState.Disconnected;
            state.LastPublicError = null;
            AddSystem(state, "Disconnected from assigned companion.");
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    public async ValueTask ReconnectAsync(
        SimulatorClientId clientId,
        CancellationToken cancellationToken = default)
    {
        await DisconnectAsync(clientId, cancellationToken).ConfigureAwait(false);
        await ConnectAsync(clientId, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask EnqueueChatAsync(
        SimulatorClientId clientId,
        string text,
        SimulatorMessagePriority priority = SimulatorMessagePriority.Normal,
        CancellationToken cancellationToken = default) =>
        EnqueueAsync(clientId, CompanionCommandKind.Chat, ValidateText(text, 160),
            priority, null, null, cancellationToken);

    public ValueTask EnqueueQuickStatusAsync(
        SimulatorClientId clientId,
        SimulatorQuickStatus status,
        CancellationToken cancellationToken = default) =>
        EnqueueAsync(clientId, CompanionCommandKind.QuickStatus, QuickStatusText(status),
            SimulatorMessagePriority.Important, null, null, cancellationToken);

    public ValueTask EnqueueAlertAsync(
        SimulatorClientId clientId,
        string text,
        SimulatorAlertSeverity severity,
        CancellationToken cancellationToken = default) =>
        EnqueueAsync(clientId, CompanionCommandKind.Alert, ValidateText(text, 120),
            severity == SimulatorAlertSeverity.Critical
                ? SimulatorMessagePriority.Critical : SimulatorMessagePriority.Important,
            severity, null, cancellationToken);

    public async ValueTask AcknowledgeAlertAsync(
        SimulatorClientId clientId,
        long alertSequence,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            var alert = state.AlertDetails.FirstOrDefault(item =>
                item.Snapshot.LocalSequence == alertSequence &&
                item.Snapshot.Direction == SimulatorMessageDirection.Inbound &&
                item.Snapshot.State == SimulatorAlertState.Active);
            if (alert is null) throw new InvalidOperationException("Only an active received alert can be acknowledged.");
            EnsureQueueSpace(state);
            var sequence = NextSequence(state);
            state.Outgoing.Enqueue(new QueuedCommand(sequence, new CompanionCommand(
                CompanionCommandKind.Acknowledgement, alert.Correlation,
                "Acknowledged", SimulatorMessagePriority.Important, null)));
            AddMessage(state, new(sequence, SimulatorMessageDirection.Outbound,
                SimulatorMessageKind.Acknowledgement, SimulatorMessagePriority.Important,
                "Acknowledged", _clock.UtcNow, SimulatorDeliveryState.Queued));
            ReplaceAlert(state, alert, alert.Snapshot with { State = SimulatorAlertState.Acknowledged });
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    public async ValueTask ServiceAsync(
        SimulatorClientId clientId,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            if (state.Transport is null || state.Connection == SimulatorConnectionState.Faulted) return;
            try
            {
                if (state.Outgoing.TryPeek(out var queued))
                {
                    await state.Transport.SendAsync(queued.Command, cancellationToken).ConfigureAwait(false);
                    _ = state.Outgoing.Dequeue();
                    UpdateMessageDelivery(state, queued.LocalSequence,
                        SimulatorDeliveryState.BridgeAccepted);
                }

                for (var i = 0; i < MaximumReceivesPerService; i++)
                {
                    var observation = await state.Transport.ReceiveAsync(cancellationToken).ConfigureAwait(false);
                    if (observation is null) break;
                    state.LastObservationAt = _clock.UtcNow;
                    AcceptObservation(state, observation);
                }
                state.LastPublicError = null;
                state.Connection = SimulatorConnectionState.Connected;
            }
            catch (OperationCanceledException) { throw; }
            catch
            {
                state.Connection = SimulatorConnectionState.Faulted;
                state.LastPublicError = "The companion session stopped unexpectedly. Reconnect to continue.";
                if (state.Outgoing.TryPeek(out var failed))
                    UpdateMessageDelivery(state, failed.LocalSequence, SimulatorDeliveryState.Failed);
            }
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    public async ValueTask DisposeAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (_disposed) return;
            _disposed = true;
            foreach (var state in _clients)
            {
                var transport = state.Transport;
                state.Transport = null;
                try
                {
                    if (transport is not null)
                        await transport.DisposeAsync().ConfigureAwait(false);
                }
                catch
                {
                    // Continue closing the independent peer session.
                }
                finally
                {
                    state.Connection = SimulatorConnectionState.Disconnected;
                    state.Outgoing.Clear();
                }
            }
        }
        finally { _gate.Release(); }
    }

    private async ValueTask EnqueueAsync(
        SimulatorClientId clientId, CompanionCommandKind kind, string text,
        SimulatorMessagePriority priority, SimulatorAlertSeverity? severity,
        long? correlation, CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            EnsureQueueSpace(state);
            var sequence = NextSequence(state);
            var wireCorrelation = correlation ?? sequence;
            state.Outgoing.Enqueue(new(sequence,
                new(kind, wireCorrelation, text, priority, severity)));
            AddMessage(state, new(sequence, SimulatorMessageDirection.Outbound,
                ToMessageKind(kind), priority, text, _clock.UtcNow,
                SimulatorDeliveryState.Queued));
            if (kind == CompanionCommandKind.Alert)
                AddAlert(state, new(new(sequence, SimulatorMessageDirection.Outbound,
                    text, severity!.Value, SimulatorAlertState.Active, _clock.UtcNow), wireCorrelation));
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    private void AssignAvailableCandidates(IReadOnlyList<CompanionCandidate> candidates)
    {
        var distinct = candidates
            .Where(static item => item.PublicFamily is CompanionDeviceFamily.Simulated or
                CompanionDeviceFamily.HeltecV4Companion or CompanionDeviceFamily.WioTrackerL1Companion)
            .GroupBy(static item => item.Endpoint)
            .Select(static group => group.First())
            .ToArray();
        foreach (var state in _clients.Where(static item => item.Candidate is null))
        {
            var candidate = distinct.FirstOrDefault(item =>
                _clients.All(client => client.Candidate is null ||
                    !client.Candidate.Endpoint.Equals(item.Endpoint)));
            if (candidate is not null) state.Candidate = candidate;
        }
    }

    private void AcceptObservation(ClientState state, CompanionObservation observation)
    {
        var sequence = NextSequence(state);
        AddMessage(state, new(sequence, SimulatorMessageDirection.Inbound,
            ToMessageKind(observation.Kind), observation.Priority,
            ValidateText(observation.Text, 160), _clock.UtcNow,
            SimulatorDeliveryState.BridgeObserved));
        if (observation.Kind == CompanionCommandKind.Alert)
            AddAlert(state, new(new(sequence, SimulatorMessageDirection.Inbound,
                observation.Text, observation.AlertSeverity ?? SimulatorAlertSeverity.Important,
                SimulatorAlertState.Active, _clock.UtcNow), observation.Correlation));
        else if (observation.Kind == CompanionCommandKind.Acknowledgement)
        {
            var alert = state.AlertDetails.FirstOrDefault(item =>
                item.Correlation == observation.Correlation &&
                item.Snapshot.Direction == SimulatorMessageDirection.Outbound);
            if (alert is not null)
            {
                ReplaceAlert(state, alert, alert.Snapshot with { State = SimulatorAlertState.Acknowledged });
                UpdateMessageDelivery(state, alert.Snapshot.LocalSequence,
                    SimulatorDeliveryState.Acknowledged);
            }
        }
    }

    private SimulatorClientSnapshot CreateSnapshot(ClientState state)
    {
        TimeSpan? age = state.LastObservationAt.HasValue
            ? _clock.UtcNow - state.LastObservationAt.Value
            : null;
        var clockRollback = age < TimeSpan.Zero;
        if (clockRollback) age = TimeSpan.Zero;
        var stale = state.Connection == SimulatorConnectionState.Connected && age > _staleAfter;
        return new(state.Id, state.Id == SimulatorClientId.A ? "OpenTrail Client A" : "OpenTrail Client B",
            clockRollback ? SimulatorConnectionState.Faulted :
                stale ? SimulatorConnectionState.Stale : state.Connection,
            state.Candidate?.PublicLabel, state.Candidate?.PublicFamily,
            age, stale || clockRollback, Array.AsReadOnly(state.Messages.ToArray()),
            Array.AsReadOnly(state.AlertDetails.Select(static item => item.Snapshot).ToArray()),
            state.Outgoing.Count, QueueCapacity,
            clockRollback ? "The simulator clock moved backward; restart this session." : state.LastPublicError);
    }

    private void Publish(SimulatorClientId id)
    {
        if (_disposed) return;
        SimulatorClientSnapshot snapshot;
        _gate.Wait();
        try { snapshot = CreateSnapshot(State(id)); }
        finally { _gate.Release(); }
        var handlers = SnapshotChanged;
        if (handlers is null) return;
        foreach (EventHandler<SimulatorClientSnapshotChangedEventArgs> handler in
            handlers.GetInvocationList())
        {
            try { handler(this, new(snapshot)); }
            catch { /* A UI callback cannot unwind committed bridge state. */ }
        }
    }

    private ClientState State(SimulatorClientId id) => id switch
    {
        SimulatorClientId.A => _clients[0], SimulatorClientId.B => _clients[1],
        _ => throw new ArgumentOutOfRangeException(nameof(id)),
    };

    private static string ValidateText(string text, int maximum)
    {
        ArgumentNullException.ThrowIfNull(text);
        var value = text.Trim();
        if (value.Length is < 1 || value.Length > maximum ||
            value.Any(static character => char.IsControl(character)))
            throw new ArgumentException("Simulator text must be bounded printable text.", nameof(text));
        return value;
    }

    private static string QuickStatusText(SimulatorQuickStatus status) => status switch
    {
        SimulatorQuickStatus.ImOk => "I'm OK",
        SimulatorQuickStatus.NeedAssistance => "Need assistance",
        SimulatorQuickStatus.AnyoneOnline => "Anyone online?",
        SimulatorQuickStatus.AvailableToHelp => "Available to help",
        _ => throw new ArgumentOutOfRangeException(nameof(status)),
    };

    private static SimulatorMessageKind ToMessageKind(CompanionCommandKind kind) => kind switch
    {
        CompanionCommandKind.Chat => SimulatorMessageKind.Chat,
        CompanionCommandKind.QuickStatus => SimulatorMessageKind.QuickStatus,
        CompanionCommandKind.Alert => SimulatorMessageKind.Alert,
        CompanionCommandKind.Acknowledgement => SimulatorMessageKind.Acknowledgement,
        _ => throw new ArgumentOutOfRangeException(nameof(kind)),
    };

    private static void EnsureQueueSpace(ClientState state)
    {
        if (state.Outgoing.Count >= QueueCapacity)
            throw new InvalidOperationException("The client command queue is full.");
    }

    private static long NextSequence(ClientState state) => checked(++state.NextSequence);

    private void AddSystem(ClientState state, string text) => AddMessage(state,
        new(NextSequence(state), SimulatorMessageDirection.Local, SimulatorMessageKind.System,
            SimulatorMessagePriority.Normal, text, _clock.UtcNow,
            SimulatorDeliveryState.BridgeObserved));

    private static void AddMessage(ClientState state, MessageSnapshot message)
    {
        state.Messages.Add(message);
        if (state.Messages.Count > MessageHistoryCapacity) state.Messages.RemoveAt(0);
    }

    private static void AddAlert(ClientState state, AlertDetail alert)
    {
        state.AlertDetails.Add(alert);
        if (state.AlertDetails.Count > AlertHistoryCapacity) state.AlertDetails.RemoveAt(0);
    }

    private static void ReplaceAlert(ClientState state, AlertDetail existing, AlertSnapshot replacement)
    {
        var index = state.AlertDetails.IndexOf(existing);
        if (index >= 0) state.AlertDetails[index] = existing with { Snapshot = replacement };
    }

    private static void UpdateMessageDelivery(
        ClientState state, long sequence, SimulatorDeliveryState delivery)
    {
        var index = state.Messages.FindIndex(item => item.LocalSequence == sequence);
        if (index >= 0) state.Messages[index] = state.Messages[index] with { DeliveryState = delivery };
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    private sealed class ClientState(SimulatorClientId id)
    {
        internal SimulatorClientId Id { get; } = id;
        internal SimulatorConnectionState Connection { get; set; }
        internal CompanionCandidate? Candidate { get; set; }
        internal ICompanionTransport? Transport { get; set; }
        internal Queue<QueuedCommand> Outgoing { get; } = new();
        internal List<MessageSnapshot> Messages { get; } = [];
        internal List<AlertDetail> AlertDetails { get; } = [];
        internal long NextSequence { get; set; }
        internal DateTimeOffset? LastObservationAt { get; set; }
        internal string? LastPublicError { get; set; }
    }

    private sealed record QueuedCommand(long LocalSequence, CompanionCommand Command);
    private sealed record AlertDetail(AlertSnapshot Snapshot, long Correlation);
}
