using System.Windows;
using OpenTrail.Simulator.Core;
using OpenTrail.Simulator.Windows;

namespace OpenTrail.Simulator;

public partial class App : Application
{
    private WindowsSimulatorRuntime? simulatorRuntime;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ApplyCurrentTheme();
        SystemParameters.StaticPropertyChanged += HandleSystemParametersChanged;

        SimulatorStartupComposition? composition = null;
        VirtualLcdWindow? windowA = null;
        VirtualLcdWindow? windowB = null;
        try
        {
            composition = SimulatorStartupComposition.Create(
                Dispatcher, NativePortableUiSession.CreateFromEnvironment);
            windowA = new VirtualLcdWindow(
                composition.ClientA, composition.SessionA);
            windowB = new VirtualLcdWindow(
                composition.ClientB, composition.SessionB);
            PlaceWindows(windowA, windowB);
            windowA.Show();
            windowB.Show();

            var clientA = composition.ClientA;
            var clientB = composition.ClientB;
            simulatorRuntime = composition.TransferToWindows();
            composition = null;
            _ = ConnectInitialClientsAsync(clientA, clientB);
        }
        catch (Exception)
        {
            windowB?.Hide();
            windowA?.Hide();
            DisposeQuietly(composition);
            MessageBox.Show(
                "The two native portable UI sessions could not be started safely. No device connection was opened.",
                "OpenTrail Simulator",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(-1);
        }
    }

    private static void DisposeQuietly(IAsyncDisposable? disposable)
    {
        if (disposable is null) return;
        try { disposable.DisposeAsync().AsTask().GetAwaiter().GetResult(); }
        catch (Exception) { }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemParameters.StaticPropertyChanged -= HandleSystemParametersChanged;
        if (simulatorRuntime is not null)
        {
            simulatorRuntime.DisposeAsync().AsTask().GetAwaiter().GetResult();
            simulatorRuntime = null;
        }
        base.OnExit(e);
    }

    private static void PlaceWindows(Window a, Window b)
    {
        var work = SystemParameters.WorkArea;
        a.Left = work.Left + 12;
        a.Top = work.Top + 12;
        if (work.Width >= a.Width + b.Width + 36)
        {
            b.Left = a.Left + a.Width + 12;
            b.Top = a.Top;
        }
        else
        {
            b.Left = Math.Min(work.Right - b.Width - 12, a.Left + 42);
            b.Top = Math.Min(work.Bottom - b.Height - 12, a.Top + 42);
        }
    }

    private static async Task ConnectInitialClientsAsync(
        ISimulatorClientPresenter a,
        ISimulatorClientPresenter b)
    {
        await ConnectOneAsync(a);
        await ConnectOneAsync(b);
    }

    private static async Task ConnectOneAsync(ISimulatorClientPresenter presenter)
    {
        try
        {
            await presenter.ConnectAsync(CancellationToken.None);
        }
        catch (Exception)
        {
            // This client's failure cannot suppress the independent peer attempt.
        }
    }

    private void HandleSystemParametersChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(SystemParameters.HighContrast), StringComparison.Ordinal))
        {
            ApplyCurrentTheme();
        }
    }

    private void ApplyCurrentTheme()
    {
        SimulatorThemeResources.Apply(
            Resources,
            SystemParameters.HighContrast
                ? SimulatorThemeResources.CreateSystemHighContrastPalette()
                : SimulatorThemeResources.CreateClassicPalette());
    }
}

internal sealed class SimulatorStartupComposition : IAsyncDisposable
{
    private WindowsSimulatorRuntime? runtime;
    private CoreSimulatorClientPresenter? clientA;
    private CoreSimulatorClientPresenter? clientB;
    private IPortableUiSession? sessionA;
    private IPortableUiSession? sessionB;

    private SimulatorStartupComposition() { }

    internal CoreSimulatorClientPresenter ClientA =>
        clientA ?? throw new InvalidOperationException("Client A is unavailable.");
    internal CoreSimulatorClientPresenter ClientB =>
        clientB ?? throw new InvalidOperationException("Client B is unavailable.");
    internal IPortableUiSession SessionA =>
        sessionA ?? throw new InvalidOperationException("Session A is unavailable.");
    internal IPortableUiSession SessionB =>
        sessionB ?? throw new InvalidOperationException("Session B is unavailable.");

    internal static SimulatorStartupComposition Create(
        System.Windows.Threading.Dispatcher dispatcher,
        Func<IPortableUiSession> sessionFactory)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        ArgumentNullException.ThrowIfNull(sessionFactory);
        var result = new SimulatorStartupComposition();
        try
        {
            result.runtime = new WindowsSimulatorRuntime();
            result.clientA = new CoreSimulatorClientPresenter(
                result.runtime.Bridge, SimulatorClientId.A, dispatcher);
            result.clientB = new CoreSimulatorClientPresenter(
                result.runtime.Bridge, SimulatorClientId.B, dispatcher);
            result.sessionA = sessionFactory();
            result.sessionB = sessionFactory();
            return result;
        }
        catch
        {
            result.DisposeAsync().AsTask().GetAwaiter().GetResult();
            throw;
        }
    }

    internal WindowsSimulatorRuntime TransferToWindows()
    {
        var transferred = runtime ??
            throw new InvalidOperationException("Runtime is unavailable.");
        runtime = null;
        clientA = null;
        clientB = null;
        sessionA = null;
        sessionB = null;
        return transferred;
    }

    public async ValueTask DisposeAsync()
    {
        if (sessionB is not null) await DisposeQuietlyAsync(sessionB);
        if (sessionA is not null) await DisposeQuietlyAsync(sessionA);
        if (clientB is not null) await DisposeQuietlyAsync(clientB);
        if (clientA is not null) await DisposeQuietlyAsync(clientA);
        if (runtime is not null) await DisposeQuietlyAsync(runtime);
        sessionB = null;
        sessionA = null;
        clientB = null;
        clientA = null;
        runtime = null;
    }

    private static async ValueTask DisposeQuietlyAsync(IAsyncDisposable disposable)
    {
        try { await disposable.DisposeAsync(); }
        catch (Exception) { }
    }
}
