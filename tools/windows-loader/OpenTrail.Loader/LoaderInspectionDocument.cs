using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenTrail.Loader;

public sealed class LoaderInspectionDocument
{
    [JsonPropertyName("schema")]
    public string Schema { get; init; } = string.Empty;

    [JsonPropertyName("screen")]
    public LoaderScreen Screen { get; init; } = new();

    [JsonPropertyName("global_actions")]
    public LoaderGlobalActions GlobalActions { get; init; } = new();

    [JsonPropertyName("candidate_count")]
    public int CandidateCount { get; init; }

    [JsonPropertyName("inspected_count")]
    public int InspectedCount { get; init; }

    [JsonPropertyName("ready_to_flash_count")]
    public int ReadyToFlashCount { get; init; }

    [JsonPropertyName("privacy")]
    public LoaderPrivacy Privacy { get; init; } = new();

    [JsonPropertyName("devices")]
    public IReadOnlyList<LoaderDeviceCard> Devices { get; init; } = [];

    public static LoaderInspectionDocument Parse(string json)
    {
        LoaderInspectionDocument? document;
        try
        {
            document = JsonSerializer.Deserialize<LoaderInspectionDocument>(json);
        }
        catch (JsonException error)
        {
            throw new InvalidDataException("Loader inspection output is invalid.", error);
        }

        if (document is null)
        {
            throw new InvalidDataException("Loader inspection output is empty.");
        }

        document.Validate();
        return document;
    }

    public void Validate()
    {
        if (Schema != "ot_loader_inspection_view_v0" ||
            CandidateCount < 0 || CandidateCount > 64 ||
            InspectedCount < 0 || InspectedCount > CandidateCount ||
            ReadyToFlashCount != 0 || Devices.Count != CandidateCount)
        {
            throw new InvalidDataException("Loader inspection document failed validation.");
        }

        if (string.IsNullOrWhiteSpace(Screen.Title) ||
            string.IsNullOrWhiteSpace(Screen.Phase) ||
            string.IsNullOrWhiteSpace(Screen.Summary) ||
            string.IsNullOrWhiteSpace(Screen.Notice))
        {
            throw new InvalidDataException("Loader screen copy is incomplete.");
        }

        if (!GlobalActions.Refresh.Enabled ||
            GlobalActions.SelectFirmware.Enabled ||
            GlobalActions.Flash.Enabled ||
            GlobalActions.CleanInstall.Enabled ||
            GlobalActions.Recovery.Enabled)
        {
            throw new InvalidDataException("Loader action authority is invalid.");
        }

        if (Privacy.LocalPortsIncluded || Privacy.SerialNumbersIncluded ||
            Privacy.HardwareInstanceIdsIncluded || Privacy.DeviceLocationsIncluded ||
            Privacy.RawResponsesIncluded || Privacy.PairingDataIncluded ||
            Privacy.DeviceIdentityIncluded)
        {
            throw new InvalidDataException("Loader inspection document is not privacy safe.");
        }

        foreach (var device in Devices)
        {
            if (string.IsNullOrWhiteSpace(device.Candidate) ||
                string.IsNullOrWhiteSpace(device.DisplayName) ||
                string.IsNullOrWhiteSpace(device.InstalledRuntime) ||
                string.IsNullOrWhiteSpace(device.Connection) ||
                string.IsNullOrWhiteSpace(device.InspectionStatus) ||
                !IsSafeHardwareProfile(device.HardwareProfile) ||
                device.FlashStatus != "Blocked" ||
                !device.Actions.Inspect || device.Actions.Flash ||
                device.Blockers.Count == 0 || device.Blockers.Count > 8 ||
                device.Blockers.Any(static blocker =>
                    string.IsNullOrWhiteSpace(blocker) ||
                    blocker.Length > 240 ||
                    blocker.Any(char.IsControl)))
            {
                throw new InvalidDataException("Loader device card failed validation.");
            }
        }
    }

    private static bool IsSafeHardwareProfile(
        LoaderHardwareProfileEvidence profile)
    {
        var values = new[]
        {
            profile.ProfileCandidate,
            profile.EvidenceLevel,
            profile.ObservedNow,
            profile.PublishedBaseline,
            profile.NextStep,
            profile.MaintenanceCaution,
        };
        return !profile.AuthoritativeForFlash &&
            profile.MaintenanceAttemptLimit is >= 0 and <= LoaderMaintenanceProbePolicy.MaximumAttemptsPerSession &&
            profile.RuntimeRecoveryRequiredBeforeRetry &&
            (profile.MaintenanceRestartRequired
                ? profile.MaintenanceAttemptLimit == LoaderMaintenanceProbePolicy.MaximumAttemptsPerSession
                : profile.MaintenanceAttemptLimit == 0) &&
            values.All(static value =>
                !string.IsNullOrWhiteSpace(value) &&
                value.Length <= 320 &&
                !value.Any(char.IsControl));
    }
}

