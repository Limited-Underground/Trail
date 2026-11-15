using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using OpenTrail.Simulator;

internal static partial class Program
{
    private static void SampleGroupFirstAndCreate()
    {
        var screen = new ConsoleSampleConversations();
        Require(screen.ConsoleNameLabel.Text == "Limited Underground MeshTrail Console",
            "The sample screen did not display the Console product name.");
        Require(screen.ConversationTitle.Text == ConsoleSampleStrings.GroupName &&
            screen.ConversationStatus.Text == ConsoleSampleStrings.GroupInvitation &&
            screen.ThreadMessages.Children.Count == 2 && !screen.SampleReplyButton.IsEnabled,
            "The sample must open on a group invitation with its preview and sending gated.");

        ClickSampleControl(screen.CreateGroupButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.GroupAdmin &&
            screen.ConversationTitle.Text == ConsoleSampleStrings.CreatedGroupName &&
            screen.PolicyOff.IsChecked == true && screen.SampleReplyButton.IsEnabled,
            "An admin must start a local group with an explicit location setting.");
        ClickSampleControl(screen.PolicyRequired);
        Require(screen.GroupSharing.IsChecked == true && !screen.GroupSharing.IsEnabled,
            "The sample group creator could opt out while requiring other members to share.");
        ClickSampleControl(screen.SampleReplyButton);
        Require(screen.ThreadMessages.Children.Count == 1 && screen.ThreadMessages.Children[0] is Border,
            "A sample reply did not appear as a conversation message.");

        ClickSampleControl(screen.DirectTabButton);
        ClickSampleControl(screen.GroupTabButton);
        Require(screen.ThreadMessages.Children.Count == 1 &&
            screen.ConversationTitle.Text == ConsoleSampleStrings.CreatedGroupName,
            "Switching to the direct screen discarded the local group.");
        for (int reply = 0; reply < 20; ++reply) ClickSampleControl(screen.SampleReplyButton);
        Require(screen.ThreadMessages.Children.Count == 16, "Sample conversation history exceeded its bound.");
    }

    private static void SampleGroupLocationOff()
    {
        var screen = new ConsoleSampleConversations();
        ClickSampleControl(screen.PolicyOff);
        Require(screen.PolicyDetails.Text == ConsoleSampleStrings.OffDetails &&
            screen.JoinAndShareButton.Visibility == Visibility.Collapsed,
            "An off-policy group offered to share location.");
        ClickSampleControl(screen.JoinWithoutSharingButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.GroupMember &&
            screen.GroupSharing.IsChecked == false && !screen.GroupSharing.IsEnabled,
            "Location-off membership must work without sharing.");
    }

    private static void SampleGroupLocationPreferred()
    {
        var screen = new ConsoleSampleConversations();
        Require(screen.PolicyPreferred.IsChecked == true &&
            screen.JoinAndShareButton.Visibility == Visibility.Visible,
            "A preferred-policy invitation did not ask to share location.");
        ClickSampleControl(screen.JoinWithoutSharingButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.GroupMember &&
            screen.GroupSharing.IsChecked == false && screen.SampleReplyButton.IsEnabled &&
            !screen.PolicyRequired.IsEnabled,
            "Declining preferred location sharing either blocked joining or exposed admin policy to a member.");
        SetSampleChoice(screen.GroupSharing, true);
        Require(screen.JoinFeedback.Text == ConsoleSampleStrings.GroupSharingOn,
            "A preferred-policy member could not opt in.");
    }

    private static void SampleGroupLocationRequired()
    {
        var screen = new ConsoleSampleConversations();
        ClickSampleControl(screen.PolicyRequired);
        ClickSampleControl(screen.JoinWithoutSharingButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.GroupInvitation &&
            screen.JoinFeedback.Text == ConsoleSampleStrings.RequiredRefusal &&
            !screen.SampleReplyButton.IsEnabled,
            "Refusing required location sharing completed the join.");
        ClickSampleControl(screen.JoinAndShareButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.GroupMember &&
            screen.SampleReplyButton.IsEnabled && screen.GroupSharing.IsChecked == true &&
            !screen.GroupSharing.IsEnabled,
            "Agreeing to share did not complete required-policy membership.");
    }

