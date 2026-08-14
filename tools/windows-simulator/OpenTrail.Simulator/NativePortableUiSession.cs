using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;

namespace OpenTrail.Simulator;

internal enum PortableUiGesture { Activate, Hold }

internal sealed record PortableUiSnapshotMessage(
    ulong Sequence,
    int Direction,
    int Kind,
    int Priority,
    int Delivery,
    bool AcknowledgeAvailable,
    bool Truncated,
    bool TextUnavailable,
    string Text)
{
    internal string Serialize()
    {
        if (Sequence == 0 || Direction is < 0 or > 1 || Kind is < 0 or > 3 ||
            Priority is < 0 or > 2 || Delivery is < 0 or > 4 ||
            (Truncated && TextUnavailable) || Text.Length > 96 ||
            Text.Any(character => character is < ' ' or > '~') ||
            (!Truncated && !TextUnavailable && Text.Length == 0) ||
            ((Truncated || TextUnavailable) && Text.Length != 0))
        {
            throw new InvalidOperationException(
                "The portable UI message snapshot was outside its bounded schema.");
        }
        var bytes = Encoding.ASCII.GetBytes(Text);
        return string.Join('|', Sequence, Direction, Kind, Priority, Delivery,
            AcknowledgeAvailable ? 1 : 0, Truncated ? 1 : 0,
            TextUnavailable ? 1 : 0, bytes.Length,
            bytes.Length == 0 ? "-" : Convert.ToHexString(bytes));
    }
}

internal sealed record PortableUiSnapshotState(
    int PositionState,
    int ArchiveState,
    int RadioIndicator,
    int PositionIndicator,
    int PowerIndicator,
    bool PeerCountValid,
    byte PeerCount,
    byte UnreadMessages,
    bool ArchiveQueueCountValid,
    byte ArchiveQueueCount,
    bool RecoveryDiagnosticValid,
    uint RecoveryDiagnosticWord,
    ulong BridgeSessionEpoch,
    IReadOnlyList<PortableUiSnapshotMessage> Messages)
{
    internal static PortableUiSnapshotState Initial { get; } =
        new(0, 0, 0, 0, 0, false, 0, 0, true, 0, false, 0, 0, []);

    internal string Serialize()
    {
        if (Messages.Count > 12 || UnreadMessages != 0 ||
            Messages.Where((message, index) => index > 0 &&
                message.Sequence <= Messages[index - 1].Sequence).Any())
        {
            throw new InvalidOperationException(
                "The portable UI snapshot was outside its bounded schema.");
        }
        var fields = new List<string>
        {
            PositionState.ToString(CultureInfo.InvariantCulture),
            ArchiveState.ToString(CultureInfo.InvariantCulture),
            RadioIndicator.ToString(CultureInfo.InvariantCulture),
            PositionIndicator.ToString(CultureInfo.InvariantCulture),
            PowerIndicator.ToString(CultureInfo.InvariantCulture),
            PeerCountValid ? "1" : "0",
            PeerCount.ToString(CultureInfo.InvariantCulture),
            "0",
            ArchiveQueueCountValid ? "1" : "0",
            ArchiveQueueCount.ToString(CultureInfo.InvariantCulture),
            RecoveryDiagnosticValid ? "1" : "0",
            RecoveryDiagnosticWord.ToString(CultureInfo.InvariantCulture),
            BridgeSessionEpoch.ToString(CultureInfo.InvariantCulture),
            Messages.Count.ToString(CultureInfo.InvariantCulture),
        };
        fields.AddRange(Messages.Select(message => message.Serialize()));
        return string.Join('|', fields);
    }
}

internal sealed record PortableUiActionBinding(int Action, bool Enabled);

internal sealed record PortableUiPrimitive(
    int Kind, int X, int Y, int Width, int Height, int TextToken, int Style,
    int ActionSlot, bool Enabled, bool RequiresHold,
    bool NumericValueValid, int NumericValue, int OwnedTextIndex);

internal sealed record PortableUiOwnedText(
    string Text, bool Truncated, bool Unavailable);

internal sealed record PortableUiMessageRow(
    int TextIndex, int Delivery, int Kind, int Priority, bool Unread);

internal sealed record PortableUiMessagePresentation(
    int ListKind,
    byte PageIndex,
    byte PageCount,
    IReadOnlyList<PortableUiMessageRow> Rows,
    bool DetailValid,
    int DetailDelivery,
    int DetailKind,
    int DetailPriority,
    bool DetailUnread,
    bool DetailAcknowledgeAvailable,
    int ComposeTemplateId);