public sealed class LoaderScreen
{
    [JsonPropertyName("title")]
    public string Title { get; init; } = string.Empty;

    [JsonPropertyName("eyebrow")]
    public string Eyebrow { get; init; } = string.Empty;

    [JsonPropertyName("phase")]
    public string Phase { get; init; } = string.Empty;

    [JsonPropertyName("summary")]
    public string Summary { get; init; } = string.Empty;

    [JsonPropertyName("notice")]
    public string Notice { get; init; } = string.Empty;
}

public sealed class LoaderGlobalActions
{
    [JsonPropertyName("refresh")]
    public LoaderAction Refresh { get; init; } = new();

    [JsonPropertyName("select_firmware")]
    public LoaderAction SelectFirmware { get; init; } = new();

    [JsonPropertyName("flash")]
    public LoaderAction Flash { get; init; } = new();

    [JsonPropertyName("clean_install")]
    public LoaderAction CleanInstall { get; init; } = new();

    [JsonPropertyName("recovery")]
    public LoaderAction Recovery { get; init; } = new();
}

public sealed class LoaderAction
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; init; }

    [JsonPropertyName("reason")]
    public string? Reason { get; init; }
}

public sealed class LoaderPrivacy
{
    [JsonPropertyName("local_ports_included")]
    public bool LocalPortsIncluded { get; init; }

    [JsonPropertyName("serial_numbers_included")]
    public bool SerialNumbersIncluded { get; init; }

    [JsonPropertyName("hardware_instance_ids_included")]
    public bool HardwareInstanceIdsIncluded { get; init; }

    [JsonPropertyName("device_locations_included")]
    public bool DeviceLocationsIncluded { get; init; }

    [JsonPropertyName("raw_responses_included")]
    public bool RawResponsesIncluded { get; init; }

    [JsonPropertyName("pairing_data_included")]
    public bool PairingDataIncluded { get; init; }

    [JsonPropertyName("device_identity_included")]
    public bool DeviceIdentityIncluded { get; init; }
}

public sealed class LoaderDeviceCard
{
    [JsonIgnore]
    internal string? PrivateDiagnosticCategory { get; init; }

    [JsonIgnore]
    internal LoaderAuthoritativeDeviceProfile? AuthoritativeProfile { get; init; }

    [JsonPropertyName("candidate")]
    public string Candidate { get; init; } = string.Empty;

    [JsonPropertyName("display_name")]
    public string DisplayName { get; init; } = string.Empty;

    [JsonPropertyName("installed_runtime")]
    public string InstalledRuntime { get; init; } = string.Empty;

    [JsonPropertyName("firmware")]
    public string? Firmware { get; init; }

    [JsonPropertyName("connection")]
    public string Connection { get; init; } = string.Empty;

    [JsonPropertyName("inspection_status")]
    public string InspectionStatus { get; init; } = string.Empty;

    [JsonPropertyName("hardware_profile")]
    public LoaderHardwareProfileEvidence HardwareProfile { get; init; } = new();

    [JsonPropertyName("flash_status")]
    public string FlashStatus { get; init; } = string.Empty;

    [JsonPropertyName("blockers")]
    public IReadOnlyList<string> Blockers { get; init; } = [];

    [JsonPropertyName("actions")]
    public LoaderDeviceActions Actions { get; init; } = new();

    public string FirmwareDisplay => Firmware ?? "Not available";

    public string AccessibleSummary =>
        $"{DisplayName}. {InstalledRuntime}. {Connection}. {InspectionStatus}. " +
        $"Firmware {FirmwareDisplay}. Hardware profile {HardwareProfile.EvidenceLevel}. " +
        "Flash blocked.";

    public string FlashHelpText =>
        $"Flash unavailable. {string.Join(" ", Blockers)}";
}

public sealed class LoaderDeviceActions
{
    [JsonPropertyName("inspect")]
    public bool Inspect { get; init; }

    [JsonPropertyName("flash")]
    public bool Flash { get; init; }
}
