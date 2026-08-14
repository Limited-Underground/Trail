using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace OpenTrail.Loader;

internal sealed record MeshCoreRuntimeObservation(
    bool Succeeded,
    string? DisplayName,
    string? InstalledRuntime,
    string? Firmware,
    string? FailureCategory = null)
{
    internal static MeshCoreRuntimeObservation Unavailable { get; } =
        new(false, null, null, null, "unavailable");

    internal static MeshCoreRuntimeObservation Failed(string category) =>
        new(false, null, null, null, category);
}

/// <summary>
/// Performs only the fixed MeshCore runtime identity queries required by the
/// inspection view. It exposes no arbitrary command, setting, reset, erase,
/// bootloader, or firmware-write surface.
/// </summary>
internal static partial class MeshCoreRuntimeProbe
{
    private const int MaximumFramePayload = 300;
    private const int MaximumConsoleBytes = 2048;
    private static readonly TimeSpan CommandTimeout = TimeSpan.FromSeconds(3);
    private static readonly TimeSpan ConsoleIdle = TimeSpan.FromMilliseconds(350);

    [GeneratedRegex(@"^v\d{1,3}\.\d{1,3}\.\d{1,3}-[0-9a-f]{7,40}$",
        RegexOptions.CultureInvariant)]
    private static partial Regex FirmwarePattern();

    [GeneratedRegex(@"^\d{2}-[A-Z][a-z]{2}-\d{4}$",
        RegexOptions.CultureInvariant)]
    private static partial Regex BuildDatePattern();

    [GeneratedRegex(
        @"^(v\d{1,3}\.\d{1,3}\.\d{1,3}-[0-9a-f]{7,40}) \(Build: (\d{2}-[A-Z][a-z]{2}-\d{4})\)$",
        RegexOptions.CultureInvariant)]
    private static partial Regex RepeaterVersionPattern();

