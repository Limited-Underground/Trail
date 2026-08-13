using System.IO;

namespace OpenTrail.Loader;

internal sealed class LoaderDeviceSelectionAuthority
{
    private const int MaximumCandidates = 64;

    private readonly HashSet<string> _candidates = new(StringComparer.Ordinal);
    private ulong _snapshotRevision;
    private bool _snapshotAvailable;

    internal string? SelectedCandidate { get; private set; }

    internal bool HasSelection =>
        _snapshotAvailable && SelectedCandidate is not null;

    internal void InvalidateForDeviceRefresh()
    {
        if (_snapshotRevision == ulong.MaxValue)
        {
            throw new InvalidOperationException(
                "Device selection snapshot revision is exhausted.");
        }

        _snapshotRevision++;
        _snapshotAvailable = false;
        SelectedCandidate = null;
        _candidates.Clear();
    }

    internal void PublishSnapshot(IEnumerable<string> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        if (_snapshotRevision == 0)
        {
            throw new InvalidOperationException(
                "A device refresh must begin before selection candidates are published.");
        }

        var next = new HashSet<string>(StringComparer.Ordinal);
        foreach (var candidate in candidates)
        {
            if (!IsSafeCandidate(candidate) ||
                !next.Add(candidate) ||
                next.Count > MaximumCandidates)
            {
                throw new InvalidDataException(
                    "Device selection candidates are not accepted.");
            }
        }

        _candidates.Clear();
        _candidates.UnionWith(next);
        SelectedCandidate = null;
        _snapshotAvailable = true;
    }

    internal bool TrySelect(string candidate)
    {
        if (!_snapshotAvailable ||
            !IsSafeCandidate(candidate) ||
            !_candidates.Contains(candidate))
        {
            return false;
        }

        SelectedCandidate = candidate;
        return true;
    }

    internal bool IsSelected(string candidate) =>
        HasSelection &&
        string.Equals(SelectedCandidate, candidate, StringComparison.Ordinal);

    internal void InvalidateAll()
    {
        _snapshotAvailable = false;
        SelectedCandidate = null;
        _candidates.Clear();
    }

    private static bool IsSafeCandidate(string? candidate)
    {
        const string prefix = "usb_candidate_";
        if (candidate is null ||
            !candidate.StartsWith(prefix, StringComparison.Ordinal) ||
            candidate.Length is < 15 or > 17)
        {
            return false;
        }

        return int.TryParse(
                candidate.AsSpan(prefix.Length),
                System.Globalization.NumberStyles.None,
                System.Globalization.CultureInfo.InvariantCulture,
                out var ordinal) &&
            ordinal is >= 1 and <= 999;
    }
}
