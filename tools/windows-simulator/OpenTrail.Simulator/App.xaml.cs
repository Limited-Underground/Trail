using System.Windows;
using OpenTrail.Simulator.Core;

namespace OpenTrail.Simulator;

public partial class App : Application
{
    private LocalLoopbackSimulator? localSimulator;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ApplyCurrentTheme();
        SystemParameters.StaticPropertyChanged += HandleSystemParametersChanged;

        localSimulator = LocalLoopbackSimulator.Create();
        var clientA = new CoreSimulatorClientPresenter(
            localSimulator.Bridge, SimulatorClientId.A, Dispatcher);
        var clientB = new CoreSimulatorClientPresenter(
            localSimulator.Bridge, SimulatorClientId.B, Dispatcher);
        var windowA = new VirtualLcdWindow(clientA);
        var windowB = new VirtualLcdWindow(clientB);
        PlaceWindows(windowA, windowB);
        windowA.Show();
        windowB.Show();
        _ = ConnectInitialClientsAsync(clientA, clientB);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemParameters.StaticPropertyChanged -= HandleSystemParametersChanged;
        if (localSimulator is not null)
        {
            localSimulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
            localSimulator = null;
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
