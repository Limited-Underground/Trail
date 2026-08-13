using System.Globalization;
using System.IO;
using Microsoft.Win32;

namespace OpenTrail.Loader;

/// <summary>
/// Provides a dependency-free, privacy-safe fallback when the development
/// Python probe is not packaged with the loader. It observes only the Windows
/// serial transport inventory and never exposes port names or device identity.
/// </summary>
public static class WindowsSerialInspectionProvider
{
    private const int MaximumCandidates = 64;
    private const string SerialRegistryPath = @"HARDWARE\DEVICEMAP\SERIALCOMM";

    public static LoaderInspectionDocument Inspect(CancellationToken cancellationToken)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException(
                "Built-in serial inspection requires Windows.");
        }

        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            var allowlistedCandidates = WindowsUsbSerialDiscovery.Discover(
                cancellationToken);
            if (allowlistedCandidates.Count != 0)
            {
                return CreateDocument(
                    allowlistedCandidates,
                    MeshCoreRuntimeProbe.Inspect,
                    cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch
        {
            // Some Windows installations deny Ports-class inventory. Fall back
            // to the narrower serial map without exposing the local failure.
        }

        return CreateDocument(ReadPrivateUsbSerialMap(cancellationToken));
    }

    private static IReadOnlyList<string?> ReadPrivateUsbSerialMap(
        CancellationToken cancellationToken)
    {
        using var serialMap = Registry.LocalMachine.OpenSubKey(
            SerialRegistryPath, writable: false);
        if (serialMap is null)
        {
            return [];
        }

        var privatePortNames = new List<string?>();
        foreach (var valueName in serialMap.GetValueNames())
        {
            cancellationToken.ThrowIfCancellationRequested();
            // The serial map can also contain Bluetooth and legacy ports. Only
            // retain entries Windows privately labels as USB-backed. Neither
            // the label nor the associated COM name leaves this provider.
            if (!valueName.Contains("USB", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            privatePortNames.Add(serialMap.GetValue(valueName)?.ToString());
        }

        return privatePortNames;
    }

    public static LoaderInspectionDocument CreateDocument(
        IEnumerable<string?> privatePortNames)
    {
        ArgumentNullException.ThrowIfNull(privatePortNames);

        var uniquePortNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var privatePortName in privatePortNames)
        {
            if (!TryNormalizePortName(privatePortName, out var normalizedPortName))
            {
                continue;
            }

            uniquePortNames.Add(normalizedPortName);
            if (uniquePortNames.Count > MaximumCandidates)
            {
                throw new InvalidDataException(
                    "Windows reported too many serial candidates for bounded inspection.");
            }
        }

        var candidates = uniquePortNames
            .Select(static portName => new WindowsUsbSerialCandidate(
                portName,
                MeshCoreUsbRuntimeFamily.Unknown))
            .ToArray();
        return CreateDocument(
            candidates,
            static (_, _) => MeshCoreRuntimeObservation.Unavailable,
            CancellationToken.None);
    }

    internal static LoaderInspectionDocument CreateDocument(
        IEnumerable<WindowsUsbSerialCandidate> privateCandidates,
        Func<WindowsUsbSerialCandidate, CancellationToken, MeshCoreRuntimeObservation>
            runtimeProbe,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(privateCandidates);
        ArgumentNullException.ThrowIfNull(runtimeProbe);

        var candidatesByPort = new Dictionary<string, WindowsUsbSerialCandidate>(
            StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in privateCandidates)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!TryNormalizePortName(
                    candidate.PrivatePortName,
                    out var normalizedPortName))
            {
                continue;
            }

            candidatesByPort.TryAdd(
                normalizedPortName,
                candidate with { PrivatePortName = normalizedPortName });
            if (candidatesByPort.Count > MaximumCandidates)
            {
                throw new InvalidDataException(
                    "Windows reported too many serial candidates for bounded inspection.");
            }
        }

        var devices = new List<LoaderDeviceCard>(candidatesByPort.Count);
        foreach (var candidate in candidatesByPort.Values
            .OrderBy(static item => item.RuntimeFamily)
            .ThenBy(static item => item.PrivatePortName, StringComparer.OrdinalIgnoreCase))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var observation = runtimeProbe(candidate, cancellationToken);
            var runtimeIdentified = observation.Succeeded &&
                !string.IsNullOrWhiteSpace(observation.DisplayName) &&
                !string.IsNullOrWhiteSpace(observation.InstalledRuntime) &&
                !string.IsNullOrWhiteSpace(observation.Firmware);
            var hardwareProfile = LoaderHardwareProfileEvidenceResolver.Resolve(
                candidate.RuntimeFamily,
                runtimeIdentified);
            var ordinal = devices.Count + 1;
            devices.Add(new LoaderDeviceCard
            {
                PrivateDiagnosticCategory = runtimeIdentified
                    ? null
                    : observation.FailureCategory,
                Candidate = $"usb_candidate_{ordinal}",
                DisplayName = runtimeIdentified
                    ? observation.DisplayName!
                    : "USB-connected device",
                InstalledRuntime = runtimeIdentified
                    ? observation.InstalledRuntime!
                    : "Runtime not identified",
                Firmware = runtimeIdentified ? observation.Firmware : null,
                Connection = "USB",
                InspectionStatus = runtimeIdentified
                    ? "Runtime identified; maintenance profile pending"
                    : "USB transport detected; runtime query unavailable",
                HardwareProfile = hardwareProfile,
                FlashStatus = "Blocked",
                Blockers = runtimeIdentified
                    ?
                    [
                        "Installed runtime is not authoritative for the received hardware profile",
                        "Low-level processor and memory probe required",
                        "Exact hardware profile required",
                        "Product target role unresolved",
                        "Board revision unresolved",
                        "Bootloader schema unresolved",
                    ]
                    :
                    [
                        "Recognized MeshCore runtime evidence required",
                        "Low-level processor and memory probe required",
                        "Exact hardware profile required",
                        "Product target role unresolved",
                        "Board revision unresolved",
                        "Bootloader schema unresolved",
                    ],
                Actions = new LoaderDeviceActions
                {
                    Inspect = true,
                    Flash = false,
                },
            });
        }

        var candidateCount = devices.Count;
        var runtimeIdentifiedCount = devices.Count(static device =>
            device.Firmware is not null);

        var noun = candidateCount == 1 ? "candidate" : "candidates";
        var document = new LoaderInspectionDocument
        {
            Schema = "ot_loader_inspection_view_v0",
            Screen = new LoaderScreen
            {
                Title = ProductIdentity.Current.UtilityRoleName,
                Eyebrow = "Connected devices",
                Phase = "Built-in read-only runtime inspection",
                Summary = $"{candidateCount} USB {noun} found · " +
                    $"{runtimeIdentifiedCount} runtime-identified · 0 ready to flash",
                Notice =
                    "An installed MeshCore runtime name is not an exact supported-board claim. " +
                    "Port names and device identity remain private, and Flash stays disabled.",
            },
            GlobalActions = new LoaderGlobalActions
            {
                Refresh = new LoaderAction { Enabled = true },
                SelectFirmware = new LoaderAction
                {
                    Enabled = false,
                    Reason = "No approved signed firmware-bundle workflow is connected",
                },
                Flash = new LoaderAction
                {
                    Enabled = false,
                    Reason = "No connected candidate has final write admission",
                },
                CleanInstall = new LoaderAction { Enabled = false },
                Recovery = new LoaderAction { Enabled = false },
            },
            CandidateCount = candidateCount,
            InspectedCount = candidateCount,
            ReadyToFlashCount = 0,
            Privacy = new LoaderPrivacy(),
            Devices = devices.ToArray(),
        };

        document.Validate();
        return document;
    }

    internal static bool TryNormalizePortName(
        string? privatePortName,
        out string normalizedPortName)
    {
        normalizedPortName = string.Empty;
        if (string.IsNullOrWhiteSpace(privatePortName))
        {
            return false;
        }

        var candidate = privatePortName.Trim();
        if (!candidate.StartsWith("COM", StringComparison.OrdinalIgnoreCase) ||
            candidate.Length is < 4 or > 7 ||
            !int.TryParse(
                candidate.AsSpan(3),
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var ordinal) ||
            ordinal is < 1 or > 4096)
        {
            return false;
        }

        normalizedPortName = $"COM{ordinal}";
        return true;
    }
}
