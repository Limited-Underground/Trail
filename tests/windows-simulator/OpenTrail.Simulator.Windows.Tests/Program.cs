using System.Text.Json;
using OpenTrail.Simulator.Core;
using OpenTrail.Simulator.Windows;

var failures = 0;
var groups = 0;

void Expect(bool condition, string message)
{
    if (condition) return;
    Console.Error.WriteLine($"FAIL: {message}");
    failures++;
}

Dictionary<string, object?> Device(
    string ordinal,
    string family,
    string firmware,
    string role = "meshcore_companion",
    int protocol = 10,
    bool succeeded = true) => new()
{
    ["candidate"] = ordinal,
    ["runtime_query_succeeded"] = succeeded,
    ["runtime_board_family"] = family,
    ["runtime_firmware"] = firmware,
    ["runtime_protocol_version"] = protocol,
    ["runtime_role"] = role,
    ["runtime_identity_authoritative_for_flash"] = false,
    ["flashing_allowed"] = false,
};

string Evidence(params Dictionary<string, object?>[] devices) =>
    JsonSerializer.Serialize(new Dictionary<string, object?>
    {
        ["schema"] = "ot_meshcore_runtime_evidence_v0",
        ["read_only"] = true,
        ["state_changes_made"] = false,
        ["privacy"] = new Dictionary<string, object?>
        {
            ["local_ports_included"] = false,
            ["serial_numbers_included"] = false,
            ["hardware_instance_ids_included"] = false,
            ["device_locations_included"] = false,
            ["raw_responses_included"] = false,
            ["pairing_data_included"] = false,
            ["device_identity_included"] = false,
        },
        ["candidate_count"] = devices.Length,
        ["devices"] = devices,
    });

groups++;
var admitted = WindowsCompanionDiscovery.ParseRedactedEvidence(Evidence(
    Device("usb_candidate_1", "heltec_v4_oled", "v1.16.0-07a3ca9"),
    Device("usb_candidate_2", "seeed_wio_tracker_l1", "v1.17.0-727fc05"),
    Device("usb_candidate_3", "seeed_sensecap_solar", "v1.16.0-07a3ca9",
        role: "meshcore_repeater", protocol: 0)));
Expect(admitted.Count == 2 &&
    admitted.Select(item => item.PublicLabel).SequenceEqual(
        new[] { "Heltec V4 OLED", "Wio Tracker L1" }) &&
    admitted.All(item => !item.ConnectionReady &&
        item.Source == SimulatorConnectionSource.CompatibleUsbCompanion),
    "only exact companion runtime tuples must become connection-blocked choices");

groups++;
var unsupported = WindowsCompanionDiscovery.ParseRedactedEvidence(Evidence(
    Device("usb_candidate_1", "heltec_v4_oled", "v9.9.9-fffffff"),
    Device("usb_candidate_2", "seeed_wio_tracker_l1", "v1.17.0-727fc05",
        protocol: 2),
    Device("usb_candidate_3", "heltec_v4_oled", "v1.16.0-07a3ca9",
        role: "meshcore_repeater")));
Expect(unsupported.Count == 0,
    "unsupported firmware, protocol, and role observations must be omitted");

groups++;
var failedQuery = Device(
    "usb_candidate_1", "private ignored family", "private ignored firmware",
    succeeded: false);
var failedReduced = WindowsCompanionDiscovery.ParseRedactedEvidence(Evidence(failedQuery));
Expect(failedReduced.Count == 0,
    "a failed runtime query must not create a selectable device");

groups++;
var privateDevice = Device(
    "usb_candidate_1", "heltec_v4_oled", "v1.16.0-07a3ca9");
privateDevice["local_port"] = "private transport";
try
{
    _ = WindowsCompanionDiscovery.ParseRedactedEvidence(Evidence(privateDevice));
    Expect(false, "a private transport field must reject the entire roster");
}
catch (InvalidDataException) { }

groups++;
var privateEvidence = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(
    Evidence(Device("usb_candidate_1", "heltec_v4_oled", "v1.16.0-07a3ca9")))!;
var privacy = JsonSerializer.Deserialize<Dictionary<string, object?>>(
    privateEvidence["privacy"].GetRawText())!;