    internal static MeshCoreRuntimeObservation Inspect(
        WindowsUsbSerialCandidate candidate,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            return candidate.RuntimeFamily switch
            {
                MeshCoreUsbRuntimeFamily.HeltecV4Companion =>
                    QueryCompanion(candidate, cancellationToken),
                MeshCoreUsbRuntimeFamily.WioTrackerL1Companion =>
                    QueryCompanion(candidate, cancellationToken),
                MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater =>
                    QueryRepeater(candidate.PrivatePortName, cancellationToken),
                _ => MeshCoreRuntimeObservation.Unavailable,
            };
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return MeshCoreRuntimeObservation.Failed("windows_serial_error");
        }
        catch (TimeoutException)
        {
            return MeshCoreRuntimeObservation.Failed("runtime_timeout");
        }
        catch (InvalidDataException)
        {
            return MeshCoreRuntimeObservation.Failed("runtime_response_rejected");
        }
        catch (IOException)
        {
            return MeshCoreRuntimeObservation.Failed("serial_io_error");
        }
        catch
        {
            // Raw I/O, port names, and device responses never cross this
            // boundary. The card receives one generic unavailable outcome.
            return MeshCoreRuntimeObservation.Failed("internal_probe_error");
        }
    }

    private static MeshCoreRuntimeObservation QueryCompanion(
        WindowsUsbSerialCandidate candidate,
        CancellationToken cancellationToken)
    {
        using var connection = WindowsNativeSerialConnection.Open(
            candidate.PrivatePortName,
            rtsEnabled: false);
        cancellationToken.ThrowIfCancellationRequested();
        Thread.Sleep(150);
        connection.DiscardInput();

        var decoder = new MeshCoreSerialFrameDecoder(MaximumFramePayload);
        SendCompanionCommand(
            connection,
            [0x01, 0x03, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
                0x6D, 0x63, 0x63, 0x6C, 0x69],
            expectedResponseType: 0x05,
            decoder,
            cancellationToken);
        var deviceInfo = SendCompanionCommand(
            connection,
            [0x16, 0x03],
            expectedResponseType: 0x0D,
            decoder,
            cancellationToken);
        return ParseCompanionDeviceInfo(deviceInfo, candidate.RuntimeFamily);
    }

    private static byte[] SendCompanionCommand(
        WindowsNativeSerialConnection connection,
        ReadOnlySpan<byte> command,
        byte expectedResponseType,
        MeshCoreSerialFrameDecoder decoder,
        CancellationToken cancellationToken)
    {
        var framed = new byte[command.Length + 3];
        framed[0] = 0x3C;
        framed[1] = checked((byte)(command.Length & 0xFF));
        framed[2] = checked((byte)((command.Length >> 8) & 0xFF));
        command.CopyTo(framed.AsSpan(3));
        connection.Write(framed);

        var timer = Stopwatch.StartNew();
        var readBuffer = new byte[256];
        var frameCount = 0;
        while (timer.Elapsed < CommandTimeout)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var read = connection.Read(readBuffer);
            if (read == 0)
            {
                continue;
            }

            decoder.Feed(readBuffer.AsSpan(0, read));
            while (decoder.TryTakeFrame(out var response))
            {
                frameCount++;
                if (frameCount > 32)
                {
                    throw new InvalidDataException(
                        "The runtime emitted too many frames during inspection.");
                }

                if (response[0] == 0x01)
                {
                    throw new InvalidDataException(
                        "The runtime rejected the fixed inspection command.");
                }

                if (response[0] == expectedResponseType)
                {
                    return response;
                }
            }
        }

        throw new TimeoutException("The runtime identity query timed out.");
    }

    internal static MeshCoreRuntimeObservation ParseCompanionDeviceInfo(
        ReadOnlySpan<byte> response)
    {
        return ParseCompanionDeviceInfo(
            response,
            MeshCoreUsbRuntimeFamily.HeltecV4Companion);
    }

    internal static MeshCoreRuntimeObservation ParseCompanionDeviceInfo(
        ReadOnlySpan<byte> response,
        MeshCoreUsbRuntimeFamily runtimeFamily)
    {
        // type + protocol + contacts + channels + private BLE PIN + build/model/version
        if (response.Length < 80 || response[0] != 0x0D)
        {
            throw new InvalidDataException("The companion device-info response is incomplete.");
        }

        var protocolVersion = response[1];
        if (protocolVersion < 3)
        {
            throw new InvalidDataException("The companion protocol is unsupported.");
        }

        // Bytes 4..7 contain the BLE PIN. They are intentionally skipped and
        // never converted to a value or retained in the public observation.
        var buildDate = DecodeFixedAscii(response.Slice(8, 12));
        var model = DecodeFixedAscii(response.Slice(20, 40));
        var firmware = DecodeFixedAscii(response.Slice(60, 20));
        if (!BuildDatePattern().IsMatch(buildDate) ||
            !FirmwarePattern().IsMatch(firmware))
        {
            throw new InvalidDataException(
                "The companion runtime identity is not allowlisted.");
        }

        var displayName = runtimeFamily switch
        {
            MeshCoreUsbRuntimeFamily.HeltecV4Companion
                when model == "Heltec V4 OLED" => "Heltec V4 OLED",
            MeshCoreUsbRuntimeFamily.WioTrackerL1Companion
                when model == "Seeed Wio Tracker L1" => "Wio Tracker L1",
            _ => throw new InvalidDataException(
                "The companion runtime identity is not allowlisted."),
        };

        return new MeshCoreRuntimeObservation(
            true,
            displayName,
            "MeshCore USB companion",
            firmware);
    }

    private static MeshCoreRuntimeObservation QueryRepeater(
        string privatePortName,
        CancellationToken cancellationToken)
    {
        using var connection = WindowsNativeSerialConnection.Open(
            privatePortName,
            rtsEnabled: true);
        cancellationToken.ThrowIfCancellationRequested();
        Thread.Sleep(200);
        connection.DiscardInput();

        var responses = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var command in new[] { "board", "ver", "get role" })
        {
            cancellationToken.ThrowIfCancellationRequested();
            responses[command] = QueryRepeaterConsole(
                connection,
                command,
                cancellationToken);
        }

        return ParseRepeaterResponses(responses);
    }

    private static string QueryRepeaterConsole(
        WindowsNativeSerialConnection connection,
        string command,
        CancellationToken cancellationToken)
    {
        var request = Encoding.ASCII.GetBytes(command + "\r\n");
        connection.Write(request);

        var output = new MemoryStream();
        var readBuffer = new byte[256];
        var timer = Stopwatch.StartNew();
        var lastData = TimeSpan.Zero;
        while (timer.Elapsed < CommandTimeout)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var read = connection.Read(readBuffer);
            if (read > 0)
            {
                if (output.Length + read > MaximumConsoleBytes)
                {
                    throw new InvalidDataException(
                        "The repeater runtime response exceeded its inspection limit.");
                }

                output.Write(readBuffer, 0, read);
                lastData = timer.Elapsed;
                continue;
            }

            if (output.Length > 0 && timer.Elapsed - lastData >= ConsoleIdle)
            {
                break;
            }
        }

        if (output.Length == 0)
        {
            throw new TimeoutException("The repeater runtime query timed out.");
        }

        return Encoding.UTF8.GetString(output.ToArray());
    }

    internal static MeshCoreRuntimeObservation ParseRepeaterResponses(
        IReadOnlyDictionary<string, string> responses)
    {
        ArgumentNullException.ThrowIfNull(responses);
        if (responses.Count != 3 ||
            !responses.TryGetValue("board", out var boardResponse) ||
            !responses.TryGetValue("ver", out var versionResponse) ||
            !responses.TryGetValue("get role", out var roleResponse))
        {
            throw new InvalidDataException("The repeater runtime response is incomplete.");
        }

        var board = ExtractRepeaterValue("board", boardResponse);
        var version = ExtractRepeaterValue("ver", versionResponse);
        var role = ExtractRepeaterValue("get role", roleResponse);
        var versionMatch = RepeaterVersionPattern().Match(version);
        if (board != "Seeed SenseCap Solar" ||
            !versionMatch.Success ||
            !role.Equals("repeater", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                "The repeater runtime identity is not allowlisted.");
        }

        return new MeshCoreRuntimeObservation(
            true,
            "SenseCAP Solar",
            "MeshCore repeater",
            versionMatch.Groups[1].Value);
    }

    private static string ExtractRepeaterValue(string command, string response)
    {
        if (response.Length > MaximumConsoleBytes || response.Any(static value =>
                char.IsControl(value) && value is not '\r' and not '\n' and not '\t'))
        {
            throw new InvalidDataException("The repeater runtime response is invalid.");
        }

        var lines = response
            .Replace("\r", string.Empty, StringComparison.Ordinal)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Where(line => line != command && line is not ">" and not ">>")
            .ToArray();
        if (lines.Length != 1 || !lines[0].StartsWith("->", StringComparison.Ordinal))
        {
            throw new InvalidDataException("The repeater runtime response shape is invalid.");
        }

        var value = lines[0][2..].Trim();
        if (value.StartsWith('>'))
        {
            value = value[1..].Trim();
        }

        if (string.IsNullOrWhiteSpace(value) || value.Length > 120)
        {
            throw new InvalidDataException("The repeater runtime value is invalid.");
        }

        return value;
    }

    private static string DecodeFixedAscii(ReadOnlySpan<byte> bytes)
    {
        var length = bytes.IndexOf((byte)0);
        if (length < 0)
        {
            length = bytes.Length;
        }

        var value = Encoding.ASCII.GetString(bytes[..length]);
        if (value.Any(static character => character is < ' ' or > '~'))
        {
            throw new InvalidDataException("The runtime identity contains invalid text.");
        }

        return value;
    }
}

