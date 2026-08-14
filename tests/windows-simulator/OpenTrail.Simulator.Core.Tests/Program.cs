using OpenTrail.Simulator.Core;

var failures = 0;
var groups = 0;

void Expect(bool condition, string message)
{
    if (condition) return;
    Console.Error.WriteLine($"FAIL: {message}");
    failures++;
}

async Task ConnectBoth(LocalLoopbackSimulator simulator)
{
    await simulator.Bridge.ConnectAsync(SimulatorClientId.A);
    await simulator.Bridge.ConnectAsync(SimulatorClientId.B);
}

groups++;
await using (var simulator = LocalLoopbackSimulator.Create().Bridge)
{
    var a = simulator.GetSnapshot(SimulatorClientId.A);
    var b = simulator.GetSnapshot(SimulatorClientId.B);
    Expect(a.ClientLabel == "OpenTrail Client A" && b.ClientLabel == "OpenTrail Client B",
        "the two clients must have stable public labels");
    Expect(a.Messages.Count == 0 && b.Messages.Count == 0,
        "new clients must have isolated empty histories");
}

groups++;
var connectedHarness = LocalLoopbackSimulator.Create();
await using (var bridge = connectedHarness.Bridge)
{
    await ConnectBoth(connectedHarness);
    var a = bridge.GetSnapshot(SimulatorClientId.A);
    var b = bridge.GetSnapshot(SimulatorClientId.B);
    Expect(a.ConnectionState == SimulatorConnectionState.Connected &&
        b.ConnectionState == SimulatorConnectionState.Connected,
        "both clients must connect independently");
    Expect(a.PublicDeviceLabel != b.PublicDeviceLabel,
        "a companion endpoint must not be assigned to both clients");

    await bridge.EnqueueChatAsync(SimulatorClientId.A, "Trail check");
    await bridge.ServiceAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.B).Messages.Count == 1,
        "peer history must remain unchanged until its own session is serviced");
    await bridge.ServiceAsync(SimulatorClientId.B);
    var received = bridge.GetSnapshot(SimulatorClientId.B).Messages.Last();
    Expect(received.Kind == SimulatorMessageKind.Chat &&
        received.Direction == SimulatorMessageDirection.Inbound &&
        received.Text == "Trail check",
        "a bounded chat command must cross the loopback bridge");
}

groups++;
var alertHarness = LocalLoopbackSimulator.Create();
await using (var bridge = alertHarness.Bridge)
{
    await ConnectBoth(alertHarness);
    await bridge.EnqueueAlertAsync(SimulatorClientId.A, "Recovery needed",
        SimulatorAlertSeverity.Critical);
    await bridge.ServiceAsync(SimulatorClientId.A);
    await bridge.ServiceAsync(SimulatorClientId.B);
    var inbound = bridge.GetSnapshot(SimulatorClientId.B).Alerts.Single();
    await bridge.AcknowledgeAlertAsync(SimulatorClientId.B, inbound.LocalSequence);
    await bridge.ServiceAsync(SimulatorClientId.B);
    await bridge.ServiceAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).Alerts.Single().State ==
        SimulatorAlertState.BridgeAcknowledgementObserved,
        "an acknowledgement must correlate only to the matching outbound alert");
}

groups++;
var queueHarness = LocalLoopbackSimulator.Create();
await using (var bridge = queueHarness.Bridge)
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    for (var index = 0; index < DualClientBridge.QueueCapacity; index++)
        await bridge.EnqueueChatAsync(SimulatorClientId.A, $"queued {index}");
    try
    {
        await bridge.EnqueueChatAsync(SimulatorClientId.A, "overflow");
        Expect(false, "a full client queue must reject another command");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.B).OutgoingQueueCount == 0,
        "one client's full queue must not affect its peer");
}

