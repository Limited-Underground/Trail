using System.IO;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Media;
using System.Windows.Threading;
using OpenTrail.Simulator;

internal static class Program
{
    private static int passed;

    [STAThread]
    private static int Main()
    {
        var application = new App { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        application.InitializeComponent();

        Run("theme round trip and contrast", ThemeRoundTripAndContrast);
        Run("independent two-window construction", IndependentTwoWindowConstruction);
        Run("minimum touch target and local evidence labels", MinimumTouchTargetAndEvidenceLabels);
        Run("navigation and keyboard-facing screen model", NavigationScreenModel);
        Run("failed queue preserves compose draft", FailedQueuePreservesComposeDraft);
        Run("overlapping UI commands are serialized", OverlappingUiCommandsAreSerialized);
        Run("core presenter maps dual-client traffic", CorePresenterMapsDualClientTraffic);
        Run("deterministic LCD renders at scale profiles", DeterministicLcdRendersAtScaleProfiles);
        Run("closing one LCD affects only its presenter", ClosingOneWindowAffectsOnlyItsPresenter);
        Run("closing drains an in-flight service tick", ClosingDrainsInFlightServiceTick);

        Console.WriteLine($"OpenTrail simulator UI tests passed: {passed}/10");
        return 0;
    }

    private static void ThemeRoundTripAndContrast()
    {
        var resources = new ResourceDictionary();
        var classic = SimulatorThemeResources.CreateClassicPalette();
        var contrast = SimulatorThemeResources.CreateDeterministicHighContrastPalette();
        SimulatorThemeResources.Apply(resources, classic);
        var originalPanel = ColorOf((Brush)resources["RaisedBrush"]);
        var disabled = ColorOf((Brush)resources["DisabledBrush"]);
        Require(ContrastRatio(disabled, originalPanel) >= 4.5, "Classic disabled contrast is below 4.5:1.");

        SimulatorThemeResources.Apply(resources, contrast);
        Require((string)resources["SimulatorThemeMode"] == "deterministic-high-contrast", "High contrast mode was not applied.");
        SimulatorThemeResources.Apply(resources, classic);
        Require((string)resources["SimulatorThemeMode"] == "classic", "Classic mode was not restored.");
        Require(ColorOf((Brush)resources["RaisedBrush"]) == originalPanel, "Theme round trip changed the classic palette.");
    }

    private static void IndependentTwoWindowConstruction()
    {
        var a = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var b = new FakePresenter(Snapshot("Client B", "Simulated device B"));
        using var windows = new WindowPair(a, b);
        Require(!ReferenceEquals(windows.A.Presenter, windows.B.Presenter), "The two LCDs share one presenter.");
        Require(windows.A.Title.Contains("Client A", StringComparison.Ordinal), "Client A title is missing.");
        Require(windows.B.Title.Contains("Client B", StringComparison.Ordinal), "Client B title is missing.");
        Require(windows.A.CurrentScreen == SimulatorScreen.Home && windows.B.CurrentScreen == SimulatorScreen.Home,
            "Each LCD must start independently at Home.");
    }

    private static void MinimumTouchTargetAndEvidenceLabels()
    {
        var presenter = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var window = new VirtualLcdWindow(presenter)
        {
            Width = 560,
            Height = 640,
            Left = -20000,
            Top = -20000,
            ShowInTaskbar = false,
        };
        window.Show();
        PumpDispatcher();
        window.UpdateLayout();
        Require(window.MessagesButton.ActualWidth >= 44 && window.MessagesButton.ActualHeight >= 44,
            "The accepted minimum shrinks a primary simulated-touch target below 44 DIPs.");
        Require(window.MinWidth >= 560 && window.MinHeight >= 640, "The accepted minimum is too small for the fixed LCD surface.");
        var disabledBorder = FindVisualDescendant<System.Windows.Controls.Border>(window.BackButton, "ButtonBorder")
            ?? throw new InvalidOperationException("The disabled button surface was not realized.");
        Require(ContrastRatio(
                ColorOf(window.BackButton.Foreground),
                ColorOf(disabledBorder.Background)) >= 4.5,
            "The realized disabled button is below 4.5:1 contrast.");
        Require(window.Title.Contains("Simulator", StringComparison.Ordinal), "The window is not visibly labeled as a simulator.");
        Require(window.DeviceLabelText.Text == "Simulated device A · local loopback", "Public simulated-device label was not preserved.");
        Require(AutomationProperties.GetHelpText(window.AcknowledgeAlertButton).Contains("selected received alert", StringComparison.Ordinal),
            "Alert acknowledgement lacks accessible context.");
        window.Close();
        PumpDispatcher();
    }

    private static void NavigationScreenModel()
    {
        var presenter = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var window = new VirtualLcdWindow(presenter);
        window.MessagesButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        Require(window.CurrentScreen == SimulatorScreen.Messages, "Messages did not open.");
        window.StatusButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        Require(window.CurrentScreen == SimulatorScreen.Status, "Status did not open.");
        window.Close();
        PumpDispatcher();
    }

    private static void ClosingOneWindowAffectsOnlyItsPresenter()
    {
        var a = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var b = new FakePresenter(Snapshot("Client B", "Simulated device B"));
        var windowA = new VirtualLcdWindow(a);
        var windowB = new VirtualLcdWindow(b);
        windowA.Show();
        windowB.Show();
        windowA.Close();
        PumpDispatcher();
        Require(a.DisconnectCount == 1 && a.DisposeCount == 1, "Closing Client A did not release only its session.");
        Require(b.DisconnectCount == 0 && b.DisposeCount == 0 && windowB.IsVisible,
            "Closing Client A changed Client B.");
        windowB.Close();
        PumpDispatcher();
    }

    private static void FailedQueuePreservesComposeDraft()
    {
        var presenter = new FakePresenter(Snapshot("Client A", "Simulated device A"))
        {
            FailQueue = true,
        };
        var window = new VirtualLcdWindow(presenter);
        window.ComposeButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
        window.ComposeText.Text = "retain this draft";
        window.QueueMessageButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
        PumpDispatcher();
        Require(window.CurrentScreen == SimulatorScreen.Compose, "A failed queue navigated away from Compose.");
        Require(window.ComposeText.Text == "retain this draft", "A failed queue cleared the draft.");
        Require(window.ScreenNoticeText.Text.Contains("failed", StringComparison.OrdinalIgnoreCase),
            "A failed queue showed success copy.");
        window.Close();
        PumpDispatcher();
    }

    private static void CorePresenterMapsDualClientTraffic()
    {
        var simulator = OpenTrail.Simulator.Core.LocalLoopbackSimulator.Create();
        var a = new CoreSimulatorClientPresenter(
            simulator.Bridge,
            OpenTrail.Simulator.Core.SimulatorClientId.A,
            Dispatcher.CurrentDispatcher);
        var b = new CoreSimulatorClientPresenter(
            simulator.Bridge,
            OpenTrail.Simulator.Core.SimulatorClientId.B,
            Dispatcher.CurrentDispatcher);
        try
        {
            a.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
            b.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
            a.QueueMessageAsync("hello from A", false, CancellationToken.None).GetAwaiter().GetResult();
            simulator.Bridge.ServiceAsync(OpenTrail.Simulator.Core.SimulatorClientId.B).AsTask().GetAwaiter().GetResult();
            Require(a.Snapshot.Messages.Any(message =>
                    message.Direction == "Outbound" && message.Text == "hello from A"),
                "Client A did not retain its outbound message.");
            Require(b.Snapshot.Messages.Any(message =>
                    message.Direction == "Inbound" && message.Text == "hello from A"),
                "Client B did not receive Client A's local-loopback message.");
            Require(!a.Snapshot.Messages.Any(message => message.Direction == "Inbound"),
                "Client A incorrectly received its own message.");

            b.InjectSyntheticAlertAsync(CancellationToken.None).GetAwaiter().GetResult();
            simulator.Bridge.ServiceAsync(OpenTrail.Simulator.Core.SimulatorClientId.A).AsTask().GetAwaiter().GetResult();
            var inbound = a.Snapshot.Alerts.Single(alert => alert.Direction == "Inbound");
            Require(inbound.CanAcknowledge, "The received synthetic alert cannot be acknowledged.");
            a.AcknowledgeAlertAsync(inbound.Sequence, CancellationToken.None).GetAwaiter().GetResult();
            simulator.Bridge.ServiceAsync(OpenTrail.Simulator.Core.SimulatorClientId.B).AsTask().GetAwaiter().GetResult();
            Require(a.Snapshot.Alerts.Single(alert => alert.Sequence == inbound.Sequence).State == "Acknowledged",
                "The acknowledging client did not update its local alert state.");
        }
        finally
        {
            a.DisposeAsync().AsTask().GetAwaiter().GetResult();
            b.DisposeAsync().AsTask().GetAwaiter().GetResult();
            simulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
        }
    }

    private static void OverlappingUiCommandsAreSerialized()
    {
        for (var repeat = 0; repeat < 100; ++repeat)
        {
            var blocker = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            var presenter = new FakePresenter(Snapshot("Client A", "Simulated device A"))
            {
                ConnectBlocker = blocker,
            };
            var window = new VirtualLcdWindow(presenter);
            window.StatusButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            window.ConnectButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            window.ReconnectButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpDispatcher();
            Require(presenter.ConnectCount == 1, $"Repeat {repeat}: the first Connect command did not start.");
            Require(presenter.ReconnectCount == 0, $"Repeat {repeat}: Reconnect overlapped an in-flight Connect command.");
            blocker.SetResult(true);
            PumpUntil(() =>
                presenter.ReconnectCount == 1 &&
                window.ScreenNoticeText.Text.Contains("Reconnect requested", StringComparison.Ordinal));
            Require(presenter.MaximumCommandConcurrency == 1,
                $"Repeat {repeat}: two commands entered the presenter concurrently.");
            window.Close();
            PumpDispatcher();
        }
    }

    private static void DeterministicLcdRendersAtScaleProfiles()
    {
        var snapshot = Snapshot("Client A", "Simulated device A") with
        {
            ConnectionState = SimulatorUiConnectionState.Connected,
            FreshnessText = "Local bridge observation 0 seconds ago",
            Messages =
            [
                new(1, "Outbound", "Chat", "Normal", "Meet at the trailhead", "Sent", DateTimeOffset.Parse("2026-08-14T12:00:00Z")),
                new(2, "Inbound", "QuickStatus", "Important", "Available to help", "Received", DateTimeOffset.Parse("2026-08-14T12:00:01Z")),
            ],
            Alerts =
            [
                new(3, "Inbound", "Important", "Synthetic test assistance alert", "Active", DateTimeOffset.Parse("2026-08-14T12:00:02Z"), true),
            ],
        };
        var presenter = new FakePresenter(snapshot);
        var window = new VirtualLcdWindow(presenter)
        {
            Width = 580,
            Height = 720,
            Left = -20000,
            Top = -20000,
            ShowInTaskbar = false,
        };
        window.Show();
        PumpDispatcher();
        window.UpdateLayout();

        foreach (var scale in new[] { 1.0, 1.25, 1.5, 2.0 })
        {
            var width = checked((int)Math.Ceiling(window.ActualWidth * scale));
            var height = checked((int)Math.Ceiling(window.ActualHeight * scale));
            var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(
                width,
                height,
                96.0 * scale,
                96.0 * scale,
                PixelFormats.Pbgra32);
            bitmap.Render(window);
            var stride = checked(width * 4);
            var pixels = new byte[checked(stride * height)];
            bitmap.CopyPixels(pixels, stride, 0);
            var opaque = 0;
            var sampledColors = new HashSet<int>();
            for (var index = 0; index < pixels.Length; index += 4)
            {
                if (pixels[index + 3] > 0)
                {
                    ++opaque;
                }
                if ((index / 4) % 97 == 0)
                {
                    sampledColors.Add(
                        pixels[index] |
                        (pixels[index + 1] << 8) |
                        (pixels[index + 2] << 16) |
                        (pixels[index + 3] << 24));
                }
            }
            Require(opaque > width * height * 0.90, $"The {scale:P0} render is mostly transparent.");
            Require(sampledColors.Count >= 8, $"The {scale:P0} render lacks expected visual variation.");

            if (scale == 1.0)
            {
                var renderDirectory = Environment.GetEnvironmentVariable("OPENTRAIL_SIMULATOR_RENDER_DIR");
                if (!string.IsNullOrWhiteSpace(renderDirectory))
                {
                    Directory.CreateDirectory(renderDirectory);
                    var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
                    encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
                    using var stream = File.Create(Path.Combine(renderDirectory, "client-a-home.png"));
                    encoder.Save(stream);
                }
            }
        }

        window.Close();
        PumpDispatcher();
    }

    private static void ClosingDrainsInFlightServiceTick()
    {
        var simulator = OpenTrail.Simulator.Core.LocalLoopbackSimulator.Create();
        var serviceEntered = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseService = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var presenter = new CoreSimulatorClientPresenter(
            simulator.Bridge,
            OpenTrail.Simulator.Core.SimulatorClientId.A,
            Dispatcher.CurrentDispatcher,
            async token =>
            {
                serviceEntered.TrySetResult(true);
                await releaseService.Task.WaitAsync(token);
            },
            TimeSpan.FromMilliseconds(1));
        var window = new VirtualLcdWindow(presenter)
        {
            Left = -20000,
            Top = -20000,
            ShowInTaskbar = false,
        };
        window.Show();
        PumpUntil(() => serviceEntered.Task.IsCompleted);
        window.Close();
        PumpDispatcher();
        Require(window.IsVisible, "The window closed before its in-flight service lease drained.");
        releaseService.SetResult(true);
        PumpUntil(() => !window.IsVisible);
        simulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
    }

    private static SimulatorUiSnapshot Snapshot(string client, string device) =>
        new(
            client,
            device,
            "local loopback",
            SimulatorUiConnectionState.Disconnected,
            "No observation",
            Array.Empty<SimulatorUiMessage>(),
            Array.Empty<SimulatorUiAlert>(),
            0,
            32,
            null);

    private static void PumpDispatcher()
    {
        var frame = new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(
            DispatcherPriority.Background,
            new Action(() => frame.Continue = false));
        Dispatcher.PushFrame(frame);
    }

    private static void PumpUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
        while (!condition())
        {
            if (DateTime.UtcNow >= deadline)
            {
                throw new TimeoutException("The expected dispatcher state did not settle.");
            }
            PumpDispatcher();
            Thread.Sleep(10);
        }
    }