internal sealed class MeshCoreSerialFrameDecoder
{
    private readonly int _maximumPayload;
    private readonly List<byte> _buffer = [];
    private readonly Queue<byte[]> _frames = new();

    internal MeshCoreSerialFrameDecoder(int maximumPayload)
    {
        if (maximumPayload is < 1 or > 4096)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumPayload));
        }

        _maximumPayload = maximumPayload;
    }

    internal void Feed(ReadOnlySpan<byte> bytes)
    {
        if (_buffer.Count + bytes.Length > (_maximumPayload + 3) * 4)
        {
            throw new InvalidDataException("The serial frame buffer exceeded its limit.");
        }

        foreach (var value in bytes)
        {
            _buffer.Add(value);
        }

        DecodeAvailableFrames();
    }

    internal bool TryTakeFrame(out byte[] frame)
    {
        return _frames.TryDequeue(out frame!);
    }

    private void DecodeAvailableFrames()
    {
        while (true)
        {
            var marker = _buffer.IndexOf(0x3E);
            if (marker < 0)
            {
                _buffer.Clear();
                return;
            }

            if (marker > 0)
            {
                _buffer.RemoveRange(0, marker);
            }

            if (_buffer.Count < 3)
            {
                return;
            }

            var payloadLength = _buffer[1] | (_buffer[2] << 8);
            if (payloadLength is < 1 || payloadLength > _maximumPayload)
            {
                _buffer.RemoveAt(0);
                continue;
            }

            if (_buffer.Count < payloadLength + 3)
            {
                return;
            }

            var payload = _buffer.GetRange(3, payloadLength).ToArray();
            _buffer.RemoveRange(0, payloadLength + 3);
            _frames.Enqueue(payload);
        }
    }
}
