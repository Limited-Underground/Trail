using System.Diagnostics;
using System.Text;
using System.Text.Json;
using OpenTrail.Simulator.Core;

namespace OpenTrail.Simulator.Windows;

internal sealed record PrivateHelperCandidate(
    string Token,
    string Family,
    string Label,
    bool Ready);

internal sealed record PrivateHelperObservation(
    string Kind,
    long Correlation,
    string? Code);

internal sealed record PrivateHelperSend(
    string Kind,
    long Correlation,
    string? Code);

internal interface IPrivateCompanionHelper : IAsyncDisposable
{
    ValueTask<IReadOnlyList<PrivateHelperCandidate>> DiscoverAsync(
        CancellationToken cancellationToken);
    ValueTask<string> OpenAsync(string candidateToken, CancellationToken cancellationToken);
    ValueTask SendAsync(
        string sessionToken, PrivateHelperSend command, CancellationToken cancellationToken);
    ValueTask<PrivateHelperObservation?> PollAsync(
        string sessionToken, CancellationToken cancellationToken);
    ValueTask CloseAsync(string sessionToken, CancellationToken cancellationToken);
}

/// <summary>
/// Owns one private helper process and supplies both discovery and transports.
/// Passive USB candidates are runtime-verified only on explicit Open. Ports,
/// USB identities, helper tokens, and raw device replies never enter the public
/// simulator model or exceptions.
/// </summary>
public sealed class MeshCoreCompanionHost :
    ICompanionDeviceDiscovery, ICompanionTransportFactory, IAsyncDisposable
{
    private const int MaximumCandidates = 8;
    private readonly IPrivateCompanionHelper _helper;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Dictionary<CompanionEndpoint, string> _candidateTokens = [];
    private readonly Dictionary<CompanionEndpoint, string> _sessions = [];
    private bool _disposed;

    public MeshCoreCompanionHost() : this(new ProcessPrivateCompanionHelper()) { }

    internal MeshCoreCompanionHost(IPrivateCompanionHelper helper) =>
        _helper = helper ?? throw new ArgumentNullException(nameof(helper));

    public async ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            if (_sessions.Count != 0)
                throw new InvalidOperationException(
                    "Disconnect live companion sessions before refreshing devices.");
            IReadOnlyList<PrivateHelperCandidate> discovered;
            try
            {
                discovered = await _helper.DiscoverAsync(cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                _candidateTokens.Clear();
                throw;
            }
            catch
            {
                _candidateTokens.Clear();
                throw new InvalidOperationException(
                    "Compatible USB companion discovery failed.");
            }
            if (discovered.Count > MaximumCandidates ||
                discovered.Select(item => item.Token).Distinct(StringComparer.Ordinal).Count() !=
                    discovered.Count)
                throw new InvalidDataException(
                    "Compatible USB companion discovery returned an invalid roster.");

            var stagedTokens = new Dictionary<CompanionEndpoint, string>();
            var stagedCandidates = new List<CompanionCandidate>(discovered.Count);
            foreach (var item in discovered)
            {
                ValidateHelperToken(item.Token);
                if (!item.Ready)
                    throw new InvalidDataException(
                        "Compatible USB companion admission was incomplete.");
                var endpoint = CompanionEndpoint.CreateSessionPrivate();
                var (label, family) = (item.Family, item.Label) switch
                {
                    ("esp32_s3_usb", "ESP32-S3 USB candidate") =>
                        (item.Label, CompanionDeviceFamily.Esp32S3UsbCandidate),
                    ("wio_tracker_l1", "Wio Tracker L1 USB candidate") =>
                        (item.Label, CompanionDeviceFamily.WioTrackerL1Companion),
                    _ => throw new InvalidDataException(
                        "Compatible USB companion admission was not allowlisted."),
                };
                stagedTokens.Add(endpoint, item.Token);
                stagedCandidates.Add(CompanionCandidate.CreateConnectableUsbCandidate(
                    endpoint, label, family));
            }

            _candidateTokens.Clear();
            foreach (var item in stagedTokens) _candidateTokens.Add(item.Key, item.Value);
            return stagedCandidates.AsReadOnly();
        }
        finally { _gate.Release(); }
    }

    public async ValueTask<ICompanionTransport> OpenAsync(
        CompanionCandidate candidate,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            if (!candidate.ConnectionReady ||
                candidate.Source != SimulatorConnectionSource.CompatibleUsbCompanion ||
                !_candidateTokens.TryGetValue(candidate.Endpoint, out var candidateToken) ||
                _sessions.ContainsKey(candidate.Endpoint))
                throw new InvalidOperationException(
                    "The selected USB companion is not available for this session.");
            string? session = null;
            var helperOpened = false;
            try
            {
                session = await _helper.OpenAsync(candidateToken, cancellationToken)
                    .ConfigureAwait(false);
                helperOpened = true;
                ValidateHelperToken(session);
                _sessions.Add(candidate.Endpoint, session);
            }
            catch (OperationCanceledException)
            {
                if (helperOpened && session is not null)
                {
                    try
                    {
                        await _helper.CloseAsync(session, CancellationToken.None)
                            .ConfigureAwait(false);
                    }
                    catch { }
                    try { await _helper.DisposeAsync().ConfigureAwait(false); }
                    catch { }
                    _disposed = true;
                }
                _candidateTokens.Clear();
                _sessions.Clear();
                throw;
            }
            catch
            {
                if (helperOpened)
                {
                    try
                    {
                        await _helper.CloseAsync(session!, CancellationToken.None)
                            .ConfigureAwait(false);
                    }
                    catch { }
                    try { await _helper.DisposeAsync().ConfigureAwait(false); }
                    catch { }
                    _disposed = true;
                }
                _candidateTokens.Clear();
                _sessions.Clear();
                throw new InvalidOperationException(
                    "The selected USB companion could not be opened.");
            }
            return new LiveTransport(this, candidate.Endpoint);
        }
        finally { _gate.Release(); }
    }

    private async ValueTask SendAsync(
        CompanionEndpoint endpoint,
        CompanionCommand command,
        CancellationToken cancellationToken)
    {
        var wire = ReduceCommand(command);
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var session = Session(endpoint);
            await _helper.SendAsync(session, wire, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            _candidateTokens.Clear();
            _sessions.Clear();
            throw;
        }
        catch (InvalidOperationException) { throw; }
        catch
        {
            _candidateTokens.Clear();
            _sessions.Clear();
            throw new IOException("The companion session send failed.");
        }
        finally { _gate.Release(); }
    }

    private async ValueTask<CompanionObservation?> PollAsync(
        CompanionEndpoint endpoint,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var observation = await _helper.PollAsync(
                Session(endpoint), cancellationToken).ConfigureAwait(false);
            return observation is null ? null : ExpandObservation(observation);
        }
        catch (OperationCanceledException)
        {
            _candidateTokens.Clear();
            _sessions.Clear();
            throw;
        }
        catch (InvalidOperationException) { throw; }
        catch
        {
            _candidateTokens.Clear();
            _sessions.Clear();
            throw new IOException("The companion session poll failed.");
        }
        finally { _gate.Release(); }
    }

    private async ValueTask CloseAsync(CompanionEndpoint endpoint)
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (!_sessions.Remove(endpoint, out var session)) return;
            try { await _helper.CloseAsync(session, CancellationToken.None).ConfigureAwait(false); }
            catch
            {
                _candidateTokens.Clear();
                _sessions.Clear();
                throw new IOException("The companion session close failed.");
            }
        }
        finally { _gate.Release(); }
    }

    public async ValueTask DisposeAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (_disposed) return;
            _disposed = true;
            _sessions.Clear();
            _candidateTokens.Clear();
            try { await _helper.DisposeAsync().ConfigureAwait(false); }
            catch { }
        }
        finally { _gate.Release(); }
    }

    private string Session(CompanionEndpoint endpoint) =>
        _sessions.TryGetValue(endpoint, out var session)
            ? session
            : throw new InvalidOperationException("The companion session is closed.");

    private static PrivateHelperSend ReduceCommand(CompanionCommand command)
    {
        if (command.Correlation <= 0)
            throw new InvalidOperationException("The command correlation is invalid.");
        return command.Kind switch
        {
            CompanionCommandKind.QuickStatus => new(
                "quick", command.Correlation, command.Text switch
                {
                    "I'm OK" => "OK",
                    "Need assistance" => "HELP",
                    "Anyone online?" => "ONLINE",
                    "Available to help" => "AVAILABLE",
                    _ => throw new InvalidOperationException(
                        "Only fixed quick-status commands may use a live companion."),
                }),
            CompanionCommandKind.Alert when
                command.Priority == SimulatorMessagePriority.Critical &&
                command.AlertSeverity == SimulatorAlertSeverity.Critical &&
                command.Text == "Critical alert" =>
                    new("critical", command.Correlation, null),
            _ => throw new InvalidOperationException(
                "This command is not available through a live companion."),
        };
    }

    private static CompanionObservation ExpandObservation(
        PrivateHelperObservation observation)
    {
        if (observation.Correlation <= 0)
            throw new InvalidDataException("The companion observation is invalid.");
        return observation.Kind switch
        {
            "quick" => new(CompanionCommandKind.QuickStatus,
                observation.Correlation, observation.Code switch
                {
                    "OK" => "I'm OK",
                    "HELP" => "Need assistance",
                    "ONLINE" => "Anyone online?",
                    "AVAILABLE" => "Available to help",
                    _ => throw new InvalidDataException(
                        "The companion quick-status observation is invalid."),
                }, SimulatorMessagePriority.Important, null),
            "critical" when observation.Code is null => new(
                CompanionCommandKind.Alert, observation.Correlation,
                "Critical alert (unauthenticated simulator test)",
                SimulatorMessagePriority.Critical,
                SimulatorAlertSeverity.Critical),
            "ack" when observation.Code is null => new(
                CompanionCommandKind.Acknowledgement, observation.Correlation,
                "Acknowledged", SimulatorMessagePriority.Important, null),
            _ => throw new InvalidDataException(
                "The companion observation kind is invalid."),
        };
    }

    private static void ValidateHelperToken(string token)
    {
        if (string.IsNullOrWhiteSpace(token) || token.Length is < 16 or > 80 ||
            token.Any(character => !(char.IsAsciiLetterOrDigit(character) ||
                character is '-' or '_')))
            throw new InvalidDataException("The private helper token is invalid.");
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    private sealed class LiveTransport(
        MeshCoreCompanionHost owner,
        CompanionEndpoint endpoint) : ICompanionTransport
    {
        private int _closed;

        public ValueTask SendAsync(
            CompanionCommand command, CancellationToken cancellationToken) =>
            Volatile.Read(ref _closed) == 0
                ? owner.SendAsync(endpoint, command, cancellationToken)
                : ValueTask.FromException(new ObjectDisposedException(nameof(LiveTransport)));

        public ValueTask<CompanionObservation?> ReceiveAsync(
            CancellationToken cancellationToken) =>
            Volatile.Read(ref _closed) == 0
                ? owner.PollAsync(endpoint, cancellationToken)
                : ValueTask.FromException<CompanionObservation?>(
                    new ObjectDisposedException(nameof(LiveTransport)));

        public ValueTask DisposeAsync() =>
            Interlocked.Exchange(ref _closed, 1) == 0
                ? owner.CloseAsync(endpoint)
                : ValueTask.CompletedTask;
    }
}