privacy["serial_numbers_included"] = true;
var privateRoot = new Dictionary<string, object?>
{
    ["schema"] = privateEvidence["schema"],
    ["read_only"] = privateEvidence["read_only"],
    ["state_changes_made"] = privateEvidence["state_changes_made"],
    ["privacy"] = privacy,
    ["candidate_count"] = privateEvidence["candidate_count"],
    ["devices"] = privateEvidence["devices"],
};
try
{
    _ = WindowsCompanionDiscovery.ParseRedactedEvidence(
        JsonSerializer.Serialize(privateRoot));
    Expect(false, "privacy flags must fail closed when any private class is present");
}
catch (InvalidDataException) { }

groups++;
try
{
    _ = WindowsCompanionDiscovery.ParseRedactedEvidence(Evidence(
        Device("usb_candidate_1", "heltec_v4_oled", "v1.16.0-07a3ca9"),
        Device("usb_candidate_1", "seeed_wio_tracker_l1", "v1.17.0-727fc05")));
    Expect(false, "duplicate public ordinals must reject the roster");
}
catch (InvalidDataException) { }

groups++;
var fakeProvider = new FixedProvider(Evidence(
    Device("usb_candidate_1", "heltec_v4_oled", "v1.16.0-07a3ca9")));
var discovery = new WindowsCompanionDiscovery(fakeProvider);
var discovered = await discovery.DiscoverAsync(CancellationToken.None);
Expect(fakeProvider.Calls == 1 && discovered.Count == 1 &&
    discovered[0].ToString() == "Heltec V4 OLED" &&
    CompanionEndpoint.CreateSessionPrivate().ToString() == "[private companion endpoint]",
    "the provider boundary must publish no endpoint or transport identifier");

groups++;
var local = LocalLoopbackSimulator.CreateWithAdditionalDiscovery(discovery);
await using (local.Bridge)
{
    var choices = await local.Bridge.RefreshDeviceChoicesAsync();
    Expect(choices.Count == 3 && choices.Count(choice => choice.ConnectionReady) == 2 &&
        choices.Count(choice => !choice.ConnectionReady) == 1,
        "composed discovery must keep two local choices and one blocked USB choice separate");
    var usb = choices.Single(choice => !choice.ConnectionReady);
    await local.Bridge.SelectDeviceAsync(SimulatorClientId.A, usb);
    await local.Bridge.ConnectAsync(SimulatorClientId.A);
    Expect(local.Bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
        SimulatorConnectionState.Faulted,
        "the composed local transport must never be asked to open a USB choice");
}