    private static void SampleDirectInvitationAndSharing()
    {
        var screen = new ConsoleSampleConversations();
        ClickSampleControl(screen.DirectTabButton);
        SetSampleChoice(screen.DirectLocalSharing, true);
        Require(screen.ThreadMessages.Children.Count == 0 && !screen.SampleReplyButton.IsEnabled &&
            Equals(screen.DirectLocalSharing.Content, ConsoleSampleStrings.ShareOnAcceptance) &&
            screen.DirectPeerSharing.Visibility == Visibility.Collapsed,
            "A direct conversation or live sharing appeared before the invitation was sent.");
        ClickSampleControl(screen.SendInviteButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.DirectPending &&
            !screen.SampleReplyButton.IsEnabled && !screen.DirectLocalSharing.IsEnabled &&
            screen.AcceptInviteButton.Visibility == Visibility.Visible,
            "Sending an invite failed to preserve the pending acceptance boundary.");
        ClickSampleControl(screen.AcceptInviteButton);
        Require(screen.ConversationStatus.Text == ConsoleSampleStrings.DirectStarted &&
            screen.SampleReplyButton.IsEnabled && screen.DirectLocalSharing.IsChecked == true &&
            screen.DirectPeerSharing.IsChecked == false,
            "Accepting did not preserve the sender and recipient's independent choices.");
        SetSampleChoice(screen.DirectPeerSharing, true);
        SetSampleChoice(screen.DirectLocalSharing, false);
        Require(screen.DirectLocalSharing.IsChecked == false && screen.DirectPeerSharing.IsChecked == true,
            "Changing one person's sharing incorrectly changed the other person's choice.");
        ClickSampleControl(screen.SampleReplyButton);
        Require(screen.ThreadMessages.Children.Count == 2, "The accepted direct thread lost a sample reply.");

        var reciprocal = new ConsoleSampleConversations();
        ClickSampleControl(reciprocal.DirectTabButton);
        ClickSampleControl(reciprocal.SendInviteButton);
        SetSampleChoice(reciprocal.DirectPeerSharing, true);
        ClickSampleControl(reciprocal.AcceptInviteButton);
        Require(reciprocal.DirectLocalSharing.IsChecked == false && reciprocal.DirectPeerSharing.IsChecked == true,
            "The recipient's opt-in at acceptance was overwritten by the sender's choice.");
    }

    private static void SampleConversationButtonsFit()
    {
        foreach (var size in new[] { new Size(760, 440), new Size(800, 480), new Size(824, 520) })
        {
            var screen = new ConsoleSampleConversations();
            VerifyVisibleButtonLabels(screen, size);

            ClickSampleControl(screen.PolicyRequired);
            ClickSampleControl(screen.JoinWithoutSharingButton);
            VerifyVisibleButtonLabels(screen, size);

            ClickSampleControl(screen.CreateGroupButton);
            VerifyVisibleButtonLabels(screen, size);

            ClickSampleControl(screen.DirectTabButton);
            VerifyVisibleButtonLabels(screen, size);
            ClickSampleControl(screen.SendInviteButton);
            VerifyVisibleButtonLabels(screen, size);
            ClickSampleControl(screen.AcceptInviteButton);
            VerifyVisibleButtonLabels(screen, size);

            screen.CreateGroupButton.Content = "Create a sample group for this weekend's trail outing";
            VerifyVisibleButtonLabels(screen, size);
            var label = FindVisibleVisualChildren<TextBlock>(screen.CreateGroupButton).Single();
            Require(label.ActualHeight > label.FontSize * 2,
                "A long sample button label did not grow to multiple lines.");
        }
    }