internal sealed class ProcessPrivateCompanionHelper : IPrivateCompanionHelper
{
    private const int MaximumResponseCharacters = 8192;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly string? _scriptOverride;
    private readonly TimeSpan _requestTimeout;
    private Process? _process;
    private Task? _stderrDrain;
    private bool _disposed;

    internal ProcessPrivateCompanionHelper(
        string? scriptOverride = null,
        TimeSpan? requestTimeout = null)
    {
        _scriptOverride = scriptOverride;
        _requestTimeout = requestTimeout ?? TimeSpan.FromSeconds(10);
        if (_requestTimeout <= TimeSpan.Zero || _requestTimeout > TimeSpan.FromSeconds(10))
            throw new ArgumentOutOfRangeException(nameof(requestTimeout));
    }

    public async ValueTask<IReadOnlyList<PrivateHelperCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        using var document = await RequestAsync(
            new Dictionary<string, object?> { ["v"] = 1, ["op"] = "discover" }, cancellationToken)
            .ConfigureAwait(false);
        var root = SuccessfulRoot(document);
        RequireProperties(root, "v", "ok", "candidates");
        var candidates = root.GetProperty("candidates");
        if (candidates.ValueKind != JsonValueKind.Array || candidates.GetArrayLength() > 8)
            throw new InvalidDataException("The companion helper roster is invalid.");
        var result = new List<PrivateHelperCandidate>(candidates.GetArrayLength());
        foreach (var item in candidates.EnumerateArray())
        {
            RequireProperties(item, "token", "family", "label", "ready");
            result.Add(new(
                RequiredString(item, "token"), RequiredString(item, "family"),
                RequiredString(item, "label"), RequiredBoolean(item, "ready")));
        }
        return result.AsReadOnly();
    }

    public async ValueTask<string> OpenAsync(
        string candidateToken, CancellationToken cancellationToken)
    {
        using var document = await RequestAsync(new Dictionary<string, object?>
        {
            ["v"] = 1, ["op"] = "open", ["token"] = candidateToken,
        }, cancellationToken).ConfigureAwait(false);
        var root = SuccessfulRoot(document);
        RequireProperties(root, "v", "ok", "session");
        return RequiredString(root, "session");
    }

    public async ValueTask SendAsync(
        string sessionToken, PrivateHelperSend command, CancellationToken cancellationToken)
    {
        var request = new Dictionary<string, object?>
        {
            ["v"] = 1, ["op"] = "send", ["session"] = sessionToken,
            ["kind"] = command.Kind, ["correlation"] = command.Correlation,
        };
        if (command.Code is not null) request["code"] = command.Code;
        using var document = await RequestAsync(request, cancellationToken).ConfigureAwait(false);
        var root = SuccessfulRoot(document);
        RequireProperties(root, "v", "ok", "accepted");
        if (!RequiredBoolean(root, "accepted"))
            throw new IOException("The companion helper rejected the command.");
    }

    public async ValueTask<PrivateHelperObservation?> PollAsync(
        string sessionToken, CancellationToken cancellationToken)
    {
        using var document = await RequestAsync(new Dictionary<string, object?>
        {
            ["v"] = 1, ["op"] = "poll", ["session"] = sessionToken,
        }, cancellationToken).ConfigureAwait(false);
        var root = SuccessfulRoot(document);
        RequireProperties(root, "v", "ok", "observation");
        var observation = root.GetProperty("observation");
        if (observation.ValueKind == JsonValueKind.Null) return null;
        if (observation.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("The companion helper observation is invalid.");
        foreach (var property in observation.EnumerateObject())
            if (property.Name is not ("kind" or "correlation" or "code"))
                throw new InvalidDataException("The companion helper observation is invalid.");
        var kind = RequiredString(observation, "kind");
        if (!observation.TryGetProperty("correlation", out var correlationValue) ||
            !correlationValue.TryGetInt64(out var correlation))
            throw new InvalidDataException("The companion helper correlation is invalid.");
        string? code = observation.TryGetProperty("code", out var codeValue)
            ? codeValue.ValueKind == JsonValueKind.String ? codeValue.GetString() :
                throw new InvalidDataException("The companion helper code is invalid.")
            : null;
        return new(kind, correlation, code);
    }

    public async ValueTask CloseAsync(
        string sessionToken, CancellationToken cancellationToken)
    {
        using var document = await RequestAsync(new Dictionary<string, object?>
        {
            ["v"] = 1, ["op"] = "close", ["session"] = sessionToken,
        }, cancellationToken).ConfigureAwait(false);
        var root = SuccessfulRoot(document);
        RequireProperties(root, "v", "ok", "closed");
        if (!RequiredBoolean(root, "closed"))
            throw new IOException("The companion helper did not close the session.");
    }

    private async ValueTask<JsonDocument> RequestAsync(
        IReadOnlyDictionary<string, object?> request,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var writeStarted = false;
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            await EnsureStartedAsync().ConfigureAwait(false);
            var process = _process!;
            var line = JsonSerializer.Serialize(request);
            writeStarted = true;
            await process.StandardInput.WriteLineAsync(line.AsMemory(), cancellationToken)
                .ConfigureAwait(false);
            await process.StandardInput.FlushAsync(cancellationToken).ConfigureAwait(false);
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(_requestTimeout);
            var response = await ReadBoundedLineAsync(
                    process.StandardOutput, MaximumResponseCharacters, timeout.Token)
                .ConfigureAwait(false);
            if (response is null)
                throw new IOException("The companion helper stopped unexpectedly.");
            return JsonDocument.Parse(response, new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 8,
            });
        }
        catch (OperationCanceledException)
        {
            await InvalidateOwnedProcessAsync().ConfigureAwait(false);
            if (writeStarted)
                throw new IOException(
                    "The private companion helper request outcome is unavailable.");
            throw;
        }
        catch
        {
            await InvalidateOwnedProcessAsync().ConfigureAwait(false);
            throw new IOException("The private companion helper failed.");
        }
        finally { _gate.Release(); }
    }

    private async ValueTask EnsureStartedAsync()
    {
        if (_process is { HasExited: false }) return;
        if (_process is not null)
            await InvalidateOwnedProcessAsync().ConfigureAwait(false);
        var root = _scriptOverride is null
            ? AppContext.BaseDirectory
            : Path.GetDirectoryName(Path.GetFullPath(_scriptOverride))!;
        var script = _scriptOverride is null
            ? ResolveBundledHelperPath(root)
            : Path.GetFullPath(_scriptOverride);
        if (!File.Exists(script))
            throw new InvalidOperationException("The companion helper is unavailable.");
        var start = new ProcessStartInfo
        {
            FileName = "python",
            WorkingDirectory = root,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        start.ArgumentList.Add(script);
        start.Environment["PYTHONUNBUFFERED"] = "1";
        var process = new Process { StartInfo = start };
        if (!process.Start())
            throw new InvalidOperationException("The companion helper could not start.");
        _process = process;
        _stderrDrain = DrainAndDiscardAsync(process.StandardError);
    }

    private static async Task DrainAndDiscardAsync(TextReader reader)
    {
        var buffer = new char[1024];
        while (true)
        {
            var read = await reader.ReadAsync(buffer.AsMemory())
                .ConfigureAwait(false);
            if (read == 0) return;
            Array.Clear(buffer, 0, read);
        }
    }

    public async ValueTask DisposeAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (_disposed) return;
            _disposed = true;
            if (_process is { HasExited: false } process)
            {
                try
                {
                    await process.StandardInput.WriteLineAsync("{\"v\":1,\"op\":\"shutdown\"}")
                        .ConfigureAwait(false);
                    await process.StandardInput.FlushAsync().ConfigureAwait(false);
                    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
                    await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
                }
                catch { await InvalidateOwnedProcessAsync().ConfigureAwait(false); }
            }
            if (_stderrDrain is not null)
            {
                try { await _stderrDrain.ConfigureAwait(false); }
                catch { }
            }
            _process?.Dispose();
            _process = null;
        }
        finally { _gate.Release(); }
    }

    private async ValueTask InvalidateOwnedProcessAsync()
    {
        var process = _process;
        _process = null;
        var stderrDrain = _stderrDrain;
        _stderrDrain = null;
        try
        {
            if (process is { HasExited: false })
            {
                process.Kill(entireProcessTree: true);
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
                await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
            }
        }
        catch { }
        if (stderrDrain is not null)
        {
            try
            {
                await stderrDrain.WaitAsync(TimeSpan.FromSeconds(1))
                    .ConfigureAwait(false);
            }
            catch { }
        }
        process?.Dispose();
    }

    private static async ValueTask<string?> ReadBoundedLineAsync(
        TextReader reader,
        int maximum,
        CancellationToken cancellationToken)
    {
        var builder = new StringBuilder(Math.Min(maximum, 1024));
        var buffer = new char[1];
        while (true)
        {
            var read = await reader.ReadAsync(buffer.AsMemory(), cancellationToken)
                .ConfigureAwait(false);
            if (read == 0)
            {
                if (builder.Length == 0) return null;
                throw new InvalidDataException(
                    "The companion helper response ended before its NDJSON delimiter.");
            }
            if (buffer[0] == '\n') return builder.ToString();
            if (builder.Length >= maximum)
                throw new InvalidDataException(
                    "The companion helper response exceeded its fixed bound.");
            if (buffer[0] != '\r') builder.Append(buffer[0]);
        }
    }

    private static JsonElement SuccessfulRoot(JsonDocument document)
    {
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("v", out var version) ||
            !version.TryGetInt32(out var protocolVersion) || protocolVersion != 1 ||
            version.GetRawText() != "1" ||
            !root.TryGetProperty("ok", out var ok) || ok.ValueKind != JsonValueKind.True)
            throw new IOException("The companion helper rejected the operation.");
        return root;
    }

    private static void RequireProperties(JsonElement value, params string[] expected)
    {
        var names = value.EnumerateObject().Select(property => property.Name)
            .Order(StringComparer.Ordinal).ToArray();
        var required = expected.Order(StringComparer.Ordinal).ToArray();
        if (!names.SequenceEqual(required, StringComparer.Ordinal))
            throw new InvalidDataException("The companion helper response is invalid.");
    }

    private static string RequiredString(JsonElement value, string name)
    {
        if (!value.TryGetProperty(name, out var item) ||
            item.ValueKind != JsonValueKind.String)
            throw new InvalidDataException("The companion helper response is invalid.");
        return item.GetString()!;
    }

    private static bool RequiredBoolean(JsonElement value, string name)
    {
        if (!value.TryGetProperty(name, out var item) ||
            item.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
            throw new InvalidDataException("The companion helper response is invalid.");
        return item.GetBoolean();
    }

    internal static string ResolveBundledHelperPath(string baseDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(baseDirectory);
        var root = Path.GetFullPath(baseDirectory);
        var candidate = Path.GetFullPath(Path.Combine(
            root, "meshcore_companion_bridge.py"));
        var prefix = root.EndsWith(Path.DirectorySeparatorChar)
            ? root
            : root + Path.DirectorySeparatorChar;
        if (!candidate.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ||
            !File.Exists(candidate))
            throw new InvalidOperationException("The companion helper is unavailable.");
        return candidate;
    }
}
