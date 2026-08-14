using System.Text.Json;
using System.Text.RegularExpressions;
using OpenTrail.Simulator.Core;

namespace OpenTrail.Simulator.Windows;

internal interface IRedactedRuntimeEvidenceProvider
{
    ValueTask<string> CollectAsync(CancellationToken cancellationToken);
}

/// <summary>
/// Reduces the existing privacy-safe, read-only runtime evidence into
/// connection-blocked USB companion choices. It never receives a port or raw
/// runtime response and supplies no transport-opening implementation.
/// </summary>
internal sealed partial class WindowsCompanionDiscovery : ICompanionDeviceDiscovery
{
    private const int MaximumCandidates = 16;
    private const string HeltecFirmware = "v1.16.0-07a3ca9";
    private const string WioFirmware = "v1.17.0-727fc05";
    private readonly IRedactedRuntimeEvidenceProvider _provider;

    internal WindowsCompanionDiscovery(IRedactedRuntimeEvidenceProvider provider) =>
        _provider = provider ?? throw new ArgumentNullException(nameof(provider));

    public async ValueTask<IReadOnlyList<CompanionCandidate>> DiscoverAsync(
        CancellationToken cancellationToken)
    {
        var json = await _provider.CollectAsync(cancellationToken).ConfigureAwait(false);
        return ParseRedactedEvidence(json);
    }

    public static IReadOnlyList<CompanionCandidate> ParseRedactedEvidence(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        if (json.Length is < 2 or > 256 * 1024)
            throw new InvalidDataException("Redacted runtime evidence has an invalid size.");

        using var document = JsonDocument.Parse(json, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 16,
        });
        var root = document.RootElement;
        RequireObject(root);
        if (ReadRequiredString(root, "schema") != "ot_meshcore_runtime_evidence_v0" ||
            !ReadRequiredBoolean(root, "read_only") ||
            ReadRequiredBoolean(root, "state_changes_made"))
            throw new InvalidDataException("Runtime evidence does not prove a read-only snapshot.");

        ValidatePrivacy(root.GetProperty("privacy"));
        var devices = root.GetProperty("devices");
        if (devices.ValueKind != JsonValueKind.Array || devices.GetArrayLength() > MaximumCandidates ||
            ReadRequiredInt32(root, "candidate_count") != devices.GetArrayLength())
            throw new InvalidDataException("Runtime evidence has an invalid candidate collection.");

        var result = new List<CompanionCandidate>(devices.GetArrayLength());
        var ordinals = new HashSet<string>(StringComparer.Ordinal);
        foreach (var device in devices.EnumerateArray())
        {
            RequireObject(device);
            RejectPrivateFields(device);
            var ordinal = ReadRequiredString(device, "candidate");
            if (!CandidateOrdinalPattern().IsMatch(ordinal) || !ordinals.Add(ordinal))
                throw new InvalidDataException("Runtime evidence has an invalid candidate ordinal.");
            if (!ReadRequiredBoolean(device, "runtime_query_succeeded"))
                continue;
            if (ReadRequiredString(device, "runtime_role") != "meshcore_companion" ||
                ReadRequiredBoolean(device, "flashing_allowed") ||
                ReadRequiredBoolean(device, "runtime_identity_authoritative_for_flash"))
                continue;
            var protocol = ReadRequiredInt32(device, "runtime_protocol_version");
            if (protocol is < 3 or > ushort.MaxValue)
                continue;

            var family = ReadRequiredString(device, "runtime_board_family");
            var firmware = ReadRequiredString(device, "runtime_firmware");
            var admitted = (family, firmware) switch
            {
                ("heltec_v4_oled", HeltecFirmware) => new CompanionCandidate(
                    CompanionEndpoint.CreateSessionPrivate(),
                    "Heltec V4 OLED",
                    CompanionDeviceFamily.HeltecV4Companion,
                    connectionReady: false),
                ("seeed_wio_tracker_l1", WioFirmware) => new CompanionCandidate(
                    CompanionEndpoint.CreateSessionPrivate(),
                    "Wio Tracker L1",
                    CompanionDeviceFamily.WioTrackerL1Companion,
                    connectionReady: false),
                _ => null,
            };
            if (admitted is not null) result.Add(admitted);
        }
        return result.AsReadOnly();
    }

    private static void ValidatePrivacy(JsonElement privacy)
    {
        RequireObject(privacy);
        foreach (var name in new[]
        {
            "local_ports_included", "serial_numbers_included",
            "hardware_instance_ids_included", "device_locations_included",
            "raw_responses_included", "pairing_data_included",
            "device_identity_included",
        })
        {
            if (ReadRequiredBoolean(privacy, name))
                throw new InvalidDataException("Runtime evidence contains private fields.");
        }
    }

    private static void RejectPrivateFields(JsonElement value)
    {
        foreach (var property in value.EnumerateObject())
        {
            if (ForbiddenFieldPattern().IsMatch(property.Name))
                throw new InvalidDataException("Runtime evidence contains a forbidden field.");
        }
    }

    private static void RequireObject(JsonElement value)
    {
        if (value.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("Runtime evidence has an invalid object.");
    }

    private static string ReadRequiredString(JsonElement value, string name)
    {
        if (!value.TryGetProperty(name, out var property) ||
            property.ValueKind != JsonValueKind.String)
            throw new InvalidDataException("Runtime evidence is missing required text.");
        var result = property.GetString();
        if (string.IsNullOrWhiteSpace(result) || result.Length > 120 ||
            result.Any(char.IsControl))
            throw new InvalidDataException("Runtime evidence contains invalid text.");
        return result;
    }

    private static bool ReadRequiredBoolean(JsonElement value, string name)
    {
        if (!value.TryGetProperty(name, out var property) ||
            property.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
            throw new InvalidDataException("Runtime evidence is missing a required flag.");
        return property.GetBoolean();
    }

    private static int ReadRequiredInt32(JsonElement value, string name)
    {
        if (!value.TryGetProperty(name, out var property) ||
            !property.TryGetInt32(out var result))
            throw new InvalidDataException("Runtime evidence is missing a required count.");
        return result;
    }

    [GeneratedRegex("^usb_candidate_[1-9][0-9]{0,2}$", RegexOptions.CultureInvariant)]
    private static partial Regex CandidateOrdinalPattern();

    [GeneratedRegex(
        "(?i)^(?:local_port|serial_number|hardware_instance_id|device_location|raw_response|pairing_data|device_identity|public_key|private_key|(?:channel_)?secret|coordinates?|mac|pin|device_path|channel_name)$",
        RegexOptions.CultureInvariant)]
    private static partial Regex ForbiddenFieldPattern();
}
