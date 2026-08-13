using System.IO;

namespace OpenTrail.Loader;

internal sealed record LoaderAuthoritativeDeviceProfile(
    uint HardwareProfileId,
    string Processor,
    string TargetRole,
    ushort BoardRevision,
    ushort BootloaderSchema,
    uint MaximumImageBytes);

internal sealed record LoaderDeviceBundleMatchResult(
    bool AuthoritativeDeviceProfile,
    bool HardwareProfileMatched,
    bool ProcessorMatched,
    bool TargetRoleMatched,
    bool BoardRevisionMatched,
    bool BootloaderSchemaMatched,
    bool ImageSizeMatched,
    bool ExactDeviceMatch,
    string Summary,
    string BlockerText);

internal static class LoaderDeviceBundleMatchAuthority
{
    internal static LoaderDeviceBundleMatchResult Evaluate(
        LoaderDeviceCard device,
        FirmwareBundleCandidateResult bundle)
    {
        ArgumentNullException.ThrowIfNull(device);
        ArgumentNullException.ThrowIfNull(bundle);

        if (device.AuthoritativeProfile is null)
        {
            return Blocked(
                "Exact-device match unavailable",
                "BLOCKED: The selected device has runtime candidate evidence only; an authoritative received-unit profile is required.");
        }

        var profile = device.AuthoritativeProfile;
        Validate(profile);

        var hardwareProfileMatched =
            profile.HardwareProfileId == bundle.HardwareProfileId;
        var processorMatched =
            string.Equals(profile.Processor, bundle.Processor, StringComparison.Ordinal);
        var targetRoleMatched =
            string.Equals(profile.TargetRole, bundle.TargetRole, StringComparison.Ordinal);
        var boardRevisionMatched =
            profile.BoardRevision >= bundle.MinimumBoardRevision &&
            profile.BoardRevision <= bundle.MaximumBoardRevision;
        var bootloaderSchemaMatched =
            profile.BootloaderSchema >= bundle.MinimumBootloaderSchema;
        var imageSizeMatched =
            bundle.ImageBytes > 0 && bundle.ImageBytes <= profile.MaximumImageBytes;
        var exactDeviceMatch =
            hardwareProfileMatched &&
            processorMatched &&
            targetRoleMatched &&
            boardRevisionMatched &&
            bootloaderSchemaMatched &&
            imageSizeMatched;

        return new LoaderDeviceBundleMatchResult(
            AuthoritativeDeviceProfile: true,
            HardwareProfileMatched: hardwareProfileMatched,
            ProcessorMatched: processorMatched,
            TargetRoleMatched: targetRoleMatched,
            BoardRevisionMatched: boardRevisionMatched,
            BootloaderSchemaMatched: bootloaderSchemaMatched,
            ImageSizeMatched: imageSizeMatched,
            ExactDeviceMatch: exactDeviceMatch,
            Summary: exactDeviceMatch
                ? "Exact selected-device manifest match verified"
                : "Selected device does not match this firmware bundle",
            BlockerText: exactDeviceMatch
                ? "BLOCKED: Exact-device match is not release admission or Flash permission."
                : "BLOCKED: One or more authoritative device fields do not match the signed manifest.");
    }

    private static LoaderDeviceBundleMatchResult Blocked(
        string summary,
        string blockerText) =>
        new(
            AuthoritativeDeviceProfile: false,
            HardwareProfileMatched: false,
            ProcessorMatched: false,
            TargetRoleMatched: false,
            BoardRevisionMatched: false,
            BootloaderSchemaMatched: false,
            ImageSizeMatched: false,
            ExactDeviceMatch: false,
            Summary: summary,
            BlockerText: blockerText);

    private static void Validate(LoaderAuthoritativeDeviceProfile profile)
    {
        if (profile.HardwareProfileId == 0 ||
            profile.BoardRevision == 0 ||
            profile.MaximumImageBytes == 0 ||
            profile.MaximumImageBytes > FirmwareBundleCandidateInspector.MaximumImageBytes ||
            profile.Processor is not ("esp32_s3" or "nrf52840") ||
            profile.TargetRole is not ("bench_client" or "complete_client" or "packaged_repeater"))
        {
            throw new InvalidDataException("Authoritative device profile is invalid.");
        }
    }
}
