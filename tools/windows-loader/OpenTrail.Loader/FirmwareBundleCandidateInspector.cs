using System.IO.Compression;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OpenTrail.Loader;

internal sealed record FirmwareBundleCandidateResult(
    bool StructureVerified,
    bool ImageDigestVerified,
    bool SignaturePresent,
    bool SignerTrusted,
    bool SignatureVerified,
    bool AdmissionAllowed,
    uint HardwareProfileId,
    string Processor,
    string TargetRole,
    ushort MinimumBoardRevision,
    ushort MaximumBoardRevision,
    ushort MinimumBootloaderSchema,
    string ProcessorDisplay,
    string TargetRoleDisplay,
    ulong ReleaseGeneration,
    uint ImageBytes,
    string Summary,
    string Details,
    string BlockerText);

internal static class FirmwareBundleCandidateInspector
{
    internal const string FileExtension = ".fwbundle";
    internal const string Schema = "firmware_bundle_candidate_v0";
    internal const int MaximumArchiveBytes = 20 * 1024 * 1024;
    internal const int MaximumManifestBytes = 4096;
    internal const int MaximumImageBytes = 16 * 1024 * 1024;
    internal const int RsaPss3072SignatureBytes = 384;
    internal const string SignatureAlgorithm = "rsa_pss_3072_sha256";

    private const string ManifestEntryName = "manifest.json";
    private const string ImageEntryName = "image.bin";
    private const string SignatureEntryName = "manifest.sig";

    private static readonly string[] CanonicalPropertyNames =
    [
        "schema",
        "canonical_manifest_bytes",
        "hardware_profile_id",
        "processor",
        "target_role",
        "minimum_board_revision",
        "maximum_board_revision",
        "minimum_bootloader_schema",
        "release_generation",
        "image_bytes",
        "image_sha256",
        "signer_id",
        "signature_algorithm",
    ];

    internal static FirmwareBundleCandidateResult Inspect(
        string path,
        FirmwareSignerTrustCatalog? trustCatalog = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        if (!string.Equals(
                Path.GetExtension(path),
                FileExtension,
                StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Candidate bundle extension is not accepted.");
        }

        using var input = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            bufferSize: 64 * 1024,
            FileOptions.SequentialScan);

        if (input.Length <= 0 || input.Length > MaximumArchiveBytes)
        {
            throw new InvalidDataException("Candidate bundle size is outside the inspection limit.");
        }

        using var archive = new ZipArchive(input, ZipArchiveMode.Read, leaveOpen: false);
        if (archive.Entries.Count != 3)
        {
            throw new InvalidDataException("Candidate bundle must contain exactly three entries.");
        }

        var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.Ordinal);
        foreach (var entry in archive.Entries)
        {
            if (!entries.TryAdd(entry.FullName, entry) ||
                (entry.FullName != ManifestEntryName &&
                 entry.FullName != ImageEntryName &&
                 entry.FullName != SignatureEntryName))
            {
                throw new InvalidDataException("Candidate bundle entry set is not accepted.");
            }
        }

        var manifestBytes = ReadBoundedEntry(
            entries[ManifestEntryName],
            minimumBytes: 2,
            maximumBytes: MaximumManifestBytes,
            "manifest");
        var manifest = ParseCanonicalManifest(manifestBytes);

        var imageEntry = entries[ImageEntryName];
        if (imageEntry.Length <= 0 ||
            imageEntry.Length > MaximumImageBytes ||
            imageEntry.Length != manifest.ImageBytes)
        {
            throw new InvalidDataException("Candidate image length does not match its manifest.");
        }

        byte[] imageDigest;
        using (var image = imageEntry.Open())
        {
            imageDigest = SHA256.HashData(image);
        }
        if (!CryptographicOperations.FixedTimeEquals(imageDigest, manifest.ImageSha256))
        {
            throw new InvalidDataException("Candidate image digest does not match its manifest.");
        }

        var signature = ReadBoundedEntry(
            entries[SignatureEntryName],
            RsaPss3072SignatureBytes,
            RsaPss3072SignatureBytes,
            "signature");
        if (signature.All(static value => value == 0))
        {
            throw new InvalidDataException("Candidate signature is empty.");
        }

        trustCatalog ??= FirmwareSignerTrustCatalog.Current;
        var signerTrusted = trustCatalog.TryVerify(
            manifest.SignerId,
            manifestBytes,
            signature,
            out var signatureVerified);
        if (signerTrusted && !signatureVerified)
        {
            throw new InvalidDataException("Candidate signature verification failed.");
        }