groups++;
var reconnectHarness = LocalLoopbackSimulator.Create();
await using (var bridge = reconnectHarness.Bridge)
{
    await ConnectBoth(reconnectHarness);
    reconnectHarness.SetOnline(SimulatorClientId.A, false);
    await bridge.ServiceAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Faulted,
        "transport loss must fail the affected client closed");
    Expect(bridge.GetSnapshot(SimulatorClientId.B).ConnectionState ==
        SimulatorConnectionState.Connected,
        "transport loss must not fault the peer client");
    reconnectHarness.SetOnline(SimulatorClientId.A, true);
    await bridge.ReconnectAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Connected,
        "reconnect must release and reopen the retained private assignment");
}

groups++;
var clock = new ManualClock(DateTimeOffset.Parse("2026-08-14T12:00:00Z"));
var staleHarness = LocalLoopbackSimulator.Create(clock, TimeSpan.FromSeconds(5));
await using (var bridge = staleHarness.Bridge)
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    clock.Advance(TimeSpan.FromSeconds(6));
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Stale,
        "an otherwise connected session must become stale after the configured interval");
    clock.Advance(TimeSpan.FromSeconds(-7));
    var rollback = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(rollback.ConnectionState == SimulatorConnectionState.Faulted &&
        rollback.LastPublicError is not null,
        "clock rollback must produce only a public-safe fail-closed snapshot");
}

groups++;
var boundedHarness = LocalLoopbackSimulator.Create();
await using (var bridge = boundedHarness.Bridge)
{
    await ConnectBoth(boundedHarness);
    for (var index = 0; index < 140; index++)
    {
        await bridge.EnqueueChatAsync(SimulatorClientId.A, $"message {index}");
        await bridge.ServiceAsync(SimulatorClientId.A);
        await bridge.ServiceAsync(SimulatorClientId.B);
    }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).Messages.Count ==
        DualClientBridge.MessageHistoryCapacity,
        "message history must remain fixed-capacity");
    Expect(bridge.GetSnapshot(SimulatorClientId.B).Messages.Count ==
        DualClientBridge.MessageHistoryCapacity,
        "peer message history must remain fixed-capacity independently");
}

groups++;
try
{
    var harness = LocalLoopbackSimulator.Create();
    await harness.Bridge.EnqueueChatAsync(SimulatorClientId.A, "bad\ntext");
    Expect(false, "control characters must be rejected before transport admission");
}
catch (ArgumentException) { }

groups++;
var eventHarness = LocalLoopbackSimulator.Create();
await using (var bridge = eventHarness.Bridge)
{
    var events = 0;
    bridge.SnapshotChanged += (_, args) =>
    {
        events++;
        _ = args.Snapshot.ClientLabel;
    };
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.EnqueueQuickStatusAsync(SimulatorClientId.A,
        SimulatorQuickStatus.AnyoneOnline);
    Expect(events == 2, "connect and enqueue must publish deterministic snapshot changes");
}

groups++;
var throwingEventHarness = LocalLoopbackSimulator.Create();
await using (var bridge = throwingEventHarness.Bridge)
{
    bridge.SnapshotChanged += static (_, _) => throw new InvalidOperationException("subscriber");
    await bridge.ConnectAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Connected,
        "a throwing UI subscriber must not unwind committed bridge state");
}

groups++;
var staleGenerationHarness = LocalLoopbackSimulator.Create();
await using (var bridge = staleGenerationHarness.Bridge)
{
    await ConnectBoth(staleGenerationHarness);
    await bridge.EnqueueChatAsync(SimulatorClientId.A, "old session");
    await bridge.ServiceAsync(SimulatorClientId.A);
    await bridge.ReconnectAsync(SimulatorClientId.B);
    var before = bridge.GetSnapshot(SimulatorClientId.B).Messages.Count;
    await bridge.ServiceAsync(SimulatorClientId.B);
    Expect(bridge.GetSnapshot(SimulatorClientId.B).Messages.Count == before,
        "a reconnect must discard an observation queued for the prior session");
}

