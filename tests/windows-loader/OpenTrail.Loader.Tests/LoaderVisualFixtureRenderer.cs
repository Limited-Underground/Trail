using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace OpenTrail.Loader;

internal sealed record LoaderWindowAcceptanceResult(
    int SuccessfulRefreshes,
    int AcceptedDpiProfiles,
    int AcceptedThemeProfiles,
    IReadOnlyList<string> RenderedFiles);

internal static class LoaderVisualFixtureRenderer
{
    private const string OutputEnvironmentVariable = "OT_LOADER_VISUAL_OUTPUT";

    internal static int RunLiveRefreshAcceptance()
    {
        var successfulRefreshes = 0;
        Exception? refreshFailure = null;
        var thread = new Thread(() =>
        {
            SynchronizationContext.SetSynchronizationContext(
                new DispatcherSynchronizationContext(Dispatcher.CurrentDispatcher));
            var window = new MainWindow(cancellationToken => Task.Run(
                () => WindowsSerialInspectionProvider.Inspect(cancellationToken),
                cancellationToken));
            try
            {
                for (var cycle = 0; cycle < 3; cycle++)
                {
                    AwaitWithDispatcherPump(window.RefreshForAcceptanceAsync());
                    Require(window.DeviceCards.Items.Count == 3,
                        $"live refresh cycle {cycle + 1} did not publish the three connected bench candidates " +
                        $"(items={window.DeviceCards.Items.Count}, summary={window.SummaryText.Text}, error={window.ErrorText.Text})");
                    Require(window.DeviceCards.SelectedItem is null &&
                        !window.SelectFirmwareButton.IsEnabled &&
                        window.RefreshButton.IsEnabled &&
                        string.Equals(window.RefreshButton.Content as string,
                            "Refresh devices", StringComparison.Ordinal),
                        $"live refresh cycle {cycle + 1} did not settle in the safe unselected state");
                    Require(window.SummaryText.Text.StartsWith(
                            "3 USB candidates found · 3 runtime-identified · 0 ready to flash",
                            StringComparison.Ordinal),
                        $"live refresh cycle {cycle + 1} published an unexpected summary: {window.SummaryText.Text}");
                    successfulRefreshes++;
                }
            }
            catch (Exception error)
            {
                refreshFailure = error;
            }
            finally
            {
                window.Close();
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        if (refreshFailure is not null)
        {
            throw new InvalidOperationException(
                "Could not complete repeated live Windows loader refresh acceptance.",
                refreshFailure);
        }
        return successfulRefreshes;
    }

    private static void AwaitWithDispatcherPump(Task task)
    {
        var dispatcher = Dispatcher.CurrentDispatcher;
        var frame = new DispatcherFrame();
        _ = task.ContinueWith(
            _ => dispatcher.BeginInvoke(
                new Action(() => frame.Continue = false)),
            CancellationToken.None,
            TaskContinuationOptions.None,
            TaskScheduler.Default);
        Dispatcher.PushFrame(frame);
        task.GetAwaiter().GetResult();
    }

    internal static LoaderWindowAcceptanceResult Run()
    {
        var outputDirectory = Environment.GetEnvironmentVariable(
            OutputEnvironmentVariable);
        outputDirectory = string.IsNullOrWhiteSpace(outputDirectory)
            ? null
            : outputDirectory;

        var renderedFiles = new List<string>();
        var successfulRefreshes = 0;
        var acceptedDpiProfiles = 0;
        var acceptedThemeProfiles = 0;
        Exception? renderingFailure = null;
        var thread = new Thread(() =>
        {
            try
            {
                if (outputDirectory is not null)
                {
                    Directory.CreateDirectory(outputDirectory);
                }
                var app = new App();
                app.InitializeComponent();

                var refreshCalls = 0;
                var window = new MainWindow(_ =>
                {
                    refreshCalls++;
                    return Task.FromResult(CreateDocument());
                });
                try
                {
                    RunRefreshCycles(window);
                    successfulRefreshes = refreshCalls;
                    acceptedDpiProfiles = AcceptDpiProfiles(
                        window,
                        outputDirectory,
                        renderedFiles);
                    acceptedThemeProfiles = AcceptThemeProfiles(
                        app,
                        window,
                        outputDirectory,
                        renderedFiles);

                    if (outputDirectory is not null)
                    {
                        renderedFiles.Add(Render(
                            window,
                            outputDirectory,
                            "loader-desktop-1600x900.png",
                            1600,
                            900,
                            1.0));
                        renderedFiles.Add(Render(
                            window,
                            outputDirectory,
                            "loader-minimum-900x620.png",
                            900,
                            620,
                            1.0));
                        window.ContentScroll.ScrollToEnd();
                        window.ContentScroll.UpdateLayout();
                        renderedFiles.Add(Render(
                            window,
                            outputDirectory,
                            "loader-minimum-scrolled-900x620.png",
                            900,
                            620,
                            1.0));
                    }
                }
                finally
                {
                    window.Close();
                }
            }
            catch (Exception error)
            {
                renderingFailure = error;
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        if (renderingFailure is not null)
        {
            throw new InvalidOperationException(
                "Could not render the Windows loader visual fixture.",
                renderingFailure);
        }

        return new(
            successfulRefreshes,
            acceptedDpiProfiles,
            acceptedThemeProfiles,
            renderedFiles);
    }

    private static void RunRefreshCycles(MainWindow window)
    {
        for (var cycle = 0; cycle < 3; cycle++)
        {
            window.RefreshForAcceptanceAsync().GetAwaiter().GetResult();
            Require(window.DeviceCards.Items.Count == 3,
                $"refresh cycle {cycle + 1} did not publish three cards");
            Require(window.DeviceCards.SelectedItem is null,
                $"refresh cycle {cycle + 1} retained a stale device selection");
            Require(!window.SelectFirmwareButton.IsEnabled,
                $"refresh cycle {cycle + 1} enabled bundle selection without a device");
            Require(window.RefreshButton.IsEnabled &&
                string.Equals(
                    window.RefreshButton.Content as string,
                    "Refresh devices",
                    StringComparison.Ordinal),
                $"refresh cycle {cycle + 1} did not restore Refresh");
            Require(string.Equals(
                    window.SummaryText.Text,
                    "3 found · 3 inspected · 0 ready to flash",
                    StringComparison.Ordinal),
                $"refresh cycle {cycle + 1} published the wrong summary");
            Require(string.Equals(
                    window.BundleSummaryText.Text,
                    "No firmware bundle selected",
                    StringComparison.Ordinal),
                $"refresh cycle {cycle + 1} retained stale bundle status");

            window.DeviceCards.SelectedIndex = (cycle + 1) % 3;
            Require(window.DeviceCards.SelectedItem is LoaderDeviceCard,
                $"refresh cycle {cycle + 1} could not select a current card");
            Require(window.SelectFirmwareButton.IsEnabled,
                $"refresh cycle {cycle + 1} did not enable bounded bundle selection");
            Require(window.SelectionText.Text.StartsWith(
                    "Selected:",
                    StringComparison.Ordinal),
                $"refresh cycle {cycle + 1} did not publish selected status");
            Require(string.Equals(
                    window.BundleSummaryText.Text,
                    "No firmware bundle selected for the current device",
                    StringComparison.Ordinal),
                $"refresh cycle {cycle + 1} did not bind bundle status to the selection");
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private static int AcceptDpiProfiles(
        MainWindow window,
        string? outputDirectory,
        ICollection<string> renderedFiles)
    {
        var accepted = 0;
        foreach (var profile in new[]
        {
            (Scale: 1.25, Label: "125pct"),
            (Scale: 1.50, Label: "150pct"),
            (Scale: 2.00, Label: "200pct"),
        })
        {
            var bitmap = RenderBitmap(window, 900, 620, profile.Scale);
            var expectedWidth = checked((int)Math.Round(900 * profile.Scale));
            var expectedHeight = checked((int)Math.Round(620 * profile.Scale));
            Require(
                bitmap.PixelWidth == expectedWidth &&
                bitmap.PixelHeight == expectedHeight &&
                Math.Abs(bitmap.DpiX - (96 * profile.Scale)) < 0.01 &&
                Math.Abs(bitmap.DpiY - (96 * profile.Scale)) < 0.01,
                $"{profile.Label} render did not preserve the minimum logical layout at the expected pixel density");
            Require(HasVisibleContent(bitmap),
                $"{profile.Label} production-window render was blank or visually collapsed");
            Require(
                window.RefreshButton.ActualWidth > 80 &&
                window.RefreshButton.ActualHeight > 24 &&
                window.SelectFirmwareButton.ActualWidth > 120 &&
                window.SelectFirmwareButton.ActualHeight > 24 &&
                window.ContentScroll.ViewportWidth > 0 &&
                window.ContentScroll.ViewportHeight > 0,
                $"{profile.Label} production controls or scroll viewport collapsed");

            if (outputDirectory is not null)
            {
                renderedFiles.Add(SaveBitmap(
                    bitmap,
                    outputDirectory,
                    $"loader-minimum-900x620-{profile.Label}.png"));
            }
            accepted++;
        }

        return accepted;
    }

    private static int AcceptThemeProfiles(
        App app,
        MainWindow window,
        string? outputDirectory,
        ICollection<string> renderedFiles)
    {
        var classicButtonFace = BrushColor(
            app.Resources["ButtonFaceBrush"] as Brush,
            "classic button face");
        var classicDisabledText = BrushColor(
            app.Resources["DisabledTextBrush"] as Brush,
            "classic disabled text");
        Require(
            ContrastRatio(classicButtonFace, classicDisabledText) >= 4.5 &&
            BrushColor(
                window.FlashSelectedButton.Foreground,
                "classic disabled Flash foreground") == classicDisabledText,
            "classic disabled-button text did not preserve 4.5:1 contrast on the production button face");

        var systemPaletteResources = new ResourceDictionary();
        LoaderThemeResources.Apply(
            systemPaletteResources,
            LoaderThemeResources.CreateSystemHighContrastPalette());
        Require(string.Equals(
                systemPaletteResources[LoaderThemeResources.ThemeModeResourceKey] as string,
                "system-high-contrast",
                StringComparison.Ordinal),
            "the production system high-contrast palette was incomplete");

        var contrastPalette =
            LoaderThemeResources.CreateDeterministicHighContrastPalette();
        try
        {
            app.ApplyThemeForAcceptance(contrastPalette);
            var bitmap = RenderBitmap(window, 900, 620, 1.0);
            Require(HasVisibleContent(bitmap),
                "the deterministic high-contrast production render was blank");
            Require(string.Equals(
                    app.Resources[LoaderThemeResources.ThemeModeResourceKey] as string,
                    "deterministic-high-contrast",
                    StringComparison.Ordinal),
                "the production window did not publish the accepted contrast mode");

            var background = BrushColor(
                app.Resources["BackgroundBrush"] as Brush,
                "BackgroundBrush");
            var text = BrushColor(
                app.Resources["TextBrush"] as Brush,
                "TextBrush");
            var disabled = BrushColor(
                app.Resources["DisabledTextBrush"] as Brush,
                "DisabledTextBrush");
            Require(
                ContrastRatio(background, text) >= 7.0 &&
                ContrastRatio(background, disabled) >= 7.0,
                "the deterministic contrast palette did not preserve strong text contrast");
            Require(
                BrushColor(window.RefreshButton.Background, "Refresh background") == background &&
                BrushColor(window.RefreshButton.Foreground, "Refresh foreground") == text &&
                BrushColor(window.FlashSelectedButton.Foreground, "disabled Flash foreground") == disabled,
                "dynamic theme resources did not reach the production controls");
            Require(
                window.DeviceCards.Items.Count == 3 &&
                window.DeviceCards.SelectedItem is LoaderDeviceCard &&
                window.ContentScroll.ViewportWidth > 0 &&
                window.ContentScroll.ViewportHeight > 0,
                "the high-contrast theme collapsed production state or navigation");

            if (outputDirectory is not null)
            {
                renderedFiles.Add(SaveBitmap(
                    bitmap,
                    outputDirectory,
                    "loader-minimum-900x620-high-contrast.png"));
            }
            return 1;
        }
        finally
        {
            app.ApplyThemeForAcceptance(
                LoaderThemeResources.CreateClassicPalette());
        }
    }

    private static Color BrushColor(Brush? brush, string name)
    {
        if (brush is not SolidColorBrush solid)
        {
            throw new InvalidOperationException($"{name} is not a solid color brush.");
        }
        return solid.Color;
    }

    private static double ContrastRatio(Color first, Color second)
    {
        static double Luminance(Color color)
        {
            static double Channel(byte value)
            {
                var normalized = value / 255.0;
                return normalized <= 0.04045
                    ? normalized / 12.92
                    : Math.Pow((normalized + 0.055) / 1.055, 2.4);
            }

            return (0.2126 * Channel(color.R)) +
                (0.7152 * Channel(color.G)) +
                (0.0722 * Channel(color.B));
        }

        var lighter = Math.Max(Luminance(first), Luminance(second));
        var darker = Math.Min(Luminance(first), Luminance(second));
        return (lighter + 0.05) / (darker + 0.05);
    }

    private static string Render(
        MainWindow window,
        string outputDirectory,
        string fileName,
        int width,
        int height,
        double dpiScale)
    {
        var bitmap = RenderBitmap(window, width, height, dpiScale);
        return SaveBitmap(bitmap, outputDirectory, fileName);
    }

    private static RenderTargetBitmap RenderBitmap(
        MainWindow window,
        int logicalWidth,
        int logicalHeight,
        double dpiScale)
    {
        if (logicalWidth <= 0 || logicalHeight <= 0 ||
            !double.IsFinite(dpiScale) || dpiScale < 1.0 || dpiScale > 4.0)
        {
            throw new ArgumentOutOfRangeException(nameof(dpiScale));
        }

        var content = (FrameworkElement)window.Content;
        content.Measure(new Size(logicalWidth, logicalHeight));
        content.Arrange(new Rect(0, 0, logicalWidth, logicalHeight));
        content.UpdateLayout();

        var dpi = 96 * dpiScale;
        var bitmap = new RenderTargetBitmap(
            checked((int)Math.Round(logicalWidth * dpiScale)),
            checked((int)Math.Round(logicalHeight * dpiScale)),
            dpi,
            dpi,
            PixelFormats.Pbgra32);
        bitmap.Render(content);
        return bitmap;
    }

    private static bool HasVisibleContent(RenderTargetBitmap bitmap)
    {
        var stride = checked(bitmap.PixelWidth * 4);
        var pixels = new byte[checked(stride * bitmap.PixelHeight)];
        bitmap.CopyPixels(pixels, stride, 0);
        var first = (B: pixels[0], G: pixels[1], R: pixels[2], A: pixels[3]);
        var visible = first.A != 0;
        var distinct = false;
        for (var offset = 4; offset < pixels.Length; offset += 4)
        {
            var alpha = pixels[offset + 3];
            visible |= alpha != 0;
            if (pixels[offset] != first.B ||
                pixels[offset + 1] != first.G ||
                pixels[offset + 2] != first.R ||
                alpha != first.A)
            {
                distinct = true;
            }
            if (visible && distinct)
            {
                return true;
            }
        }
        return false;
    }

    private static string SaveBitmap(
        BitmapSource bitmap,
        string outputDirectory,
        string fileName)
    {
        var path = Path.GetFullPath(Path.Combine(outputDirectory, fileName));
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var output = new FileStream(
            path,
            FileMode.Create,
            FileAccess.Write,
            FileShare.None);
        encoder.Save(output);
        return path;
    }

    private static LoaderInspectionDocument CreateDocument()
    {
        var blockers = new[]
        {
            "Low-level processor and memory probe required",
            "Exact hardware profile required",
            "Product target role unresolved",
            "Board revision unresolved",
            "Bootloader schema unresolved",
        };
        var actions = new LoaderDeviceActions { Inspect = true, Flash = false };

        return new LoaderInspectionDocument
        {
            Schema = "ot_loader_inspection_view_v0",
            Screen = new LoaderScreen
            {
                Title = "Limited Underground Trail Device Utility",
                Eyebrow = "Connected devices",
                Phase = "Inspection only",
                Summary = "3 found · 3 inspected · 0 ready to flash",
                Notice = "USB and installed runtime names do not prove an exact supported board. Flash remains disabled until a signed bundle and every board gate pass.",
            },
            GlobalActions = new LoaderGlobalActions
            {
                Refresh = new LoaderAction { Enabled = true },
                SelectFirmware = new LoaderAction { Enabled = false },
                Flash = new LoaderAction { Enabled = false },
                CleanInstall = new LoaderAction { Enabled = false },
                Recovery = new LoaderAction { Enabled = false },
            },
            CandidateCount = 3,
            InspectedCount = 3,
            ReadyToFlashCount = 0,
            Privacy = new LoaderPrivacy(),
            Devices =
            [
                CreateDevice(
                    "usb_candidate_1",
                    "SenseCAP Solar",
                    "MeshCore repeater",
                    MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater,
                    blockers,
                    actions),
                CreateDevice(
                    "usb_candidate_2",
                    "Heltec V4 OLED",
                    "MeshCore companion",
                    MeshCoreUsbRuntimeFamily.HeltecV4Companion,
                    blockers,
                    actions),
                CreateDevice(
                    "usb_candidate_3",
                    "Heltec V4 OLED",
                    "MeshCore companion",
                    MeshCoreUsbRuntimeFamily.HeltecV4Companion,
                    blockers,
                    actions),
            ],
        };
    }

    private static LoaderDeviceCard CreateDevice(
        string candidate,
        string displayName,
        string installedRuntime,
        MeshCoreUsbRuntimeFamily runtimeFamily,
        IReadOnlyList<string> blockers,
        LoaderDeviceActions actions) =>
        new()
        {
            Candidate = candidate,
            DisplayName = displayName,
            InstalledRuntime = installedRuntime,
            Firmware = "v1.16.0-07a3ca9",
            Connection = "USB",
            InspectionStatus = "Connected and inspected",
            HardwareProfile = LoaderHardwareProfileEvidenceResolver.Resolve(
                runtimeFamily,
                runtimeIdentified: true),
            FlashStatus = "Blocked",
            Blockers = blockers,
            Actions = actions,
        };
}
