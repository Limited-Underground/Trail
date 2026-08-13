using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace OpenTrail.Loader;

internal static class LoaderVisualFixtureRenderer
{
    private const string OutputEnvironmentVariable = "OT_LOADER_VISUAL_OUTPUT";

    internal static IReadOnlyList<string> RenderIfRequested()
    {
        var outputDirectory = Environment.GetEnvironmentVariable(
            OutputEnvironmentVariable);
        if (string.IsNullOrWhiteSpace(outputDirectory))
        {
            return [];
        }

        var renderedFiles = new List<string>();
        Exception? renderingFailure = null;
        var thread = new Thread(() =>
        {
            try
            {
                Directory.CreateDirectory(outputDirectory);
                var app = new App();
                app.InitializeComponent();

                var window = new MainWindow();
                try
                {
                    _ = window.BeginDisplayRefresh();
                    window.PublishInspectionDocumentForDisplay(CreateDocument());
                    window.DeviceCards.SelectedIndex = 1;

                    renderedFiles.Add(Render(
                        window,
                        outputDirectory,
                        "loader-desktop-1600x900.png",
                        1600,
                        900));
                    renderedFiles.Add(Render(
                        window,
                        outputDirectory,
                        "loader-minimum-900x620.png",
                        900,
                        620));
                    window.ContentScroll.ScrollToEnd();
                    window.ContentScroll.UpdateLayout();
                    renderedFiles.Add(Render(
                        window,
                        outputDirectory,
                        "loader-minimum-scrolled-900x620.png",
                        900,
                        620));
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

        return renderedFiles;
    }

    private static string Render(
        MainWindow window,
        string outputDirectory,
        string fileName,
        int width,
        int height)
    {
        var content = (FrameworkElement)window.Content;
        content.Measure(new Size(width, height));
        content.Arrange(new Rect(0, 0, width, height));
        content.UpdateLayout();

        var bitmap = new RenderTargetBitmap(
            width,
            height,
            96,
            96,
            PixelFormats.Pbgra32);
        bitmap.Render(content);

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
