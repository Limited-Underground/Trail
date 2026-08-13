using OpenTrail.Loader;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using System.Windows;
using System.Windows.Media;

var failures = 0;

void Expect(bool condition, string message)
{
    if (!condition)
    {
        Console.Error.WriteLine($"FAIL: {message}");
        failures++;
    }
}

var applicationManifest = XDocument.Load(
    Path.Combine(AppContext.BaseDirectory, "app.manifest"));
var dpiAwarenessValues = applicationManifest
    .Descendants()
    .Where(static element => element.Name.LocalName is "dpiAware" or "dpiAwareness")
    .ToDictionary(
        static element => element.Name.LocalName,
        static element => element.Value,
        StringComparer.Ordinal);
Expect(
    dpiAwarenessValues.Count == 2 &&
    string.Equals(dpiAwarenessValues["dpiAware"], "true/pm", StringComparison.Ordinal) &&
    string.Equals(dpiAwarenessValues["dpiAwareness"], "PerMonitorV2", StringComparison.Ordinal),
    "the production application manifest must declare the legacy fallback and PerMonitorV2 DPI awareness");

try
{
    LoaderThemeResources.Apply(
        new ResourceDictionary(),
        new LoaderThemePalette(
            "incomplete",
            new Dictionary<string, Brush>(StringComparer.Ordinal)));
    Expect(false, "an incomplete production theme palette must fail closed");
}
catch (ArgumentException)
{
}

void ExpectIdentityRejected(
    string parentName,
    string familyName,
    string utilityRoleName,
    string message)
{
    try
    {
        _ = ProductIdentity.CreateWorking(
            parentName,
            familyName,
            utilityRoleName);
        Expect(false, message);
    }
    catch (ArgumentException)
    {
    }
}

byte[] CompanionDeviceInfo(
    string model = "Heltec V4 OLED",
    string firmware = "v1.16.0-07a3ca9",
    string buildDate = "08-Aug-2026")
{
    var response = new byte[80];
    response[0] = 0x0D;
    response[1] = 10;
    response[2] = 64;
    response[3] = 8;
    // Deliberately nonzero private PIN bytes. The parser must skip them.
    response[4] = 0x11;
    response[5] = 0x22;
    response[6] = 0x33;
    response[7] = 0x44;
    System.Text.Encoding.ASCII.GetBytes(buildDate).CopyTo(response, 8);
    System.Text.Encoding.ASCII.GetBytes(model).CopyTo(response, 20);
    System.Text.Encoding.ASCII.GetBytes(firmware).CopyTo(response, 60);
    return response;
}

string CreateCandidateBundle(
    string root,
    string fileName,
    RSA signer,
    string processor = "esp32_s3",
    string targetRole = "complete_client",
    bool nonCanonicalManifest = false,
    bool mismatchedImageDigest = false,
    bool zeroSignature = false,
    bool tamperedSignature = false,
    bool tamperedManifestAfterSigning = false,
    bool addUnexpectedEntry = false,
    uint? declaredImageBytes = null)
{
    var image = Enumerable.Range(0, 1024)
        .Select(static index => (byte)(index % 251))
        .ToArray();
    var digest = SHA256.HashData(image);
    if (mismatchedImageDigest)
    {
        digest[0] ^= 0xFF;
    }

    var signerId = FirmwareSignerTrustCatalog.DeriveSignerId(
        signer.ExportSubjectPublicKeyInfo());
    ushort canonicalBytes = 0;
    byte[] manifest = [];
    for (var attempt = 0; attempt < 8; attempt++)
    {
        manifest = FirmwareBundleCandidateInspector.SerializeCanonicalManifest(
            canonicalBytes,
            hardwareProfileId: 42,
            processor,
            targetRole,
            minimumBoardRevision: 1,
            maximumBoardRevision: 2,
            minimumBootloaderSchema: 1,
            releaseGeneration: 7,
            imageBytes: declaredImageBytes ?? (uint)image.Length,
            imageSha256: Convert.ToHexString(digest).ToLowerInvariant(),
            signerId);
        if (manifest.Length == canonicalBytes)
        {
            break;
        }
        canonicalBytes = checked((ushort)manifest.Length);
    }
    if (manifest.Length != canonicalBytes)
    {
        throw new InvalidOperationException("Could not stabilize the canonical test manifest length.");
    }
    if (nonCanonicalManifest)
    {
        manifest = [.. manifest, (byte)'\n'];
    }

    var signature = signer.SignData(
        manifest,
        HashAlgorithmName.SHA256,
        RSASignaturePadding.Pss);
    if (tamperedManifestAfterSigning)
    {
        manifest = FirmwareBundleCandidateInspector.SerializeCanonicalManifest(
            canonicalBytes,
            hardwareProfileId: 43,
            processor,
            targetRole,
            minimumBoardRevision: 1,
            maximumBoardRevision: 2,
            minimumBootloaderSchema: 1,
            releaseGeneration: 7,
            imageBytes: declaredImageBytes ?? (uint)image.Length,
            imageSha256: Convert.ToHexString(digest).ToLowerInvariant(),
            signerId);
    }
    if (zeroSignature)
    {
        signature = new byte[FirmwareBundleCandidateInspector.RsaPss3072SignatureBytes];
    }
    else if (tamperedSignature)
    {
        signature[signature.Length / 2] ^= 0x01;
    }

    var path = Path.Combine(root, fileName);
    using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
    using var archive = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: false);

    void WriteEntry(string name, byte[] content)
    {
        var entry = archive.CreateEntry(name, CompressionLevel.Optimal);
        using var stream = entry.Open();
        stream.Write(content);
    }

    WriteEntry("manifest.json", manifest);
    WriteEntry("image.bin", image);
    WriteEntry("manifest.sig", signature);
    if (addUnexpectedEntry)
    {
        WriteEntry("notes.txt", [0x01]);
    }
    return path;
}