    private static void VerifyVisibleButtonLabels(ConsoleSampleConversations screen, Size size)
    {
        LayoutSample(screen, size);
        var screenBounds = new Rect(size);
        const double tolerance = 1;
        var buttons = FindVisibleVisualChildren<Button>(screen).ToArray();
        Require(buttons.Length >= 5, $"The sample controls were not laid out at {size}.");
        foreach (var button in buttons)
        {
            var label = FindVisibleVisualChildren<TextBlock>(button).SingleOrDefault();
            Require(label is not null && Equals(label.Text, button.Content),
                $"The {button.Name} label did not render its full text at {size}.");
            var text = label!;
            var bounds = new Rect(text.RenderSize);
            var buttonBounds = text.TransformToAncestor(button).TransformBounds(bounds);
            var padding = button.Padding;
            var border = button.BorderThickness;
            var contentBounds = new Rect(
                padding.Left + border.Left, padding.Top + border.Top,
                button.ActualWidth - padding.Left - padding.Right - border.Left - border.Right,
                button.ActualHeight - padding.Top - padding.Bottom - border.Top - border.Bottom);
            contentBounds.Inflate(tolerance, tolerance);
            var metrics = new FormattedText(text.Text, text.Language.GetSpecificCulture(), text.FlowDirection,
                new Typeface(text.FontFamily, text.FontStyle, text.FontWeight, text.FontStretch),
                text.FontSize, Brushes.Black, VisualTreeHelper.GetDpi(text).PixelsPerDip);
            if (text.TextWrapping != TextWrapping.NoWrap && bounds.Width > 0)
                metrics.MaxTextWidth = bounds.Width + tolerance;
            Require(bounds.Width > 0 && metrics.Width <= bounds.Width + tolerance &&
                metrics.Height <= bounds.Height + tolerance && contentBounds.Contains(buttonBounds) &&
                screenBounds.Contains(text.TransformToAncestor(screen).TransformBounds(bounds)),
                $"The {button.Name} label '{text.Text}' clips at {size}. " +
                $"Text requires {metrics.Width:F1} × {metrics.Height:F1} " +
                $"and was arranged in {bounds.Width:F1} × {bounds.Height:F1}.");
        }
    }

    private static IEnumerable<T> FindVisibleVisualChildren<T>(DependencyObject parent) where T : DependencyObject
    {
        for (var index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is UIElement { Visibility: not Visibility.Visible })
                continue;
            if (child is T match)
                yield return match;
            foreach (var descendant in FindVisibleVisualChildren<T>(child))
                yield return descendant;
        }
    }

    private static void LayoutSample(ConsoleSampleConversations screen, Size size)
    {
        screen.Measure(size);
        screen.Arrange(new Rect(size));
        screen.UpdateLayout();
    }

    private static void SampleConversationScreensRender()
    {
        var screen = new ConsoleSampleConversations();
        RenderSample(screen, "console-group-location-preferred.png");
        ClickSampleControl(screen.PolicyOff);
        ClickSampleControl(screen.JoinWithoutSharingButton);
        RenderSample(screen, "console-group-location-off.png");
        ClickSampleControl(screen.PreviewGroupButton);
        ClickSampleControl(screen.PolicyRequired);
        ClickSampleControl(screen.JoinWithoutSharingButton);
        RenderSample(screen, "console-group-location-required.png");
        ClickSampleControl(screen.DirectTabButton);
        SetSampleChoice(screen.DirectLocalSharing, true);
        ClickSampleControl(screen.SendInviteButton);
        ClickSampleControl(screen.AcceptInviteButton);
        ClickSampleControl(screen.SampleReplyButton);
        RenderSample(screen, "console-direct-conversation.png");
    }

    private static void RenderSample(ConsoleSampleConversations screen, string fileName)
    {
        const int width = 800;
        const int height = 480;
        LayoutSample(screen, new Size(width, height));
        Require(PresentationSource.FromVisual(screen) is null &&
            screen.ActualWidth == width && screen.ActualHeight == height,
            "The sample render required a window or did not receive the target canvas size.");

        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(screen);
        var corner = new byte[4];
        bitmap.CopyPixels(new Int32Rect(0, 0, 1, 1), corner, 4, 0);
        Require(corner[3] == 255, "The sample render did not include an opaque screen background.");
        var directory = Environment.GetEnvironmentVariable("OPENTRAIL_CONSOLE_SAMPLE_SCREENSHOTS");
        if (string.IsNullOrWhiteSpace(directory)) return;
        Directory.CreateDirectory(directory);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var destination = File.Create(Path.Combine(directory, fileName));
        encoder.Save(destination);
    }

    private static void ClickSampleControl(ButtonBase control) =>
        control.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent, control));

    private static void SetSampleChoice(CheckBox control, bool value)
    {
        control.IsChecked = value;
        ClickSampleControl(control);
    }
}
