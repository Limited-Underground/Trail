using System.Diagnostics;
using System.IO;
using System.Windows;
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
        Run("theme round trip", ThemeRoundTrip);
        Run("native offer schema and round viewport", NativeOfferSchemaAndRoundViewport);
        Run("native stale and held input", NativeStaleAndHeldInput);
        Run("native malformed and EOF fail closed", NativeMalformedAndEofFailClosed);
        Run("two independent native LCD windows", TwoIndependentNativeLcdWindows);
        Run("host chrome remains outside LCD", HostChromeRemainsOutsideLcd);
        Run("deterministic circular LCD scales", DeterministicCircularLcdScales);
        Run("core presenter copy remains bridge-local", CorePresenterCopyRemainsBridgeLocal);
        Run("request completion sends exactly once", RequestCompletionSendsExactlyOnce);
        Run("snapshot during blocked start reaches native UI", SnapshotDuringBlockedStartReachesNativeUi);
        Run("snapshot refresh burst is bounded and touch remains live", SnapshotRefreshBurstIsBoundedAndTouchRemainsLive);
        Run("dual LCD native request crosses local bridge", DualLcdNativeRequestCrossesLocalBridge);
        Run("second native session failure rolls startup back", SecondNativeSessionFailureRollsStartupBack);
        Console.WriteLine($"OpenTrail simulator UI tests passed: {passed}/13");
        return 0;
    }

    private static string NativePath =>
        Environment.GetEnvironmentVariable("OPENTRAIL_PORTABLE_UI_HOST")
        ?? throw new InvalidOperationException("Native UI host path missing.");

    private static void ThemeRoundTrip()
    {
        var resources = new ResourceDictionary();
        SimulatorThemeResources.Apply(resources, SimulatorThemeResources.CreateClassicPalette());
        var original = ((SolidColorBrush)resources["PanelBrush"]).Color;
        SimulatorThemeResources.Apply(resources, SimulatorThemeResources.CreateDeterministicHighContrastPalette());
        SimulatorThemeResources.Apply(resources, SimulatorThemeResources.CreateClassicPalette());
        Require(((SolidColorBrush)resources["PanelBrush"]).Color == original, "Classic palette did not restore.");
    }

    private static void NativeOfferSchemaAndRoundViewport()
    {
        using var owner = new SessionOwner();
        var start = owner.Session.StartAsync(PortableUiSnapshotState.Initial, CancellationToken.None).GetAwaiter().GetResult();
        var offer = start.Offer ?? throw new InvalidOperationException("START did not offer a frame.");
        Require(offer.Screen == 0 && offer.Actions.Select(item => item.Action).SequenceEqual([1, 21, 2, 3]),
            "Native Home action order diverged.");
        Require(offer.ViewportShape == 1 && offer.LogicalWidth == 466 && offer.LogicalHeight == 466,
            "Native round logical profile diverged.");
        foreach (var action in offer.Primitives.Where(item => item.Kind == 4))
        {
            foreach (var point in new[] { (action.X, action.Y), (action.X + action.Width, action.Y),
                         (action.X, action.Y + action.Height), (action.X + action.Width, action.Y + action.Height) })
            {
                var dx = point.Item1 - 233;
                var dy = point.Item2 - 233;
                Require(dx * dx + dy * dy <= 233 * 233, "An action corner leaves the circular viewport.");
            }
            Require(action.Width >= 64 && action.Height >= 64, "An action target is below the frozen logical minimum.");
        }
        var committed = owner.Session.PresentedAsync(offer.Generation, offer.Revision, CancellationToken.None).GetAwaiter().GetResult();
        Require(committed.Kind == "COMMITTED", "Rendered native Home was not committed.");
        var disconnectedCenter = owner.Session.InputAsync(
            offer.Generation, offer.Revision, 1, PortableUiGesture.Activate,
            CancellationToken.None).GetAwaiter().GetResult().Offer!;
        Require(disconnectedCenter.Actions[2].Action == 24 &&
            !disconnectedCenter.Actions[2].Enabled,
            "Epoch-zero message center did not preserve disabled Compose authority.");
        owner.Session.PresentedAsync(
            disconnectedCenter.Generation, disconnectedCenter.Revision,
            CancellationToken.None).GetAwaiter().GetResult();
        var connectedCenter = owner.Session.RefreshAsync(
            disconnectedCenter.Generation, disconnectedCenter.Revision,
            ConnectedMessageState(), CancellationToken.None)
            .GetAwaiter().GetResult().Offer!;
        Require(connectedCenter.Actions[2] == new PortableUiActionBinding(24, true),
            "A new connected epoch did not re-enable firmware-owned Compose authority.");
    }

    private static void NativeStaleAndHeldInput()
    {
        using var owner = new SessionOwner();
        var home = owner.Session.StartAsync(ConnectedMessageState(), CancellationToken.None).GetAwaiter().GetResult().Offer!;
        owner.Session.PresentedAsync(home.Generation, home.Revision, CancellationToken.None).GetAwaiter().GetResult();
        var critical = owner.Session.InputAsync(home.Generation, home.Revision, 3, PortableUiGesture.Activate, CancellationToken.None).GetAwaiter().GetResult().Offer!;
        owner.Session.PresentedAsync(critical.Generation, critical.Revision, CancellationToken.None).GetAwaiter().GetResult();
        var stale = owner.Session.InputAsync(critical.Generation, home.Revision, 0, PortableUiGesture.Hold, CancellationToken.None).GetAwaiter().GetResult();
        Require(stale.Kind == "REJECT", "Stale native input was accepted.");
        var tap = owner.Session.InputAsync(critical.Generation, critical.Revision, 0, PortableUiGesture.Activate, CancellationToken.None).GetAwaiter().GetResult();
        Require(tap.Kind == "REJECT", "Critical confirmation accepted a tap.");
        var held = owner.Session.InputAsync(critical.Generation, critical.Revision, 0, PortableUiGesture.Hold, CancellationToken.None).GetAwaiter().GetResult().Offer!;
        var request = owner.Session.PresentedAsync(held.Generation, held.Revision, CancellationToken.None).GetAwaiter().GetResult();
        Require(request.RequestKind == 5 && request.RequestId == 1, "Held critical request lost typed correlation.");
    }

    private static void NativeMalformedAndEofFailClosed()
    {
        var process = Process.Start(new ProcessStartInfo
        {
            FileName = NativePath,
            UseShellExecute = false,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            CreateNoWindow = true,
        }) ?? throw new InvalidOperationException("Native host did not start.");
        process.StandardInput.WriteLine("START|99");
        Require(process.StandardOutput.ReadLine() == "REJECT|2|VERSION", "Unknown protocol version was accepted.");
        process.StandardInput.WriteLine("INPUT|2|1|1|9|A");
        Require(process.StandardOutput.ReadLine() == "REJECT|2|SCHEMA", "Out-of-range slot was accepted.");
        process.StandardInput.WriteLine($"START|2|{ConnectedMessageState().Serialize()}");
        var offerLine = process.StandardOutput.ReadLine() ?? throw new InvalidOperationException("START response missing.");
        var parsedOffer = PortableUiProtocolParser.Parse(offerLine).Offer!;
        process.StandardInput.WriteLine("INPUT|2|99|99|0|A");
        Require(process.StandardOutput.ReadLine() == "REJECT|2|PRECONDITION",
            "Stale command was accepted while an offer was pending.");
        process.StandardInput.WriteLine($"NOT_READY|2|{parsedOffer.Generation}|{parsedOffer.Revision}");
        Require(process.StandardOutput.ReadLine() == offerLine,
            "A stale command destroyed the exact pending NOT_READY retry.");

        var corruptCircle = offerLine.Split('|');
        var primitiveStart = PrimitiveStart(corruptCircle);
        corruptCircle[primitiveStart + 13 + 1] = "0";
        ExpectParserReject(string.Join('|', corruptCircle),
            "Out-of-circle native heading was accepted.");
        var corruptKind = offerLine.Split('|');
        corruptKind[primitiveStart + 13 + 5] = "1";
        ExpectParserReject(string.Join('|', corruptKind),
            "A text primitive accepted a metric token.");
        var corruptMetric = offerLine.Split('|');
        corruptMetric[primitiveStart + 5 * 13 + 11] = "9";
        ExpectParserReject(string.Join('|', corruptMetric),
            "A metric value diverging from the frame header was accepted.");
        var corruptAction = offerLine.Split('|');
        var firstAction = primitiveStart + parsedOffer.Primitives
            .Select((primitive, primitiveIndex) => (primitive, primitiveIndex))
            .First(item => item.primitive.Kind == 4).primitiveIndex * 13;
        corruptAction[firstAction + 4] = "1";
        ExpectParserReject(string.Join('|', corruptAction),
            "An undersized native action target was accepted.");
        var corruptNotice = offerLine.Split('|');
        corruptNotice[6] = "28";
        ExpectParserReject(string.Join('|', corruptNotice),
            "Unknown native notice 28 was accepted.");

        ExpectParserReject(offerLine.Replace("OFFER|2|", "OFFER|1|", StringComparison.Ordinal),
            "Protocol v1 OFFER was accepted.");

        process.StandardInput.WriteLine(
            $"PRESENTED|2|{parsedOffer.Generation}|{parsedOffer.Revision}");
        _ = PortableUiProtocolParser.Parse(process.StandardOutput.ReadLine() ?? string.Empty);
        process.StandardInput.WriteLine(
            $"INPUT|2|{parsedOffer.Generation}|{parsedOffer.Revision}|1|A");
        var centerLine = process.StandardOutput.ReadLine() ?? string.Empty;
        var center = PortableUiProtocolParser.Parse(centerLine).Offer!;
        var corruptCenter = centerLine.Split('|');
        corruptCenter[ActionCountIndex(corruptCenter) + 2] = "0";
        ExpectParserReject(string.Join('|', corruptCenter),
            "Message center accepted a disabled canonical Inbox action.");
        process.StandardInput.WriteLine(
            $"PRESENTED|2|{center.Generation}|{center.Revision}");
        _ = PortableUiProtocolParser.Parse(process.StandardOutput.ReadLine() ?? string.Empty);
        process.StandardInput.WriteLine(
            $"INPUT|2|{center.Generation}|{center.Revision}|0|A");
        var listLine = process.StandardOutput.ReadLine() ?? string.Empty;
        var list = PortableUiProtocolParser.Parse(listLine).Offer!;
        var corruptList = listLine.Split('|');
        corruptList[ActionCountIndex(corruptList) + 1] = "24";
        ExpectParserReject(string.Join('|', corruptList),
            "Message list accepted a noncanonical first-row action.");

        process.StandardInput.WriteLine(
            $"PRESENTED|2|{list.Generation}|{list.Revision}");
        _ = PortableUiProtocolParser.Parse(process.StandardOutput.ReadLine() ?? string.Empty);
        process.StandardInput.WriteLine(
            $"INPUT|2|{list.Generation}|{list.Revision}|{list.Actions.Count - 1}|A");
        var secondCenterLine = process.StandardOutput.ReadLine() ?? string.Empty;
        var secondCenter = PortableUiProtocolParser.Parse(secondCenterLine).Offer!;
        process.StandardInput.WriteLine(
            $"PRESENTED|2|{secondCenter.Generation}|{secondCenter.Revision}");
        _ = PortableUiProtocolParser.Parse(process.StandardOutput.ReadLine() ?? string.Empty);
        process.StandardInput.WriteLine(
            $"INPUT|2|{secondCenter.Generation}|{secondCenter.Revision}|2|A");
        var composeLine = process.StandardOutput.ReadLine() ?? string.Empty;
        var compose = PortableUiProtocolParser.Parse(composeLine).Offer!;
        process.StandardInput.WriteLine(
            $"PRESENTED|2|{compose.Generation}|{compose.Revision}");
        _ = PortableUiProtocolParser.Parse(process.StandardOutput.ReadLine() ?? string.Empty);
        process.StandardInput.WriteLine(
            $"INPUT|2|{compose.Generation}|{compose.Revision}|0|A");
        var confirmationLine = process.StandardOutput.ReadLine() ?? string.Empty;
        var confirmation = PortableUiProtocolParser.Parse(confirmationLine).Offer!;
        var corruptSend = confirmationLine.Split('|');
        corruptSend[ActionCountIndex(corruptSend) + 2] = "0";
        var confirmationPrimitiveStart = PrimitiveStart(corruptSend);
        var sendPrimitiveIndex = confirmation.Primitives
            .Select((primitive, primitiveIndex) => (primitive, primitiveIndex))
            .First(item => item.primitive.Kind == 4 &&
                item.primitive.ActionSlot == 0).primitiveIndex;
        corruptSend[confirmationPrimitiveStart + sendPrimitiveIndex * 13 + 8] = "0";
        var disabledConfirmation = PortableUiProtocolParser.Parse(
            string.Join('|', corruptSend)).Offer!;
        Require(!disabledConfirmation.Actions[0].Enabled,
            "Message confirmation did not preserve a firmware-disabled Send action.");

        var tamperedTemplateText = PortableUiProtocolParser.Parse(
            $"COMMITTED|2|{confirmation.Generation}|{confirmation.Revision}|2|42|10|7|1|0|11|436865636B696E67206978");
        try
        {
            NativePortableUiSession.ValidateCommittedRequestEvidence(
                confirmation, tamperedTemplateText);
            throw new InvalidOperationException(
                "A tampered template request bypassed displayed-text correlation.");
        }
        catch (InvalidOperationException error) when (
            error.Message.Contains("exact displayed template", StringComparison.Ordinal)) { }

        var tamperedTemplateId = PortableUiProtocolParser.Parse(
            $"COMMITTED|2|{confirmation.Generation}|{confirmation.Revision}|2|42|10|7|2|0|11|436865636B696E6720696E");
        try
        {
            NativePortableUiSession.ValidateCommittedRequestEvidence(
                confirmation, tamperedTemplateId);
            throw new InvalidOperationException(
                "A tampered template ID bypassed displayed-confirmation correlation.");
        }
        catch (InvalidOperationException error) when (
            error.Message.Contains("exact displayed template", StringComparison.Ordinal)) { }

        process.StandardInput.WriteLine(new string('X', 4097));
        Require(process.StandardOutput.ReadLine() == "REJECT|2|COMMAND_TOO_LONG",
            "Oversized native command was accepted.");
        process.StandardInput.Close();
        Require(process.WaitForExit(2000) && process.ExitCode == 0, "EOF did not close the owned native host cleanly.");
        process.Dispose();
        try { PortableUiProtocolParser.Parse("OFFER|2|1|1"); throw new InvalidOperationException("Partial offer was accepted."); }
        catch (InvalidOperationException error) when (error.Message.Contains("exact public schema", StringComparison.Ordinal)) { }

        using var rejectedPresentation = new SessionOwner();
        var pending = rejectedPresentation.Session.StartAsync(
            PortableUiSnapshotState.Initial, CancellationToken.None).GetAwaiter().GetResult().Offer!;
        try
        {
            rejectedPresentation.Session.PresentedAsync(
                pending.Generation, pending.Revision + 1, CancellationToken.None)
                .GetAwaiter().GetResult();
            throw new InvalidOperationException("Rejected PRESENTED response was accepted.");
        }
        catch (InvalidOperationException error) when (
            error.Message.Contains("command contract", StringComparison.Ordinal)) { }
    }

    private static void ExpectParserReject(string line, string failure)
    {
        try { _ = PortableUiProtocolParser.Parse(line); throw new InvalidOperationException(failure); }
        catch (InvalidOperationException error) when (
            error.Message.Contains("exact public schema", StringComparison.Ordinal)) { }
    }

    private static int ActionCountIndex(string[] fields)
    {
        var index = 15;
        var ownedCount = int.Parse(fields[index++]);
        index += ownedCount * 4;
        index += 3;
        var rowCount = int.Parse(fields[index++]);
        index += rowCount * 5;
        index += 7;
        return index;
    }

    private static int PrimitiveStart(string[] fields)
    {
        var index = ActionCountIndex(fields);
        var actionCount = int.Parse(fields[index++]);
        index += actionCount * 2;
        index += 4;
        return index;
    }

    private static void TwoIndependentNativeLcdWindows()
    {
        var a = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var b = new FakePresenter(Snapshot("Client B", "Simulated device B"));
        var windowA = new VirtualLcdWindow(a, new NativePortableUiSession(NativePath));
        var windowB = new VirtualLcdWindow(b, new NativePortableUiSession(NativePath));
        ShowOffscreen(windowA); ShowOffscreen(windowB);
        PumpUntil(() => windowA.CurrentOffer is not null && windowB.CurrentOffer is not null);
        Require(!ReferenceEquals(windowA.Presenter, windowB.Presenter), "The LCDs share a presenter.");
        Require(windowA.CurrentOffer!.Generation == 1 && windowB.CurrentOffer!.Generation == 1,
            "Either native session failed independent startup.");
        windowA.Close(); PumpUntil(() => !windowA.IsVisible);
        Require(windowB.IsVisible && b.DisposeCount == 0, "Closing Client A changed Client B.");
        windowB.Close(); PumpUntil(() => !windowB.IsVisible);
    }

    private static void HostChromeRemainsOutsideLcd()
    {
        var presenter = new FakePresenter(Snapshot("Client A", "Simulated device A"));
        var window = new VirtualLcdWindow(presenter, new NativePortableUiSession(NativePath));
        ShowOffscreen(window); PumpUntil(() => window.CurrentOffer is not null);
        Require(!IsDescendant(window.LcdCanvas, window.ConnectButton), "Host Connect was placed inside the firmware LCD.");
        Require(window.ActionButtons.Count == window.CurrentOffer!.Actions.Count, "WPF did not draw exact native action slots.");
        Require(window.LcdCanvas.Clip is EllipseGeometry ellipse && ellipse.RadiusX == 233,
            "WPF did not apply the native circular viewport.");
        window.Close(); PumpUntil(() => !window.IsVisible);
    }

    private static void DeterministicCircularLcdScales()
    {
        var simulator = OpenTrail.Simulator.Core.LocalLoopbackSimulator.Create();
        var presenter = new CoreSimulatorClientPresenter(
            simulator.Bridge, OpenTrail.Simulator.Core.SimulatorClientId.A,
            Dispatcher.CurrentDispatcher);
        presenter.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
        var window = new VirtualLcdWindow(
            presenter, new NativePortableUiSession(NativePath));
        try
        {
            ShowOffscreen(window); PumpUntil(() => window.CurrentOffer is not null);
            Require(window.DeviceChoiceCombo.Visibility == Visibility.Collapsed &&
                window.AssignedDeviceChoiceBorder.Visibility == Visibility.Visible &&
                !string.IsNullOrWhiteSpace(window.AssignedDeviceChoiceText.Text),
                "Connected host chrome did not expose a readable assigned device choice.");
            foreach (var scale in new[] { 1.0, 1.25, 1.5, 2.0 })
            {
                var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(
                    (int)Math.Ceiling(window.ActualWidth * scale),
                    (int)Math.Ceiling(window.ActualHeight * scale), 96 * scale, 96 * scale,
                    PixelFormats.Pbgra32);
                bitmap.Render(window);
                Require(bitmap.PixelWidth > 0 && bitmap.PixelHeight > 0, "Scaled circular render was empty.");
            }
            SaveRenderedWindow(window, "opentrail-native-portable-home.png");

            window.ActionButtons[1].RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntil(() => window.CurrentOffer?.Screen == 7);
            window.ActionButtons[2].RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntil(() => window.CurrentOffer?.Screen == 10);
            SaveRenderedWindow(window, "opentrail-native-portable-compose.png");
        }
        finally
        {
            window.Close();
            PumpUntil(() => !window.IsVisible);
            simulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
        }
    }

    private static void CorePresenterCopyRemainsBridgeLocal()
    {
        var simulator = OpenTrail.Simulator.Core.LocalLoopbackSimulator.Create();
        var presenter = new CoreSimulatorClientPresenter(simulator.Bridge,
            OpenTrail.Simulator.Core.SimulatorClientId.A, Dispatcher.CurrentDispatcher);
        try
        {
            presenter.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
            presenter.QueueQuickStatusAsync(
                1, presenter.Snapshot.ConnectedSessionEpoch,
                CancellationToken.None).GetAwaiter().GetResult();
            Require(presenter.Snapshot.Messages.Any(item =>
                item.Kind == SimulatorUiMessageKind.QuickStatus),
                "Typed quick status did not reach bridge.");
        }
        finally
        {
            presenter.DisposeAsync().AsTask().GetAwaiter().GetResult();
            simulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
        }
    }

    private static void RequestCompletionSendsExactlyOnce()
    {
        var presenter = new FakePresenter(Snapshot(
            "Client A", "Simulated device A", connectedEpoch: 42));
        var session = new CompletionEchoSession();
        var window = new VirtualLcdWindow(presenter, session);
        var emitted = PortableUiProtocolParser.Parse(
            "COMMITTED|2|1|1|2|7|5|42|0|0|0|-");
        Require(emitted.Disposition == 2, "Request-emitted disposition was lost.");
        window.DispatchCommittedRequestForTestAsync(emitted).GetAwaiter().GetResult();
        var completion = PortableUiProtocolParser.Parse(
            "COMMITTED|2|1|2|1|7|5|42|0|0|0|-");
        window.DispatchCommittedRequestForTestAsync(completion).GetAwaiter().GetResult();
        Require(presenter.CriticalAlertCount == 1,
            "One critical firmware request did not produce exactly one bridge send.");
        Require(session.CompleteCount == 1,
            "The exact request was not completed exactly once.");
    }

    private static void SnapshotDuringBlockedStartReachesNativeUi()
    {
        var presenter = new FakePresenter(Snapshot(
            "Client A", "Simulated device A", connectedEpoch: 42));
        var session = new BlockingRefreshSession(NativePath);
        session.BlockNextStart();
        var window = new VirtualLcdWindow(presenter, session);
        try
        {
            ShowOffscreen(window);
            PumpUntil(() => session.BlockedStartCount == 1);
            presenter.Publish(Snapshot(
                "Client A", "Simulated device A", connectedEpoch: 42,
                messages: [UiMessage(1, "Arrived during startup")]));
            PumpDispatcher();
            Require(session.RefreshCount == 0,
                "A snapshot bypassed the in-flight native START command.");

            session.ReleaseStart();
            PumpUntil(() => session.CompletedRefreshCount == 1);
            Require(session.LastRefreshState?.Messages.Single().Text == "Arrived during startup",
                "The latest snapshot was lost while native START was in flight.");
        }
        finally
        {
            window.Close();
            PumpUntil(() => !window.IsVisible);
        }
    }

    private static void SnapshotRefreshBurstIsBoundedAndTouchRemainsLive()
    {
        var presenter = new FakePresenter(Snapshot(
            "Client A", "Simulated device A", connectedEpoch: 42));
        var session = new BlockingRefreshSession(NativePath);
        var window = new VirtualLcdWindow(presenter, session);
        try
        {
            ShowOffscreen(window);
            PumpUntil(() => window.CurrentOffer is not null);
            for (var index = 0; index < 20; ++index)
                presenter.Publish(presenter.Snapshot);
            PumpDispatcher();
            Require(session.RefreshCount == 0,
                "Semantically identical snapshot events reached native REFRESH.");

            var staleMessagesButton = window.ActionButtons[1];
            session.BlockNextRefresh();
            presenter.Publish(Snapshot(
                "Client A", "Simulated device A", connectedEpoch: 42,
                messages: [UiMessage(1, "First burst state")]));
            PumpUntil(() => session.BlockedRefreshCount == 1);

            for (var sequence = 2; sequence <= 20; ++sequence)
            {
                presenter.Publish(Snapshot(
                    "Client A", "Simulated device A", connectedEpoch: 42,
                    messages: [UiMessage(sequence, $"Burst state {sequence}")]));
            }
            staleMessagesButton.RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            staleMessagesButton.RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            session.ReleaseRefresh();
            PumpUntil(() => session.CompletedRefreshCount == 2 &&
                session.LastCompletedRefreshOffer is { } lastOffer &&
                window.CurrentOffer?.Generation == lastOffer.Generation &&
                window.CurrentOffer?.Revision == lastOffer.Revision);

            Require(session.RefreshCount == 2,
                "A snapshot burst queued more than one latest pending native refresh.");
            Require(session.InputCount == 0,
                "An action captured from a replaced native frame was replayed.");
            Require(session.LastRefreshState?.Messages.Single().Text == "Burst state 20",
                "The coalesced refresh did not retain the latest firmware snapshot.");

            var liveMessagesButton = window.ActionButtons[1];
            liveMessagesButton.RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            liveMessagesButton.RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntil(() => window.CurrentOffer?.Screen == 7);
            Require(session.InputCount == 1,
                "Duplicate touch input created more than one pending native command.");
            window.ActionButtons[2].RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntil(() => window.CurrentOffer?.Screen == 10);
            Require(session.InputCount == 2,
                "Current native actions did not remain live after the bounded refresh burst.");

            var priorBlockedRefreshes = session.BlockedRefreshCount;
            session.BlockNextRefresh();
            presenter.Publish(Snapshot(
                "Client A", "Simulated device A", connectedEpoch: 42,
                messages: [UiMessage(21, "Close active refresh")]));
            PumpUntil(() => session.BlockedRefreshCount == priorBlockedRefreshes + 1);
            presenter.Publish(Snapshot(
                "Client A", "Simulated device A", connectedEpoch: 42,
                messages: [UiMessage(22, "Close pending refresh")]));
            window.ActionButtons[0].RaiseEvent(new RoutedEventArgs(
                System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
        }
        finally
        {
            window.Close();
            PumpUntil(() => !window.IsVisible);
            Require(session.DisposeCount == 1,
                "Close did not dispose the blocked native session exactly once.");
            Require(session.InputCount == 2,
                "Close allowed a pending interactive command to reach native INPUT.");
        }
    }

    private static void DualLcdNativeRequestCrossesLocalBridge()
    {
        var simulator = OpenTrail.Simulator.Core.LocalLoopbackSimulator.Create();
        static Task NoAutomaticService(CancellationToken token)
        {
            token.ThrowIfCancellationRequested();
            return Task.CompletedTask;
        }
        var a = new CoreSimulatorClientPresenter(
            simulator.Bridge, OpenTrail.Simulator.Core.SimulatorClientId.A,
            Dispatcher.CurrentDispatcher, NoAutomaticService, TimeSpan.FromSeconds(10));
        var b = new CoreSimulatorClientPresenter(
            simulator.Bridge, OpenTrail.Simulator.Core.SimulatorClientId.B,
            Dispatcher.CurrentDispatcher, NoAutomaticService, TimeSpan.FromSeconds(10));
        var windowA = new VirtualLcdWindow(a, new NativePortableUiSession(NativePath));
        var windowB = new VirtualLcdWindow(b, new NativePortableUiSession(NativePath));
        try
        {
            a.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
            b.ConnectAsync(CancellationToken.None).GetAwaiter().GetResult();
            ShowOffscreen(windowA);
            ShowOffscreen(windowB);
            PumpUntil(() => windowA.CurrentOffer is not null && windowB.CurrentOffer is not null);

            windowA.ActionButtons[1].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowA, 7);
            windowA.ActionButtons[2].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowA, 10);
            windowA.ActionButtons[0].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowA, 11);
            windowA.ActionButtons[0].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowA, 8);
            simulator.Bridge.ServiceAsync(OpenTrail.Simulator.Core.SimulatorClientId.A)
                .AsTask().GetAwaiter().GetResult();
            simulator.Bridge.ServiceAsync(OpenTrail.Simulator.Core.SimulatorClientId.B)
                .AsTask().GetAwaiter().GetResult();
            PumpUntil(() => b.Snapshot.Messages.Any(message =>
                message.Direction == SimulatorUiMessageDirection.Inbound &&
                message.Kind == SimulatorUiMessageKind.Chat &&
                message.Text == "Checking in"));
            PumpUntil(() => windowB.InteractiveCommandReadyForTest);

            windowB.ActionButtons[1].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowB, 7);
            windowB.ActionButtons[0].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowB, 8);
            var inboxOffer = windowB.CurrentOffer!;
            Require(inboxOffer.Messages.ListKind == 1,
                $"Client B native inbox list kind was {inboxOffer.Messages.ListKind}, not 1.");
            Require(inboxOffer.Messages.Rows.Count > 0 &&
                    inboxOffer.Messages.Rows[0].TextIndex == 0,
                "Client B native inbox did not present row 0 with owned text index 0.");
            Require(inboxOffer.OwnedTexts.Count > 0 &&
                    inboxOffer.OwnedTexts[0].Text == "NEW: Checking in",
                $"Client B native inbox row text was '{inboxOffer.OwnedTexts.FirstOrDefault()?.Text ?? "<missing>"}'.");
            Require(inboxOffer.Actions.Count > 0 &&
                    inboxOffer.Actions[0] == new PortableUiActionBinding(25, true) &&
                    windowB.ActionButtons[0].IsEnabled,
                "Client B native inbox row 0 action was not the enabled action 25.");
            windowB.ActionButtons[0].RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));
            PumpUntilInteractiveReady(windowB, 9);
            Require(windowB.CurrentOffer!.OwnedTexts.Any(text => text.Text == "Checking in"),
                "Client B did not render the exact native-owned inbound message text.");
            SaveRenderedWindow(windowA, "opentrail-client-a-after-send.png");
            SaveRenderedWindow(windowB, "opentrail-client-b-inbound-detail.png");

            Require(a.Snapshot.Messages.Any(message =>
                    message.DeliveryState == SimulatorUiDeliveryState.BridgeAccepted) &&
                b.Snapshot.Messages.Any(message =>
                    message.DeliveryState == SimulatorUiDeliveryState.BridgeObserved),
                "Local bridge E2E did not preserve distinct bridge-only evidence states.");
        }
        finally
        {
            windowA.Close();
            windowB.Close();
            PumpUntil(() => !windowA.IsVisible && !windowB.IsVisible);
            simulator.Bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
        }
    }

    private static void SecondNativeSessionFailureRollsStartupBack()
    {
        var first = new CompletionEchoSession();
        var calls = 0;
        try
        {
            _ = SimulatorStartupComposition.Create(
                Dispatcher.CurrentDispatcher,
                () => ++calls == 1
                    ? first
                    : throw new InvalidOperationException("Synthetic second-session failure."));
            throw new InvalidOperationException("Second-session construction failure was ignored.");
        }
        catch (InvalidOperationException error) when (
            error.Message.Contains("Synthetic second-session", StringComparison.Ordinal)) { }
        Require(first.DisposeCount == 1,
            "First native session was not disposed during staged startup rollback.");
    }

    private static void ShowOffscreen(Window window)
    {
        window.Left = -20000; window.Top = -20000; window.ShowInTaskbar = false; window.Show(); PumpDispatcher();
    }

    private static void SaveRenderedWindow(Window window, string fileName)
    {
        var renderDirectory = Environment.GetEnvironmentVariable(
            "OPENTRAIL_SIMULATOR_RENDER_DIR");
        if (string.IsNullOrWhiteSpace(renderDirectory)) return;
        Directory.CreateDirectory(renderDirectory);
        var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(
            (int)Math.Ceiling(window.ActualWidth),
            (int)Math.Ceiling(window.ActualHeight), 96, 96,
            PixelFormats.Pbgra32);
        bitmap.Render(window);
        var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
        encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
        using var output = File.Create(Path.Combine(renderDirectory, fileName));
        encoder.Save(output);
    }

    private static bool IsDescendant(DependencyObject ancestor, DependencyObject candidate)
    {
        for (DependencyObject? current = candidate; current is not null; current = VisualTreeHelper.GetParent(current))
            if (ReferenceEquals(current, ancestor)) return true;
        return false;
    }

    private static SimulatorUiSnapshot Snapshot(
        string client, string device, long connectedEpoch = 0,
        IReadOnlyList<SimulatorUiMessage>? messages = null) => new(
        client, device, "local loopback", SimulatorUiConnectionState.Disconnected,
        "No observation", messages ?? [], [], connectedEpoch, 0, 32, null);

    private static SimulatorUiMessage UiMessage(long sequence, string text) => new(
        sequence,
        SimulatorUiMessageDirection.Inbound,
        SimulatorUiMessageKind.Chat,
        SimulatorUiMessagePriority.Normal,
        text,
        SimulatorUiDeliveryState.BridgeObserved,
        DateTimeOffset.UnixEpoch,
        false);

    private static PortableUiSnapshotState ConnectedMessageState() =>
        PortableUiSnapshotState.Initial with
        {
            BridgeSessionEpoch = 7,
            Messages =
            [
                new(1, 0, 2, 2, 2, true, false, false, "Need help"),
                new(2, 1, 0, 0, 1, false, false, false, "Checking in"),
            ],
        };

    private static void PumpDispatcher()
    {
        var frame = new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() => frame.Continue = false));
        Dispatcher.PushFrame(frame);
    }

    private static void PumpUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!condition()) { if (DateTime.UtcNow >= deadline) throw new TimeoutException(); PumpDispatcher(); Thread.Sleep(10); }
    }

    private static void PumpUntilInteractiveReady(VirtualLcdWindow window, int screen) =>
        PumpUntil(() => window.CurrentOffer?.Screen == screen && window.InteractiveCommandReadyForTest);

    private static void Run(string name, Action test) { test(); ++passed; Console.WriteLine($"PASS {name}"); }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }

    private sealed class SessionOwner : IDisposable
    {
        internal NativePortableUiSession Session { get; } = new(NativePath);
        public void Dispose() => Session.DisposeAsync().AsTask().GetAwaiter().GetResult();
    }

    private sealed class FakePresenter : ISimulatorClientPresenter
    {
        internal FakePresenter(SimulatorUiSnapshot snapshot) => Snapshot = snapshot;
        public event EventHandler? SnapshotChanged;
        public SimulatorUiSnapshot Snapshot { get; private set; }
        public IReadOnlyList<SimulatorUiDeviceChoice> DeviceChoices => [];
        public SimulatorUiConnectionSource ConnectionSource =>
            SimulatorUiConnectionSource.LocalSimulation;
        internal int DisposeCount { get; private set; }
        public Task RefreshDeviceChoicesAsync(CancellationToken token) => Task.CompletedTask;
        public Task SelectDeviceAsync(SimulatorUiDeviceChoice choice, CancellationToken token) => Task.CompletedTask;
        public Task ForgetDeviceAsync(CancellationToken token) => Task.CompletedTask;
        public Task ConnectAsync(CancellationToken token) => Task.CompletedTask;
        public Task DisconnectAsync(CancellationToken token) => Task.CompletedTask;
        public Task ReconnectAsync(CancellationToken token) => Task.CompletedTask;
        public Task QueueMessageAsync(string text, bool high, CancellationToken token) => Task.CompletedTask;
        public Task<SimulatorUiCommandAdmission> QueueMessageTemplateAsync(
            int templateId, string exactText, long expectedEpoch,
            CancellationToken token) => Task.FromResult(new SimulatorUiCommandAdmission(expectedEpoch, 1));
        public Task<SimulatorUiCommandAdmission> QueueQuickStatusAsync(
            int kind, long expectedEpoch, CancellationToken token) =>
            Task.FromResult(new SimulatorUiCommandAdmission(expectedEpoch, 1));
        public Task<SimulatorUiCommandAdmission> QueueCriticalAlertAsync(
            long expectedEpoch, CancellationToken token)
        {
            ++CriticalAlertCount;
            return Task.FromResult(new SimulatorUiCommandAdmission(expectedEpoch, 1));
        }
        internal int CriticalAlertCount { get; private set; }
        public Task<SimulatorUiCommandAdmission> AcknowledgeAlertAsync(
            long sequence, long expectedEpoch, CancellationToken token) =>
            Task.FromResult(new SimulatorUiCommandAdmission(expectedEpoch, 1));
        public ValueTask DisposeAsync() { ++DisposeCount; return ValueTask.CompletedTask; }
        internal void Publish(SimulatorUiSnapshot snapshot) { Snapshot = snapshot; SnapshotChanged?.Invoke(this, EventArgs.Empty); }
    }

    private sealed class CompletionEchoSession : IPortableUiSession
    {
        internal int CompleteCount { get; private set; }
        internal int DisposeCount { get; private set; }
        public Task<PortableUiProtocolResult> CompleteAsync(
            uint generation, uint revision, uint requestId, int requestKind,
            bool succeeded, ulong appliedEpoch, ulong appliedSequence,
            int templateId, ulong targetSequence, PortableUiSnapshotState state,
            CancellationToken token)
        {
            ++CompleteCount;
            return Task.FromResult(new PortableUiProtocolResult(
                "IDLE", generation, revision, 0, 0, null, null));
        }
        public Task<PortableUiProtocolResult> StartAsync(PortableUiSnapshotState state, CancellationToken token) => throw new NotSupportedException();
        public Task<PortableUiProtocolResult> InputAsync(uint generation, uint revision, int slot, PortableUiGesture gesture, CancellationToken token) => throw new NotSupportedException();
        public Task<PortableUiProtocolResult> RefreshAsync(uint generation, uint revision, PortableUiSnapshotState state, CancellationToken token) => throw new NotSupportedException();
        public Task<PortableUiProtocolResult> PresentedAsync(uint generation, uint revision, CancellationToken token) => throw new NotSupportedException();
        public Task<PortableUiProtocolResult> NotReadyAsync(uint generation, uint revision, CancellationToken token) => throw new NotSupportedException();
        public Task<PortableUiProtocolResult> RenderFailedAsync(uint generation, uint revision, CancellationToken token) => throw new NotSupportedException();
        public ValueTask DisposeAsync() { ++DisposeCount; return ValueTask.CompletedTask; }
    }

    private sealed class BlockingRefreshSession : IPortableUiSession
    {
        private readonly NativePortableUiSession inner;
        private TaskCompletionSource<bool>? refreshEntered;
        private TaskCompletionSource<bool>? refreshRelease;
        private TaskCompletionSource<bool>? startEntered;
        private TaskCompletionSource<bool>? startRelease;
        private int refreshCount;
        private int completedRefreshCount;
        private int blockedRefreshCount;
        private int inputCount;
        private int blockedStartCount;
        private int disposeCount;
        private int blockNextRefresh;
        private int blockNextStart;

        internal BlockingRefreshSession(string executablePath) =>
            inner = new NativePortableUiSession(executablePath);

        internal int RefreshCount => Volatile.Read(ref refreshCount);
        internal int CompletedRefreshCount => Volatile.Read(ref completedRefreshCount);
        internal int BlockedRefreshCount => Volatile.Read(ref blockedRefreshCount);
        internal int InputCount => Volatile.Read(ref inputCount);
        internal int BlockedStartCount => Volatile.Read(ref blockedStartCount);
        internal int DisposeCount => Volatile.Read(ref disposeCount);
        internal PortableUiSnapshotState? LastRefreshState { get; private set; }
        internal PortableUiOffer? LastCompletedRefreshOffer { get; private set; }

        internal void BlockNextRefresh()
        {
            refreshEntered = new(TaskCreationOptions.RunContinuationsAsynchronously);
            refreshRelease = new(TaskCreationOptions.RunContinuationsAsynchronously);
            Interlocked.Exchange(ref blockNextRefresh, 1);
        }

        internal void ReleaseRefresh() => refreshRelease?.TrySetResult(true);

        internal void BlockNextStart()
        {
            startEntered = new(TaskCreationOptions.RunContinuationsAsynchronously);
            startRelease = new(TaskCreationOptions.RunContinuationsAsynchronously);
            Interlocked.Exchange(ref blockNextStart, 1);
        }

        internal void ReleaseStart() => startRelease?.TrySetResult(true);

        public async Task<PortableUiProtocolResult> StartAsync(
            PortableUiSnapshotState state,
            CancellationToken token)
        {
            var entered = startEntered;
            var release = startRelease;
            if (entered is not null && release is not null &&
                Interlocked.Exchange(ref blockNextStart, 0) == 1)
            {
                Interlocked.Increment(ref blockedStartCount);
                entered.TrySetResult(true);
                await release.Task.WaitAsync(token);
            }
            return await inner.StartAsync(state, token);
        }

        public async Task<PortableUiProtocolResult> InputAsync(
            uint generation,
            uint revision,
            int slot,
            PortableUiGesture gesture,
            CancellationToken token)
        {
            Interlocked.Increment(ref inputCount);
            return await inner.InputAsync(generation, revision, slot, gesture, token);
        }

        public async Task<PortableUiProtocolResult> RefreshAsync(
            uint generation,
            uint revision,
            PortableUiSnapshotState state,
            CancellationToken token)
        {
            Interlocked.Increment(ref refreshCount);
            LastRefreshState = state;
            var entered = refreshEntered;
            var release = refreshRelease;
            if (entered is not null && release is not null &&
                Interlocked.Exchange(ref blockNextRefresh, 0) == 1)
            {
                Interlocked.Increment(ref blockedRefreshCount);
                entered.TrySetResult(true);
                await release.Task.WaitAsync(token);
            }
            var result = await inner.RefreshAsync(generation, revision, state, token);
            LastCompletedRefreshOffer = result.Offer;
            Interlocked.Increment(ref completedRefreshCount);
            return result;
        }

        public Task<PortableUiProtocolResult> CompleteAsync(
            uint generation,
            uint revision,
            uint requestId,
            int requestKind,
            bool succeeded,
            ulong appliedBridgeSessionEpoch,
            ulong appliedMessageSequence,
            int requestTemplateId,
            ulong requestMessageSequence,
            PortableUiSnapshotState state,
            CancellationToken token) => inner.CompleteAsync(
                generation, revision, requestId, requestKind, succeeded,
                appliedBridgeSessionEpoch, appliedMessageSequence,
                requestTemplateId, requestMessageSequence, state, token);

        public Task<PortableUiProtocolResult> PresentedAsync(
            uint generation,
            uint revision,
            CancellationToken token) => inner.PresentedAsync(generation, revision, token);

        public Task<PortableUiProtocolResult> NotReadyAsync(
            uint generation,
            uint revision,
            CancellationToken token) => inner.NotReadyAsync(generation, revision, token);

        public Task<PortableUiProtocolResult> RenderFailedAsync(
            uint generation,
            uint revision,
            CancellationToken token) => inner.RenderFailedAsync(generation, revision, token);

        public ValueTask DisposeAsync()
        {
            Interlocked.Increment(ref disposeCount);
            return inner.DisposeAsync();
        }
    }
}