string CreateCandidateBundleFromVector(
    string root,
    byte[] manifest,
    byte[] image,
    byte[] signature)
{
    var path = Path.Combine(root, "fixed-public-vector.fwbundle");
    using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
    using var archive = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: false);

    void WriteEntry(string name, byte[] content)
    {
        var entry = archive.CreateEntry(name, CompressionLevel.Optimal);
        using var stream = entry.Open();
        stream.Write(content);
    }

    WriteEntry("manifest.json", manifest);
    WriteEntry("image.bin", image);
    WriteEntry("manifest.sig", signature);
    return path;
}

string ValidDocument(bool flashEnabled = false, string extra = "") => $$"""
{
  "schema": "ot_loader_inspection_view_v0",
  "screen": {
    "title": "Device Utility",
    "eyebrow": "Connected devices",
    "phase": "Inspection only",
    "summary": "1 found · 1 inspected · 0 ready to flash",
    "notice": "Inspection is not flash permission."
  },
  "global_actions": {
    "refresh": { "enabled": true },
    "select_firmware": { "enabled": false },
    "flash": { "enabled": {{flashEnabled.ToString().ToLowerInvariant()}} },
    "clean_install": { "enabled": false },
    "recovery": { "enabled": false }
  },
  "candidate_count": 1,
  "inspected_count": 1,
  "ready_to_flash_count": 0,
  "privacy": {
    "local_ports_included": false,
    "serial_numbers_included": false,
    "hardware_instance_ids_included": false,
    "device_locations_included": false,
    "raw_responses_included": false,
    "pairing_data_included": false,
    "device_identity_included": false
  },
  "devices": [{
    "candidate": "usb_candidate_1",
    "display_name": "Heltec V4 OLED",
    "installed_runtime": "MeshCore companion",
    "firmware": "v1.16.0-test",
    "connection": "USB",
    "inspection_status": "Connected and inspected",
    "hardware_profile": {
      "profile_candidate": "Heltec WiFi LoRa 32 V4 family",
      "evidence_level": "Runtime candidate only",
      "observed_now": "Recognized runtime evidence.",
      "published_baseline": "Published vendor-family baseline only.",
      "next_step": "Use a deliberate maintenance restart for low-level evidence.",
      "maintenance_restart_required": true,
      "maintenance_attempt_limit": 1,
      "runtime_recovery_required_before_retry": true,
      "maintenance_caution": "ONE ATTEMPT PER SESSION. Stop after failure and verify normal runtime recovery before any later attempt.",
      "authoritative_for_flash": false
    },
    "flash_status": "Blocked",
    "blockers": ["Exact hardware profile required"],
    "actions": { "inspect": true, "flash": false },
    "private_extra": "{{extra}}"
  }]
}
""";

var valid = LoaderInspectionDocument.Parse(ValidDocument(extra: "not-retained"));
Expect(valid.CandidateCount == 1, "valid candidate count");
Expect(valid.Devices[0].DisplayName == "Heltec V4 OLED", "valid board label");
Expect(valid.Devices[0].FirmwareDisplay == "v1.16.0-test", "firmware display");
Expect(!valid.GlobalActions.Flash.Enabled, "Flash remains disabled");
Expect(valid.Devices[0].AccessibleSummary ==
    "Heltec V4 OLED. MeshCore companion. USB. Connected and inspected. Firmware v1.16.0-test. Hardware profile Runtime candidate only. Flash blocked.",
    "accessible device summary uses validated public fields");
Expect(!valid.Devices[0].AccessibleSummary.Contains("usb_candidate_1", StringComparison.Ordinal),
    "accessible device summary omits the internal candidate reference");
Expect(valid.Devices[0].FlashHelpText ==
    "Flash unavailable. Exact hardware profile required",
    "disabled Flash help explains the validated blocker");

var currentIdentity = ProductIdentity.Current;
Expect(currentIdentity.FamilyDisplayName == "Limited Underground Trail" &&
    currentIdentity.UtilityRoleName == "Device Utility" &&
    currentIdentity.WindowTitle == "Limited Underground Trail Device Utility",
    "working public identity is composed from one replaceable boundary");
Expect(!currentIdentity.WindowTitle.Contains("OpenTrail", StringComparison.OrdinalIgnoreCase) &&
    !currentIdentity.WindowTitle.Contains('®') &&
    currentIdentity.ReviewStatus.Contains(
        "ATTORNEY REVIEW PENDING",
        StringComparison.Ordinal),
    "working public identity omits the engineering name and registration claim");

foreach (var bannedOffering in new[] { "LU Link", "LU Studio" })
{
    ExpectIdentityRejected(
        "Limited Underground",
        "Trail",
        bannedOffering,
        $"standalone LU and banned {bannedOffering} structure must be rejected");
}
ExpectIdentityRejected(
    "Limited Underground",
    "LU300",
    "Device Utility",
    "LU plus model-number structure must be rejected");