groups++;
var boundedPeerHarness = LocalLoopbackSimulator.Create();
await using (var bridge = boundedPeerHarness.Bridge)
{
    await ConnectBoth(boundedPeerHarness);
    for (var index = 0; index < 65; index++)
    {
        await bridge.EnqueueChatAsync(SimulatorClientId.A, $"unserviced peer {index}");
        await bridge.ServiceAsync(SimulatorClientId.A);
    }
    var bounded = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(bounded.ConnectionState == SimulatorConnectionState.Faulted &&
        bounded.OutgoingQueueCount == 0 &&
        bounded.Messages.Last().DeliveryState == SimulatorDeliveryState.Failed,
        "an unserviced synthetic peer inbox must fail the exact command without retaining it for another session");
    for (var index = 0; index < 8; index++)
        await bridge.ServiceAsync(SimulatorClientId.B);
    await bridge.ReconnectAsync(SimulatorClientId.A);
    await bridge.ServiceAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).OutgoingQueueCount == 0,
        "a prior-session command must never retry after backlog recovery and reconnect");
}

groups++;
var sharedEndpoint = CompanionEndpoint.CreateSessionPrivate();
var duplicateDiscovery = new FixedDiscovery(
[
    new(sharedEndpoint, "Simulated companion A", CompanionDeviceFamily.Simulated),
    new(sharedEndpoint, "Simulated companion B", CompanionDeviceFamily.Simulated),
]);
await using (var bridge = new DualClientBridge(
    duplicateDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.ConnectAsync(SimulatorClientId.B);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Connected &&
        bridge.GetSnapshot(SimulatorClientId.B).ConnectionState ==
        SimulatorConnectionState.Disconnected,
        "duplicate discovery records must never grant one endpoint to two clients");
}

groups++;
try
{
    _ = new CompanionCandidate(CompanionEndpoint.CreateSessionPrivate(),
        "private transport label", CompanionDeviceFamily.HeltecV4Companion);
    Expect(false, "a raw port-like public label must be rejected");
}
catch (ArgumentException) { }

groups++;
await using (var bridge = new DualClientBridge(
    new ThrowingDiscovery(), new NoopTransportFactory(),
    new ManualClock(DateTimeOffset.UtcNow), TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    var failed = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(failed.ConnectionState == SimulatorConnectionState.Faulted &&
        failed.LastPublicError == "Compatible companion discovery or connection failed.",
        "discovery failures must be reduced to a fixed public-safe error");
}

groups++;
await using (var bridge = new DualClientBridge(
    new CancelledDiscovery(), new NoopTransportFactory(),
    new ManualClock(DateTimeOffset.UtcNow), TimeSpan.FromSeconds(5)))
{
    try
    {
        await bridge.ConnectAsync(SimulatorClientId.A);
        Expect(false, "discovery cancellation must propagate");
    }
    catch (OperationCanceledException) { }
    var cancelled = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(cancelled.ConnectionState == SimulatorConnectionState.Disconnected &&
        cancelled.LastPublicError is null,
        "discovery cancellation must leave a coherent disconnected state");
}

groups++;
var closeFactory = new CloseTrackingFactory();
var closeDiscovery = new FixedDiscovery(
[
    new(CompanionEndpoint.CreateSessionPrivate(), "Simulated companion A",
        CompanionDeviceFamily.Simulated),
    new(CompanionEndpoint.CreateSessionPrivate(), "Simulated companion B",
        CompanionDeviceFamily.Simulated),
]);
await using (var bridge = new DualClientBridge(
    closeDiscovery, closeFactory, new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.ConnectAsync(SimulatorClientId.B);
    await bridge.DisposeAsync();
    Expect(closeFactory.CloseAttempts == 2,
        "a close failure in one transport must not prevent independent peer cleanup");
}

groups++;
var choiceEndpointA = CompanionEndpoint.CreateSessionPrivate();
var choiceEndpointB = CompanionEndpoint.CreateSessionPrivate();
var choiceDiscovery = new FixedDiscovery(
[
    new(choiceEndpointA, "Simulated companion A", CompanionDeviceFamily.Simulated),
    new(choiceEndpointB, "Simulated companion B", CompanionDeviceFamily.Simulated),
]);
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var choices = await bridge.RefreshDeviceChoicesAsync();
    Expect(choices.Count == 2 && choices.All(choice =>
            choice.Source == SimulatorConnectionSource.LocalSimulation &&
            choice.ConnectionReady && choice.IsAvailable),
        "refresh must expose only public-safe ready local choices");
    Expect(choices.All(choice => choice.ToString() == choice.PublicLabel &&
            !choice.ToString().Contains("private", StringComparison.OrdinalIgnoreCase)),
        "choice rendering must contain only its allowlisted public label");

    var selected = choices.Single(choice => choice.PublicLabel == "Simulated companion B");
    await bridge.SelectDeviceAsync(SimulatorClientId.A, selected);
    var assigned = bridge.GetDeviceChoices().Single(choice =>
        choice.PublicLabel == "Simulated companion B");
    Expect(assigned.AssignedClient == SimulatorClientId.A && !assigned.IsAvailable,
        "explicit selection must publish only the owning client label");
    await bridge.ConnectAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).PublicDeviceLabel ==
        "Simulated companion B",
        "connect must use the exact explicitly selected companion");
}

