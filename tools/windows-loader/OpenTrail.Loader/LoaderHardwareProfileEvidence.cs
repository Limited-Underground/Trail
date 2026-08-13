using System.Text.Json.Serialization;

namespace OpenTrail.Loader;

public sealed class LoaderHardwareProfileEvidence
{
    [JsonPropertyName("profile_candidate")]
    public string ProfileCandidate { get; init; } = string.Empty;

    [JsonPropertyName("evidence_level")]
    public string EvidenceLevel { get; init; } = string.Empty;

    [JsonPropertyName("observed_now")]
    public string ObservedNow { get; init; } = string.Empty;

    [JsonPropertyName("published_baseline")]
    public string PublishedBaseline { get; init; } = string.Empty;

    [JsonPropertyName("next_step")]
    public string NextStep { get; init; } = string.Empty;

    [JsonPropertyName("maintenance_restart_required")]
    public bool MaintenanceRestartRequired { get; init; }

    [JsonPropertyName("authoritative_for_flash")]
    public bool AuthoritativeForFlash { get; init; }

    public string RestartDisplay => MaintenanceRestartRequired
        ? "MAINTENANCE RESTART REQUIRED"
        : "RUNTIME IDENTIFICATION REQUIRED FIRST";
}

internal static class LoaderHardwareProfileEvidenceResolver
{
    internal static LoaderHardwareProfileEvidence Resolve(
        MeshCoreUsbRuntimeFamily runtimeFamily,
        bool runtimeIdentified)
    {
        if (!runtimeIdentified)
        {
            return UnknownUsbDevice();
        }

        return runtimeFamily switch
        {
            MeshCoreUsbRuntimeFamily.HeltecV4Companion =>
                new LoaderHardwareProfileEvidence
                {
                    ProfileCandidate = "Heltec WiFi LoRa 32 V4 family",
                    EvidenceLevel = "Runtime candidate only",
                    ObservedNow =
                        "Recognized Espressif USB family and allowlisted Heltec V4 OLED companion response.",
                    PublishedBaseline =
                        "Heltec's V4 family documentation lists ESP32-S3R2, 16 MB external flash, SX1262, and a 0.96-inch OLED.",
                    NextStep =
                        "Use a deliberate maintenance restart for processor, memory, and bootloader evidence; confirm the received revision separately.",
                    MaintenanceRestartRequired = true,
                    AuthoritativeForFlash = false,
                },
            MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater =>
                new LoaderHardwareProfileEvidence
                {
                    ProfileCandidate = "SenseCAP Solar Node family",
                    EvidenceLevel = "Runtime candidate only",
                    ObservedNow =
                        "Recognized Seeed USB family and allowlisted SenseCAP Solar repeater response.",
                    PublishedBaseline =
                        "Seeed's Solar Node documentation lists XIAO nRF52840 Plus and Wio-SX1262; P1-Pro adds L76K GNSS.",
                    NextStep =
                        "Use a deliberate DFU or bootloader session for low-level evidence; confirm the received P1/P1-Pro revision separately.",
                    MaintenanceRestartRequired = true,
                    AuthoritativeForFlash = false,
                },
            _ => UnknownUsbDevice(),
        };
    }

    private static LoaderHardwareProfileEvidence UnknownUsbDevice() =>
        new()
        {
            ProfileCandidate = "No supported profile candidate",
            EvidenceLevel = "USB transport only",
            ObservedNow = "A bounded USB serial candidate is present; its runtime was not identified.",
            PublishedBaseline = "No vendor hardware baseline is applied to an unidentified device.",
            NextStep =
                "Identify an allowlisted MeshCore runtime before offering any maintenance profiling step.",
            MaintenanceRestartRequired = false,
            AuthoritativeForFlash = false,
        };
}
