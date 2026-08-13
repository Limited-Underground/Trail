using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace OpenTrail.Loader;

internal sealed record LoaderWindowAcceptanceResult(
    int SuccessfulRefreshes,
    int AcceptedDpiProfiles,
    int AcceptedThemeProfiles,
    int AcceptedThemeTransitions,
    int AcceptedResizeTransitions,
    int AcceptedKeyboardPaths,
    int AcceptedAutomationPaths,
    int AcceptedAutomationScrollPaths,
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
        var acceptedThemeTransitions = 0;
        var acceptedResizeTransitions = 0;
        var acceptedKeyboardPaths = 0;
        var acceptedAutomationPaths = 0;
        var acceptedAutomationScrollPaths = 0;
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
                    acceptedResizeTransitions = AcceptShownResizeTransition(window);
                    acceptedThemeTransitions = AcceptThemeTransition(
                        app,
                        outputDirectory,
                        renderedFiles);
                    acceptedKeyboardPaths = AcceptKeyboardNavigation();
                    acceptedAutomationPaths = AcceptAutomationSemantics();
                    acceptedAutomationScrollPaths =
                        AcceptAutomationScrollReachability();

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
            acceptedThemeTransitions,
            acceptedResizeTransitions,
            acceptedKeyboardPaths,
            acceptedAutomationPaths,
            acceptedAutomationScrollPaths,
            renderedFiles);
    }

    private static int AcceptThemeTransition(
        App app,
        string? outputDirectory,
        ICollection<string> renderedFiles)
    {
        var window = new MainWindow(_ => Task.FromResult(CreateDocument()));
        try
        {
            app.ApplyThemeForAcceptance(
                LoaderThemeResources.CreateClassicPalette());
            window.Show();
            window.Width = window.MinWidth;
            window.Height = window.MinHeight;
            window.UpdateLayout();
            window.DeviceCards.SelectedIndex = 2;
            var selectedItem = ContainerAt(window, 2);
            selectedItem.BringIntoView();
            _ = selectedItem.Focus();
            window.ContentScroll.ScrollToEnd();
            window.UpdateLayout();

            var selectedDevice = (LoaderDeviceCard)window.DeviceCards.SelectedItem;
            var originalOffset = window.ContentScroll.VerticalOffset;
            Require(
                originalOffset > 0 &&
                ReferenceEquals(Keyboard.FocusedElement, selectedItem) &&
                selectedItem.FocusVisualStyle is not null &&
                window.SelectFirmwareButton.IsEnabled &&
                !window.FlashSelectedButton.IsEnabled,
                "classic theme transition fixture did not establish focused safe state");

            if (outputDirectory is not null)
            {
                renderedFiles.Add(SaveBitmap(
                    RenderBitmap(window, 900, 620, 1.0),
                    outputDirectory,
                    "loader-focus-classic-900x620.png"));
            }

            app.ApplyThemeForAcceptance(
                LoaderThemeResources.CreateDeterministicHighContrastPalette());
            window.UpdateLayout();
            RequireThemeState(
                app,
                window,
                selectedItem,
                selectedDevice,
                originalOffset,
                "deterministic-high-contrast",
                Colors.White);
            if (outputDirectory is not null)
            {
                renderedFiles.Add(SaveBitmap(
                    RenderBitmap(window, 900, 620, 1.0),
                    outputDirectory,
                    "loader-focus-high-contrast-900x620.png"));
            }

            app.ApplyThemeForAcceptance(
                LoaderThemeResources.CreateClassicPalette());
            window.UpdateLayout();
            RequireThemeState(
                app,
                window,
                selectedItem,
                selectedDevice,
                originalOffset,
                "classic",
                Colors.Black);
            return 1;
        }
        finally
        {
            app.ApplyThemeForAcceptance(
                LoaderThemeResources.CreateClassicPalette());
            window.Close();
        }
    }

    private static int AcceptShownResizeTransition(MainWindow window)
    {
        window.WindowStartupLocation = WindowStartupLocation.Manual;
        window.Left = SystemParameters.VirtualScreenLeft;
        window.Top = SystemParameters.VirtualScreenTop;
        window.Width = 1120;
        window.Height = 760;
        window.Show();
        window.Dispatcher.Invoke(
            DispatcherPriority.ApplicationIdle,
            new Action(static () => { }));
        foreach (var otherWindow in Application.Current.Windows
            .Cast<Window>()
            .Where(candidate => !ReferenceEquals(candidate, window))
            .ToArray())
        {
            otherWindow.Close();
        }
        Application.Current.MainWindow = window;
        Keyboard.ClearFocus();
        window.UpdateLayout();
        window.DeviceCards.SelectedIndex = 2;
        var selectedDevice = (LoaderDeviceCard)window.DeviceCards.SelectedItem;
        var selectedItem = ContainerAt(window, 2);
        var firstItem = ContainerAt(window, 0);
        var middleItem = ContainerAt(window, 1);
        var requiredOneRowWidth =
            firstItem.ActualWidth + middleItem.ActualWidth + selectedItem.ActualWidth;
        var requiredTwoCardWidth = firstItem.ActualWidth + middleItem.ActualWidth;
        var wideExpectedWrapped =
            window.DeviceCards.ActualWidth + 1 < requiredOneRowWidth;
        Require(
            window.DeviceCards.ActualWidth + 1 >= requiredTwoCardWidth,
            $"the shown desktop could not fit two device cards " +
            $"(available={window.DeviceCards.ActualWidth}, required={requiredTwoCardWidth})");
        var initialActualWidth = window.ActualWidth;
        var initialActualHeight = window.ActualHeight;
        var initialViewportHeight = window.ContentScroll.ViewportHeight;
        _ = selectedItem.Focus();
        selectedItem.BringIntoView();
        window.UpdateLayout();
        RequireShownResizeState(
            window,
            selectedItem,
            selectedDevice,
            expectWrapped: wideExpectedWrapped,
            "wide resize baseline");

        window.Width = window.MinWidth;
        window.Height = window.MinHeight;
        window.UpdateLayout();
        SettleResizeVisibility(window);
        Require(
            initialActualWidth - window.ActualWidth >= 50 &&
            initialViewportHeight - window.ContentScroll.ViewportHeight >= 50,
            $"minimum resize did not materially change the shown window " +
            $"(width {initialActualWidth}->{window.ActualWidth}, viewport height " +
            $"{initialViewportHeight}->{window.ContentScroll.ViewportHeight})");
        RequireShownResizeState(
            window,
            selectedItem,
            selectedDevice,
            expectWrapped: true,
            "minimum wrapped resize");

        window.Width = initialActualWidth;
        window.Height = initialActualHeight;
        window.UpdateLayout();
        SettleResizeVisibility(window);
        RequireShownResizeState(
            window,
            selectedItem,
            selectedDevice,
            expectWrapped: wideExpectedWrapped,
            "restored wide resize");
        Require(
            Math.Abs(window.ActualWidth - initialActualWidth) <= 1 &&
            Math.Abs(
                window.ContentScroll.ViewportHeight - initialViewportHeight) <= 1,
            "restored resize did not return to the initial shown dimensions");

        window.Width = window.MinWidth;
        window.Height = window.MinHeight;
        window.UpdateLayout();
        Require(window.RefreshButton.Focus(),
            "resize focus-race acceptance could not move focus to Refresh");
        SettleResizeVisibility(window);
        Require(
            ReferenceEquals(
                Keyboard.FocusedElement,
                window.RefreshButton) &&
            ReferenceEquals(
                window.DeviceCards.SelectedItem,
                selectedDevice) &&
            window.DeviceCards.SelectedIndex == 2,
            "a queued resize visibility update reclaimed focus from Refresh or changed selection");

        Require(selectedItem.Focus(),
            "resize item-focus race could not refocus the selected card");
        window.Width = 1120;
        window.Height = 760;
        window.UpdateLayout();
        var nonselectedItem = ContainerAt(window, 0);
        Require(nonselectedItem.Focus() &&
            window.DeviceCards.SelectedIndex == 2,
            "resize item-focus race could not focus a nonselected card without changing selection");
        SettleResizeVisibility(window);
        Require(
            ReferenceEquals(Keyboard.FocusedElement, nonselectedItem) &&
            ReferenceEquals(
                window.DeviceCards.SelectedItem,
                selectedDevice) &&
            window.DeviceCards.SelectedIndex == 2,
            "a queued resize visibility update reclaimed focus from a nonselected card or changed selection");
        Keyboard.ClearFocus();
        window.ContentScroll.ScrollToTop();
        window.Hide();
        window.UpdateLayout();
        return 1;
    }

    private static void SettleResizeVisibility(MainWindow window)
    {
        window.Dispatcher.Invoke(
            DispatcherPriority.ApplicationIdle,
            new Action(static () => { }));
        window.UpdateLayout();
    }

    private static void RequireShownResizeState(
        MainWindow window,
        ListBoxItem selectedItem,
        LoaderDeviceCard selectedDevice,
        bool expectWrapped,
        string label)
    {
        var firstItem = ContainerAt(window, 0);
        var middleItem = ContainerAt(window, 1);
        var lastItem = ContainerAt(window, 2);
        var firstOrigin = firstItem.TranslatePoint(
            new Point(0, 0),
            window.DeviceCards);
        var middleOrigin = middleItem.TranslatePoint(
            new Point(0, 0),
            window.DeviceCards);
        var lastOrigin = lastItem.TranslatePoint(
            new Point(0, 0),
            window.DeviceCards);
        var hasExpectedGeometry = expectWrapped
            ? Math.Abs(firstOrigin.Y - middleOrigin.Y) <= 1 &&
              lastOrigin.Y > firstOrigin.Y + 1 &&
              middleOrigin.X > firstOrigin.X + 1 &&
              Math.Abs(lastOrigin.X - firstOrigin.X) <= 1
            : Math.Abs(firstOrigin.Y - middleOrigin.Y) <= 1 &&
              Math.Abs(firstOrigin.Y - lastOrigin.Y) <= 1 &&
              middleOrigin.X > firstOrigin.X + 1 &&
              lastOrigin.X > middleOrigin.X + 1;
        var itemOrigin = selectedItem.TranslatePoint(
            new Point(0, 0),
            window.ContentScroll);
        var itemBounds = new Rect(
            itemOrigin,
            selectedItem.RenderSize);
        var viewportBounds = new Rect(
            new Point(0, 0),
            new Size(
                window.ContentScroll.ViewportWidth,
                window.ContentScroll.ViewportHeight));
        var listPeer = new ListBoxAutomationPeer(window.DeviceCards);
        var itemPeers = listPeer.GetChildren();
        Require(
            hasExpectedGeometry &&
            ReferenceEquals(window.DeviceCards.SelectedItem, selectedDevice) &&
            window.DeviceCards.SelectedIndex == 2 &&
            ReferenceEquals(ContainerAt(window, 2), selectedItem) &&
            ReferenceEquals(Keyboard.FocusedElement, selectedItem) &&
            itemBounds.IntersectsWith(viewportBounds) &&
            window.ContentScroll.ViewportWidth > 0 &&
            window.ContentScroll.ViewportHeight > 0 &&
            double.IsFinite(window.ContentScroll.VerticalOffset) &&
            window.ContentScroll.VerticalOffset >= 0 &&
            window.ContentScroll.VerticalOffset <=
                window.ContentScroll.ScrollableHeight + 0.5 &&
            window.SelectFirmwareButton.IsEnabled &&
            !window.FlashSelectedButton.IsEnabled &&
            window.SelectionText.Text.Contains(
                selectedDevice.DisplayName,
                StringComparison.Ordinal) &&
            itemPeers is { Count: 3 } &&
            string.Equals(
                itemPeers[2].GetName(),
                selectedDevice.AccessibleSummary,
                StringComparison.Ordinal),
            $"{label} did not preserve wrap geometry, focus, reachability, automation, and safe state " +
            $"(geometry={hasExpectedGeometry}, selected={ReferenceEquals(window.DeviceCards.SelectedItem, selectedDevice)}, " +
            $"index={window.DeviceCards.SelectedIndex}, container={ReferenceEquals(ContainerAt(window, 2), selectedItem)}, " +
            $"focus={ReferenceEquals(Keyboard.FocusedElement, selectedItem)}, focusedType={Keyboard.FocusedElement?.GetType().Name ?? "null"}, " +
            $"bounds={itemBounds}, viewport={viewportBounds}, " +
            $"intersects={itemBounds.IntersectsWith(viewportBounds)}, offset={window.ContentScroll.VerticalOffset}, " +
            $"scrollable={window.ContentScroll.ScrollableHeight})");
    }

    private static void RequireThemeState(
        App app,
        MainWindow window,
        ListBoxItem selectedItem,
        LoaderDeviceCard selectedDevice,
        double expectedOffset,
        string expectedMode,
        Color expectedFocusColor)
    {
        var listPeer = new ListBoxAutomationPeer(window.DeviceCards);
        var itemPeers = listPeer.GetChildren();
        Require(
            string.Equals(
                app.Resources[LoaderThemeResources.ThemeModeResourceKey]
                    as string,
                expectedMode,
                StringComparison.Ordinal) &&
            BrushColor(
                app.Resources["FocusBrush"] as Brush,
                $"{expectedMode} focus brush") == expectedFocusColor &&
            ReferenceEquals(window.DeviceCards.SelectedItem, selectedDevice) &&
            ReferenceEquals(Keyboard.FocusedElement, selectedItem) &&
            Math.Abs(window.ContentScroll.VerticalOffset - expectedOffset) < 0.5 &&
            window.SelectFirmwareButton.IsEnabled &&
            !window.FlashSelectedButton.IsEnabled &&
            window.SummaryText.Text.Contains(
                "0 ready to flash",
                StringComparison.Ordinal) &&
            itemPeers is { Count: 3 } &&
            string.Equals(
                itemPeers[2].GetName(),
                selectedDevice.AccessibleSummary,
                StringComparison.Ordinal),
            $"{expectedMode} transition did not preserve focus, selection, scroll, automation, and safe state");
    }

    private static int AcceptAutomationSemantics()
    {
        var window = new MainWindow(_ => Task.FromResult(CreateDocument()));
        try
        {
            window.Show();
            window.UpdateLayout();

            var windowPeer = new WindowAutomationPeer(window);
            Require(
                windowPeer.GetAutomationControlType() == AutomationControlType.Window &&
                string.Equals(
                    windowPeer.GetAutomationId(),
                    "device-utility-window",
                    StringComparison.Ordinal) &&
                string.Equals(
                    windowPeer.GetName(),
                    ProductIdentity.Current.WindowTitle,
                    StringComparison.Ordinal),
                "the production window did not expose its current accessible identity");

            var refreshPeer = new ButtonAutomationPeer(window.RefreshButton);
            Require(
                refreshPeer.GetAutomationControlType() == AutomationControlType.Button &&
                string.Equals(
                    refreshPeer.GetAutomationId(),
                    "refresh-devices",
                    StringComparison.Ordinal) &&
                string.Equals(
                    refreshPeer.GetName(),
                    "Refresh connected devices",
                    StringComparison.Ordinal) &&
                refreshPeer.GetPattern(PatternInterface.Invoke) is IInvokeProvider,
                "Refresh did not expose the expected accessible button/invoke contract");

            var listPeer = new ListBoxAutomationPeer(window.DeviceCards);
            Require(
                listPeer.GetAutomationControlType() == AutomationControlType.List &&
                string.Equals(
                    listPeer.GetAutomationId(),
                    "connected-device-candidates",
                    StringComparison.Ordinal) &&
                string.Equals(
                    listPeer.GetName(),
                    "Connected device candidates",
                    StringComparison.Ordinal) &&
                listPeer.GetPattern(PatternInterface.Selection) is ISelectionProvider selectionProvider &&
                !selectionProvider.CanSelectMultiple &&
                !selectionProvider.IsSelectionRequired,
                "the connected-device surface did not expose single optional selection");

            var firstDevice = (LoaderDeviceCard)window.DeviceCards.Items[0];
            var listChildren = listPeer.GetChildren();
            Require(listChildren is { Count: 3 },
                "the connected-device list did not expose three item peers");
            var firstCardPeer = listChildren![0];
            var selectionItem = firstCardPeer.GetPattern(
                PatternInterface.SelectionItem) as ISelectionItemProvider;
            Require(
                firstCardPeer.GetAutomationControlType() == AutomationControlType.ListItem &&
                firstCardPeer.GetPositionInSet() == 1 &&
                firstCardPeer.GetSizeOfSet() == 3 &&
                string.Equals(
                    firstCardPeer.GetName(),
                    firstDevice.AccessibleSummary,
                    StringComparison.Ordinal) &&
                string.Equals(
                    firstCardPeer.GetHelpText(),
                    firstDevice.FlashHelpText,
                    StringComparison.Ordinal) &&
                selectionItem is not null,
                "the connected-device item did not expose its validated summary, blockers, and selection pattern");
            Require(
                !firstCardPeer.GetName().Contains(
                    firstDevice.Candidate,
                    StringComparison.Ordinal) &&
                !firstCardPeer.GetHelpText().Contains(
                    firstDevice.Candidate,
                    StringComparison.Ordinal),
                "the connected-device item exposed its internal candidate ordinal");
            selectionItem!.Select();
            Require(
                window.DeviceCards.SelectedIndex == 0 &&
                window.SelectFirmwareButton.IsEnabled,
                "accessible item selection did not reach the bounded current-device workflow");

            var selectBundlePeer = new ButtonAutomationPeer(
                window.SelectFirmwareButton);
            Require(
                selectBundlePeer.IsEnabled() &&
                string.Equals(
                    selectBundlePeer.GetAutomationId(),
                    "select-firmware-bundle",
                    StringComparison.Ordinal) &&
                string.Equals(
                    selectBundlePeer.GetName(),
                    "Select firmware bundle",
                    StringComparison.Ordinal) &&
                selectBundlePeer.GetPattern(PatternInterface.Invoke) is IInvokeProvider,
                "bounded bundle selection did not expose its stable accessible button contract");

            RequireLiveRegion(
                window.SummaryText,
                AutomationLiveSetting.Polite,
                window.SummaryText.Text,
                "inspection-summary",
                "inspection summary");
            RequireLiveRegion(
                window.NoticeText,
                AutomationLiveSetting.Off,
                window.NoticeText.Text,
                "safety-notice",
                "safety notice");
            RequireLiveRegion(
                window.SelectionText,
                AutomationLiveSetting.Polite,
                window.SelectionText.Text,
                "device-selection-status",
                "device selection status");
            RequireLiveRegion(
                window.BundleSummaryText,
                AutomationLiveSetting.Polite,
                window.BundleSummaryText.Text,
                "bundle-summary",
                "bundle status");

            const string acceptanceError =
                "Acceptance-only inspection error message";
            window.ErrorText.Text = acceptanceError;
            RequireLiveRegion(
                window.ErrorText,
                AutomationLiveSetting.Assertive,
                acceptanceError,
                "inspection-error",
                "inspection error");

            var disabledFlashPeer = new ButtonAutomationPeer(
                window.FlashSelectedButton);
            Require(
                !disabledFlashPeer.IsEnabled() &&
                string.Equals(
                    disabledFlashPeer.GetAutomationId(),
                    "flash-selected-device",
                    StringComparison.Ordinal) &&
                string.Equals(
                    disabledFlashPeer.GetName(),
                    "Flash selected device",
                    StringComparison.Ordinal) &&
                !string.IsNullOrWhiteSpace(disabledFlashPeer.GetHelpText()),
                "the disabled Flash action did not expose its name and blocker help");
            return 1;
        }
        finally
        {
            window.Close();
        }
    }

    private static int AcceptAutomationScrollReachability()
    {
        var window = new MainWindow(_ => Task.FromResult(CreateDocument()))
        {
            WindowStartupLocation = WindowStartupLocation.Manual,
            Left = SystemParameters.VirtualScreenLeft,
            Top = SystemParameters.VirtualScreenTop,
            Width = 900,
            Height = 620,
        };
        try
        {
            window.Show();
            window.UpdateLayout();
            window.ContentScroll.ScrollToTop();
            window.UpdateLayout();
            Require(window.RefreshButton.Focus(),
                "automation scrolling could not establish Refresh focus");

            var listPeer = new ListBoxAutomationPeer(window.DeviceCards);
            var itemPeers = listPeer.GetChildren();
            Require(itemPeers is { Count: 3 },
                "automation scrolling did not expose three device-item peers");
            var lastPeer = itemPeers![2];
            var scrollItemProvider = lastPeer.GetPattern(
                PatternInterface.ScrollItem) as IScrollItemProvider;
            Require(scrollItemProvider is not null,
                "the final device item did not expose its advertised ScrollItem provider");

            var lastDevice = (LoaderDeviceCard)window.DeviceCards.Items[2];
            var lastItem = ContainerAt(window, 2);
            var expectedName = lastPeer.GetName();
            var expectedHelp = lastPeer.GetHelpText();
            var initialOffset = window.ContentScroll.VerticalOffset;
            var initialBounds = BoundsInViewport(window, lastItem);
            var viewport = ContentViewport(window);
            Require(
                initialOffset <= 0.5 &&
                !initialBounds.IntersectsWith(viewport) &&
                lastPeer.IsOffscreen() &&
                window.DeviceCards.SelectedItem is null &&
                ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton) &&
                !window.SelectFirmwareButton.IsEnabled &&
                !window.FlashSelectedButton.IsEnabled,
                "automation scrolling did not establish an offscreen, unselected, fail-closed item");

            scrollItemProvider!.ScrollIntoView();
            window.Dispatcher.Invoke(
                DispatcherPriority.ApplicationIdle,
                new Action(static () => { }));
            window.UpdateLayout();

            var settledBounds = BoundsInViewport(window, lastItem);
            Require(
                window.ContentScroll.VerticalOffset > initialOffset + 0.5 &&
                window.ContentScroll.VerticalOffset <=
                    window.ContentScroll.ScrollableHeight + 0.5 &&
                HasPositiveAreaOverlap(
                    settledBounds,
                    ContentViewport(window)) &&
                !lastPeer.IsOffscreen() &&
                window.DeviceCards.SelectedItem is null &&
                ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton) &&
                !window.SelectFirmwareButton.IsEnabled &&
                !window.FlashSelectedButton.IsEnabled &&
                window.SummaryText.Text.Contains(
                    "0 ready to flash",
                    StringComparison.Ordinal) &&
                string.Equals(lastPeer.GetName(), expectedName, StringComparison.Ordinal) &&
                string.Equals(lastPeer.GetHelpText(), expectedHelp, StringComparison.Ordinal) &&
                string.Equals(expectedName, lastDevice.AccessibleSummary, StringComparison.Ordinal) &&
                string.Equals(expectedHelp, lastDevice.FlashHelpText, StringComparison.Ordinal),
                "ScrollItem did not reveal the final card through the outer viewport without changing focus, selection, metadata, or authority");
            return 1;
        }
        finally
        {
            window.Close();
        }
    }

    private static Rect BoundsInViewport(MainWindow window, FrameworkElement element)
    {
        return new Rect(
            element.TranslatePoint(new Point(0, 0), window.ContentScroll),
            element.RenderSize);
    }

    private static Rect ContentViewport(MainWindow window)
    {
        return new Rect(
            new Point(0, 0),
            new Size(
                window.ContentScroll.ViewportWidth,
                window.ContentScroll.ViewportHeight));
    }

    private static bool HasPositiveAreaOverlap(Rect first, Rect second)
    {
        var overlap = Rect.Intersect(first, second);
        return !overlap.IsEmpty && overlap.Width > 1 && overlap.Height > 1;
    }

    private static void RequireLiveRegion(
        TextBlock element,
        AutomationLiveSetting expectedSetting,
        string expectedName,
        string expectedAutomationId,
        string label)
    {
        var peer = new TextBlockAutomationPeer(element);
        Require(
            AutomationProperties.GetLiveSetting(element) == expectedSetting &&
            string.Equals(
                peer.GetAutomationId(),
                expectedAutomationId,
                StringComparison.Ordinal) &&
            string.Equals(peer.GetName(), expectedName, StringComparison.Ordinal),
            $"the {label} live region did not expose its current visible message");
    }

    private static int AcceptKeyboardNavigation()
    {
        var refreshCalls = 0;
        var failNextRefresh = false;
        var window = new MainWindow(_ =>
        {
            refreshCalls++;
            if (failNextRefresh)
            {
                failNextRefresh = false;
                return Task.FromException<LoaderInspectionDocument>(
                    new InvalidOperationException(
                        "acceptance-only refresh failure"));
            }
            return Task.FromResult(CreateDocument());
        });
        try
        {
            window.Show();
            window.UpdateLayout();
            Require(window.DeviceCards.Items.Count == 3,
                "keyboard acceptance window did not publish three cards");

            window.DeviceCards.SelectedIndex = 0;
            Require(window.SelectFirmwareButton.IsEnabled,
                "keyboard acceptance could not enable the bounded bundle action");
            Require(window.RefreshButton.Focus(),
                "keyboard acceptance could not focus Refresh");
            Require(ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton),
                "Refresh did not own keyboard focus");

            Require(window.RefreshButton.MoveFocus(
                    new TraversalRequest(FocusNavigationDirection.Next)),
                "Tab traversal could not leave Refresh");
            Require(IsWithin(Keyboard.FocusedElement as DependencyObject, window.DeviceCards),
                "Tab traversal did not enter the connected-device list after Refresh");

            RequireSelectedItem(window, 0, "initial keyboard selection");
            RaiseRoutedKey(FocusedListItem(window), Key.Right);
            RequireSelectedItem(window, 1, "wide-layout Right Arrow");
            RaiseRoutedKey(FocusedListItem(window), Key.Left);
            RequireSelectedItem(window, 0, "wide-layout Left Arrow");
            RaiseRoutedKey(FocusedListItem(window), Key.End);
            RequireSelectedItem(window, 2, "wide-layout End");
            RaiseRoutedKey(FocusedListItem(window), Key.Home);
            RequireSelectedItem(window, 0, "wide-layout Home");

            window.Width = window.MinWidth;
            window.Height = window.MinHeight;
            window.UpdateLayout();
            var firstItem = ContainerAt(window, 0);
            var lastItem = ContainerAt(window, 2);
            Require(
                lastItem.TranslatePoint(new Point(0, 0), window.DeviceCards).Y >
                firstItem.TranslatePoint(new Point(0, 0), window.DeviceCards).Y,
                "minimum-width keyboard acceptance did not produce a wrapped final card");

            RaiseRoutedKey(FocusedListItem(window), Key.Down);
            RequireSelectedItem(window, 2, "wrapped-layout Down Arrow");
            RaiseRoutedKey(FocusedListItem(window), Key.Up);
            RequireSelectedItem(window, 0, "wrapped-layout Up Arrow");
            RaiseRoutedKey(FocusedListItem(window), Key.End);
            RequireSelectedItem(window, 2, "wrapped-layout End");
            RaiseRoutedKey(FocusedListItem(window), Key.Home);
            RequireSelectedItem(window, 0, "wrapped-layout Home");

            var listFocus = Keyboard.FocusedElement as UIElement;
            Require(listFocus is not null && listFocus.MoveFocus(
                    new TraversalRequest(FocusNavigationDirection.Next)),
                "Tab traversal could not leave the connected-device list");
            Require(ReferenceEquals(Keyboard.FocusedElement, window.SelectFirmwareButton),
                $"the enabled bundle action did not follow the device list in Tab order " +
                $"(focused={Keyboard.FocusedElement?.GetType().Name})");

            Require(window.SelectFirmwareButton.MoveFocus(
                    new TraversalRequest(FocusNavigationDirection.Next)),
                "cycle traversal could not leave the bundle action");
            Require(ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton),
                "disabled Flash actions were not skipped before focus cycled to Refresh");

            RaiseRoutedKey(FocusedListItem(window), Key.End);
            RequireSelectedItem(window, 2, "focused-card F5 setup");
            var refreshCallsBeforeF5 = refreshCalls;
            RaiseRoutedKey(FocusedListItem(window), Key.F5);
            Require(
                refreshCalls == refreshCallsBeforeF5 + 1 &&
                window.DeviceCards.Items.Count == 3 &&
                window.DeviceCards.SelectedItem is null &&
                !window.SelectFirmwareButton.IsEnabled &&
                window.RefreshButton.IsEnabled &&
                string.Equals(
                    window.RefreshButton.Content as string,
                    "Refresh devices",
                    StringComparison.Ordinal) &&
                string.Equals(
                    window.BundleSummaryText.Text,
                    "No firmware bundle selected",
                    StringComparison.Ordinal) &&
                ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton),
                "focused-card F5 did not complete refresh and hand focus to the safe Refresh action");
            Require(window.RefreshButton.MoveFocus(
                    new TraversalRequest(FocusNavigationDirection.Next)) &&
                IsWithin(Keyboard.FocusedElement as DependencyObject, window.DeviceCards),
                "focus traversal could not reenter the fresh device list after focused-card F5");

            window.DeviceCards.SelectedIndex = 2;
            var failureFocus = ContainerAt(window, 2);
            _ = failureFocus.Focus();
            failNextRefresh = true;
            var refreshCallsBeforeFailure = refreshCalls;
            RaiseRoutedKey(failureFocus, Key.F5);
            Require(
                refreshCalls == refreshCallsBeforeFailure + 1 &&
                window.DeviceCards.Items.Count == 0 &&
                window.ErrorBanner.Visibility == Visibility.Visible &&
                string.Equals(
                    window.SummaryText.Text,
                    "Inspection unavailable",
                    StringComparison.Ordinal) &&
                !window.SelectFirmwareButton.IsEnabled &&
                !window.FlashSelectedButton.IsEnabled &&
                window.RefreshButton.IsEnabled &&
                ReferenceEquals(Keyboard.FocusedElement, window.RefreshButton),
                "failed focused-card F5 did not restore Refresh focus and the safe unavailable state");
            return 1;
        }
        finally
        {
            window.Close();
        }
    }

    private static ListBoxItem ContainerAt(MainWindow window, int index)
    {
        window.DeviceCards.UpdateLayout();
        return window.DeviceCards.ItemContainerGenerator.ContainerFromIndex(index)
            as ListBoxItem ?? throw new InvalidOperationException(
                $"device item container {index} was not generated");
    }

    private static ListBoxItem FocusedListItem(MainWindow window)
    {
        var focused = Keyboard.FocusedElement as DependencyObject;
        while (focused is not null && focused is not ListBoxItem)
        {
            focused = focused is Visual or System.Windows.Media.Media3D.Visual3D
                ? VisualTreeHelper.GetParent(focused)
                : LogicalTreeHelper.GetParent(focused);
        }
        return focused as ListBoxItem ?? ContainerAt(
            window,
            window.DeviceCards.SelectedIndex);
    }

    private static void RaiseRoutedKey(UIElement element, Key key)
    {
        var source = PresentationSource.FromVisual(element) ??
            throw new InvalidOperationException(
                $"{key} routed input had no presentation source");
        var preview = new KeyEventArgs(
            Keyboard.PrimaryDevice,
            source,
            Environment.TickCount,
            key)
        {
            RoutedEvent = Keyboard.PreviewKeyDownEvent,
        };
        element.RaiseEvent(preview);
        if (!preview.Handled)
        {
            var bubble = new KeyEventArgs(
                Keyboard.PrimaryDevice,
                source,
                Environment.TickCount,
                key)
            {
                RoutedEvent = Keyboard.KeyDownEvent,
            };
            element.RaiseEvent(bubble);
        }
    }

    private static void RequireSelectedItem(
        MainWindow window,
        int expectedIndex,
        string label)
    {
        var item = ContainerAt(window, expectedIndex);
        var device = (LoaderDeviceCard)window.DeviceCards.Items[expectedIndex];
        Require(
            window.DeviceCards.SelectedIndex == expectedIndex &&
            ReferenceEquals(Keyboard.FocusedElement, item) &&
            window.SelectFirmwareButton.IsEnabled &&
            window.SelectionText.Text.Contains(
                device.DisplayName,
                StringComparison.Ordinal),
            $"{label} did not keep focus, selection, and bounded bundle state synchronized");
    }

    private static bool IsWithin(DependencyObject? element, DependencyObject ancestor)
    {
        for (var current = element; current is not null;)
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }
            current = current is Visual or System.Windows.Media.Media3D.Visual3D
                ? VisualTreeHelper.GetParent(current)
                : LogicalTreeHelper.GetParent(current);
        }
        return false;
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