groups++;
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var choices = await bridge.RefreshDeviceChoicesAsync();
    var changedClients = new HashSet<SimulatorClientId>();
    bridge.SnapshotChanged += static (_, _) =>
        throw new InvalidOperationException("synthetic subscriber failure");
    bridge.SnapshotChanged += (_, args) =>
        changedClients.Add(args.Snapshot.ClientId);

    var selected = choices.Single(choice => choice.PublicLabel == "Simulated companion A");
    await bridge.SelectDeviceAsync(SimulatorClientId.A, selected);
    var assigned = bridge.GetDeviceChoices().Single(choice =>
        choice.PublicLabel == selected.PublicLabel);
    Expect(changedClients.SetEquals([SimulatorClientId.A, SimulatorClientId.B]) &&
        assigned.AssignedClient == SimulatorClientId.A && !assigned.IsAvailable,
        "selection must notify both clients despite an independent subscriber failure");

    changedClients.Clear();
    await bridge.ForgetDeviceAsync(SimulatorClientId.A);
    var available = bridge.GetDeviceChoices().Single(choice =>
        choice.PublicLabel == selected.PublicLabel);
    Expect(changedClients.SetEquals([SimulatorClientId.A, SimulatorClientId.B]) &&
        available.AssignedClient is null && available.IsAvailable,
        "forget must notify both clients when a shared companion becomes available");
}

groups++;
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var choices = await bridge.RefreshDeviceChoicesAsync();
    var selected = choices[0];
    await bridge.SelectDeviceAsync(SimulatorClientId.A, selected);
    try
    {
        await bridge.SelectDeviceAsync(SimulatorClientId.B, selected);
        Expect(false, "one choice must not be assigned to both clients");
    }
    catch (InvalidOperationException) { }
}

groups++;
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var stale = (await bridge.RefreshDeviceChoicesAsync())[0];
    _ = await bridge.RefreshDeviceChoicesAsync();
    try
    {
        await bridge.SelectDeviceAsync(SimulatorClientId.A, stale);
        Expect(false, "a choice from an earlier roster revision must be rejected");
    }
    catch (InvalidOperationException) { }

    var foreignBridge = new DualClientBridge(
        new FixedDiscovery(
        [new(CompanionEndpoint.CreateSessionPrivate(), "Simulated companion A",
            CompanionDeviceFamily.Simulated)]),
        new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
        TimeSpan.FromSeconds(5));
    var foreign = (await foreignBridge.RefreshDeviceChoicesAsync())[0];
    try
    {
        await bridge.SelectDeviceAsync(SimulatorClientId.A, foreign);
        Expect(false, "a choice from another bridge must be rejected");
    }
    catch (InvalidOperationException) { }
    await foreignBridge.DisposeAsync();
}