internal sealed record PortableUiOffer(
    uint Generation,
    uint Revision,
    int Screen,
    int Attention,
    int Notice,
    int Radio,
    int Position,
    int Power,
    bool PeerCountValid,
    byte PeerCount,
    byte UnreadMessages,
    bool ArchiveQueueCountValid,
    byte ArchiveQueueCount,
    IReadOnlyList<PortableUiOwnedText> OwnedTexts,
    PortableUiMessagePresentation Messages,
    IReadOnlyList<PortableUiActionBinding> Actions,
    int LogicalWidth,
    int LogicalHeight,
    int ViewportShape,
    IReadOnlyList<PortableUiPrimitive> Primitives);

internal sealed record PortableUiProtocolResult(
    string Kind,
    uint Generation,
    uint Revision,
    uint RequestId,
    int RequestKind,
    PortableUiOffer? Offer,
    string? Rejection,
    int Disposition = -1,
    ulong RequestBridgeSessionEpoch = 0,
    int RequestTemplateId = 0,
    ulong RequestMessageSequence = 0,
    string RequestText = "");

internal interface IPortableUiSession : IAsyncDisposable
{
    Task<PortableUiProtocolResult> StartAsync(PortableUiSnapshotState state, CancellationToken token);
    Task<PortableUiProtocolResult> InputAsync(uint generation, uint revision, int slot, PortableUiGesture gesture, CancellationToken token);
    Task<PortableUiProtocolResult> RefreshAsync(uint generation, uint revision, PortableUiSnapshotState state, CancellationToken token);
    Task<PortableUiProtocolResult> CompleteAsync(
        uint generation, uint revision, uint requestId, int requestKind,
        bool succeeded, ulong appliedBridgeSessionEpoch,
        ulong appliedMessageSequence, int requestTemplateId,
        ulong requestMessageSequence, PortableUiSnapshotState state,
        CancellationToken token);
    Task<PortableUiProtocolResult> PresentedAsync(uint generation, uint revision, CancellationToken token);
    Task<PortableUiProtocolResult> NotReadyAsync(uint generation, uint revision, CancellationToken token);
    Task<PortableUiProtocolResult> RenderFailedAsync(uint generation, uint revision, CancellationToken token);
}

internal sealed class NativePortableUiSession : IPortableUiSession
{
    private const int MaximumResponseLength = 8192;
    private readonly Process process;
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly CancellationTokenSource shutdown = new();
    private readonly object disposeSync = new();
    private readonly Task stderrDrain;
    private volatile bool disposalStarted;
    private Task? disposalTask;
    private uint committedGeneration;
    private uint committedRevision;
    private PortableUiOffer? pendingOffer;
    private PortableUiOffer? committedOffer;

    internal NativePortableUiSession(string executablePath)
    {
        if (string.IsNullOrWhiteSpace(executablePath) ||
            !Path.IsPathFullyQualified(executablePath) ||
            !File.Exists(executablePath))
        {
            throw new InvalidOperationException("The native portable UI host is unavailable.");
        }
        process = Process.Start(new ProcessStartInfo
        {
            FileName = executablePath,
            UseShellExecute = false,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        }) ?? throw new InvalidOperationException("The native portable UI host could not start.");
        stderrDrain = DrainStandardErrorAsync();
    }

    internal static NativePortableUiSession CreateFromEnvironment()
    {
        var path = Environment.GetEnvironmentVariable("OPENTRAIL_PORTABLE_UI_HOST");
        return new NativePortableUiSession(path ?? string.Empty);
    }

    public Task<PortableUiProtocolResult> StartAsync(PortableUiSnapshotState state, CancellationToken token) =>
        SendAsync($"START|2|{state.Serialize()}", token, ReplyContract.OfferOrReject);

    public Task<PortableUiProtocolResult> InputAsync(uint generation, uint revision, int slot, PortableUiGesture gesture, CancellationToken token) =>
        SendAsync($"INPUT|2|{generation}|{revision}|{slot}|{(gesture == PortableUiGesture.Hold ? "H" : "A")}", token, ReplyContract.OfferOrReject);