groups++;
var helper = new FakePrivateHelper(
[
    new("candidate_token_alpha_1234", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
    new("candidate_token_bravo_1234", "wio_tracker_l1", "Wio Tracker L1 USB candidate", true),
]);
await using (var host = new MeshCoreCompanionHost(helper))
{
    var candidates = await host.DiscoverAsync(CancellationToken.None);
    Expect(candidates.Count == 2 && candidates.All(candidate =>
            candidate.ConnectionReady && candidate.PublicStatus ==
                "USB candidate ready to verify on Connect") &&
        candidates.Select(candidate => candidate.ToString()).SequenceEqual(
            new[] { "ESP32-S3 USB candidate", "Wio Tracker L1 USB candidate" }),
        "the live host must expose only exact public labels for helper-admitted companions");
    var transport = await host.OpenAsync(candidates[0], CancellationToken.None);
    try
    {
        _ = await host.OpenAsync(candidates[0], CancellationToken.None);
        Expect(false, "one helper candidate must have one exclusive live lease");
    }
    catch (InvalidOperationException) { }

    await transport.SendAsync(new(
        CompanionCommandKind.QuickStatus, 11, "Need assistance",
        SimulatorMessagePriority.Important, null), CancellationToken.None);
    await transport.SendAsync(new(
        CompanionCommandKind.Alert, 12, "Critical alert",
        SimulatorMessagePriority.Critical, SimulatorAlertSeverity.Critical),
        CancellationToken.None);
    try
    {
        await transport.SendAsync(new(
            CompanionCommandKind.Acknowledgement, 12, "Acknowledged",
            SimulatorMessagePriority.Important, null), CancellationToken.None);
        Expect(false, "received-alert acknowledgement must fail closed on the live helper");
    }
    catch (InvalidOperationException) { }
    Expect(helper.Sends.Select(send => (send.Kind, send.Correlation, send.Code))
        .SequenceEqual(new[]
        {
            ("quick", 11L, "HELP"), ("critical", 12L, (string?)null),
        }), "the host must reduce live sends to the fixed typed helper vocabulary");
    try
    {
        await transport.SendAsync(new(
            CompanionCommandKind.Chat, 13, "free text",
            SimulatorMessagePriority.Normal, null), CancellationToken.None);
        Expect(false, "arbitrary chat must never reach a live companion helper");
    }
    catch (InvalidOperationException) { }

    helper.Observations.Enqueue(new("quick", 21, "AVAILABLE"));
    helper.Observations.Enqueue(new("critical", 22, null));
    helper.Observations.Enqueue(new("ack", 22, null));
    var quick = await transport.ReceiveAsync(CancellationToken.None);
    var critical = await transport.ReceiveAsync(CancellationToken.None);
    var ack = await transport.ReceiveAsync(CancellationToken.None);
    Expect(quick is { Kind: CompanionCommandKind.QuickStatus,
            Text: "Available to help" } &&
        critical is { Kind: CompanionCommandKind.Alert,
            Text: "Critical alert (unauthenticated simulator test)",
            AlertSeverity: SimulatorAlertSeverity.Critical } &&
        ack is { Kind: CompanionCommandKind.Acknowledgement, Correlation: 22 },
        "typed helper observations must expand without sender or identity material");

    try
    {
        _ = await host.DiscoverAsync(CancellationToken.None);
        Expect(false, "discovery must not replace the roster while a live lease exists");
    }
    catch (InvalidOperationException) { }
    await transport.DisposeAsync();
    Expect(helper.ClosedSessions == 1,
        "disposing a live transport must close exactly its private helper session");
}

groups++;
var privateFailureHelper = new FakePrivateHelper(
[
    new("candidate_token_charlie_12", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
]) { FailDiscovery = true };
await using (var host = new MeshCoreCompanionHost(privateFailureHelper))
{
    try
    {
        _ = await host.DiscoverAsync(CancellationToken.None);
        Expect(false, "helper discovery failure must fail closed");
    }
    catch (InvalidOperationException error)
    {
        Expect(error.Message == "Compatible USB companion discovery failed." &&
            !error.ToString().Contains("private", StringComparison.OrdinalIgnoreCase),
            "raw helper failure detail must not escape the live host");
    }
}

groups++;
var composedHelper = new FakePrivateHelper(
[
    new("candidate_token_echo_123456", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
]);
await using (var runtime = new WindowsSimulatorRuntime(
    new MeshCoreCompanionHost(composedHelper)))
{
    var choices = await runtime.Bridge.RefreshDeviceChoicesAsync();
    Expect(choices.Count == 3 &&
        choices.Count(choice => choice.Source ==
            SimulatorConnectionSource.LocalSimulation) == 2 &&
        choices.Count(choice => choice.Source ==
            SimulatorConnectionSource.CompatibleUsbCompanion) == 1,
        "production-local composition must expose synthetic and admitted USB sources separately");
    var usb = choices.Single(choice =>
        choice.Source == SimulatorConnectionSource.CompatibleUsbCompanion);
    await runtime.Bridge.SelectDeviceAsync(SimulatorClientId.A, usb);
    await runtime.Bridge.ConnectAsync(SimulatorClientId.A);
    await runtime.Bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.AnyoneOnline);
    await runtime.Bridge.ServiceAsync(SimulatorClientId.A);
    Expect(runtime.Bridge.GetSnapshot(SimulatorClientId.A).ConnectionState ==
            SimulatorConnectionState.Connected &&
        composedHelper.Sends.Single() == new PrivateHelperSend("quick", 2, "ONLINE"),
        "an explicitly selected USB choice must route through its live helper instead of loopback");
}

groups++;
var malformedHelper = new FakePrivateHelper(
[
    new("candidate_token_delta_1234", "unsupported", "private device", true),
]);
await using (var host = new MeshCoreCompanionHost(malformedHelper))
{
    try
    {
        _ = await host.DiscoverAsync(CancellationToken.None);
        Expect(false, "a helper cannot inject a non-allowlisted public choice");
    }
    catch (InvalidDataException) { }
}

groups++;
var terminalHelper = new FakePrivateHelper(
[
    new("candidate_token_golf_123456", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
]) { FailSend = true };
await using (var runtime = new WindowsSimulatorRuntime(
    new MeshCoreCompanionHost(terminalHelper)))
{
    var usb = (await runtime.Bridge.RefreshDeviceChoicesAsync()).Single(choice =>
        choice.Source == SimulatorConnectionSource.CompatibleUsbCompanion);
    await runtime.Bridge.SelectDeviceAsync(SimulatorClientId.A, usb);
    await runtime.Bridge.ConnectAsync(SimulatorClientId.A);
    await runtime.Bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.ImOk);
    await runtime.Bridge.EnqueueQuickStatusAsync(
        SimulatorClientId.A, SimulatorQuickStatus.NeedAssistance);
    await runtime.Bridge.ServiceAsync(SimulatorClientId.A);
    var faulted = runtime.Bridge.GetSnapshot(SimulatorClientId.A);
    Expect(faulted.ConnectionState == SimulatorConnectionState.Faulted &&
        faulted.OutgoingQueueCount == 0 &&
        faulted.Messages.Where(message =>
            message.Direction == SimulatorMessageDirection.Outbound)
            .All(message => message.DeliveryState == SimulatorDeliveryState.Failed),
        "an ambiguous live send outcome must fault the session and clear every session-bound command");
}

groups++;
var runtimeRejectedHelper = new FakePrivateHelper(
[
    new("candidate_token_hotel_12345", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
]) { FailOpen = true };
await using (var runtime = new WindowsSimulatorRuntime(
    new MeshCoreCompanionHost(runtimeRejectedHelper)))
{
    var usb = (await runtime.Bridge.RefreshDeviceChoicesAsync()).Single(choice =>
        choice.Source == SimulatorConnectionSource.CompatibleUsbCompanion);
    await runtime.Bridge.SelectDeviceAsync(SimulatorClientId.A, usb);
    await runtime.Bridge.ConnectAsync(SimulatorClientId.A);
    var rejected = runtime.Bridge.GetSnapshot(SimulatorClientId.A);
    Expect(rejected.ConnectionState == SimulatorConnectionState.Faulted &&
        rejected.OutgoingQueueCount == 0 && runtimeRejectedHelper.Sends.Count == 0,
        "a candidate rejected by the explicit runtime handshake must never become an open sending session");
}

groups++;
var invalidSessionHelper = new FakePrivateHelper(
[
    new("candidate_token_foxtrot_123", "esp32_s3_usb", "ESP32-S3 USB candidate", true),
]) { OpenSessionToken = "invalid token with spaces" };
await using (var host = new MeshCoreCompanionHost(invalidSessionHelper))
{
    var candidate = (await host.DiscoverAsync(CancellationToken.None)).Single();
    try
    {
        _ = await host.OpenAsync(candidate, CancellationToken.None);
        Expect(false, "an invalid applied helper session response must fail closed");
    }
    catch (InvalidOperationException) { }
    Expect(invalidSessionHelper.ClosedSessions == 1 && invalidSessionHelper.Disposed,
        "an ambiguous applied open must close the returned lease and reset its helper owner");
}

var processTestRoot = Path.Combine(Path.GetTempPath(),
    "OpenTrail.PrivateHelper.Tests." + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(processTestRoot);
try
{
    string WriteScript(string name, string body)
    {
        var path = Path.Combine(processTestRoot, name);
        File.WriteAllText(path, body, new System.Text.UTF8Encoding(false));
        return path;
    }

    groups++;
    var bundledRoot = Path.Combine(processTestRoot, "bundled");
    var unrelatedRoot = Path.Combine(processTestRoot, "unrelated");
    Directory.CreateDirectory(bundledRoot);
    Directory.CreateDirectory(Path.Combine(
        unrelatedRoot, "tools", "windows-simulator"));
    var bundledHelper = Path.Combine(bundledRoot, "meshcore_companion_bridge.py");
    var unrelatedHelper = Path.Combine(unrelatedRoot, "tools", "windows-simulator",
        "meshcore_companion_bridge.py");
    File.WriteAllText(bundledHelper, "# exact bundled helper", new System.Text.UTF8Encoding(false));
    File.WriteAllText(unrelatedHelper, "# unrelated helper", new System.Text.UTF8Encoding(false));
    var previousDirectory = Environment.CurrentDirectory;
    try
    {
        Environment.CurrentDirectory = unrelatedRoot;
        var resolved = ProcessPrivateCompanionHelper.ResolveBundledHelperPath(bundledRoot);
        Expect(Path.GetFullPath(resolved) == Path.GetFullPath(bundledHelper) &&
            Path.GetFullPath(resolved) != Path.GetFullPath(unrelatedHelper),
            "helper resolution must ignore arbitrary working-directory ancestors");
    }
    finally { Environment.CurrentDirectory = previousDirectory; }

    groups++;
    var noisyScript = WriteScript("noisy.py", """
import json, sys
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        sys.stderr.write("s" * 262144)
        sys.stderr.flush()
        print('{"v":1,"ok":true,"candidates":[]}', flush=True)
    elif request.get("op") == "shutdown":
        print('{"v":1,"ok":true,"shutdown":true}', flush=True)
        break
""");
    await using (var helperProcess = new ProcessPrivateCompanionHelper(noisyScript))
    {
        var roster = await helperProcess.DiscoverAsync(CancellationToken.None);
        Expect(roster.Count == 0,
            "stderr beyond pipe capacity must be drained without retention or deadlock");
    }

    groups++;
    var oversizedScript = WriteScript("oversized.py", """
import json, sys
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        print("x" * 9000, flush=True)
    else:
        break
""");
    await using (var helperProcess = new ProcessPrivateCompanionHelper(oversizedScript))
    {
        try
        {
            _ = await helperProcess.DiscoverAsync(CancellationToken.None);
            Expect(false, "oversized helper stdout must fail before unbounded allocation");
        }
        catch (IOException) { }
    }

    groups++;
    var restartScript = WriteScript("restart.py", """
import json, pathlib, sys, time
marker = pathlib.Path(__file__).with_suffix('.marker')
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        if not marker.exists():
            marker.write_text("first", encoding="ascii")
            sys.stdout.write("{")
            sys.stdout.flush()
            time.sleep(2)
        else:
            print('{"v":1,"ok":true,"candidates":[]}', flush=True)
    elif request.get("op") == "shutdown":
        print('{"v":1,"ok":true,"shutdown":true}', flush=True)
        break
""");
    await using (var helperProcess = new ProcessPrivateCompanionHelper(restartScript))
    {
        using var firstTimeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(150));
        try
        {
            _ = await helperProcess.DiscoverAsync(firstTimeout.Token);
            Expect(false, "a partial helper response must time out");
        }
        catch (IOException) { }
        var restarted = await helperProcess.DiscoverAsync(CancellationToken.None);
        Expect(restarted.Count == 0,
            "a timed-out helper must restart so its stale partial response cannot satisfy the next request");
    }

    groups++;
    var unterminatedScript = WriteScript("unterminated.py", """
import json, sys
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        sys.stdout.write('{"v":1,"ok":true,"candidates":[]}')
        sys.stdout.flush()
        break
""");
    await using (var helperProcess = new ProcessPrivateCompanionHelper(unterminatedScript))
    {
        try
        {
            _ = await helperProcess.DiscoverAsync(CancellationToken.None);
            Expect(false, "an EOF-terminated partial line must not admit helper tokens");
        }
        catch (IOException) { }
    }

    groups++;
    var floatVersionScript = WriteScript("float-version.py", """
import json, sys
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        print('{"v":1.0,"ok":true,"candidates":[]}', flush=True)
    else:
        break
""");
    await using (var helperProcess = new ProcessPrivateCompanionHelper(floatVersionScript))
    {
        try
        {
            _ = await helperProcess.DiscoverAsync(CancellationToken.None);
            Expect(false, "a non-integer helper protocol version must fail closed");
        }
        catch (IOException) { }
    }

    groups++;
    var raceScript = WriteScript("race.py", """
import json, sys, time
for line in sys.stdin:
    request = json.loads(line)
    if request.get("op") == "discover":
        time.sleep(0.1)
        print('{"v":1,"ok":true,"candidates":[]}', flush=True)
    elif request.get("op") == "shutdown":
        print('{"v":1,"ok":true,"shutdown":true}', flush=True)
        break
""");
    var raced = new ProcessPrivateCompanionHelper(raceScript);
    var inFlight = raced.DiscoverAsync(CancellationToken.None).AsTask();
    var disposing = raced.DisposeAsync().AsTask();
    await Task.WhenAll(inFlight, disposing);
    Expect(inFlight.Result.Count == 0,
        "helper disposal must drain an in-flight request without semaphore disposal races");
}
finally
{
    if (Directory.Exists(processTestRoot))
        Directory.Delete(processTestRoot, recursive: true);
}

groups++;
var firstDispose = new TrackingDisposable(throws: true);
var secondDispose = new TrackingDisposable(throws: false);
try
{
    await WindowsSimulatorRuntime.DisposeBothAsync(firstDispose, secondDispose);
    Expect(false, "a runtime close failure must remain visible after cleanup");
}
catch (IOException error)
{
    Expect(error.Message == "The simulator runtime did not close cleanly." &&
        firstDispose.Attempts == 1 && secondDispose.Attempts == 1,
        "USB helper disposal must still run when bridge disposal fails");
}

Console.WriteLine($"OpenTrail simulator Windows bridge: {groups} groups, {failures} failures");
return failures == 0 ? 0 : 1;

sealed class FixedProvider(string json) : IRedactedRuntimeEvidenceProvider
{
    public int Calls { get; private set; }

    public ValueTask<string> CollectAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        Calls++;
        return ValueTask.FromResult(json);
    }
}

sealed class FakePrivateHelper(IReadOnlyList<PrivateHelperCandidate> candidates)
    : IPrivateCompanionHelper
{
    public List<PrivateHelperSend> Sends { get; } = [];
    public Queue<PrivateHelperObservation> Observations { get; } = new();
    public int ClosedSessions { get; private set; }
    public bool FailDiscovery { get; init; }
    public bool FailSend { get; init; }
    public bool FailOpen { get; init; }
    public string OpenSessionToken { get; init; } = "session_token_private_1234";
    public bool Disposed { get; private set; }

    public ValueTask<IReadOnlyList<PrivateHelperCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        if (FailDiscovery)
            throw new IOException("private port and identity detail");
        return ValueTask.FromResult(candidates);
    }

    public ValueTask<string> OpenAsync(
        string candidateToken, CancellationToken cancellationToken) =>
        FailOpen
            ? ValueTask.FromException<string>(
                new IOException("private wrong runtime tuple"))
            : ValueTask.FromResult(OpenSessionToken);

    public ValueTask SendAsync(
        string sessionToken, PrivateHelperSend command,
        CancellationToken cancellationToken)
    {
        if (FailSend)
            throw new IOException("private ambiguous send outcome");
        Sends.Add(command);
        return ValueTask.CompletedTask;
    }

    public ValueTask<PrivateHelperObservation?> PollAsync(
        string sessionToken, CancellationToken cancellationToken) =>
        ValueTask.FromResult(Observations.TryDequeue(out var observation)
            ? observation : null);

    public ValueTask CloseAsync(
        string sessionToken, CancellationToken cancellationToken)
    {
        ClosedSessions++;
        return ValueTask.CompletedTask;
    }

    public ValueTask DisposeAsync()
    {
        Disposed = true;
        return ValueTask.CompletedTask;
    }
}

sealed class TrackingDisposable(bool throws) : IAsyncDisposable
{
    public int Attempts { get; private set; }

    public ValueTask DisposeAsync()
    {
        Attempts++;
        return throws
            ? ValueTask.FromException(new IOException("private cleanup failure"))
            : ValueTask.CompletedTask;
    }
}