    private static Color ColorOf(Brush brush) =>
        brush is SolidColorBrush solid
            ? solid.Color
            : throw new InvalidOperationException("Expected a solid brush.");

    private static double ContrastRatio(Color left, Color right)
    {
        static double Channel(byte value)
        {
            var normalized = value / 255.0;
            return normalized <= 0.03928
                ? normalized / 12.92
                : Math.Pow((normalized + 0.055) / 1.055, 2.4);
        }

        static double Luminance(Color color) =>
            0.2126 * Channel(color.R) + 0.7152 * Channel(color.G) + 0.0722 * Channel(color.B);

        var a = Luminance(left);
        var b = Luminance(right);
        return (Math.Max(a, b) + 0.05) / (Math.Min(a, b) + 0.05);
    }

    private static T? FindVisualDescendant<T>(DependencyObject parent, string name)
        where T : FrameworkElement
    {
        for (var index = 0; index < VisualTreeHelper.GetChildrenCount(parent); ++index)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T typed && string.Equals(typed.Name, name, StringComparison.Ordinal))
            {
                return typed;
            }
            var nested = FindVisualDescendant<T>(child, name);
            if (nested is not null)
            {
                return nested;
            }
        }
        return null;
    }

    private static void Run(string name, Action test)
    {
        test();
        ++passed;
        Console.WriteLine($"PASS {name}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class FakePresenter : ISimulatorClientPresenter
    {
        internal FakePresenter(SimulatorUiSnapshot snapshot) => Snapshot = snapshot;

        public event EventHandler? SnapshotChanged;

        public SimulatorUiSnapshot Snapshot { get; private set; }

        internal int DisconnectCount { get; private set; }

        internal int DisposeCount { get; private set; }

        internal TaskCompletionSource<bool>? ConnectBlocker { get; init; }

        internal int ConnectCount { get; private set; }

        internal int ReconnectCount { get; private set; }

        internal int MaximumCommandConcurrency { get; private set; }

        private int activeCommands;

        public async Task ConnectAsync(CancellationToken cancellationToken)
        {
            ++ConnectCount;
            EnterCommand();
            try
            {
                if (ConnectBlocker is not null)
                {
                    await ConnectBlocker.Task.WaitAsync(cancellationToken);
                }
            }
            finally
            {
                --activeCommands;
            }
        }

        public Task DisconnectAsync(CancellationToken cancellationToken)
        {
            ++DisconnectCount;
            return Task.CompletedTask;
        }

        public Task ReconnectAsync(CancellationToken cancellationToken)
        {
            ++ReconnectCount;
            EnterCommand();
            --activeCommands;
            return Task.CompletedTask;
        }

        internal bool FailQueue { get; init; }

        public Task QueueMessageAsync(string text, bool highPriority, CancellationToken cancellationToken) =>
            FailQueue
                ? Task.FromException(new InvalidOperationException("Synthetic queue failure."))
                : Task.CompletedTask;

        public Task InjectSyntheticAlertAsync(CancellationToken cancellationToken) => Task.CompletedTask;

        public Task AcknowledgeAlertAsync(long sequence, CancellationToken cancellationToken) => Task.CompletedTask;

        public ValueTask DisposeAsync()
        {
            ++DisposeCount;
            return ValueTask.CompletedTask;
        }

        internal void Publish(SimulatorUiSnapshot snapshot)
        {
            Snapshot = snapshot;
            SnapshotChanged?.Invoke(this, EventArgs.Empty);
        }

        private void EnterCommand()
        {
            ++activeCommands;
            MaximumCommandConcurrency = Math.Max(MaximumCommandConcurrency, activeCommands);
        }
    }

    private sealed class WindowPair : IDisposable
    {
        internal WindowPair(ISimulatorClientPresenter a, ISimulatorClientPresenter b)
        {
            A = new VirtualLcdWindow(a);
            B = new VirtualLcdWindow(b);
        }

        internal VirtualLcdWindow A { get; }

        internal VirtualLcdWindow B { get; }

        public void Dispose()
        {
            A.Close();
            B.Close();
            PumpDispatcher();
        }
    }
}