    public Task<PortableUiProtocolResult> RefreshAsync(uint generation, uint revision, PortableUiSnapshotState state, CancellationToken token) =>
        SendAsync($"REFRESH|2|{generation}|{revision}|{state.Serialize()}", token, ReplyContract.OfferIdleOrReject);

    public Task<PortableUiProtocolResult> CompleteAsync(
        uint generation, uint revision, uint requestId, int requestKind,
        bool succeeded, ulong appliedBridgeSessionEpoch,
        ulong appliedMessageSequence, int requestTemplateId,
        ulong requestMessageSequence, PortableUiSnapshotState state,
        CancellationToken token) =>
        SendAsync($"COMPLETE|2|{generation}|{revision}|{requestId}|{requestKind}|{(succeeded ? 1 : 0)}|{appliedBridgeSessionEpoch}|{appliedMessageSequence}|{requestTemplateId}|{requestMessageSequence}|{state.Serialize()}", token, ReplyContract.OfferOrReject);

    public Task<PortableUiProtocolResult> PresentedAsync(uint generation, uint revision, CancellationToken token) =>
        SendAsync($"PRESENTED|2|{generation}|{revision}", token, ReplyContract.Presented, commitOnSuccess: true);

    public Task<PortableUiProtocolResult> NotReadyAsync(uint generation, uint revision, CancellationToken token) =>
        SendAsync($"NOT_READY|2|{generation}|{revision}", token, ReplyContract.OfferOrReject);

    public Task<PortableUiProtocolResult> RenderFailedAsync(uint generation, uint revision, CancellationToken token) =>
        SendAsync($"RENDER_FAILED|2|{generation}|{revision}", token, ReplyContract.RejectOnly);

    private enum ReplyContract
    {
        OfferOrReject,
        OfferIdleOrReject,
        Presented,
        RejectOnly,
    }