foreach (var retiredName in new[] { "TLU", "LUT", "LUTrail" })
{
    ExpectIdentityRejected(
        "Limited Underground",
        retiredName,
        "Device Utility",
        $"retired compact name {retiredName} must be rejected");
}
ExpectIdentityRejected(
    "Limited Underground®",
    "Trail",
    "Device Utility",
    "registered symbol must be rejected while attorney review is pending");

Expect(WindowsUsbSerialDiscovery.ClassifyHardwareIds(
        "USB\\VID_303A&PID_0002&REV_0101") ==
        MeshCoreUsbRuntimeFamily.HeltecV4Companion &&
    WindowsUsbSerialDiscovery.ClassifyHardwareIds(
        "USB\\VID_2886&PID_0059") ==
        MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater &&
    WindowsUsbSerialDiscovery.ClassifyHardwareIds(
        "USB\\VID_303A&PID_0003") == MeshCoreUsbRuntimeFamily.Unknown,
    "USB runtime-family discovery uses exact allowlisted VID/PID pairs");

var heltecHardwareProfile = LoaderHardwareProfileEvidenceResolver.Resolve(
    MeshCoreUsbRuntimeFamily.HeltecV4Companion,
    runtimeIdentified: true);
var senseCapHardwareProfile = LoaderHardwareProfileEvidenceResolver.Resolve(
    MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater,
    runtimeIdentified: true);
var unknownHardwareProfile = LoaderHardwareProfileEvidenceResolver.Resolve(
    MeshCoreUsbRuntimeFamily.Unknown,
    runtimeIdentified: false);
Expect(
    heltecHardwareProfile.ProfileCandidate == "Heltec WiFi LoRa 32 V4 family" &&
    heltecHardwareProfile.PublishedBaseline.Contains("ESP32-S3R2", StringComparison.Ordinal) &&
    senseCapHardwareProfile.ProfileCandidate == "SenseCAP Solar Node family" &&
    senseCapHardwareProfile.PublishedBaseline.Contains("nRF52840", StringComparison.Ordinal) &&
    heltecHardwareProfile.MaintenanceRestartRequired &&
    senseCapHardwareProfile.MaintenanceRestartRequired &&
    !unknownHardwareProfile.MaintenanceRestartRequired &&
    heltecHardwareProfile.MaintenanceAttemptLimit == 1 &&
    senseCapHardwareProfile.MaintenanceAttemptLimit == 1 &&
    unknownHardwareProfile.MaintenanceAttemptLimit == 0 &&
    heltecHardwareProfile.RuntimeRecoveryRequiredBeforeRetry &&
    senseCapHardwareProfile.RuntimeRecoveryRequiredBeforeRetry &&
    unknownHardwareProfile.RuntimeRecoveryRequiredBeforeRetry &&
    heltecHardwareProfile.MaintenanceCaution.Contains(
        "ONE ATTEMPT PER SESSION", StringComparison.Ordinal) &&
    !heltecHardwareProfile.AuthoritativeForFlash &&
    !senseCapHardwareProfile.AuthoritativeForFlash &&
    !unknownHardwareProfile.AuthoritativeForFlash,
    "hardware profile hints distinguish runtime evidence, vendor baseline, and flash authority");

var maintenanceAwaitingConfirmation = LoaderMaintenanceProbePolicy.Evaluate(
    maintenanceRequired: true,
    operatorConfirmedDisruption: false,
    attemptsThisSession: 0,
    normalRuntimeRecoveredAfterAttempt: false);
var maintenanceReadyOnce = LoaderMaintenanceProbePolicy.Evaluate(
    maintenanceRequired: true,
    operatorConfirmedDisruption: true,
    attemptsThisSession: 0,
    normalRuntimeRecoveredAfterAttempt: false);
var maintenanceRecoveryRequired = LoaderMaintenanceProbePolicy.Evaluate(
    maintenanceRequired: true,
    operatorConfirmedDisruption: true,
    attemptsThisSession: 1,
    normalRuntimeRecoveredAfterAttempt: false);
var maintenanceConsumed = LoaderMaintenanceProbePolicy.Evaluate(
    maintenanceRequired: true,
    operatorConfirmedDisruption: true,
    attemptsThisSession: 1,
    normalRuntimeRecoveredAfterAttempt: true);
var maintenanceNotOffered = LoaderMaintenanceProbePolicy.Evaluate(
    maintenanceRequired: false,
    operatorConfirmedDisruption: true,
    attemptsThisSession: 0,
    normalRuntimeRecoveredAfterAttempt: false);
Expect(
    maintenanceAwaitingConfirmation.Status == LoaderMaintenanceProbeStatus.AwaitingOperatorConfirmation &&
    !maintenanceAwaitingConfirmation.CanStartProbe &&
    maintenanceReadyOnce.Status == LoaderMaintenanceProbeStatus.ReadyForSingleAttempt &&
    maintenanceReadyOnce.CanStartProbe && maintenanceReadyOnce.RuntimeRecoveryMustBeVerified &&
    maintenanceRecoveryRequired.Status == LoaderMaintenanceProbeStatus.RuntimeRecoveryRequired &&
    !maintenanceRecoveryRequired.CanStartProbe && maintenanceRecoveryRequired.RuntimeRecoveryMustBeVerified &&
    maintenanceConsumed.Status == LoaderMaintenanceProbeStatus.SessionAttemptConsumed &&
    !maintenanceConsumed.CanStartProbe &&
    maintenanceNotOffered.Status == LoaderMaintenanceProbeStatus.NotOffered &&
    !maintenanceNotOffered.CanStartProbe,
    "maintenance probe policy allows one confirmed attempt and blocks retry pending runtime recovery");