groups++;
var sharedChoiceDiscovery = new FixedDiscovery(
[
    new(choiceEndpointA, "Simulated companion A", CompanionDeviceFamily.Simulated),
]);
await using (var firstBridge = new DualClientBridge(
    sharedChoiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
await using (var secondBridge = new DualClientBridge(
    sharedChoiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var foreignSameEndpointAndRevision =
        (await firstBridge.RefreshDeviceChoicesAsync()).Single();
    _ = await secondBridge.RefreshDeviceChoicesAsync();
    try
    {
        await secondBridge.SelectDeviceAsync(
            SimulatorClientId.A, foreignSameEndpointAndRevision);
        Expect(false, "a choice must be bound to its exact bridge instance");
    }
    catch (InvalidOperationException) { }
}

groups++;
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var selected = (await bridge.RefreshDeviceChoicesAsync())[0];
    await bridge.SelectDeviceAsync(SimulatorClientId.A, selected);
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.EnqueueChatAsync(SimulatorClientId.A, "retained queue");
    try
    {
        await bridge.ForgetDeviceAsync(SimulatorClientId.A);
        Expect(false, "queued work must block companion reassignment");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).PublicDeviceLabel ==
        selected.PublicLabel,
        "failed forget must preserve the selected companion");
    await bridge.DisconnectAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).OutgoingQueueCount == 0 &&
        bridge.GetSnapshot(SimulatorClientId.A).Messages.Last(message =>
            message.Direction == SimulatorMessageDirection.Outbound).DeliveryState ==
                SimulatorDeliveryState.Failed,
        "disconnect must fail and remove queued work bound to the prior session");
}

groups++;
var disabledFactory = new CountingTransportFactory();
var recognizedUsbDiscovery = new FixedDiscovery(
[
    new(CompanionEndpoint.CreateSessionPrivate(), "Heltec V4 OLED",
        CompanionDeviceFamily.HeltecV4Companion, connectionReady: false),
]);
await using (var bridge = new DualClientBridge(
    recognizedUsbDiscovery, disabledFactory, new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var choice = (await bridge.RefreshDeviceChoicesAsync()).Single();
    Expect(choice.Source == SimulatorConnectionSource.CompatibleUsbCompanion &&
        !choice.ConnectionReady && choice.PublicStatus ==
            "USB companion recognized; live session adapter not ready",
        "recognized USB choices must remain explicitly connection-blocked");
    await bridge.SelectDeviceAsync(SimulatorClientId.A, choice);
    await bridge.ConnectAsync(SimulatorClientId.A);
    var blocked = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(blocked.ConnectionState == SimulatorConnectionState.Faulted &&
        blocked.LastPublicError ==
            "The selected USB companion is recognized, but live connection is not available." &&
        disabledFactory.OpenCount == 0,
        "a recognized USB choice must not reach the disabled live factory");
}

groups++;
await using (var bridge = new DualClientBridge(
    choiceDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    try
    {
        _ = await bridge.RefreshDeviceChoicesAsync();
        Expect(false, "roster refresh must not replace endpoints while a session is open");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Connected,
        "rejected connected refresh must not disturb the active session");
}

groups++;
var changingDiscovery = new MutableDiscovery(
[
    new(choiceEndpointA, "Simulated companion A", CompanionDeviceFamily.Simulated),
    new(choiceEndpointB, "Simulated companion B", CompanionDeviceFamily.Simulated),
]);
await using (var bridge = new DualClientBridge(
    changingDiscovery, new NoopTransportFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    var selected = (await bridge.RefreshDeviceChoicesAsync()).Single(choice =>
        choice.PublicLabel == "Simulated companion A");
    await bridge.SelectDeviceAsync(SimulatorClientId.A, selected);
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.EnqueueChatAsync(SimulatorClientId.A, "must keep assignment");
    changingDiscovery.Candidates =
    [new(choiceEndpointB, "Simulated companion B", CompanionDeviceFamily.Simulated)];
    try
    {
        _ = await bridge.RefreshDeviceChoicesAsync();
        Expect(false, "refresh must not replace any assignment while a session owns queued work");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).PublicDeviceLabel ==
            "Simulated companion A" && bridge.GetDeviceChoices().Count == 2,
        "rejected refresh must atomically preserve the open session roster and assignment");
}

groups++;
var offlineQueueHarness = LocalLoopbackSimulator.Create();
await using (var bridge = offlineQueueHarness.Bridge)
{
    try
    {
        await bridge.EnqueueQuickStatusAsync(
            SimulatorClientId.A, SimulatorQuickStatus.ImOk);
        Expect(false, "commands must not enter a queue without an exact open session");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).OutgoingQueueCount == 0,
        "an offline enqueue rejection must leave no ambiguous future-session work");
}

groups++;
var receiveFailureEndpoint = CompanionEndpoint.CreateSessionPrivate();
await using (var bridge = new DualClientBridge(
    new FixedDiscovery(
    [new(receiveFailureEndpoint, "Simulated companion A",
        CompanionDeviceFamily.Simulated)]),
    new ReceiveFailureFactory(), new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.ImOk);
    await bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.NeedAssistance);
    await bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.AnyoneOnline);
    await bridge.ServiceAsync(SimulatorClientId.A);
    var failed = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(failed.ConnectionState == SimulatorConnectionState.Faulted &&
        failed.OutgoingQueueCount == 0 &&
        failed.Messages.Where(message =>
            message.Direction == SimulatorMessageDirection.Outbound)
            .All(message => message.DeliveryState is
                SimulatorDeliveryState.BridgeAccepted or SimulatorDeliveryState.Failed),
        "a terminal receive failure must fail every unsent command without touching the accepted command");
}

