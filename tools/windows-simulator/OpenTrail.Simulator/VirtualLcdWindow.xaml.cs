using System.ComponentModel;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace OpenTrail.Simulator;

public partial class VirtualLcdWindow : Window
{
    private readonly ISimulatorClientPresenter presenter;
    private readonly IPortableUiSession uiSession;
    private readonly CancellationTokenSource lifetime = new();
    private readonly SemaphoreSlim commandGate = new(1, 1);
    private readonly SemaphoreSlim hostCommandGate = new(1, 1);
    private readonly object snapshotRefreshSync = new();
    private readonly List<Button> actionButtons = [];
    private PortableUiSnapshotState firmwareState = PortableUiSnapshotState.Initial;
    private PortableUiOffer? currentOffer;
    private PortableUiOffer? pendingRenderedOffer;
    private int notReadyAttempts;
    private volatile bool closeStarted;
    private volatile bool closeCommitted;
    private volatile bool firmwareSessionFaulted;
    private PendingSnapshotRefresh? pendingSnapshotRefresh;
    private ulong nextSnapshotRefreshGeneration;
    private bool snapshotRefreshDrainScheduled;
    private bool interactiveCommandPending;

    internal VirtualLcdWindow(
        ISimulatorClientPresenter presenter,
        IPortableUiSession uiSession)
    {
        this.presenter = presenter ?? throw new ArgumentNullException(nameof(presenter));
        this.uiSession = uiSession ?? throw new ArgumentNullException(nameof(uiSession));
        InitializeComponent();
        firmwareState = BuildFirmwareState(this.presenter.Snapshot, firmwareState);
        Title = $"OpenTrail Simulator — {presenter.Snapshot.ClientLabel}";
        AutomationProperties.SetName(this, $"{presenter.Snapshot.ClientLabel} virtual touchscreen simulator");
        this.presenter.SnapshotChanged += HandleSnapshotChanged;
        Loaded += HandleLoaded;
        RenderHostSnapshot(this.presenter.Snapshot);
    }

    internal ISimulatorClientPresenter Presenter => presenter;
    internal PortableUiOffer? CurrentOffer => currentOffer;
    internal IReadOnlyList<Button> ActionButtons => actionButtons;
    internal Task DispatchCommittedRequestForTestAsync(PortableUiProtocolResult result) =>
        DispatchCommittedRequestAsync(result);
    private sealed record RenderedAction(PortableUiPrimitive Primitive, uint Generation, uint Revision);
    private sealed record PendingSnapshotRefresh(ulong Generation, SimulatorUiSnapshot Snapshot);