var decoder = new MeshCoreSerialFrameDecoder(300);
decoder.Feed([0x00, 0x7F, 0x3E, 0x03]);
decoder.Feed([0x00, 0x0D, 0x01, 0x02, 0x3E, 0x01, 0x00, 0x05]);
Expect(decoder.TryTakeFrame(out var firstFrame) &&
    firstFrame.SequenceEqual(new byte[] { 0x0D, 0x01, 0x02 }) &&
    decoder.TryTakeFrame(out var secondFrame) &&
    secondFrame.SequenceEqual(new byte[] { 0x05 }) &&
    !decoder.TryTakeFrame(out _),
    "MeshCore frame decoder handles junk, fragmentation, and adjacent frames");

var parsedCompanion = MeshCoreRuntimeProbe.ParseCompanionDeviceInfo(
    CompanionDeviceInfo());
Expect(parsedCompanion.Succeeded &&
    parsedCompanion.DisplayName == "Heltec V4 OLED" &&
    parsedCompanion.InstalledRuntime == "MeshCore USB companion" &&
    parsedCompanion.Firmware == "v1.16.0-07a3ca9" &&
    !parsedCompanion.ToString().Contains("11223344", StringComparison.OrdinalIgnoreCase),
    "companion parser retains only allowlisted runtime evidence and skips the private PIN");

try
{
    _ = MeshCoreRuntimeProbe.ParseCompanionDeviceInfo(
        CompanionDeviceInfo(model: "Unrecognized board"));
    Expect(false, "unrecognized companion model must fail closed");
}
catch (InvalidDataException)
{
}

var parsedRepeater = MeshCoreRuntimeProbe.ParseRepeaterResponses(
    new Dictionary<string, string>
    {
        ["board"] = "board\r\n-> Seeed SenseCap Solar\r\n",
        ["ver"] = "ver\r\n-> v1.16.0-07a3ca9 (Build: 08-Aug-2026)\r\n",
        ["get role"] = "get role\r\n-> repeater\r\n",
    });
Expect(parsedRepeater.Succeeded &&
    parsedRepeater.DisplayName == "SenseCAP Solar" &&
    parsedRepeater.InstalledRuntime == "MeshCore repeater" &&
    parsedRepeater.Firmware == "v1.16.0-07a3ca9",
    "repeater parser accepts only the fixed board/version/role response set");

var bundleTestRoot = Path.Combine(
    Path.GetTempPath(),
    $"OpenTrail.Loader.BundleTests.{Guid.NewGuid():N}");