    private async Task<PortableUiProtocolResult> SendAsync(
        string command,
        CancellationToken token,
        ReplyContract replyContract,
        bool commitOnSuccess = false)
    {
        ObjectDisposedException.ThrowIf(disposalStarted, this);
        await gate.WaitAsync(token).ConfigureAwait(false);
        var commandStarted = false;
        try
        {
            ObjectDisposedException.ThrowIf(disposalStarted, this);
            if (process.HasExited)
            {
                throw new InvalidOperationException("The native portable UI host exited unexpectedly.");
            }
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token, shutdown.Token);
            timeout.CancelAfter(TimeSpan.FromSeconds(3));
            commandStarted = true;
            await process.StandardInput.WriteLineAsync(command.AsMemory(), timeout.Token).ConfigureAwait(false);
            await process.StandardInput.FlushAsync(timeout.Token).ConfigureAwait(false);
            var response = await ReadBoundedLineAsync(process.StandardOutput, timeout.Token).ConfigureAwait(false);
            if (response.Length == 0 || response.Length > MaximumResponseLength)
            {
                throw new InvalidOperationException("The native portable UI response was outside its bounded schema.");
            }
            PortableUiProtocolResult parsed;
            parsed = PortableUiProtocolParser.Parse(response);
            ValidateReply(parsed, replyContract);
            if (parsed.Offer is not null)
            {
                pendingOffer = parsed.Offer;
            }
            if (commitOnSuccess && parsed.Kind == "COMMITTED")
            {
                if (pendingOffer is null ||
                    pendingOffer.Generation != parsed.Generation ||
                    pendingOffer.Revision != parsed.Revision)
                {
                    throw new InvalidOperationException(
                        "The native portable UI commit did not match its pending offer.");
                }
                ValidateCommittedRequestEvidence(committedOffer, parsed);
                committedGeneration = parsed.Generation;
                committedRevision = parsed.Revision;
                committedOffer = pendingOffer;
                pendingOffer = null;
            }
            return parsed;
        }
        catch (OperationCanceledException)
        {
            TerminateOwnedProcess();
            throw;
        }
        catch
        {
            if (commandStarted)
            {
                TerminateOwnedProcess();
            }
            throw;
        }
        finally
        {
            gate.Release();
        }
    }

    private static void ValidateReply(
        PortableUiProtocolResult result,
        ReplyContract contract)
    {
        var accepted = contract switch
        {
            ReplyContract.OfferOrReject => result.Kind is "OFFER" or "REJECT",
            ReplyContract.OfferIdleOrReject => result.Kind is "OFFER" or "IDLE" or "REJECT",
            // OFFER is the exact same frame when the canonical sink reports
            // NOT_READY. A REJECT after WPF drew the candidate is terminal.
            ReplyContract.Presented => result.Kind is "COMMITTED" or "OFFER",
            ReplyContract.RejectOnly => result.Kind == "REJECT",
            _ => false,
        };
        if (!accepted)
        {
            throw new InvalidOperationException(
                "The native portable UI response did not match the command contract.");
        }
    }

    internal static void ValidateCommittedRequestEvidence(
        PortableUiOffer? priorCommittedOffer,
        PortableUiProtocolResult result)
    {
        if (result.Kind != "COMMITTED" || result.Disposition != 2 ||
            result.RequestKind != 10)
        {
            return;
        }
        if (priorCommittedOffer is not
                { Screen: 11, OwnedTexts.Count: 1 } ||
            result.RequestTemplateId !=
                priorCommittedOffer.Messages.ComposeTemplateId ||
            result.RequestText != priorCommittedOffer.OwnedTexts[0].Text)
        {
            throw new InvalidOperationException(
                "The native message request did not match the exact displayed template.");
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (disposeSync)
        {
            disposalTask ??= DisposeCoreAsync();
            return new ValueTask(disposalTask);
        }
    }

    private async Task DisposeCoreAsync()
    {
        disposalStarted = true;
        var entered = false;
        try
        {
            using var wait = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try
            {
                await gate.WaitAsync(wait.Token).ConfigureAwait(false);
                entered = true;
            }
            catch (OperationCanceledException)
            {
                shutdown.Cancel();
                TerminateOwnedProcess();
                await gate.WaitAsync().ConfigureAwait(false);
                entered = true;
            }
            if (!process.HasExited)
            {
                using var closeTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
                if (committedGeneration != 0 && committedRevision != 0)
                {
                    await WriteAndReadForCloseAsync(
                        $"CLOSE|2|{committedGeneration}|{committedRevision}",
                        "COMMITTED", closeTimeout.Token)
                        .ConfigureAwait(false);
                }
                await WriteAndReadForCloseAsync(
                    "QUIT|2", "BYE", closeTimeout.Token).ConfigureAwait(false);
                await process.WaitForExitAsync(closeTimeout.Token).ConfigureAwait(false);
            }
        }
        catch (Exception)
        {
            TerminateOwnedProcess();
        }
        finally
        {
            shutdown.Cancel();
            TerminateOwnedProcess();
            try { await stderrDrain.ConfigureAwait(false); } catch { }
            process.Dispose();
            if (entered) gate.Release();
            shutdown.Dispose();
        }
    }

    private async Task WriteAndReadForCloseAsync(
        string command,
        string expectedKind,
        CancellationToken token)
    {
        await process.StandardInput.WriteLineAsync(command.AsMemory(), token).ConfigureAwait(false);
        await process.StandardInput.FlushAsync(token).ConfigureAwait(false);
        var response = await ReadBoundedLineAsync(process.StandardOutput, token).ConfigureAwait(false);
        var parsed = PortableUiProtocolParser.Parse(response);
        if (parsed.Kind != expectedKind ||
            (expectedKind == "COMMITTED" && parsed.Disposition != 1))
        {
            throw new InvalidOperationException(
                "The native portable UI close response failed its exact contract.");
        }
    }

    private static async Task<string> ReadBoundedLineAsync(StreamReader reader, CancellationToken token)
    {
        var result = new System.Text.StringBuilder(256, MaximumResponseLength);
        var buffer = new char[1];
        while (true)
        {
            var read = await reader.ReadAsync(buffer.AsMemory(), token).ConfigureAwait(false);
            if (read == 0) throw new InvalidOperationException("The native portable UI host closed its response stream.");
            if (buffer[0] == '\n') return result.ToString().TrimEnd('\r');
            if (result.Length == MaximumResponseLength)
                throw new InvalidOperationException("The native portable UI response exceeded its bounded schema.");
            result.Append(buffer[0]);
        }
    }

    private async Task DrainStandardErrorAsync()
    {
        var total = 0;
        var buffer = new char[256];
        try
        {
            while (true)
            {
                var read = await process.StandardError.ReadAsync(buffer.AsMemory(), shutdown.Token).ConfigureAwait(false);
                if (read == 0) return;
                total += read;
                if (total > 4096)
                {
                    TerminateOwnedProcess();
                    return;
                }
            }
        }
        catch (OperationCanceledException) when (shutdown.IsCancellationRequested) { }
    }

    private void TerminateOwnedProcess()
    {
        try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
        catch (InvalidOperationException) { }
    }
}

