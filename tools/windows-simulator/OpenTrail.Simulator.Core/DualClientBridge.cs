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
    private readonly object _choiceOwner = new();
    private readonly ClientState[] _clients = [new(SimulatorClientId.A), new(SimulatorClientId.B)];
    private IReadOnlyList<CompanionCandidate> _deviceRoster = [];
    private long _deviceRosterRevision;
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

    public IReadOnlyList<SimulatorDeviceChoice> GetDeviceChoices()
    {
        _gate.Wait();
        try { ThrowIfDisposed(); return CreateDeviceChoices(); }
        finally { _gate.Release(); }
    }

    public async ValueTask<IReadOnlyList<SimulatorDeviceChoice>> RefreshDeviceChoicesAsync(
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            if (_clients.Any(static state => state.Transport is not null ||
                    state.Connection == SimulatorConnectionState.Connecting))
                throw new InvalidOperationException(
                    "Disconnect both clients before refreshing companion choices.");

            IReadOnlyList<CompanionCandidate> discovered;
            try
            {
                discovered = await _discovery.DiscoverAsync(cancellationToken)
                    .ConfigureAwait(false);
                cancellationToken.ThrowIfCancellationRequested();
                discovered = NormalizeCandidates(discovered);
            }
            catch (OperationCanceledException) { throw; }
            catch
            {
                throw new InvalidOperationException(
                    "Compatible companion discovery failed.");
            }

            var nextRevision = checked(_deviceRosterRevision + 1);
            foreach (var state in _clients)
            {
                if (state.Candidate is not null && state.Outgoing.Count != 0 &&
                    discovered.All(candidate =>
                        !candidate.Endpoint.Equals(state.Candidate.Endpoint)))
                    throw new InvalidOperationException(
                        "Resolve queued commands before refreshing a missing companion assignment.");
            }

            _deviceRoster = discovered;
            _deviceRosterRevision = nextRevision;
            foreach (var state in _clients)
            {
                if (state.Candidate is null) continue;
                var current = _deviceRoster.FirstOrDefault(candidate =>
                    candidate.Endpoint.Equals(state.Candidate.Endpoint));
                if (current is not null)
                {
                    state.Candidate = current;
                    continue;
                }

                state.Candidate = null;
                state.LastObservationAt = null;
                state.LastPublicError =
                    "The previously selected companion is no longer available.";
            }
            return CreateDeviceChoices();
        }
        finally
        {
            _gate.Release();
            Publish(SimulatorClientId.A);
            Publish(SimulatorClientId.B);
        }
    }

    public async ValueTask SelectDeviceAsync(
        SimulatorClientId clientId,
        SimulatorDeviceChoice choice,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(choice);
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            EnsureSelectionMayChange(state);
            if (!ReferenceEquals(choice.BridgeOwner, _choiceOwner) ||
                choice.RosterRevision != _deviceRosterRevision)
                throw new InvalidOperationException(
                    "The companion choice is stale; refresh and select again.");
            var candidate = _deviceRoster.FirstOrDefault(item =>
                item.Endpoint.Equals(choice.Candidate.Endpoint));
            if (candidate is null ||
                candidate.PublicFamily != choice.PublicFamily ||
                !string.Equals(candidate.PublicLabel, choice.PublicLabel,
                    StringComparison.Ordinal))
                throw new InvalidOperationException(
                    "The companion choice does not belong to the current roster.");
            if (_clients.Any(other => other.Id != clientId &&
                    other.Candidate is not null &&
                    other.Candidate.Endpoint.Equals(candidate.Endpoint)))
                throw new InvalidOperationException(
                    "That companion is already assigned to the other client.");

            state.Candidate = candidate;
            state.LastObservationAt = null;
            state.LastPublicError = candidate.ConnectionReady
                ? null
                : "The selected USB companion is recognized, but live connection is not available.";
        }
        finally
        {
            _gate.Release();
            Publish(SimulatorClientId.A);
            Publish(SimulatorClientId.B);
        }
    }

    public async ValueTask ForgetDeviceAsync(
        SimulatorClientId clientId,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            EnsureSelectionMayChange(state);
            state.Candidate = null;
            state.LastObservationAt = null;
            state.LastPublicError = null;
        }
        finally
        {
            _gate.Release();
            Publish(SimulatorClientId.A);
            Publish(SimulatorClientId.B);
        }
    }

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
                if (state.Candidate is null)
                {
                    AssignAvailableCandidates(_deviceRoster.Where(static candidate =>
                        candidate.Source == SimulatorConnectionSource.LocalSimulation &&
                        candidate.ConnectionReady).ToArray());
                    if (state.Candidate is null && _deviceRoster.Count == 0)
                    {
                        var candidates = NormalizeCandidates(
                            await _discovery.DiscoverAsync(cancellationToken).ConfigureAwait(false));
                        var nextRevision = checked(_deviceRosterRevision + 1);
                        _deviceRoster = candidates;
                        _deviceRosterRevision = nextRevision;
                        AssignAvailableCandidates(candidates.Where(static candidate =>
                            candidate.Source == SimulatorConnectionSource.LocalSimulation &&
                            candidate.ConnectionReady).ToArray());
                    }
                }
                if (state.Candidate is null)
                {
                    state.Connection = SimulatorConnectionState.Disconnected;
                    state.LastPublicError = "No unassigned compatible companion device is available.";
                    return;
                }
                if (!state.Candidate.ConnectionReady)
                {
                    state.Connection = SimulatorConnectionState.Faulted;
                    state.LastPublicError =
                        "The selected USB companion is recognized, but live connection is not available.";
                    return;
                }
                var nextSessionGeneration = checked(state.SessionGeneration + 1);
                state.Transport = await _transportFactory.OpenAsync(
                    state.Candidate, cancellationToken).ConfigureAwait(false) ??
                    throw new InvalidOperationException(
                        "The companion transport factory returned no session.");
                state.SessionGeneration = nextSessionGeneration;
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
            FailAndClearQueued(state);
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

    public async ValueTask EnqueueChatAsync(
        SimulatorClientId clientId,
        string text,
        SimulatorMessagePriority priority = SimulatorMessagePriority.Normal,
        CancellationToken cancellationToken = default) =>
        _ = await EnqueueAsync(clientId, CompanionCommandKind.Chat,
            ValidateText(text, 160), priority, null, null, null,
            cancellationToken).ConfigureAwait(false);

    public async ValueTask EnqueueQuickStatusAsync(
        SimulatorClientId clientId,
        SimulatorQuickStatus status,
        CancellationToken cancellationToken = default) =>
        _ = await EnqueueAsync(clientId, CompanionCommandKind.QuickStatus,
            QuickStatusText(status), SimulatorMessagePriority.Important,
            null, null, null, cancellationToken).ConfigureAwait(false);

    public async ValueTask EnqueueAlertAsync(
        SimulatorClientId clientId,
        string text,
        SimulatorAlertSeverity severity,
        CancellationToken cancellationToken = default) =>
        _ = await EnqueueAsync(clientId, CompanionCommandKind.Alert, ValidateText(text, 120),
            severity == SimulatorAlertSeverity.Critical
                ? SimulatorMessagePriority.Critical : SimulatorMessagePriority.Important,
            severity, null, null, cancellationToken).ConfigureAwait(false);

    public ValueTask<SimulatorCommandAdmission> EnqueueTemplateChatForSessionAsync(
        SimulatorClientId clientId,
        int templateId,
        string exactText,
        long expectedSessionEpoch,
        CancellationToken cancellationToken = default)
    {
        if (templateId is < 1 or > 8)
            throw new ArgumentOutOfRangeException(nameof(templateId));
        return EnqueueAsync(clientId, CompanionCommandKind.Chat,
            ValidatePortableRequestText(exactText, 96),
            SimulatorMessagePriority.Normal, null, null, expectedSessionEpoch,
            cancellationToken);
    }

    public ValueTask<SimulatorCommandAdmission> EnqueueQuickStatusForSessionAsync(
        SimulatorClientId clientId,
        SimulatorQuickStatus status,
        long expectedSessionEpoch,
        CancellationToken cancellationToken = default) =>
        EnqueueAsync(clientId, CompanionCommandKind.QuickStatus,
            QuickStatusText(status), SimulatorMessagePriority.Important,
            null, null, expectedSessionEpoch, cancellationToken);

    public ValueTask<SimulatorCommandAdmission> EnqueueCriticalAlertForSessionAsync(
        SimulatorClientId clientId,
        long expectedSessionEpoch,
        CancellationToken cancellationToken = default) =>
        EnqueueAsync(clientId, CompanionCommandKind.Alert, "Critical alert",
            SimulatorMessagePriority.Critical, SimulatorAlertSeverity.Critical,
            null, expectedSessionEpoch, cancellationToken);

    public async ValueTask AcknowledgeAlertAsync(
        SimulatorClientId clientId,
        long alertSequence,
        CancellationToken cancellationToken = default)
    {
        _ = await AcknowledgeAlertCoreAsync(
            clientId, alertSequence, null, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask<SimulatorCommandAdmission> AcknowledgeAlertForSessionAsync(
        SimulatorClientId clientId,
        long alertSequence,
        long expectedSessionEpoch,
        CancellationToken cancellationToken = default) =>
        AcknowledgeAlertCoreAsync(
            clientId, alertSequence, expectedSessionEpoch, cancellationToken);

    private async ValueTask<SimulatorCommandAdmission> AcknowledgeAlertCoreAsync(
        SimulatorClientId clientId,
        long alertSequence,
        long? expectedSessionEpoch,
        CancellationToken cancellationToken)
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
            EnsureOpenSession(state);
            EnsureExpectedSession(state, expectedSessionEpoch);
            var appliedEpoch = state.SessionGeneration;
            var sequence = NextSequence(state);
            state.Outgoing.Enqueue(new QueuedCommand(sequence, state.Candidate!.Endpoint,
                state.SessionGeneration, new CompanionCommand(
                CompanionCommandKind.Acknowledgement, alert.Correlation,
                "Acknowledged", SimulatorMessagePriority.Important, null)));
            AddMessage(state, new(sequence, SimulatorMessageDirection.Outbound,
                SimulatorMessageKind.Acknowledgement, SimulatorMessagePriority.Important,
                "Acknowledged", _clock.UtcNow, SimulatorDeliveryState.Queued));
            ReplaceAlert(state, alert, alert.Snapshot with
            {
                State = SimulatorAlertState.LocalAcknowledgementQueued,
            });
            return new(appliedEpoch, sequence);
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
                    if (state.Candidate is null ||
                        !state.Candidate.Endpoint.Equals(queued.Endpoint) ||
                        state.SessionGeneration != queued.SessionGeneration)
                    {
                        FailAndClearQueued(state);
                        state.Connection = SimulatorConnectionState.Faulted;
                        state.LastPublicError =
                            "A queued command no longer belongs to the active companion session.";
                        return;
                    }
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
                FailAndClearQueued(state);
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

    private async ValueTask<SimulatorCommandAdmission> EnqueueAsync(
        SimulatorClientId clientId, CompanionCommandKind kind, string text,
        SimulatorMessagePriority priority, SimulatorAlertSeverity? severity,
        long? correlation, long? expectedSessionEpoch,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var state = State(clientId);
            EnsureQueueSpace(state);
            EnsureOpenSession(state);
            EnsureExpectedSession(state, expectedSessionEpoch);
            var appliedEpoch = state.SessionGeneration;
            var sequence = NextSequence(state);
            var wireCorrelation = correlation ?? sequence;
            state.Outgoing.Enqueue(new(sequence, state.Candidate!.Endpoint,
                state.SessionGeneration,
                new(kind, wireCorrelation, text, priority, severity)));
            AddMessage(state, new(sequence, SimulatorMessageDirection.Outbound,
                ToMessageKind(kind), priority, text, _clock.UtcNow,
                SimulatorDeliveryState.Queued));
            if (kind == CompanionCommandKind.Alert)
                AddAlert(state, new(new(sequence, SimulatorMessageDirection.Outbound,
                    text, severity!.Value, SimulatorAlertState.Active, _clock.UtcNow), wireCorrelation));
            return new(appliedEpoch, sequence);
        }
        finally { _gate.Release(); Publish(clientId); }
    }

    private void AssignAvailableCandidates(IReadOnlyList<CompanionCandidate> candidates)
    {
        foreach (var state in _clients.Where(static item => item.Candidate is null))
        {
            var candidate = candidates
                .OrderByDescending(static item => item.ConnectionReady)
                .FirstOrDefault(item =>
                _clients.All(client => client.Candidate is null ||
                    !client.Candidate.Endpoint.Equals(item.Endpoint)));
            if (candidate is not null) state.Candidate = candidate;
        }
    }

    private static IReadOnlyList<CompanionCandidate> NormalizeCandidates(
        IReadOnlyList<CompanionCandidate> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        if (candidates.Count > MaximumDiscoveries)
            throw new InvalidDataException(
                "Too many compatible companion devices were discovered.");
        if (candidates.Any(static item => item is null))
            throw new InvalidDataException("Companion discovery returned an invalid entry.");
        return candidates
            .Where(static item => item.PublicFamily is CompanionDeviceFamily.Simulated or
                CompanionDeviceFamily.Esp32S3UsbCandidate or
                CompanionDeviceFamily.HeltecV4Companion or CompanionDeviceFamily.WioTrackerL1Companion)
            .GroupBy(static item => item.Endpoint)
            .Select(static group => group.First())
            .ToArray();
    }

    private IReadOnlyList<SimulatorDeviceChoice> CreateDeviceChoices() =>
        Array.AsReadOnly(_deviceRoster.Select(candidate =>
        {
            var assigned = _clients.FirstOrDefault(state => state.Candidate is not null &&
                state.Candidate.Endpoint.Equals(candidate.Endpoint));
            return new SimulatorDeviceChoice(
                candidate, _deviceRosterRevision, _choiceOwner, assigned?.Id);
        }).ToArray());

    private static void EnsureSelectionMayChange(ClientState state)
    {
        if (state.Transport is not null ||
            state.Connection is SimulatorConnectionState.Connecting or
                SimulatorConnectionState.Connected or SimulatorConnectionState.Stale)
            throw new InvalidOperationException(
                "Disconnect this client before changing its companion selection.");
        if (state.Outgoing.Count != 0)
            throw new InvalidOperationException(
                "Clear or deliver queued commands before changing companion selection.");
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
                ReplaceAlert(state, alert, alert.Snapshot with
                {
                    State = SimulatorAlertState.BridgeAcknowledgementObserved,
                });
                UpdateMessageDelivery(state, alert.Snapshot.LocalSequence,
                    SimulatorDeliveryState.BridgeAcknowledgementObserved);
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
            age, stale || clockRollback,
            !clockRollback && !stale &&
                    state.Connection == SimulatorConnectionState.Connected
                ? state.SessionGeneration
                : 0,
            Array.AsReadOnly(state.Messages.ToArray()),
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

    private static string ValidatePortableRequestText(string text, int maximum)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (text.Length is < 1 || text.Length > maximum ||
            text.Any(static character => character is < ' ' or > '~'))
            throw new ArgumentException(
                "Portable request text must be bounded printable ASCII.", nameof(text));
        return text;
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

    private static void EnsureOpenSession(ClientState state)
    {
        if (state.Transport is null || state.Candidate is null ||
            state.Connection != SimulatorConnectionState.Connected ||
            state.SessionGeneration <= 0)
            throw new InvalidOperationException(
                "Connect this client to its selected companion before queuing a command.");
    }

    private static void EnsureExpectedSession(
        ClientState state,
        long? expectedSessionEpoch)
    {
        if (expectedSessionEpoch.HasValue &&
            (expectedSessionEpoch.Value <= 0 ||
             expectedSessionEpoch.Value != state.SessionGeneration))
            throw new InvalidOperationException(
                "The command no longer belongs to the connected companion session.");
    }

    private static void FailAndClearQueued(ClientState state)
    {
        while (state.Outgoing.TryDequeue(out var queued))
            UpdateMessageDelivery(state, queued.LocalSequence, SimulatorDeliveryState.Failed);
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
        internal long SessionGeneration { get; set; }
        internal DateTimeOffset? LastObservationAt { get; set; }
        internal string? LastPublicError { get; set; }
    }

    private sealed record QueuedCommand(
        long LocalSequence,
        CompanionEndpoint Endpoint,
        long SessionGeneration,
        CompanionCommand Command);
    private sealed record AlertDetail(AlertSnapshot Snapshot, long Correlation);
}
