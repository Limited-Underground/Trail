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
        string familyName,
        string utilityRoleName)
    {
        ParentName = parentName;
        FamilyName = familyName;
        UtilityRoleName = utilityRoleName;
    }

    public static ProductIdentity Current { get; } = CreateWorking(
        parentName: "Limited Underground",
        familyName: "Trail",
        utilityRoleName: "Device Utility");

    public string ParentName { get; }

    public string FamilyName { get; }

    public string UtilityRoleName { get; }

    public string FamilyDisplayName => $"{ParentName} {FamilyName}";

    public string WindowTitle => $"{FamilyDisplayName} {UtilityRoleName}";

    public string HeaderLine =>
        $"{ParentName.ToUpperInvariant()} / {FamilyName.ToUpperInvariant()}";

    public string ReviewStatus => "WORKING NAME — ATTORNEY REVIEW PENDING";

    public static ProductIdentity CreateWorking(
        string parentName,
        string familyName,
        string utilityRoleName)
    {
        var validatedParent = ValidateSegment(parentName, nameof(parentName));
        var validatedFamily = ValidateSegment(familyName, nameof(familyName));
        var validatedRole = ValidateSegment(utilityRoleName, nameof(utilityRoleName));
        var publicIdentity =
            $"{validatedParent} {validatedFamily} {validatedRole}";

        if (StandaloneLu.IsMatch(publicIdentity) ||
            LuNumber.IsMatch(publicIdentity) ||
            BannedLuOffering.IsMatch(publicIdentity))
        {
            throw new ArgumentException(
                "Public identity violates the Limited Underground naming safeguards.");
        }

        return new ProductIdentity(
            validatedParent,
            validatedFamily,
            validatedRole);
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