Directory.CreateDirectory(bundleTestRoot);
try
{
    var vectorPath = Path.Combine(
        AppContext.BaseDirectory,
        "fixtures",
        "firmware_bundle_signature_vector_v0.json");
    using var vectorDocument = JsonDocument.Parse(File.ReadAllBytes(vectorPath));
    var vectorRoot = vectorDocument.RootElement;
    var vectorManifest = Convert.FromBase64String(
        vectorRoot.GetProperty("signed_manifest_utf8_base64").GetString() ?? string.Empty);
    var vectorPublicKey = Convert.FromBase64String(
        vectorRoot.GetProperty("public_key_spki_der_base64").GetString() ?? string.Empty);
    var vectorSignature = Convert.FromBase64String(
        vectorRoot.GetProperty("signature_base64").GetString() ?? string.Empty);
    var vectorSignerId = vectorRoot.GetProperty("signer_id").GetString() ?? string.Empty;
    var vectorImage = Enumerable.Range(0, 1024)
        .Select(static index => (byte)(index % 251))
        .ToArray();
    var vectorTrustEntry = FirmwareSignerTrustCatalog.FromPublicKey(vectorPublicKey);
    var vectorTrustCatalog = new FirmwareSignerTrustCatalog([vectorTrustEntry]);
    var vectorBundle = FirmwareBundleCandidateInspector.Inspect(
        CreateCandidateBundleFromVector(
            bundleTestRoot,
            vectorManifest,
            vectorImage,
            vectorSignature),
        vectorTrustCatalog);
    Expect(vectorRoot.GetProperty("schema").GetString() ==
            "ot_firmware_bundle_signature_vector_v0" &&
        vectorRoot.GetProperty("signature_algorithm").GetString() ==
            FirmwareBundleCandidateInspector.SignatureAlgorithm &&
        vectorRoot.GetProperty("pss_salt_bytes").GetInt32() == 32 &&
        vectorSignerId == vectorTrustEntry.SignerId &&
        vectorRoot.GetProperty("manifest_sha256").GetString() ==
            Convert.ToHexString(SHA256.HashData(vectorManifest)).ToLowerInvariant() &&
        vectorSignature.Length == FirmwareBundleCandidateInspector.RsaPss3072SignatureBytes &&
        vectorBundle.StructureVerified &&
        vectorBundle.ImageDigestVerified &&
        vectorBundle.SignerTrusted &&
        vectorBundle.SignatureVerified &&
        !vectorBundle.AdmissionAllowed,
        "the loader verifies the exact fixed public RSA-PSS vector without granting admission");

    using var bundleSigner = RSA.Create(FirmwareSignerTrustCatalog.RequiredRsaBits);
    var trustedSigner = FirmwareSignerTrustCatalog.FromPublicKey(
        bundleSigner.ExportSubjectPublicKeyInfo());
    var trustedSigners = new FirmwareSignerTrustCatalog([trustedSigner]);
    var validBundlePath = CreateCandidateBundle(
        bundleTestRoot, "valid.fwbundle", bundleSigner);
    var candidateBundle = FirmwareBundleCandidateInspector.Inspect(
        validBundlePath, trustedSigners);
    Expect(candidateBundle.StructureVerified &&
        candidateBundle.ImageDigestVerified &&
        candidateBundle.SignaturePresent &&
        candidateBundle.SignerTrusted &&
        candidateBundle.SignatureVerified &&
        !candidateBundle.AdmissionAllowed &&
        candidateBundle.ProcessorDisplay == "ESP32-S3" &&
        candidateBundle.TargetRoleDisplay == "complete client" &&
        candidateBundle.HardwareProfileId == 42 &&
        candidateBundle.Processor == "esp32_s3" &&
        candidateBundle.TargetRole == "complete_client" &&
        candidateBundle.MinimumBoardRevision == 1 &&
        candidateBundle.MaximumBoardRevision == 2 &&
        candidateBundle.MinimumBootloaderSchema == 1 &&
        candidateBundle.ReleaseGeneration == 7 &&
        candidateBundle.ImageBytes == 1024,
        "candidate bundle inspection verifies RSA-PSS signature and image digest without claiming write admission");

    var authoritativeProfile = new LoaderAuthoritativeDeviceProfile(
        HardwareProfileId: 42,
        Processor: "esp32_s3",
        TargetRole: "complete_client",
        BoardRevision: 2,
        BootloaderSchema: 1,
        MaximumImageBytes: 2 * 1024 * 1024);
    LoaderDeviceCard DeviceWith(LoaderAuthoritativeDeviceProfile? profile) =>
        new()
        {
            Candidate = "usb_candidate_1",
            DisplayName = "Test device",
            InstalledRuntime = "Test runtime",
            Connection = "USB",
            InspectionStatus = "Test-only authoritative profile",
            HardwareProfile = new LoaderHardwareProfileEvidence(),
            FlashStatus = "Blocked",
            Blockers = ["Test blocker"],
            Actions = new LoaderDeviceActions { Inspect = true, Flash = false },
            AuthoritativeProfile = profile,
        };

    var exactDeviceMatch = LoaderDeviceBundleMatchAuthority.Evaluate(
        DeviceWith(authoritativeProfile),
        candidateBundle);
    Expect(exactDeviceMatch.AuthoritativeDeviceProfile &&
        exactDeviceMatch.HardwareProfileMatched &&
        exactDeviceMatch.ProcessorMatched &&
        exactDeviceMatch.TargetRoleMatched &&
        exactDeviceMatch.BoardRevisionMatched &&
        exactDeviceMatch.BootloaderSchemaMatched &&
        exactDeviceMatch.ImageSizeMatched &&
        exactDeviceMatch.ExactDeviceMatch &&
        !candidateBundle.AdmissionAllowed,
        "an exact authoritative selected-device match remains separate from release admission");

    var exactMismatchProfiles = new[]
    {
        authoritativeProfile with { HardwareProfileId = 43 },
        authoritativeProfile with { Processor = "nrf52840" },
        authoritativeProfile with { TargetRole = "bench_client" },
        authoritativeProfile with { BoardRevision = 3 },
        authoritativeProfile with { BootloaderSchema = 0 },
        authoritativeProfile with { MaximumImageBytes = 512 },
    };
    var exactMismatchResults = exactMismatchProfiles
        .Select(profile => LoaderDeviceBundleMatchAuthority.Evaluate(
            DeviceWith(profile),
            candidateBundle))
        .ToArray();
    Expect(exactMismatchResults.All(static result => !result.ExactDeviceMatch) &&
        !exactMismatchResults[0].HardwareProfileMatched &&
        !exactMismatchResults[1].ProcessorMatched &&
        !exactMismatchResults[2].TargetRoleMatched &&
        !exactMismatchResults[3].BoardRevisionMatched &&
        !exactMismatchResults[4].BootloaderSchemaMatched &&
        !exactMismatchResults[5].ImageSizeMatched &&
        !LoaderDeviceBundleMatchAuthority.Evaluate(
            DeviceWith(profile: null),
            candidateBundle).AuthoritativeDeviceProfile,
        "runtime-only evidence and every exact authoritative field mismatch fail closed");

    var noConfiguredSigner = FirmwareBundleCandidateInspector.Inspect(validBundlePath);
    Expect(noConfiguredSigner.StructureVerified &&
        noConfiguredSigner.ImageDigestVerified &&
        !noConfiguredSigner.SignerTrusted &&
        !noConfiguredSigner.SignatureVerified &&
        !noConfiguredSigner.AdmissionAllowed,
        "the packaged empty signer catalog preserves structural evidence but grants no signature trust");

    using var otherSigner = RSA.Create(FirmwareSignerTrustCatalog.RequiredRsaBits);
    var otherCatalog = new FirmwareSignerTrustCatalog(
        [FirmwareSignerTrustCatalog.FromPublicKey(otherSigner.ExportSubjectPublicKeyInfo())]);
    var untrustedSignerResult = FirmwareBundleCandidateInspector.Inspect(
        validBundlePath, otherCatalog);
    Expect(!untrustedSignerResult.SignerTrusted &&
        !untrustedSignerResult.SignatureVerified &&
        !untrustedSignerResult.AdmissionAllowed,
        "a valid signature from a signer absent from the pinned catalog remains untrusted");

    var wrongExtensionPath = Path.Combine(bundleTestRoot, "wrong-extension.zip");
    File.Copy(validBundlePath, wrongExtensionPath);
    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(wrongExtensionPath);
        Expect(false, "candidate bundle inspection must reject an unapproved extension");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "extra-entry.fwbundle", bundleSigner, addUnexpectedEntry: true));
        Expect(false, "candidate bundle inspection must reject any unexpected archive entry");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "noncanonical.fwbundle", bundleSigner, nonCanonicalManifest: true));
        Expect(false, "candidate bundle inspection must reject noncanonical manifest bytes");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "digest-mismatch.fwbundle", bundleSigner, mismatchedImageDigest: true));
        Expect(false, "candidate bundle inspection must reject an image digest mismatch");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "empty-signature.fwbundle", bundleSigner, zeroSignature: true));
        Expect(false, "candidate bundle inspection must reject an all-zero signature field");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "length-mismatch.fwbundle", bundleSigner, declaredImageBytes: 1023));
        Expect(false, "candidate bundle inspection must reject a signed image-length mismatch");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "tampered-signature.fwbundle", bundleSigner, tamperedSignature: true),
            trustedSigners);
        Expect(false, "candidate bundle inspection must reject a tampered trusted signature");
    }
    catch (InvalidDataException)
    {
    }

    try
    {
        _ = FirmwareBundleCandidateInspector.Inspect(CreateCandidateBundle(
            bundleTestRoot, "tampered-manifest.fwbundle", bundleSigner,
            tamperedManifestAfterSigning: true), trustedSigners);
        Expect(false, "candidate bundle inspection must reject a canonical manifest changed after signing");
    }
    catch (InvalidDataException)
    {
    }

    using var undersizedSigner = RSA.Create(2048);
    try
    {
        _ = new FirmwareSignerTrustCatalog(
            [FirmwareSignerTrustCatalog.FromPublicKey(
                undersizedSigner.ExportSubjectPublicKeyInfo())]);
        Expect(false, "firmware signer catalog must reject a non-RSA-3072 key");
    }
    catch (ArgumentException)
    {
    }
}
finally
{
    if (Directory.Exists(bundleTestRoot))
    {
        Directory.Delete(bundleTestRoot, recursive: true);
    }
}

