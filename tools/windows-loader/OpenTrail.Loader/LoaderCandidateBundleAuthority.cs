namespace OpenTrail.Loader;

internal readonly record struct LoaderCandidateBundlePermit(
    ulong DeviceSnapshotRevision,
    ulong InspectionRevision);

internal sealed class LoaderCandidateBundleAuthority
{
    private ulong _deviceSnapshotRevision;
    private ulong _inspectionRevision;
    private bool _deviceSnapshotAvailable;
    private bool _inspectionActive;

    internal bool CanBeginInspection =>
        _deviceSnapshotAvailable && !_inspectionActive;

    internal void InvalidateForDeviceRefresh()
    {
        if (_deviceSnapshotRevision == ulong.MaxValue)
        {
            throw new InvalidOperationException(
                "Device snapshot revision is exhausted.");
        }

        _deviceSnapshotRevision++;
        _deviceSnapshotAvailable = false;
        _inspectionActive = false;
    }

    internal void PublishCurrentDeviceSnapshot()
    {
        if (_deviceSnapshotRevision == 0)
        {
            throw new InvalidOperationException(
                "A device refresh must begin before its snapshot is published.");
        }

        _deviceSnapshotAvailable = true;
    }

    internal void InvalidateForDeviceSelectionChange()
    {
        if (!_deviceSnapshotAvailable)
        {
            return;
        }
        if (_deviceSnapshotRevision == ulong.MaxValue)
        {
            throw new InvalidOperationException(
                "Device snapshot revision is exhausted.");
        }

        _deviceSnapshotRevision++;
        _inspectionActive = false;
    }

    internal LoaderCandidateBundlePermit BeginInspection()
    {
        if (!CanBeginInspection)
        {
            throw new InvalidOperationException(
                "A current device snapshot is required before bundle inspection.");
        }
        if (_inspectionRevision == ulong.MaxValue)
        {
            throw new InvalidOperationException(
                "Bundle inspection revision is exhausted.");
        }

        _inspectionRevision++;
        _inspectionActive = true;
        return new(_deviceSnapshotRevision, _inspectionRevision);
    }

    internal bool CanPublish(LoaderCandidateBundlePermit permit) =>
        _deviceSnapshotAvailable &&
        _inspectionActive &&
        permit.DeviceSnapshotRevision != 0 &&
        permit.DeviceSnapshotRevision == _deviceSnapshotRevision &&
        permit.InspectionRevision != 0 &&
        permit.InspectionRevision == _inspectionRevision;

    internal bool Complete(LoaderCandidateBundlePermit permit)
    {
        if (!CanPublish(permit))
        {
            return false;
        }

        _inspectionActive = false;
        return true;
    }

    internal void InvalidateAll()
    {
        _deviceSnapshotAvailable = false;
        _inspectionActive = false;
    }
}