        return new FirmwareBundleCandidateResult(
            StructureVerified: true,
            ImageDigestVerified: true,
            SignaturePresent: true,
            SignerTrusted: signerTrusted,
            SignatureVerified: signatureVerified,
            AdmissionAllowed: false,
            HardwareProfileId: manifest.HardwareProfileId,
            Processor: manifest.Processor,
            TargetRole: manifest.TargetRole,
            MinimumBoardRevision: manifest.MinimumBoardRevision,
            MaximumBoardRevision: manifest.MaximumBoardRevision,
            MinimumBootloaderSchema: manifest.MinimumBootloaderSchema,
            ProcessorDisplay: ProcessorDisplay(manifest.Processor),
            TargetRoleDisplay: TargetRoleDisplay(manifest.TargetRole),
            ReleaseGeneration: manifest.ReleaseGeneration,
            ImageBytes: manifest.ImageBytes,
            Summary: signatureVerified
                ? "Candidate bundle signature and image SHA-256 verified"
                : "Candidate bundle structure and image SHA-256 verified",
            Details: $"{ProcessorDisplay(manifest.Processor)} · " +
                $"{TargetRoleDisplay(manifest.TargetRole)} · " +
                $"generation {manifest.ReleaseGeneration} · " +
                $"{manifest.ImageBytes:N0} bytes · RSA-PSS-3072/SHA-256",
            BlockerText: signatureVerified
                ? "BLOCKED: Release admission is not connected."
                : "BLOCKED: No trusted release signer is configured.");
    }

    internal static byte[] SerializeCanonicalManifest(
        ushort canonicalManifestBytes,
        uint hardwareProfileId,
        string processor,
        string targetRole,
        ushort minimumBoardRevision,
        ushort maximumBoardRevision,
        ushort minimumBootloaderSchema,
        ulong releaseGeneration,
        uint imageBytes,
        string imageSha256,
        string signerId,
        string signatureAlgorithm = SignatureAlgorithm)
    {
        using var output = new MemoryStream();
        using (var writer = new Utf8JsonWriter(
            output,
            new JsonWriterOptions { Indented = false, SkipValidation = false }))
        {
            writer.WriteStartObject();
            writer.WriteString("schema", Schema);
            writer.WriteNumber("canonical_manifest_bytes", canonicalManifestBytes);
            writer.WriteNumber("hardware_profile_id", hardwareProfileId);
            writer.WriteString("processor", processor);
            writer.WriteString("target_role", targetRole);
            writer.WriteNumber("minimum_board_revision", minimumBoardRevision);
            writer.WriteNumber("maximum_board_revision", maximumBoardRevision);
            writer.WriteNumber("minimum_bootloader_schema", minimumBootloaderSchema);
            writer.WriteNumber("release_generation", releaseGeneration);
            writer.WriteNumber("image_bytes", imageBytes);
            writer.WriteString("image_sha256", imageSha256);
            writer.WriteString("signer_id", signerId);
            writer.WriteString("signature_algorithm", signatureAlgorithm);
            writer.WriteEndObject();
        }
        return output.ToArray();
    }

    private static CandidateManifest ParseCanonicalManifest(byte[] bytes)
    {
        if (bytes.Length > ushort.MaxValue)
        {
            throw new InvalidDataException("Candidate manifest is too large.");
        }

        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(
                bytes,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 4,
                });
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException("Candidate manifest is not valid JSON.", exception);
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object ||
                !root.EnumerateObject().Select(static property => property.Name)
                    .SequenceEqual(CanonicalPropertyNames, StringComparer.Ordinal))
            {
                throw new InvalidDataException("Candidate manifest is not canonical.");
            }

            var schema = RequiredString(root, "schema", 64);
            var canonicalBytes = RequiredUInt16(root, "canonical_manifest_bytes");
            var hardwareProfileId = RequiredUInt32(root, "hardware_profile_id");
            var processor = RequiredString(root, "processor", 24);
            var targetRole = RequiredString(root, "target_role", 32);
            var minimumBoardRevision = RequiredUInt16(root, "minimum_board_revision");
            var maximumBoardRevision = RequiredUInt16(root, "maximum_board_revision");
            var minimumBootloaderSchema = RequiredUInt16(root, "minimum_bootloader_schema");
            var releaseGeneration = RequiredUInt64(root, "release_generation");
            var imageBytes = RequiredUInt32(root, "image_bytes");
            var imageSha256Text = RequiredString(root, "image_sha256", 64);
            var signerIdText = RequiredString(root, "signer_id", 16);
            var signatureAlgorithm = RequiredString(root, "signature_algorithm", 32);

            if (schema != Schema ||
                canonicalBytes != bytes.Length ||
                hardwareProfileId == 0 ||
                minimumBoardRevision == 0 ||
                maximumBoardRevision < minimumBoardRevision ||
                releaseGeneration == 0 ||
                imageBytes == 0 ||
                imageBytes > MaximumImageBytes ||
                !IsAcceptedProcessor(processor) ||
                !IsAcceptedTargetRole(targetRole) ||
                !IsLowerHex(imageSha256Text, 64) ||
                !IsLowerHex(signerIdText, 16) ||
                signerIdText.All(static value => value == '0') ||
                signatureAlgorithm != SignatureAlgorithm)
            {
                throw new InvalidDataException("Candidate manifest fields are not accepted.");
            }

            var canonical = SerializeCanonicalManifest(
                canonicalBytes,
                hardwareProfileId,
                processor,
                targetRole,
                minimumBoardRevision,
                maximumBoardRevision,
                minimumBootloaderSchema,
                releaseGeneration,
                imageBytes,
                imageSha256Text,
                signerIdText,
                signatureAlgorithm);
            if (!bytes.AsSpan().SequenceEqual(canonical))
            {
                throw new InvalidDataException("Candidate manifest is not canonically encoded.");
            }

            return new CandidateManifest(
                hardwareProfileId,
                processor,
                targetRole,
                minimumBoardRevision,
                maximumBoardRevision,
                minimumBootloaderSchema,
                releaseGeneration,
                imageBytes,
                Convert.FromHexString(imageSha256Text),
                signerIdText);
        }
    }

    private static byte[] ReadBoundedEntry(
        ZipArchiveEntry entry,
        int minimumBytes,
        int maximumBytes,
        string label)
    {
        if (entry.Length < minimumBytes || entry.Length > maximumBytes)
        {
            throw new InvalidDataException($"Candidate {label} size is outside the inspection limit.");
        }

        using var input = entry.Open();
        using var output = new MemoryStream((int)entry.Length);
        input.CopyTo(output);
        if (output.Length != entry.Length)
        {
            throw new InvalidDataException($"Candidate {label} could not be read completely.");
        }
        return output.ToArray();
    }

    private static string RequiredString(JsonElement root, string name, int exactOrMaximumLength)
    {
        var element = root.GetProperty(name);
        if (element.ValueKind != JsonValueKind.String)
        {
            throw new InvalidDataException("Candidate manifest field type is not accepted.");
        }
        var value = element.GetString() ?? string.Empty;
        if (value.Length == 0 || value.Length > exactOrMaximumLength)
        {
            throw new InvalidDataException("Candidate manifest text is outside its limit.");
        }
        return value;
    }

    private static ushort RequiredUInt16(JsonElement root, string name)
    {
        var element = root.GetProperty(name);
        if (!element.TryGetUInt16(out var value))
        {
            throw new InvalidDataException("Candidate manifest integer is outside its limit.");
        }
        return value;
    }

    private static uint RequiredUInt32(JsonElement root, string name)
    {
        var element = root.GetProperty(name);
        if (!element.TryGetUInt32(out var value))
        {
            throw new InvalidDataException("Candidate manifest integer is outside its limit.");
        }
        return value;
    }

    private static ulong RequiredUInt64(JsonElement root, string name)
    {
        var element = root.GetProperty(name);
        if (!element.TryGetUInt64(out var value))
        {
            throw new InvalidDataException("Candidate manifest integer is outside its limit.");
        }
        return value;
    }

    private static bool IsLowerHex(string value, int length) =>
        value.Length == length && value.All(static character =>
            character is >= '0' and <= '9' or >= 'a' and <= 'f');

    private static bool IsAcceptedProcessor(string value) =>
        value is "esp32_s3" or "nrf52840";

    private static bool IsAcceptedTargetRole(string value) =>
        value is "bench_client" or "complete_client" or "packaged_repeater";

    private static string ProcessorDisplay(string value) => value switch
    {
        "esp32_s3" => "ESP32-S3",
        "nrf52840" => "nRF52840",
        _ => throw new InvalidDataException("Candidate processor is not accepted."),
    };

    private static string TargetRoleDisplay(string value) => value switch
    {
        "bench_client" => "bench client",
        "complete_client" => "complete client",
        "packaged_repeater" => "packaged repeater",
        _ => throw new InvalidDataException("Candidate target role is not accepted."),
    };

    private sealed record CandidateManifest(
        uint HardwareProfileId,
        string Processor,
        string TargetRole,
        ushort MinimumBoardRevision,
        ushort MaximumBoardRevision,
        ushort MinimumBootloaderSchema,
        ulong ReleaseGeneration,
        uint ImageBytes,
        byte[] ImageSha256,
        string SignerId);
}