var runtimeQualifiedDocument = WindowsSerialInspectionProvider.CreateDocument(
    [
        new WindowsUsbSerialCandidate(
            "COM6", MeshCoreUsbRuntimeFamily.HeltecV4Companion),
        new WindowsUsbSerialCandidate(
            "COM17", MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater),
    ],
    static (candidate, _) => candidate.RuntimeFamily switch
    {
        MeshCoreUsbRuntimeFamily.HeltecV4Companion =>
            new MeshCoreRuntimeObservation(
                true, "Heltec V4 OLED", "MeshCore USB companion", "v1.16.0-07a3ca9"),
        MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater =>
            new MeshCoreRuntimeObservation(
                true, "SenseCAP Solar", "MeshCore repeater", "v1.16.0-07a3ca9"),
        _ => MeshCoreRuntimeObservation.Unavailable,
    },
    CancellationToken.None);
var runtimeQualifiedJson = JsonSerializer.Serialize(runtimeQualifiedDocument);
Expect(runtimeQualifiedDocument.Devices.Count == 2 &&
    runtimeQualifiedDocument.Devices.All(static device =>
        device.InspectionStatus == "Runtime identified; maintenance profile pending" &&
        device.HardwareProfile.EvidenceLevel == "Runtime candidate only" &&
        device.HardwareProfile.MaintenanceRestartRequired &&
        !device.HardwareProfile.AuthoritativeForFlash &&
        device.FlashStatus == "Blocked" && !device.Actions.Flash &&
        device.Blockers.Contains(
            "Installed runtime is not authoritative for the received hardware profile")) &&
    !runtimeQualifiedJson.Contains("COM6", StringComparison.OrdinalIgnoreCase) &&
    !runtimeQualifiedJson.Contains("COM17", StringComparison.OrdinalIgnoreCase),
    "runtime-qualified cards remain privacy-safe and retain exact-hardware blockers");

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument(flashEnabled: true));
    Expect(false, "unexpected global Flash permission must fail");
}
catch (InvalidDataException)
{
}

var livePackagedInspection = WindowsSerialInspectionProvider.Inspect(
    CancellationToken.None);
var livePackagedJson = JsonSerializer.Serialize(livePackagedInspection);
var liveRuntimeSummary = string.Join(
    ", ",
    livePackagedInspection.Devices
        .Where(static device => device.Firmware is not null)
        .GroupBy(static device => device.DisplayName, StringComparer.Ordinal)
        .OrderBy(static group => group.Key, StringComparer.Ordinal)
        .Select(static group => $"{group.Count()} {group.Key}"));