internal static class PortableUiProtocolParser
{
    internal static PortableUiProtocolResult Parse(string line)
    {
        var fields = line.Split('|', StringSplitOptions.None);
        if (fields.Length < 2 || fields[1] != "2") throw Invalid();
        return fields[0] switch
        {
            "OFFER" => ParseOffer(fields),
            "COMMITTED" => ParseCommitted(fields),
            "IDLE" when fields.Length == 4 => new("IDLE", U(fields[2]), U(fields[3]), 0, 0, null, null),
            "REJECT" => ParseReject(fields),
            "BYE" when fields.Length == 2 => new("BYE", 0, 0, 0, 0, null, null),
            _ => throw Invalid(),
        };
    }

    private static PortableUiProtocolResult ParseReject(string[] fields)
    {
        if (fields.Length == 3 && fields[2] is "VERSION" or "SCHEMA" or "PRECONDITION" or
            "COMMAND" or "COMMAND_TOO_LONG" or "PLAN")
            return new("REJECT", 0, 0, 0, 0, null, fields[2]);
        if (fields.Length == 5 && Range(fields[2], 0, 11) >= 0 &&
            Range(fields[3], 0, 8) >= 0 && Range(fields[4], 0, 6) >= 0)
            return new("REJECT", 0, 0, 0, 0, null, string.Join(':', fields.Skip(2)));
        throw Invalid();
    }

    private static PortableUiProtocolResult ParseCommitted(string[] fields)
    {
        if (fields.Length != 12) throw Invalid();
        var disposition = Range(fields[4], 1, 2);
        var requestId = UI(fields[5]);
        var requestKind = Range(fields[6], 0, 11);
        var expectedEpoch = UL(fields[7]);
        var templateId = Range(fields[8], 0, 8);
        var targetSequence = UL(fields[9]);
        var textLength = Range(fields[10], 0, 96);
        var requestText = textLength == 0
            ? fields[11] == "-" ? string.Empty : throw Invalid()
            : ParseAscii(fields[11], textLength);
        if ((disposition == 2 && (requestId == 0 || requestKind == 0)) ||
            (requestId == 0) != (requestKind == 0) ||
            (requestId == 0 && (expectedEpoch != 0 || templateId != 0 ||
                targetSequence != 0 || requestText.Length != 0)) ||
            (requestKind == 10) != (templateId != 0) ||
            (requestKind == 11) != (targetSequence != 0) ||
            (requestKind != 10 && requestText.Length != 0))
        {
            throw Invalid();
        }
        return new("COMMITTED", U(fields[2]), U(fields[3]), requestId,
            requestKind, null, null, disposition, expectedEpoch, templateId,
            targetSequence, requestText);
    }

