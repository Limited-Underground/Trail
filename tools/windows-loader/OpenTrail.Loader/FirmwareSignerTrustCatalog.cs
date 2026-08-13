using System.Security.Cryptography;

namespace OpenTrail.Loader;

internal sealed record FirmwareSignerTrustEntry(
    string SignerId,
    byte[] SubjectPublicKeyInfo);

internal sealed class FirmwareSignerTrustCatalog
{
    internal const int MaximumTrustedSigners = 3;
    internal const int RequiredRsaBits = 3072;

    private readonly IReadOnlyDictionary<string, byte[]> _publicKeys;

    internal static FirmwareSignerTrustCatalog Current { get; } = new([]);

    internal FirmwareSignerTrustCatalog(IEnumerable<FirmwareSignerTrustEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var material = entries.ToArray();
        if (material.Length > MaximumTrustedSigners)
        {
            throw new ArgumentException("Too many firmware signers are configured.", nameof(entries));
        }

        var publicKeys = new Dictionary<string, byte[]>(StringComparer.Ordinal);
        foreach (var entry in material)
        {
            if (entry is null ||
                !IsLowerHex(entry.SignerId, 16) ||
                entry.SignerId.All(static value => value == '0') ||
                entry.SubjectPublicKeyInfo is null ||
                entry.SubjectPublicKeyInfo.Length == 0)
            {
                throw new ArgumentException("Firmware signer material is invalid.", nameof(entries));
            }

            var publicKey = entry.SubjectPublicKeyInfo.ToArray();
            using var rsa = RSA.Create();
            try
            {
                rsa.ImportSubjectPublicKeyInfo(publicKey, out var bytesRead);
                var exponent = rsa.ExportParameters(includePrivateParameters: false).Exponent;
                if (bytesRead != publicKey.Length ||
                    rsa.KeySize != RequiredRsaBits ||
                    exponent is null ||
                    !exponent.SequenceEqual(new byte[] { 0x01, 0x00, 0x01 }))
                {
                    throw new ArgumentException(
                        "Firmware signer must be an RSA-3072 public key with exponent 65537.",
                        nameof(entries));
                }
            }
            catch (CryptographicException exception)
            {
                throw new ArgumentException(
                    "Firmware signer public key could not be imported.",
                    nameof(entries),
                    exception);
            }

            var derivedId = DeriveSignerId(publicKey);
            if (!string.Equals(entry.SignerId, derivedId, StringComparison.Ordinal) ||
                !publicKeys.TryAdd(entry.SignerId, publicKey))
            {
                throw new ArgumentException(
                    "Firmware signer ID is not the exact public-key fingerprint or is duplicated.",
                    nameof(entries));
            }
        }
        _publicKeys = publicKeys;
    }

    internal static FirmwareSignerTrustEntry FromPublicKey(byte[] subjectPublicKeyInfo)
    {
        ArgumentNullException.ThrowIfNull(subjectPublicKeyInfo);
        return new FirmwareSignerTrustEntry(
            DeriveSignerId(subjectPublicKeyInfo),
            subjectPublicKeyInfo.ToArray());
    }

    internal bool TryVerify(
        string signerId,
        ReadOnlySpan<byte> signedManifest,
        ReadOnlySpan<byte> signature,
        out bool signatureVerified)
    {
        signatureVerified = false;
        if (!_publicKeys.TryGetValue(signerId, out var publicKey))
        {
            return false;
        }

        using var rsa = RSA.Create();
        rsa.ImportSubjectPublicKeyInfo(publicKey, out var bytesRead);
        if (bytesRead != publicKey.Length)
        {
            throw new CryptographicException("Pinned firmware signer could not be read completely.");
        }

        signatureVerified = rsa.VerifyData(
            signedManifest,
            signature,
            HashAlgorithmName.SHA256,
            RSASignaturePadding.Pss);
        return true;
    }

    internal static string DeriveSignerId(ReadOnlySpan<byte> subjectPublicKeyInfo)
    {
        var digest = SHA256.HashData(subjectPublicKeyInfo);
        return Convert.ToHexString(digest.AsSpan(0, 8)).ToLowerInvariant();
    }

    private static bool IsLowerHex(string value, int length) =>
        value.Length == length && value.All(static character =>
            character is >= '0' and <= '9' or >= 'a' and <= 'f');
}