groups++;
await using (var bridge = new DualClientBridge(
    sharedChoiceDiscovery, new NullTransportFactory(),
    new ManualClock(DateTimeOffset.UtcNow), TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Faulted,
        "a malformed transport factory result must fail closed");
}

groups++;
var invariantHarness = LocalLoopbackSimulator.Create();
await using (var bridge = invariantHarness.Bridge)
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.EnqueueQuickStatusAsync(SimulatorClientId.A, SimulatorQuickStatus.ImOk);
    await bridge.EnqueueQuickStatusAsync(SimulatorClientId.A, SimulatorQuickStatus.NeedAssistance);
    await bridge.EnqueueQuickStatusAsync(SimulatorClientId.A, SimulatorQuickStatus.AnyoneOnline);
    var clients = (Array)typeof(DualClientBridge).GetField(
        "_clients", System.Reflection.BindingFlags.Instance |
            System.Reflection.BindingFlags.NonPublic)!.GetValue(bridge)!;
    var state = clients.GetValue(0)!;
    var generation = state.GetType().GetProperty(
        "SessionGeneration", System.Reflection.BindingFlags.Instance |
            System.Reflection.BindingFlags.NonPublic)!;
    generation.SetValue(state, (long)generation.GetValue(state)! + 1);
    await bridge.ServiceAsync(SimulatorClientId.A);
    var failed = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(failed.ConnectionState == SimulatorConnectionState.Faulted &&
        failed.OutgoingQueueCount == 0 &&
        failed.Messages.Where(message =>
            message.Direction == SimulatorMessageDirection.Outbound)
            .All(message => message.DeliveryState == SimulatorDeliveryState.Failed),
        "an invariant mismatch must fail and clear every command from the invalidated session");
}

groups++;
var optionalFailureHarness = LocalLoopbackSimulator.CreateWithAdditionalDiscovery(
    new ThrowingDiscovery());