    private async void HandleLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= HandleLoaded;
        await RunFirmwareCommandAsync(
            token => uiSession.StartAsync(firmwareState, token));
    }

    private void HandleSnapshotChanged(object? sender, EventArgs e)
    {
        if (closeStarted || firmwareSessionFaulted) return;
        lock (snapshotRefreshSync)
        {
            if (closeStarted || firmwareSessionFaulted) return;
            if (nextSnapshotRefreshGeneration == ulong.MaxValue)
            {
                pendingSnapshotRefresh = null;
                firmwareSessionFaulted = true;
                _ = Dispatcher.BeginInvoke(new Action(() =>
                    FailUi("The bounded native refresh generation was exhausted.")));
                return;
            }
            ++nextSnapshotRefreshGeneration;
            pendingSnapshotRefresh = new(nextSnapshotRefreshGeneration, presenter.Snapshot);
        }
        ScheduleSnapshotRefreshDrain();
    }

    private void ScheduleSnapshotRefreshDrain()
    {
        lock (snapshotRefreshSync)
        {
            if (closeStarted || firmwareSessionFaulted || pendingSnapshotRefresh is null ||
                currentOffer is null || snapshotRefreshDrainScheduled ||
                interactiveCommandPending) return;
            snapshotRefreshDrainScheduled = true;
        }
        _ = Dispatcher.BeginInvoke(DispatcherPriority.Background,
            new Action(DrainSnapshotRefreshesAsync));
    }

    private async void DrainSnapshotRefreshesAsync()
    {
        try
        {
            while (!closeStarted && !firmwareSessionFaulted)
            {
                PendingSnapshotRefresh nextRefresh;
                lock (snapshotRefreshSync)
                {
                    // START owns an immutable snapshot. Retain the latest event
                    // until that first offer commits so it cannot be consumed
                    // without ever reaching the native presenter.
                    if (interactiveCommandPending || pendingSnapshotRefresh is null ||
                        currentOffer is null) break;
                    nextRefresh = pendingSnapshotRefresh;
                    pendingSnapshotRefresh = null;
                }

                RenderHostSnapshot(nextRefresh.Snapshot);
                var nextFirmwareState = BuildFirmwareState(nextRefresh.Snapshot, firmwareState);
                if (FirmwareStateEquals(firmwareState, nextFirmwareState)) continue;
                firmwareState = nextFirmwareState;
                var offer = currentOffer;
                if (offer is not null)
                {
                    await RunFirmwareCommandAsync(token => uiSession.RefreshAsync(
                        offer.Generation, offer.Revision, nextFirmwareState, token));
                }
            }
        }
        catch (Exception)
        {
            firmwareSessionFaulted = true;
            FailUi("Native portable UI refresh failed; this LCD is disabled.");
        }
        finally
        {
            lock (snapshotRefreshSync) snapshotRefreshDrainScheduled = false;
            ScheduleSnapshotRefreshDrain();
        }
    }

    private static bool FirmwareStateEquals(
        PortableUiSnapshotState left,
        PortableUiSnapshotState right) =>
        string.Equals(left.Serialize(), right.Serialize(), StringComparison.Ordinal);

    private void RenderHostSnapshot(SimulatorUiSnapshot snapshot)
    {
        ClientNameText.Text = snapshot.ClientLabel;
        DeviceLabelText.Text = string.IsNullOrWhiteSpace(snapshot.PublicDeviceFamily)
            ? snapshot.PublicDeviceLabel
            : $"{snapshot.PublicDeviceLabel} · {snapshot.PublicDeviceFamily}";
        ConnectionStateText.Text = ConnectionCopy(snapshot.ConnectionState);
        ConnectionStateText.Foreground = ConnectionBrush(snapshot.ConnectionState);
        FreshnessText.Text = snapshot.FreshnessText;
        PublicBridgeErrorText.Text = snapshot.PublicError ?? string.Empty;
        PublicBridgeErrorText.Visibility = string.IsNullOrWhiteSpace(snapshot.PublicError)
            ? Visibility.Collapsed
            : Visibility.Visible;
        ConnectButton.IsEnabled = snapshot.ConnectionState is
            SimulatorUiConnectionState.Disconnected or SimulatorUiConnectionState.Faulted;
        DisconnectButton.IsEnabled = snapshot.ConnectionState is not SimulatorUiConnectionState.Disconnected;
        ReconnectButton.IsEnabled = snapshot.ConnectionState is not SimulatorUiConnectionState.Connecting;
        RenderDeviceChoices(snapshot);
    }

    private void RenderDeviceChoices(SimulatorUiSnapshot snapshot)
    {
        var choices = presenter.DeviceChoices;
        DeviceChoiceCombo.ItemsSource = choices;
        // A refresh creates a new opaque roster revision. Never carry a public
        // label match across it; identical labels must require explicit choice.
        DeviceChoiceCombo.SelectedItem = choices.FirstOrDefault(choice =>
            choice.IsAssignedToThisClient);

        var disconnected = snapshot.ConnectionState == SimulatorUiConnectionState.Disconnected;
        var assigned = choices.FirstOrDefault(choice => choice.IsAssignedToThisClient);
        AssignedDeviceChoiceText.Text = assigned?.DisplayText ?? snapshot.PublicDeviceLabel;
        DeviceChoiceCombo.Visibility = disconnected
            ? Visibility.Visible
            : Visibility.Collapsed;
        AssignedDeviceChoiceBorder.Visibility = disconnected
            ? Visibility.Collapsed
            : Visibility.Visible;
        RefreshChoicesButton.IsEnabled = disconnected;
        DeviceChoiceCombo.IsEnabled = disconnected;
        ForgetChoiceButton.IsEnabled = disconnected &&
            presenter.ConnectionSource != SimulatorUiConnectionSource.Unassigned;
        UpdateSelectedChoiceState(disconnected);

        switch (presenter.ConnectionSource)
        {
            case SimulatorUiConnectionSource.LocalSimulation:
                SourceBannerText.Text = "LOCAL LOOPBACK";
                SourceEvidenceText.Text =
                    "Native C++ portable UI model · local bridge-test evidence only";
                break;
            case SimulatorUiConnectionSource.UsbCandidate:
                SourceBannerText.Text = snapshot.ConnectionState == SimulatorUiConnectionState.Connected
                    ? "USB SESSION"
                    : "USB CANDIDATE";
                SourceEvidenceText.Text =
                    "USB bridge evidence only · no radio delivery or authenticated peer proof";
                break;
            default:
                SourceBannerText.Text = "UNASSIGNED";
                SourceEvidenceText.Text =
                    "Select one public device choice before connecting";
                break;
        }
    }

    private void UpdateSelectedChoiceState(bool? disconnectedOverride = null)
    {
        var selected = DeviceChoiceCombo.SelectedItem as SimulatorUiDeviceChoice;
        DeviceChoiceStatusText.Text = selected?.PublicStatus ??
            "No public device choice selected.";
        var disconnected = disconnectedOverride ??
            presenter.Snapshot.ConnectionState == SimulatorUiConnectionState.Disconnected;
        UseChoiceButton.IsEnabled = disconnected && selected is not null &&
            (selected.IsAvailable || selected.IsAssignedToThisClient);
    }

    private async Task RunFirmwareCommandAsync(
        Func<CancellationToken, Task<PortableUiProtocolResult>> command,
        RenderedAction? expectedAction = null)
    {
        if (closeStarted || firmwareSessionFaulted) return;
        var entered = false;
        try
        {
            await commandGate.WaitAsync(lifetime.Token);
            entered = true;
            if (closeStarted || firmwareSessionFaulted) return;
            if (expectedAction is not null &&
                (currentOffer is null ||
                 currentOffer.Generation != expectedAction.Generation ||
                 currentOffer.Revision != expectedAction.Revision))
            {
                await InvokeUiAsync(() => HostNoticeText.Text =
                    "Portable UI ignored an action from a replaced frame; activate the current control again.");
                return;
            }
            var result = await command(lifetime.Token).ConfigureAwait(false);
            await ProcessProtocolResultAsync(result).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception)
        {
            firmwareSessionFaulted = true;
            await InvokeUiAsync(() => FailUi("Native portable UI session failed; this LCD is disabled."));
        }
        finally
        {
            if (entered) commandGate.Release();
        }
    }

    private async Task RunInteractiveFirmwareCommandAsync(
        RenderedAction action,
        PortableUiGesture gesture)
    {
        lock (snapshotRefreshSync)
        {
            if (closeStarted || firmwareSessionFaulted) return;
            if (interactiveCommandPending)
            {
                HostNoticeText.Text =
                    "Portable UI ignored duplicate input while the current action is pending.";
                return;
            }
            interactiveCommandPending = true;
        }
        try
        {
            await RunFirmwareCommandAsync(
                token => uiSession.InputAsync(
                    action.Generation, action.Revision, action.Primitive.ActionSlot,
                    gesture, token),
                action);
        }
        finally
        {
            lock (snapshotRefreshSync) interactiveCommandPending = false;
            ScheduleSnapshotRefreshDrain();
        }
    }

    private async Task ProcessProtocolResultAsync(PortableUiProtocolResult result)
    {
        if (result.Kind == "REJECT")
        {
            await InvokeUiAsync(() => HostNoticeText.Text = "Portable UI rejected stale, invalid, or unavailable input.");
            return;
        }
        if (result.Kind == "IDLE") return;
        if (result.Offer is not null)
        {
            var renderState = RenderState.Invalid;
            await InvokeUiAsync(() => renderState = RenderOffer(result.Offer));
            if (renderState == RenderState.NotReady)
            {
                if (++notReadyAttempts > 2)
                {
                    _ = await uiSession.RenderFailedAsync(
                        result.Offer.Generation, result.Offer.Revision,
                        lifetime.Token).ConfigureAwait(false);
                    firmwareSessionFaulted = true;
                    await InvokeUiAsync(() => FailUi(
                        "The native frame could not reach a renderable window."));
                    return;
                }
                var retry = await uiSession.NotReadyAsync(
                    result.Offer.Generation, result.Offer.Revision, lifetime.Token).ConfigureAwait(false);
                await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Loaded).Task.ConfigureAwait(false);
                await ProcessProtocolResultAsync(retry).ConfigureAwait(false);
                return;
            }
            if (renderState != RenderState.Success)
            {
                _ = await uiSession.RenderFailedAsync(
                    result.Offer.Generation, result.Offer.Revision, lifetime.Token).ConfigureAwait(false);
                firmwareSessionFaulted = true;
                await InvokeUiAsync(() => FailUi("The exact native render plan could not be drawn."));
                return;
            }
            notReadyAttempts = 0;
            var committed = await uiSession.PresentedAsync(
                result.Offer.Generation, result.Offer.Revision, lifetime.Token).ConfigureAwait(false);
            await ProcessProtocolResultAsync(committed).ConfigureAwait(false);
            return;
        }
        if (result.Kind == "COMMITTED")
        {
            var committed = false;
            await InvokeUiAsync(() => committed = CommitRenderedOffer(
                result.Generation, result.Revision));
            if (!committed)
            {
                throw new InvalidOperationException(
                    "The native commit did not match the drawn frame.");
            }
        }
        await DispatchCommittedRequestAsync(result).ConfigureAwait(false);
    }

    private Task DispatchCommittedRequestAsync(PortableUiProtocolResult result)
    {
        return result.Kind == "COMMITTED" && result.Disposition == 2
            ? CompleteRequestAsync(result)
            : Task.CompletedTask;
    }

    private async Task CompleteRequestAsync(PortableUiProtocolResult request)
    {
        var succeeded = false;
        var admission = new SimulatorUiCommandAdmission(0, 0);
        try
        {
            if (request.RequestBridgeSessionEpoch > long.MaxValue ||
                request.RequestMessageSequence > long.MaxValue)
            {
                throw new InvalidOperationException(
                    "The portable UI request correlation exceeded the Windows boundary.");
            }
            var expectedEpoch = (long)request.RequestBridgeSessionEpoch;
            switch (request.RequestKind)
            {
                case >= 1 and <= 4:
                    admission = await presenter.QueueQuickStatusAsync(
                        request.RequestKind, expectedEpoch, lifetime.Token)
                        .ConfigureAwait(false);
                    succeeded = true;
                    break;
                case 5:
                    admission = await presenter.QueueCriticalAlertAsync(
                        expectedEpoch, lifetime.Token).ConfigureAwait(false);
                    succeeded = true;
                    break;
                case 6:
                    if (presenter.ConnectionSource == SimulatorUiConnectionSource.LocalSimulation)
                    {
                        firmwareState = firmwareState with { ArchiveState = 1 };
                        succeeded = true;
                    }
                    break;
                case 7:
                    if (presenter.ConnectionSource == SimulatorUiConnectionSource.LocalSimulation)
                    {
                        firmwareState = firmwareState with { ArchiveState = 0, ArchiveQueueCount = 0 };
                        succeeded = true;
                    }
                    break;
                case 8:
                    if (presenter.ConnectionSource == SimulatorUiConnectionSource.LocalSimulation)
                    {
                        firmwareState = firmwareState with { PositionState = 1 };
                        succeeded = true;
                    }
                    break;
                case 9:
                    if (presenter.ConnectionSource == SimulatorUiConnectionSource.LocalSimulation)
                    {
                        firmwareState = firmwareState with { PositionState = 0 };
                        succeeded = true;
                    }
                    break;
                case 10:
                    admission = await presenter.QueueMessageTemplateAsync(
                        request.RequestTemplateId, request.RequestText,
                        expectedEpoch, lifetime.Token).ConfigureAwait(false);
                    succeeded = true;
                    break;
                case 11:
                    admission = await presenter.AcknowledgeAlertAsync(
                        (long)request.RequestMessageSequence, expectedEpoch,
                        lifetime.Token).ConfigureAwait(false);
                    succeeded = true;
                    break;
            }
        }
        catch (Exception)
        {
            succeeded = false;
        }
        firmwareState = BuildFirmwareState(presenter.Snapshot, firmwareState);
        var completed = await uiSession.CompleteAsync(
            request.Generation, request.Revision, request.RequestId,
            request.RequestKind, succeeded,
            succeeded ? checked((ulong)admission.AppliedSessionEpoch) : 0,
            succeeded ? checked((ulong)admission.AppliedMessageSequence) : 0,
            request.RequestTemplateId, request.RequestMessageSequence,
            firmwareState, lifetime.Token).ConfigureAwait(false);
        await ProcessProtocolResultAsync(completed).ConfigureAwait(false);
    }

    private enum RenderState { Success, NotReady, Invalid }

    private RenderState RenderOffer(PortableUiOffer offer)
    {
        if (offer.LogicalWidth != 466 || offer.LogicalHeight != 466 ||
            offer.ViewportShape != 1 || offer.Actions.Count is < 0 or > 4) return RenderState.Invalid;
        if (!IsLoaded || PresentationSource.FromVisual(this) is null)
            return RenderState.NotReady;
        var candidateElements = new List<FrameworkElement>(offer.Primitives.Count);
        var candidateActions = new List<Button>(offer.Actions.Count);
        try
        {
            foreach (var primitive in offer.Primitives)
            {
                FrameworkElement element;
                if (primitive.Kind == 0)
                {
                    element = new Border { Background = (Brush)FindResource("LcdBrush") };
                }
                else if (primitive.Kind == 4)
                {
                    var actionCopy = PrimitiveCopy(offer, primitive);
                    var button = new Button
                    {
                        Content = new TextBlock
                        {
                            Text = actionCopy,
                            TextWrapping = TextWrapping.Wrap,
                            TextAlignment = TextAlignment.Center,
                        },
                        IsEnabled = false,
                        Tag = new RenderedAction(primitive, offer.Generation, offer.Revision),
                        FontSize = 15,
                    };
                    AutomationProperties.SetName(button, actionCopy);
                    AutomationProperties.SetHelpText(button,
                        primitive.RequiresHold ? "Press and hold to confirm" : "Activate portable UI action");
                    button.Click += HandleActionClick;
                    button.PreviewMouseLeftButtonDown += HandleHoldStarted;
                    button.PreviewMouseLeftButtonUp += HandleHoldReleased;
                    button.PreviewKeyDown += HandleHoldKeyDown;
                    button.PreviewKeyUp += HandleHoldKeyUp;
                    candidateActions.Add(button);
                    element = button;
                }
                else
                {
                    element = new TextBlock
                    {
                        Text = PrimitiveCopy(offer, primitive),
                        TextWrapping = TextWrapping.Wrap,
                        TextAlignment = TextAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center,
                        FontWeight = primitive.Style == 1 ? FontWeights.Bold : FontWeights.Normal,
                        FontSize = primitive.Style == 1 ? 22 : 14,
                        Foreground = BrushForStyle(primitive.Style),
                    };
                }
                element.Width = primitive.Width;
                element.Height = primitive.Height;
                Canvas.SetLeft(element, primitive.X);
                Canvas.SetTop(element, primitive.Y);
                candidateElements.Add(element);
            }
        }
        catch (Exception) { return RenderState.Invalid; }
        if (candidateActions.Count != offer.Actions.Count ||
            candidateActions.Where((button, slot) =>
                button.Tag is not RenderedAction action ||
                action.Primitive.ActionSlot != slot ||
                action.Primitive.Enabled != offer.Actions[slot].Enabled).Any())
            return RenderState.Invalid;

        CancelAllHolds();
        LcdCanvas.Children.Clear();
        LcdCanvas.Clip = new EllipseGeometry(new Point(233, 233), 233, 233);
        foreach (var element in candidateElements) LcdCanvas.Children.Add(element);
        actionButtons.Clear();
        actionButtons.AddRange(candidateActions);
        pendingRenderedOffer = offer;
        HostNoticeText.Text = $"Native frame {offer.Generation}:{offer.Revision} drawn; awaiting commit";
        HostErrorText.Visibility = Visibility.Collapsed;
        return RenderState.Success;
    }

    private bool CommitRenderedOffer(uint generation, uint revision)
    {
        if (pendingRenderedOffer is null || pendingRenderedOffer.Generation != generation ||
            pendingRenderedOffer.Revision != revision)
        {
            FailUi("Native commit did not match the drawn frame.");
            return false;
        }
        currentOffer = pendingRenderedOffer;
        pendingRenderedOffer = null;
        foreach (var button in actionButtons)
            if (button.Tag is RenderedAction action) button.IsEnabled = action.Primitive.Enabled;
        HostNoticeText.Text = $"Native frame {generation}:{revision} committed locally";
        ScheduleSnapshotRefreshDrain();
        return true;
    }

    private async void HandleActionClick(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: RenderedAction action } ||
            action.Primitive.RequiresHold) return;
        await RunInteractiveFirmwareCommandAsync(action, PortableUiGesture.Activate);
    }

    private void HandleHoldStarted(object sender, MouseButtonEventArgs e)
    {
        if (sender is Button { Tag: RenderedAction { Primitive.RequiresHold: true } } button)
            StartHold(button);
    }

    private void HandleHoldReleased(object sender, MouseButtonEventArgs e) => CancelHold(sender as Button);
    private void HandleHoldKeyDown(object sender, KeyEventArgs e) { if (e.Key == Key.Space && sender is Button button) StartHold(button); }
    private void HandleHoldKeyUp(object sender, KeyEventArgs e) { if (e.Key == Key.Space) CancelHold(sender as Button); }

    private void StartHold(Button button)
    {
        if (button.Tag is not RenderedAction { Primitive.RequiresHold: true } action ||
            button.Resources["hold-timer"] is DispatcherTimer) return;
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(800) };
        timer.Tick += async (_, _) =>
        {
            timer.Stop();
            button.Resources.Remove("hold-timer");
            if (!button.IsPressed && !button.IsKeyboardFocused) return;
            await RunInteractiveFirmwareCommandAsync(action, PortableUiGesture.Hold);
        };
        button.Resources["hold-timer"] = timer;
        timer.Start();
    }

    private static void CancelHold(Button? button)
    {
        if (button?.Resources["hold-timer"] is DispatcherTimer timer)
        {
            timer.Stop();
            button.Resources.Remove("hold-timer");
        }
    }

    private void CancelAllHolds()
    {
        foreach (var button in actionButtons)
        {
            CancelHold(button);
        }
    }

    private Brush BrushForStyle(int style) => style switch
    {
        5 => (Brush)FindResource("WarningBrush"),
        6 => (Brush)FindResource("CriticalBrush"),
        7 => (Brush)FindResource("GoodBrush"),
        3 => (Brush)FindResource("MutedBrush"),
        _ => (Brush)FindResource("TextBrush"),
    };

    private static string PrimitiveCopy(
        PortableUiOffer offer,
        PortableUiPrimitive primitive)
    {
        if (primitive.OwnedTextIndex != 255)
        {
            if (primitive.OwnedTextIndex < 0 ||
                primitive.OwnedTextIndex >= offer.OwnedTexts.Count)
            {
                throw new InvalidOperationException(
                    "The portable UI owned-text index was invalid.");
            }
            return offer.OwnedTexts[primitive.OwnedTextIndex].Text;
        }
        return TokenCopy(
            primitive.TextToken,
            primitive.NumericValueValid,
            primitive.NumericValue);
    }

    private static PortableUiSnapshotState BuildFirmwareState(
        SimulatorUiSnapshot snapshot,
        PortableUiSnapshotState prior)
    {
        var sourceMessages = snapshot.Messages
            .Where(message => message.Kind != SimulatorUiMessageKind.System &&
                message.Direction != SimulatorUiMessageDirection.Local)
            .OrderBy(message => message.Sequence)
            .TakeLast(12)
            .ToArray();
        if (sourceMessages.Any(message => message.Sequence <= 0) ||
            sourceMessages.Where((message, index) => index > 0 &&
                message.Sequence <= sourceMessages[index - 1].Sequence).Any())
        {
            throw new InvalidOperationException(
                "The bridge message snapshot was not strictly ordered.");
        }
        var messages = sourceMessages.Select(MapFirmwareMessage).ToArray();
        return prior with
        {
            RadioIndicator = snapshot.ConnectionState switch
            {
                SimulatorUiConnectionState.Disconnected => 1,
                SimulatorUiConnectionState.Connecting => 0,
                SimulatorUiConnectionState.Connected => 2,
                SimulatorUiConnectionState.Stale => 3,
                SimulatorUiConnectionState.Faulted => 4,
                _ => 4,
            },
            PeerCountValid = false,
            PeerCount = 0,
            UnreadMessages = 0,
            BridgeSessionEpoch = snapshot.ConnectedSessionEpoch > 0
                ? checked((ulong)snapshot.ConnectedSessionEpoch)
                : 0,
            Messages = messages,
        };
    }

    private static PortableUiSnapshotMessage MapFirmwareMessage(
        SimulatorUiMessage message)
    {
        var exactAscii = message.Text.Length is >= 1 and <= 96 &&
            message.Text.All(character => character is >= ' ' and <= '~');
        var truncated = message.Text.Length > 96 &&
            message.Text.All(character => character is >= ' ' and <= '~');
        var unavailable = !exactAscii && !truncated;
        var direction = message.Direction switch
        {
            SimulatorUiMessageDirection.Inbound => 0,
            SimulatorUiMessageDirection.Outbound => 1,
            _ => throw new InvalidOperationException(
                "A local-only message reached the portable UI snapshot."),
        };
        var kind = message.Kind switch
        {
            SimulatorUiMessageKind.Chat => 0,
            SimulatorUiMessageKind.QuickStatus => 1,
            SimulatorUiMessageKind.Alert => 2,
            SimulatorUiMessageKind.Acknowledgement => 3,
            _ => throw new InvalidOperationException(
                "A system message reached the portable UI snapshot."),
        };
        var priority = message.Priority switch
        {
            SimulatorUiMessagePriority.Normal => 0,
            SimulatorUiMessagePriority.Important => 1,
            SimulatorUiMessagePriority.Critical => 2,
            _ => throw new InvalidOperationException("Unknown message priority."),
        };
        var delivery = message.DeliveryState switch
        {
            SimulatorUiDeliveryState.Queued => 0,
            SimulatorUiDeliveryState.BridgeAccepted => 1,
            SimulatorUiDeliveryState.BridgeObserved => 2,
            SimulatorUiDeliveryState.BridgeAcknowledgementObserved => 3,
            SimulatorUiDeliveryState.Failed => 4,
            _ => throw new InvalidOperationException("Unknown delivery state."),
        };
        if ((direction == 0 && delivery != 2) ||
            (direction == 1 && delivery == 2) ||
            (direction == 1 && delivery == 3 && kind != 2) ||
            (message.CanAcknowledge &&
                (direction != 0 || kind != 2 || priority != 2)))
        {
            throw new InvalidOperationException(
                "The bridge message snapshot was semantically incoherent.");
        }
        return new(
            checked((ulong)message.Sequence), direction, kind, priority,
            delivery, message.CanAcknowledge, truncated, unavailable,
            exactAscii ? message.Text : string.Empty);
    }

    private static string TokenCopy(int token, bool numericValid, int numericValue)
    {
        var text = token switch
        {
            1 => "Peers", 2 => "Unread", 3 => "Archive queue",
            100 => "Home", 101 => "Status", 102 => "Quick status",
            103 => "Confirm critical alert", 104 => "Breadcrumb archive",
            105 => "Confirm archive change", 106 => "System fault",
            107 => "Messages", 108 => "Message list", 109 => "Message detail",
            110 => "Compose message", 111 => "Confirm message",
            >= 200 and <= 227 => NoticeCopy(token - 200),
            >= 301 and <= 332 => ActionCopy(token - 300),
            >= 400 and <= 404 => $"Radio: {IndicatorCopy(token - 400)}",
            >= 410 and <= 414 => $"Position: {IndicatorCopy(token - 410)}",
            >= 420 and <= 424 => $"Power: {IndicatorCopy(token - 420)}",
            _ => throw new InvalidOperationException("Unknown native text token."),
        };
        return numericValid ? $"{text}: {numericValue}" : text;
    }

    private static string IndicatorCopy(int state) => state switch
    { 0 => "Unknown", 1 => "Unavailable", 2 => "Normal", 3 => "Warning", 4 => "Critical", _ => "Unknown" };

    private static string ActionCopy(int action) => action switch
    {
        1 => "Status", 2 => "Quick status", 3 => "Critical alert",
        4 => "Send selected status", 5 => "I'm OK",
        6 => "Need assistance", 7 => "Anyone online?", 8 => "Available to help",
        9 => "Next", 10 => "Previous", 11 => "Hold to confirm", 12 => "Back",
        13 => "Acknowledge", 14 => "Start position sharing", 15 => "Stop position sharing",
        16 => "Breadcrumb archive", 17 => "Start archive", 18 => "Stop archive",
        19 => "Hold to start archive", 20 => "Stop archive now",
        21 => "Messages", 22 => "Inbox", 23 => "Outbox", 24 => "Compose",
        25 => "Open first message", 26 => "Open second message",
        27 => "Next page", 28 => "Select first message", 29 => "Select second message",
        30 => "Next templates", 31 => "Send message", 32 => "Hold to acknowledge",
        _ => throw new InvalidOperationException("Unknown native action token."),
    };

    private static string NoticeCopy(int notice) => notice switch
    {
        1 => "Radio unavailable", 2 => "Position unavailable", 3 => "Power low",
        4 => "Power critical", 5 => "Message request failed", 6 => "Critical alert pending",
        7 => "Critical alert failed", 8 => "Update trial active",
        9 => "Update transition rejected", 10 => "Update reboot required",
        11 => "Update cleanup required", 12 => "Update safe mode",
        13 => "Update service required", 14 => "Update reconciliation required",
        15 => "Position sharing stopped", 16 => "Position sharing active",
        17 => "Waiting for a current position", 18 => "Position sharing deferred",
        19 => "Position sharing failed", 20 => "Archive stopped", 21 => "Archive active",
        22 => "Archive queued", 23 => "Archive upload waiting", 24 => "Archive queue full",
        25 => "Archive upload failed", 26 => "Hold to confirm archive start",
        27 => "Confirm archive stop",
        _ => throw new InvalidOperationException("Unknown native notice token."),
    };

    private void FailUi(string message)
    {
        foreach (var button in actionButtons) button.IsEnabled = false;
        HostErrorText.Text = message;
        HostErrorText.Visibility = Visibility.Visible;
    }

    private async void ConnectClient(object sender, RoutedEventArgs e) =>
        await RunPresenterCommandAsync(presenter.ConnectAsync, "Connect requested for this client only.");
    private async void DisconnectClient(object sender, RoutedEventArgs e) =>
        await RunPresenterCommandAsync(presenter.DisconnectAsync, "Disconnected this client only.");
    private async void ReconnectClient(object sender, RoutedEventArgs e) =>
        await RunPresenterCommandAsync(presenter.ReconnectAsync, "Reconnect requested for this client only.");

    private void HandleDeviceChoiceSelectionChanged(
        object sender,
        SelectionChangedEventArgs e) => UpdateSelectedChoiceState();

    private async void RefreshDeviceChoices(object sender, RoutedEventArgs e) =>
        await RunPresenterCommandAsync(
            presenter.RefreshDeviceChoicesAsync,
            "Public device choices refreshed. No connection was opened.");

    private async void SelectDeviceChoice(object sender, RoutedEventArgs e)
    {
        if (DeviceChoiceCombo.SelectedItem is not SimulatorUiDeviceChoice choice)
        {
            HostErrorText.Text = "Select a public device choice first.";
            HostErrorText.Visibility = Visibility.Visible;
            return;
        }
        await RunPresenterCommandAsync(
            token => presenter.SelectDeviceAsync(choice, token),
            "Device choice assigned to this client. Connect remains explicit.");
    }

    private async void ForgetDeviceChoice(object sender, RoutedEventArgs e) =>
        await RunPresenterCommandAsync(
            presenter.ForgetDeviceAsync,
            "Device assignment removed from this client.");

    private async Task RunPresenterCommandAsync(Func<CancellationToken, Task> command, string copy)
    {
        var entered = false;
        try
        {
            await hostCommandGate.WaitAsync(lifetime.Token).ConfigureAwait(false);
            entered = true;
            if (closeStarted) return;
            await command(lifetime.Token).ConfigureAwait(false);
            await InvokeUiAsync(() =>
            {
                RenderHostSnapshot(presenter.Snapshot);
                HostNoticeText.Text = copy;
                HostErrorText.Visibility = Visibility.Collapsed;
            });
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception)
        {
            await InvokeUiAsync(() =>
            {
                HostErrorText.Text = "The local bridge command failed.";
                HostErrorText.Visibility = Visibility.Visible;
            });
        }
        finally
        {
            if (entered) hostCommandGate.Release();
        }
    }

    private static string ConnectionCopy(SimulatorUiConnectionState state) => state switch
    {
        SimulatorUiConnectionState.Disconnected => "Disconnected",
        SimulatorUiConnectionState.Connecting => "Connecting",
        SimulatorUiConnectionState.Connected => "Connected to bridge session",
        SimulatorUiConnectionState.Stale => "Stale — awaiting bridge data",
        SimulatorUiConnectionState.Faulted => "Bridge fault",
        _ => "Unavailable",
    };

    private Brush ConnectionBrush(SimulatorUiConnectionState state) => state switch
    {
        SimulatorUiConnectionState.Connected => (Brush)FindResource("GoodBrush"),
        SimulatorUiConnectionState.Connecting or SimulatorUiConnectionState.Stale => (Brush)FindResource("WarningBrush"),
        SimulatorUiConnectionState.Faulted => (Brush)FindResource("CriticalBrush"),
        _ => (Brush)FindResource("MutedBrush"),
    };

    private async Task InvokeUiAsync(Action action)
    {
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished) return;
        if (Dispatcher.CheckAccess()) { action(); return; }
        await Dispatcher.InvokeAsync(action, DispatcherPriority.Normal).Task.ConfigureAwait(false);
    }

    private async void HandleWindowClosing(object? sender, CancelEventArgs e)
    {
        if (closeCommitted) return;
        if (closeStarted) { e.Cancel = true; return; }
        e.Cancel = true;
        closeStarted = true;
        IsEnabled = false;
        presenter.SnapshotChanged -= HandleSnapshotChanged;
        lock (snapshotRefreshSync) pendingSnapshotRefresh = null;
        lifetime.Cancel();
        CancelAllHolds();
        var firmwareEntered = false;
        var hostEntered = false;
        try
        {
            await commandGate.WaitAsync(CancellationToken.None);
            firmwareEntered = true;
            await hostCommandGate.WaitAsync(CancellationToken.None);
            hostEntered = true;
            await uiSession.DisposeAsync();
            await presenter.DisconnectAsync(CancellationToken.None);
            await presenter.DisposeAsync();
        }
        catch (Exception) { }
        finally
        {
            if (hostEntered) hostCommandGate.Release();
            if (firmwareEntered) commandGate.Release();
            closeCommitted = true;
            _ = Dispatcher.BeginInvoke(DispatcherPriority.Normal, Close);
        }
    }
}
