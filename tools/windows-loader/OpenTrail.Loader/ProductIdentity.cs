using System.Text.RegularExpressions;

namespace OpenTrail.Loader;

/// <summary>
/// One replaceable boundary for public loader presentation. Engineering
/// namespaces, OT-* identifiers, protocol schemas, and package identities do
/// not depend on these working display strings.
/// </summary>
public sealed class ProductIdentity
{
    private const int MaximumSegmentLength = 80;

    private static readonly Regex StandaloneLu = new(
        @"(?:^|[^A-Za-z0-9])LU(?:$|[^A-Za-z0-9])",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private static readonly Regex LuNumber = new(
        @"(?:^|[^A-Za-z0-9])LU[\s-]*\d",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private static readonly Regex BannedLuOffering = new(
        @"(?:^|[^A-Za-z0-9])LU[\s-]*(?:Link|Studio)(?:$|[^A-Za-z0-9])",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private static readonly HashSet<string> RetiredCompactNames =
        new(StringComparer.OrdinalIgnoreCase)
        {
            "TLU",
            "LUT",
            "LUTRAIL",
        };

    private ProductIdentity(
        string parentName,
        string productName,
        string releaseStage)
    {
        ParentName = parentName;
        ProductName = productName;
        ReleaseStage = releaseStage;
    }

    public static ProductIdentity Current { get; } = CreateWorking(
        parentName: "Limited Underground",
        productName: "Firmware Loader",
        releaseStage: "Preview");

    public string ParentName { get; }

    public string ProductName { get; }

    public string ReleaseStage { get; }

    public string ProductDisplayName => $"{ParentName} {ProductName}";

    // Retained as the presentation binding name so the naming change does not
    // affect schemas, automation IDs, namespaces, or loader contracts.
    public string UtilityRoleName => ProductName;

    public string WindowTitle => $"{ProductDisplayName} — {ReleaseStage}";

    public string HeaderLine => ParentName.ToUpperInvariant();

    public string ReviewStatus =>
        "PREVIEW — INSPECTION ONLY · WORKING NAME — ATTORNEY REVIEW PENDING";

    public static ProductIdentity CreateWorking(
        string parentName,
        string productName,
        string releaseStage)
    {
        var validatedParent = ValidateSegment(parentName, nameof(parentName));
        var validatedProduct = ValidateSegment(productName, nameof(productName));
        var validatedStage = ValidateSegment(releaseStage, nameof(releaseStage));
        var publicIdentity =
            $"{validatedParent} {validatedProduct} {validatedStage}";

        if (StandaloneLu.IsMatch(publicIdentity) ||
            LuNumber.IsMatch(publicIdentity) ||
            BannedLuOffering.IsMatch(publicIdentity))
        {
            throw new ArgumentException(
                "Public identity violates the Limited Underground naming safeguards.");
        }

        return new ProductIdentity(
            validatedParent,
            validatedProduct,
            validatedStage);
    }

    private static string ValidateSegment(string value, string parameterName)
    {
        ArgumentNullException.ThrowIfNull(value, parameterName);
        if (value.Length == 0 ||
            value.Length > MaximumSegmentLength ||
            !string.Equals(value, value.Trim(), StringComparison.Ordinal) ||
            value.Any(char.IsControl) ||
            value.Contains('®') ||
            RetiredCompactNames.Contains(
                string.Concat(value.Where(char.IsLetterOrDigit))))
        {
            throw new ArgumentException(
                "Public identity segment is invalid or prohibited.",
                parameterName);
        }

        return value;
    }
}