    private static PortableUiProtocolResult ParseOffer(string[] f)
    {
        if (f.Length < 32) throw Invalid();
        var index = 2;
        var generation = U(f[index++]);
        var revision = U(f[index++]);
        var screen = Range(f[index++], 0, 11);
        var attention = Range(f[index++], 0, 3);
        var notice = Range(f[index++], 0, 27);
        var radio = Range(f[index++], 0, 4);
        var position = Range(f[index++], 0, 4);
        var power = Range(f[index++], 0, 4);
        var peerValid = B(f[index++]);
        var peers = Byte(f[index++]);
        var unread = Byte(f[index++]);
        var archiveValid = B(f[index++]);
        var archiveCount = Byte(f[index++]);
        var ownedCount = Range(f[index++], 0, 4);
        var ownedTexts = new List<PortableUiOwnedText>(ownedCount);
        for (var ownedIndex = 0; ownedIndex < ownedCount; ++ownedIndex)
        {
            var truncated = B(f[index++]);
            var unavailable = B(f[index++]);
            var textLength = Range(f[index++], 1, 96);
            var text = ParseAscii(f[index++], textLength);
            if (truncated && unavailable) throw Invalid();
            ownedTexts.Add(new(text, truncated, unavailable));
        }
        var listKind = Range(f[index++], 0, 2);
        var pageIndex = Byte(f[index++]);
        var pageCount = Byte(f[index++]);
        var rowCount = Range(f[index++], 0, 2);
        var rows = new List<PortableUiMessageRow>(rowCount);
        for (var rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            rows.Add(new(
                Range(f[index++], 0, 3),
                Range(f[index++], 0, 5),
                Range(f[index++], 1, 4),
                Range(f[index++], 1, 3),
                B(f[index++])));
        }
        var detailValid = B(f[index++]);
        var detailDelivery = Range(f[index++], 0, 5);
        var detailKind = Range(f[index++], 0, 4);
        var detailPriority = Range(f[index++], 0, 3);
        var detailUnread = B(f[index++]);
        var detailAcknowledge = B(f[index++]);
        var composeTemplateId = Range(f[index++], 0, 8);
        if ((!detailValid && (detailDelivery != 0 || detailKind != 0 ||
                detailPriority != 0 || detailUnread || detailAcknowledge)) ||
            (detailValid && (detailKind == 0 || detailPriority == 0)))
        {
            throw Invalid();
        }
        var messages = new PortableUiMessagePresentation(
            listKind, pageIndex, pageCount, rows, detailValid,
            detailDelivery, detailKind, detailPriority, detailUnread,
            detailAcknowledge, composeTemplateId);
        var actionCount = Range(f[index++], 0, 4);
        var actions = new List<PortableUiActionBinding>(actionCount);
        for (var actionIndex = 0; actionIndex < actionCount; ++actionIndex)
        {
            actions.Add(new(Range(f[index++], 1, 32), B(f[index++])));
        }
        var width = I(f[index++]);
        var height = I(f[index++]);
        if (width != 466 || height != 466) throw Invalid();
        var shape = Range(f[index++], 1, 1);
        var primitiveCount = Range(f[index++], 1, 16);
        if (f.Length != index + primitiveCount * 13) throw Invalid();
        ValidateMessageFrame(screen, ownedTexts, messages, actions);
        var messageScreen = screen is >= 7 and <= 11;
        var expectedPrimitives = new List<(int Kind, int Token, int OwnedText)>
        {
            (0, 0, 255), (1, 100 + screen, 255),
        };
        if (!messageScreen)
        {
            expectedPrimitives.AddRange([
                (2, 400 + radio, 255),
                (2, 410 + position, 255),
                (2, 420 + power, 255),
            ]);
            if (peerValid) expectedPrimitives.Add((3, 1, 255));
            expectedPrimitives.Add((3, 2, 255));
            if (archiveValid) expectedPrimitives.Add((3, 3, 255));
        }
        if (notice != 0) expectedPrimitives.Add((1, 200 + notice, 255));
        if (screen == 9)
        {
            expectedPrimitives.Add((1, 0, 0));
            expectedPrimitives.Add((1, 0, 1));
        }
        else if (screen == 11)
        {
            expectedPrimitives.Add((1, 0, 0));
        }
        for (var slot = 0; slot < actionCount; ++slot)
        {
            var ownsText = (screen == 8 && slot < rowCount) ||
                (screen == 10 && slot < 2);
            expectedPrimitives.Add(ownsText
                ? (4, 0, slot)
                : (4, 300 + actions[slot].Action, 255));
        }
        if (primitiveCount != expectedPrimitives.Count) throw Invalid();
        var primitives = new List<PortableUiPrimitive>(primitiveCount);
        for (var primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex)
        {
            var kind = Range(f[index++], 0, 4);
            var x = Range(f[index++], 0, ushort.MaxValue);
            var y = Range(f[index++], 0, ushort.MaxValue);
            var primitiveWidth = Range(f[index++], 1, ushort.MaxValue);
            var primitiveHeight = Range(f[index++], 1, ushort.MaxValue);
            var token = Range(f[index++], 0, ushort.MaxValue);
            var style = Range(f[index++], 0, 7);
            var slot = I(f[index++]);
            var enabled = B(f[index++]);
            var hold = B(f[index++]);
            var numericValid = B(f[index++]);
            var numericValue = Range(f[index++], 0, ushort.MaxValue);
            var ownedTextIndex = Range(f[index++], 0, 255);
            var isAction = kind == 4;
            var isMetric = kind == 3;
            var isPanel = kind == 0;
            var expectedNumericValue = token switch
            {
                1 => peers,
                2 => unread,
                3 => archiveCount,
                _ => 0,
            };
            if (x + primitiveWidth > width || y + primitiveHeight > height ||
                !KnownToken(token) ||
                kind != expectedPrimitives[primitiveIndex].Kind ||
                token != expectedPrimitives[primitiveIndex].Token ||
                ownedTextIndex != expectedPrimitives[primitiveIndex].OwnedText ||
                (ownedTextIndex != 255 && ownedTextIndex >= ownedCount) ||
                (isAction && (slot < 0 || slot >= actionCount)) ||
                (!isAction && slot != 255) || (!isAction && (enabled || hold)) ||
                (numericValid != isMetric) ||
                (isMetric && numericValue != expectedNumericValue) ||
                (isAction && (primitiveWidth < 64 || primitiveHeight < 64)) ||
                (isAction && hold != (actions[slot].Action is 11 or 19 or 32)) ||
                (primitiveIndex == 0 && (!isPanel || x != 0 || y != 0 ||
                    primitiveWidth != 466 || primitiveHeight != 466 || token != 0 ||
                    style != 0 || numericValid || numericValue != 0)) ||
                (primitiveIndex != 0 && isPanel) ||
                (!isPanel && !InsideCircle(x, y, primitiveWidth, primitiveHeight)))
                throw Invalid();
            primitives.Add(new(kind, x, y, primitiveWidth, primitiveHeight,
                token, style, slot, enabled, hold, numericValid, numericValue,
                ownedTextIndex));
        }
        var actionPrimitives = primitives.Where(item => item.Kind == 4).ToArray();
        if (actionPrimitives.Length != actionCount ||
            actionPrimitives.Where((item, slot) => item.ActionSlot != slot ||
                item.Enabled != actions[slot].Enabled ||
                item.TextToken != expectedPrimitives[primitiveCount - actionCount + slot].Token ||
                item.OwnedTextIndex != expectedPrimitives[primitiveCount - actionCount + slot].OwnedText).Any()) throw Invalid();
        var offer = new PortableUiOffer(generation, revision, screen, attention,
            notice, radio, position, power, peerValid, peers, unread,
            archiveValid, archiveCount, ownedTexts, messages, actions, width,
            height, shape, primitives);
        return new("OFFER", generation, revision, 0, 0, offer, null);
    }