await using (var bridge = optionalFailureHarness.Bridge)
{
    var choices = await bridge.RefreshDeviceChoicesAsync();
    Expect(choices.Count == 2 && choices.All(choice =>
            choice.Source == SimulatorConnectionSource.LocalSimulation),
        "optional USB helper failure must not suppress either local simulator companion");
    await bridge.ConnectAsync(SimulatorClientId.A);
    await bridge.ConnectAsync(SimulatorClientId.B);
    Expect(bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
            SimulatorConnectionState.Connected &&
        bridge.GetSnapshot(SimulatorClientId.B).ConnectionState ==
            SimulatorConnectionState.Connected,
        "both local clients must remain independently usable when USB discovery is unavailable");
}

groups++;
var noAutoUsbFactory = new CountingTransportFactory();
await using (var bridge = new DualClientBridge(
    recognizedUsbDiscovery, noAutoUsbFactory, new ManualClock(DateTimeOffset.UtcNow),
    TimeSpan.FromSeconds(5)))
{
    await bridge.ConnectAsync(SimulatorClientId.A);
    var unselected = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(unselected.PublicDeviceLabel is null &&
        unselected.ConnectionState == SimulatorConnectionState.Disconnected &&
        noAutoUsbFactory.OpenCount == 0,
        "a recognized USB companion must never be silently assigned or opened");
}

groups++;
var sessionAdmissionHarness = LocalLoopbackSimulator.Create();
await using (var bridge = sessionAdmissionHarness.Bridge)
{
    await ConnectBoth(sessionAdmissionHarness);
    var initial = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(initial.ConnectedSessionEpoch > 0,
        "an exact connected session must expose a nonzero admission epoch");
    var admitted = await bridge.EnqueueTemplateChatForSessionAsync(
        SimulatorClientId.A, 1, "Checking in", initial.ConnectedSessionEpoch);
    var queued = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(admitted.AppliedSessionEpoch == initial.ConnectedSessionEpoch &&
        admitted.AppliedMessageSequence > 0 &&
        queued.OutgoingQueueCount == 1 &&
        queued.Messages.Single(message =>
            message.LocalSequence == admitted.AppliedMessageSequence).Text == "Checking in",
        "atomic template admission must return the exact applied epoch and queued sequence before service");
    try
    {
        _ = await bridge.EnqueueTemplateChatForSessionAsync(
            SimulatorClientId.A, 1, "Checking iné", initial.ConnectedSessionEpoch);
        Expect(false, "portable template request text must reject non-ASCII bytes");
    }
    catch (ArgumentException) { }

    await bridge.ReconnectAsync(SimulatorClientId.A);
    var reconnected = bridge.GetSnapshot(SimulatorClientId.A);
    Expect(reconnected.ConnectedSessionEpoch != initial.ConnectedSessionEpoch,
        "reconnect must advance the opaque admission epoch");
    var beforeRejected = reconnected.Messages.Count;
    try
    {
        _ = await bridge.EnqueueTemplateChatForSessionAsync(
            SimulatorClientId.A, 1, "Checking in", initial.ConnectedSessionEpoch);
        Expect(false, "a prior-session template admission must be rejected");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.A).Messages.Count == beforeRejected,
        "a rejected prior-session template must not enter the new session history");

    await bridge.EnqueueCriticalAlertForSessionAsync(
        SimulatorClientId.A, reconnected.ConnectedSessionEpoch);
    await bridge.ServiceAsync(SimulatorClientId.A);
    await bridge.ServiceAsync(SimulatorClientId.B);
    var received = bridge.GetSnapshot(SimulatorClientId.B);
    var alert = received.Alerts.Single(item =>
        item.Direction == SimulatorMessageDirection.Inbound &&
        item.State == SimulatorAlertState.Active);
    await bridge.ReconnectAsync(SimulatorClientId.B);
    var beforeAck = bridge.GetSnapshot(SimulatorClientId.B).Messages.Count;
    try
    {
        _ = await bridge.AcknowledgeAlertForSessionAsync(
            SimulatorClientId.B, alert.LocalSequence,
            received.ConnectedSessionEpoch);
        Expect(false, "a prior-session acknowledgement must be rejected");
    }
    catch (InvalidOperationException) { }
    Expect(bridge.GetSnapshot(SimulatorClientId.B).Messages.Count == beforeAck,
        "a rejected prior-session acknowledgement must not enqueue in the new session");
}

