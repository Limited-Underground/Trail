namespace OpenTrail.Loader;

public sealed class LoaderRefreshAuthority
{
    private ulong _revision;
    private bool _active;

    public ulong Begin()
    {
        if (_revision == ulong.MaxValue)
        {
            throw new InvalidOperationException("Loader refresh revision is exhausted.");
        }

        _revision++;
        _active = true;
        return _revision;
    }

    public bool CanPublish(ulong revision) =>
        _active && revision != 0 && revision == _revision;

    public bool Complete(ulong revision)
    {
        if (!CanPublish(revision))
        {
            return false;
        }

        _active = false;
        return true;
    }

    public void InvalidateAll()
    {
        _active = false;
    }
}