    private static bool KnownToken(int value) => value is 0 or 1 or 2 or 3 ||
        value is >= 100 and <= 111 || value is >= 200 and <= 227 ||
        value is >= 301 and <= 332 || value is >= 400 and <= 404 ||
        value is >= 410 and <= 414 || value is >= 420 and <= 424;

    private static void ValidateMessageFrame(
        int screen,
        IReadOnlyList<PortableUiOwnedText> owned,
        PortableUiMessagePresentation messages,
        IReadOnlyList<PortableUiActionBinding> actions)
    {
        var isMessageScreen = screen is >= 7 and <= 11;
        if (!isMessageScreen)
        {
            if (owned.Count != 0 || messages.ListKind != 0 ||
                messages.PageIndex != 0 || messages.PageCount != 0 ||
                messages.Rows.Count != 0 || messages.DetailValid ||
                messages.ComposeTemplateId != 0)
            {
                throw Invalid();
            }
            return;
        }
        if (screen == 7)
        {
            if (owned.Count != 0 || messages.ListKind != 0 ||
                messages.PageIndex != 0 || messages.PageCount != 0 ||
                messages.Rows.Count != 0 || messages.DetailValid ||
                messages.ComposeTemplateId != 0 ||
                actions.Count != 4 || actions[0] != new PortableUiActionBinding(22, true) ||
                actions[1] != new PortableUiActionBinding(23, true) ||
                actions[2].Action != 24 ||
                actions[3] != new PortableUiActionBinding(12, true))
            {
                throw Invalid();
            }
            return;
        }
        if (screen == 8)
        {
            var nonfinalPage = messages.PageIndex + 1 < messages.PageCount;
            var expectedActions = new List<PortableUiActionBinding>();
            for (var row = 0; row < messages.Rows.Count; ++row)
            {
                expectedActions.Add(new(25 + row, true));
            }
            if (messages.PageCount > 1) expectedActions.Add(new(27, true));
            expectedActions.Add(new(12, true));
            if (messages.ListKind is < 1 or > 2 || messages.DetailValid ||
                messages.ComposeTemplateId != 0 ||
                messages.PageCount is 0 or > 6 ||
                messages.PageIndex >= messages.PageCount ||
                owned.Count != messages.Rows.Count ||
                (nonfinalPage && messages.Rows.Count != 2) ||
                (messages.PageCount > 1 && !nonfinalPage && messages.Rows.Count == 0) ||
                messages.Rows.Where((row, rowIndex) =>
                    row.TextIndex != rowIndex ||
                    (messages.ListKind == 1 && row.Delivery != 0) ||
                    (messages.ListKind == 2 && (row.Delivery == 0 || row.Unread))).Any() ||
                !actions.SequenceEqual(expectedActions))
            {
                throw Invalid();
            }
            return;
        }
        if (screen == 9)
        {
            var inbox = messages.ListKind == 1;
            var acknowledge = messages.DetailAcknowledgeAvailable;
            var expectedActions = acknowledge
                ? new[] { new PortableUiActionBinding(32, true), new PortableUiActionBinding(12, true) }
                : [new PortableUiActionBinding(12, true)];
            if (messages.ListKind is < 1 or > 2 || !messages.DetailValid ||
                messages.ComposeTemplateId != 0 ||
                owned.Count != 2 || messages.Rows.Count != 0 ||
                messages.DetailUnread ||
                (inbox && messages.DetailDelivery != 0) ||
                (!inbox && (messages.DetailDelivery == 0 || acknowledge)) ||
                (acknowledge && (!inbox || messages.DetailKind != 3 ||
                    messages.DetailPriority != 3)) ||
                !actions.SequenceEqual(expectedActions))
            {
                throw Invalid();
            }
            return;
        }
        if (screen == 10)
        {
            if (owned.Count != 2 || messages.ListKind != 0 ||
                messages.PageIndex != 0 || messages.PageCount != 0 ||
                messages.Rows.Count != 0 || messages.DetailValid ||
                messages.ComposeTemplateId != 0 ||
                actions.Count != 4 ||
                actions[0] != new PortableUiActionBinding(28, true) ||
                actions[1] != new PortableUiActionBinding(29, true) ||
                actions[2] != new PortableUiActionBinding(30, true) ||
                actions[3] != new PortableUiActionBinding(12, true))
            {
                throw Invalid();
            }
            return;
        }
        if (screen == 11 && (owned.Count != 1 || messages.ListKind != 0 ||
            messages.PageIndex != 0 || messages.PageCount != 0 ||
            messages.Rows.Count != 0 || messages.DetailValid ||
            messages.ComposeTemplateId is < 1 or > 8 ||
            actions.Count != 2 ||
            actions[0].Action != 31 ||
            actions[1] != new PortableUiActionBinding(12, true)))
        {
            throw Invalid();
        }
    }