Console.WriteLine($"OpenTrail simulator core: {groups} groups, {failures} failures");
return failures == 0 ? 0 : 1;

sealed class ManualClock(DateTimeOffset now) : ISimulatorClock
{
    public DateTimeOffset UtcNow { get; private set; } = now;
    public void Advance(TimeSpan elapsed) => UtcNow += elapsed;
}

sealed class FixedDiscovery(IReadOnlyList<CompanionCandidate> candidates)
    : ICompanionDeviceDiscovery
{
    public ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(candidates);
    }
}

sealed class MutableDiscovery(IReadOnlyList<CompanionCandidate> candidates)
    : ICompanionDeviceDiscovery
{
    public IReadOnlyList<CompanionCandidate> Candidates { get; set; } = candidates;

    public ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(Candidates);
    }
}

sealed class ThrowingDiscovery : ICompanionDeviceDiscovery
{
    public ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken) =>
        throw new IOException("private transport detail must not escape");
}

sealed class CancelledDiscovery : ICompanionDeviceDiscovery
{
    public ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken) =>
        ValueTask.FromException<IReadOnlyList<CompanionCandidate>>(
            new OperationCanceledException());
}

sealed class NoopTransportFactory : ICompanionTransportFactory
{
    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<ICompanionTransport>(new NoopTransport());
    }
}

sealed class NullTransportFactory : ICompanionTransportFactory
{
    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate, CancellationToken cancellationToken) =>
        ValueTask.FromResult<ICompanionTransport>(null!);
}

sealed class ReceiveFailureFactory : ICompanionTransportFactory
{
    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate, CancellationToken cancellationToken) =>
        ValueTask.FromResult<ICompanionTransport>(new ReceiveFailureTransport());

    private sealed class ReceiveFailureTransport : ICompanionTransport
    {
        public ValueTask SendAsync(
            CompanionCommand command, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask<CompanionObservation?> ReceiveAsync(
            CancellationToken cancellationToken) =>
            ValueTask.FromException<CompanionObservation?>(
                new IOException("private receive failure"));

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}

sealed class CountingTransportFactory : ICompanionTransportFactory
{
    public int OpenCount { get; private set; }

    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate, CancellationToken cancellationToken)
    {
        OpenCount++;
        return ValueTask.FromResult<ICompanionTransport>(new NoopTransport());
    }
}

sealed class NoopTransport : ICompanionTransport
{
    public ValueTask SendAsync(CompanionCommand command, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.CompletedTask;
    }

    public ValueTask<CompanionObservation?> ReceiveAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<CompanionObservation?>(null);
    }

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}

sealed class CloseTrackingFactory : ICompanionTransportFactory
{
    private int _opened;
    public int CloseAttempts { get; private set; }

    public ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var throws = _opened++ == 0;
        return ValueTask.FromResult<ICompanionTransport>(
            new CloseTrackingTransport(this, throws));
    }

    private sealed class CloseTrackingTransport(
        CloseTrackingFactory owner, bool throws) : ICompanionTransport
    {
        public ValueTask SendAsync(CompanionCommand command, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
        public ValueTask<CompanionObservation?> ReceiveAsync(CancellationToken cancellationToken) =>
            ValueTask.FromResult<CompanionObservation?>(null);
        public ValueTask DisposeAsync()
        {
            owner.CloseAttempts++;
            return throws
                ? ValueTask.FromException(new IOException("synthetic close failure"))
                : ValueTask.CompletedTask;
        }
    }
}