var liveFailureSummary = string.Join(
    ", ",
    livePackagedInspection.Devices
        .Where(static device => device.Firmware is null)
        .GroupBy(
            static device => device.PrivateDiagnosticCategory ?? "unspecified",
            StringComparer.Ordinal)
        .OrderBy(static group => group.Key, StringComparer.Ordinal)
        .Select(static group => $"{group.Count()} {group.Key}"));
Console.WriteLine(
    $"INFO: live privacy-safe runtime result: " +
    $"{livePackagedInspection.Screen.Summary}" +
    (liveRuntimeSummary.Length == 0 ? string.Empty : $" · {liveRuntimeSummary}") +
    (liveFailureSummary.Length == 0 ? string.Empty : $" · {liveFailureSummary}"));
Expect(livePackagedInspection.ReadyToFlashCount == 0 &&
    !livePackagedInspection.GlobalActions.Flash.Enabled &&
    !Regex.IsMatch(
        livePackagedJson,
        @"\bCOM\d{1,4}\b",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant),
    "live built-in Windows inspection remains privacy-safe and read-only");

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "Exact hardware profile required", ""));
    Expect(false, "empty blocker copy must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "Exact hardware profile required", new string('x', 241)));
    Expect(false, "oversized blocker copy must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "Exact hardware profile required", "Line one\\nLine two"));
    Expect(false, "control characters in blocker copy must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "\"ready_to_flash_count\": 0", "\"ready_to_flash_count\": 1"));
    Expect(false, "nonzero ready count must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "\"local_ports_included\": false", "\"local_ports_included\": true"));
    Expect(false, "private local ports must fail");
}
catch (InvalidDataException)
{
}

var refreshAuthority = new LoaderRefreshAuthority();
var firstRefresh = refreshAuthority.Begin();
Expect(firstRefresh != 0 && refreshAuthority.CanPublish(firstRefresh),
    "a newly started refresh owns publication");

var secondRefresh = refreshAuthority.Begin();
Expect(!refreshAuthority.CanPublish(firstRefresh) &&
    refreshAuthority.CanPublish(secondRefresh),
    "a newer refresh invalidates the older result");

Expect(!refreshAuthority.Complete(firstRefresh) &&
    refreshAuthority.Complete(secondRefresh) &&
    !refreshAuthority.CanPublish(secondRefresh),
    "only the current refresh can complete once");

var closingRefresh = refreshAuthority.Begin();
refreshAuthority.InvalidateAll();
Expect(!refreshAuthority.CanPublish(closingRefresh) &&
    !refreshAuthority.CanPublish(0),
    "window close invalidates all refresh publication");

var bundleAuthority = new LoaderCandidateBundleAuthority();
Expect(!bundleAuthority.CanBeginInspection,
    "bundle inspection requires a current device snapshot");
try
{
    _ = bundleAuthority.BeginInspection();
    Expect(false, "bundle inspection without a device snapshot must fail");
}
catch (InvalidOperationException)
{
}

bundleAuthority.InvalidateForDeviceRefresh();
bundleAuthority.PublishCurrentDeviceSnapshot();
Expect(bundleAuthority.CanBeginInspection,
    "a published device snapshot enables local bundle inspection");
var staleBundle = bundleAuthority.BeginInspection();
Expect(bundleAuthority.CanPublish(staleBundle) &&
    !bundleAuthority.CanBeginInspection,
    "one active bundle inspection owns its current device snapshot");

bundleAuthority.InvalidateForDeviceRefresh();
Expect(!bundleAuthority.CanPublish(staleBundle) &&
    !bundleAuthority.Complete(staleBundle) &&
    !bundleAuthority.CanBeginInspection,
    "device refresh invalidates an in-flight bundle result");

bundleAuthority.PublishCurrentDeviceSnapshot();
var currentBundle = bundleAuthority.BeginInspection();
Expect(bundleAuthority.Complete(currentBundle) &&
    !bundleAuthority.Complete(currentBundle) &&
    bundleAuthority.CanBeginInspection,
    "only the current bundle result completes once and preserves its snapshot");
bundleAuthority.InvalidateAll();
Expect(!bundleAuthority.CanBeginInspection &&
    !bundleAuthority.CanPublish(currentBundle),
    "window close invalidates bundle and device-snapshot authority");

var selectionAuthority = new LoaderDeviceSelectionAuthority();
Expect(!selectionAuthority.HasSelection &&
    !selectionAuthority.TrySelect("usb_candidate_1"),
    "device selection requires a current snapshot");
selectionAuthority.InvalidateForDeviceRefresh();
selectionAuthority.PublishSnapshot(["usb_candidate_1", "usb_candidate_2"]);
Expect(selectionAuthority.TrySelect("usb_candidate_2") &&
    selectionAuthority.HasSelection &&
    selectionAuthority.IsSelected("usb_candidate_2") &&
    !selectionAuthority.IsSelected("usb_candidate_1") &&
    !selectionAuthority.TrySelect("usb_candidate_3") &&
    !selectionAuthority.TrySelect("private-device"),
    "selection accepts exactly one candidate from the current reduced snapshot");