    private static string ParseAscii(string value, int expectedBytes)
    {
        if (expectedBytes is < 1 or > 96 || value.Length != expectedBytes * 2 ||
            value.Any(character => !(character is >= '0' and <= '9' or >= 'A' and <= 'F')))
        {
            throw Invalid();
        }
        byte[] bytes;
        try { bytes = Convert.FromHexString(value); }
        catch (FormatException) { throw Invalid(); }
        if (bytes.Length != expectedBytes || bytes.Any(item => item is < 0x20 or > 0x7E))
        {
            throw Invalid();
        }
        return Encoding.ASCII.GetString(bytes);
    }

    private static bool InsideCircle(int x, int y, int width, int height)
    {
        foreach (var point in new[]
        {
            (X: x, Y: y), (X: x + width, Y: y),
            (X: x, Y: y + height), (X: x + width, Y: y + height),
        })
        {
            var dx = point.X - 233L;
            var dy = point.Y - 233L;
            if (dx * dx + dy * dy > 233L * 233L) return false;
        }
        return true;
    }

    private static bool B(string value) => value switch { "0" => false, "1" => true, _ => throw Invalid() };
    private static byte Byte(string value) => byte.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) ? parsed : throw Invalid();
    private static int I(string value) => int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) ? parsed : throw Invalid();
    private static uint U(string value) => uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) && parsed != 0 ? parsed : throw Invalid();
    private static uint UI(string value) => uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) ? parsed : throw Invalid();
    private static ulong UL(string value) => ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) ? parsed : throw Invalid();
    private static int Range(string value, int minimum, int maximum) { var parsed = I(value); return parsed >= minimum && parsed <= maximum ? parsed : throw Invalid(); }
    private static InvalidOperationException Invalid() => new("The native portable UI response failed its exact public schema.");
}
