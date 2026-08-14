using OpenTrail.Simulator.Core;

namespace OpenTrail.Simulator.Windows;

/// <summary>
/// Production-local composition for two synthetic companions plus explicitly
/// selected, helper-admitted USB companions. The caller owns this runtime and
/// must dispose it after every presenter/window has stopped servicing clients.
/// </summary>
public sealed class WindowsSimulatorRuntime : IAsyncDisposable
{
    private readonly MeshCoreCompanionHost _usb;
    private readonly LocalLoopbackSimulator _simulator;
    private bool _disposed;

    public WindowsSimulatorRuntime()
    {
        _usb = new MeshCoreCompanionHost();
        _simulator = LocalLoopbackSimulator.CreateWithAdditionalCompanions(
            _usb, _usb);
    }

    internal WindowsSimulatorRuntime(MeshCoreCompanionHost usb)
    {
        _usb = usb ?? throw new ArgumentNullException(nameof(usb));
        _simulator = LocalLoopbackSimulator.CreateWithAdditionalCompanions(
            _usb, _usb);
    }

    public DualClientBridge Bridge
    {
        get
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            return _simulator.Bridge;
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        await DisposeBothAsync(_simulator.Bridge, _usb).ConfigureAwait(false);
    }

    internal static async ValueTask DisposeBothAsync(
        IAsyncDisposable bridge,
        IAsyncDisposable usb)
    {
        ArgumentNullException.ThrowIfNull(bridge);
        ArgumentNullException.ThrowIfNull(usb);
        Exception? failure = null;
        try { await bridge.DisposeAsync().ConfigureAwait(false); }
        catch { failure = new IOException("The simulator runtime did not close cleanly."); }
        try { await usb.DisposeAsync().ConfigureAwait(false); }
        catch
        {
            failure ??= new IOException("The simulator runtime did not close cleanly.");
        }
        if (failure is not null) throw failure;
    }
}