bundleAuthority.InvalidateForDeviceRefresh();
bundleAuthority.PublishCurrentDeviceSnapshot();
var selectedBundle = bundleAuthority.BeginInspection();
bundleAuthority.InvalidateForDeviceSelectionChange();
Expect(!bundleAuthority.CanPublish(selectedBundle) &&
    bundleAuthority.CanBeginInspection,
    "changing current device invalidates an in-flight bundle result");

selectionAuthority.InvalidateForDeviceRefresh();
Expect(!selectionAuthority.HasSelection &&
    !selectionAuthority.IsSelected("usb_candidate_2"),
    "device refresh clears the selected candidate");
try
{
    selectionAuthority.PublishSnapshot(["usb_candidate_1", "usb_candidate_1"]);
    Expect(false, "duplicate selection candidates must fail");
}
catch (InvalidDataException)
{
}
selectionAuthority.InvalidateAll();
Expect(!selectionAuthority.HasSelection,
    "window close clears selection authority");

var boundedText = BoundedTextReader.ReadAsync(
    new StringReader("safe"), 4).GetAwaiter().GetResult();
Expect(boundedText == "safe", "bounded reader accepts its exact safe limit");

try
{
    _ = BoundedTextReader.ReadAsync(
        new StringReader("too large"), 4).GetAwaiter().GetResult();
    Expect(false, "bounded reader must reject excessive helper output");
}
catch (InvalidDataException)
{
}

try
{
    _ = BoundedTextReader.ReadAsync(
        new StringReader(string.Empty), 0).GetAwaiter().GetResult();
    Expect(false, "bounded reader must reject an invalid limit");
}
catch (ArgumentOutOfRangeException)
{
}

var packagedInspection = WindowsSerialInspectionProvider.CreateDocument(
    ["COM4", "com4", " COM18 ", "LPT1", null, "COM0", "COM4097"]);
Expect(packagedInspection.CandidateCount == 2 &&
    packagedInspection.InspectedCount == 2 &&
    packagedInspection.Devices.All(static device =>
        device.DisplayName == "USB-connected device" &&
        device.FlashStatus == "Blocked" &&
        !device.Actions.Flash),
    "built-in Windows inspection deduplicates valid serial candidates and blocks writes");

var packagedJson = JsonSerializer.Serialize(packagedInspection);
Expect(!packagedJson.Contains("COM4", StringComparison.OrdinalIgnoreCase) &&
    !packagedJson.Contains("COM18", StringComparison.OrdinalIgnoreCase) &&
    !packagedInspection.Devices.Any(static device =>
        device.AccessibleSummary.Contains("COM", StringComparison.OrdinalIgnoreCase)),
    "built-in Windows inspection does not expose private port names");

var packagedFallbackCalled = false;
var packagedService = new LoaderInspectionService(
    repositoryRootResolver: static () => null,
    packagedInspection: _ =>
    {
        packagedFallbackCalled = true;
        return packagedInspection;
    });
var packagedResult = packagedService.RefreshAsync().GetAwaiter().GetResult();
Expect(packagedFallbackCalled &&
    ReferenceEquals(packagedResult, packagedInspection),
    "loader selects the built-in inspection path when development files are absent");

try
{
    _ = WindowsSerialInspectionProvider.CreateDocument(
        Enumerable.Range(1, 65).Select(static ordinal => $"COM{ordinal}"));
    Expect(false, "built-in inspection must reject an excessive candidate collection");
}
catch (InvalidDataException)
{
}

try
{
    if (string.Equals(
            Environment.GetEnvironmentVariable("OT_LOADER_LIVE_REFRESH_ACCEPTANCE"),
            "1",
            StringComparison.Ordinal))
    {
        var liveRefreshes = LoaderVisualFixtureRenderer.RunLiveRefreshAcceptance();
        Console.WriteLine(
            $"INFO: production Windows loader completed {liveRefreshes} live USB refresh cycles");
    }

    var windowAcceptance = LoaderVisualFixtureRenderer.Run();
    Console.WriteLine(
        $"INFO: production Windows loader completed {windowAcceptance.SuccessfulRefreshes} controlled refresh/selection cycles");
    Console.WriteLine(
        $"INFO: production Windows loader accepted {windowAcceptance.AcceptedDpiProfiles} deterministic high-DPI profiles");
    Console.WriteLine(
        $"INFO: production Windows loader accepted {windowAcceptance.AcceptedThemeProfiles} deterministic contrast profile");
    Console.WriteLine(
        $"INFO: production Windows loader accepted {windowAcceptance.AcceptedKeyboardPaths} keyboard focus path");
    Console.WriteLine(
        $"INFO: production Windows loader accepted {windowAcceptance.AcceptedAutomationPaths} automation-peer path");
    foreach (var visualFile in windowAcceptance.RenderedFiles)
    {
        Console.WriteLine($"INFO: rendered Windows loader fixture {visualFile}");
    }
}
catch (Exception error)
{
    Console.Error.WriteLine($"FAIL: Windows loader visual fixture: {error}");
    failures++;
}

if (failures != 0)
{
    Console.Error.WriteLine($"{failures} Windows loader assertion(s) failed");
    return 1;
}

Console.WriteLine("PASS: 53 Windows loader document, identity-safeguard, accessibility, production-window refresh/selection/keyboard/automation-peer/high-DPI/contrast-theme, snapshot-binding/device-match, process-boundary, USB runtime/hardware-profile, fixed-vector firmware-bundle-signature, and packaged-inspection scenario groups");
return 0;
