using System.Windows;
using System.Windows.Controls;

namespace OpenTrail.Simulator;

public partial class ConsoleSampleConversations : UserControl
{
    private const int MaxMessagesPerThread = 16;

    private enum GroupRole { Visitor, Member, Admin }
    private enum DirectStage { Idle, Pending, Accepted }
    private enum LocationPolicy { Off, Preferred, Required }

    private GroupRole groupRole;
    private DirectStage directStage;
    private LocationPolicy policy = LocationPolicy.Preferred;
    private bool viewingGroup = true;
    private bool groupShares;
    private bool groupJoinRefused;
    private bool localShares;
    private bool peerShares;
    private readonly List<SampleMessage> groupMessages = [];
    private readonly List<SampleMessage> directMessages = [];

    private sealed record SampleMessage(string Sender, string Text, bool Outgoing = false);

    public ConsoleSampleConversations()
    {
        InitializeComponent();
        ConsoleNameLabel.Text = ConsoleSampleStrings.ConsoleName;
        SampleDataLabel.Text = ConsoleSampleStrings.SampleData;
        HeaderTitle.Text = ConsoleSampleStrings.Title;
        GroupLabel.Text = ConsoleSampleStrings.Group;
        OutsideGroupLabel.Text = ConsoleSampleStrings.OutsideGroup;
        DirectTabButton.Content = ConsoleSampleStrings.DirectName;
        CreateGroupButton.Content = ConsoleSampleStrings.CreateGroup;
        PreviewGroupButton.Content = ConsoleSampleStrings.PreviewGroup;
        SampleReplyButton.Content = ConsoleSampleStrings.AddReply;
        PolicyOff.Content = ConsoleSampleStrings.LocationOff;
        PolicyPreferred.Content = ConsoleSampleStrings.LocationPreferred;
        PolicyRequired.Content = ConsoleSampleStrings.LocationRequired;
        JoinAndShareButton.Content = ConsoleSampleStrings.JoinAndShare;
        JoinWithoutSharingButton.Content = ConsoleSampleStrings.JoinWithoutSharing;
        GroupSharing.Content = ConsoleSampleStrings.GroupSharingConsent;
        DirectDetails.Text = ConsoleSampleStrings.DirectInviteDetails;
        SendInviteButton.Content = ConsoleSampleStrings.SendInvite;
        AcceptInviteButton.Content = ConsoleSampleStrings.AcceptInvite;
        LoadGroupPreview();
        UpdateView();
    }

    private void OpenGroup(object sender, RoutedEventArgs e)
    {
        viewingGroup = true;
        UpdateView();
    }

    private void OpenDirect(object sender, RoutedEventArgs e)
    {
        viewingGroup = false;
        UpdateView();
    }

    private void LoadGroupPreview()
    {
        groupRole = GroupRole.Visitor;
        policy = LocationPolicy.Preferred;
        groupShares = false;
        groupJoinRefused = false;
        groupMessages.Clear();
        groupMessages.Add(new(ConsoleSampleStrings.SampleSender, ConsoleSampleStrings.SampleGroupFirst));
        groupMessages.Add(new(ConsoleSampleStrings.SampleGroupSecondSender, ConsoleSampleStrings.SampleGroupSecond));
    }

    private void PreviewGroup(object sender, RoutedEventArgs e)
    {
        LoadGroupPreview();
        OpenGroup(sender, e);
    }

    private void CreateGroup(object sender, RoutedEventArgs e)
    {
        groupRole = GroupRole.Admin;
        policy = LocationPolicy.Off;
        groupShares = false;
        groupJoinRefused = false;
        groupMessages.Clear();
        OpenGroup(sender, e);
    }

    private void SelectPolicy(object sender, RoutedEventArgs e)
    {
        if (groupRole == GroupRole.Member) return;
        policy = sender == PolicyOff ? LocationPolicy.Off
            : sender == PolicyPreferred ? LocationPolicy.Preferred : LocationPolicy.Required;
        groupJoinRefused = false;
        if (policy == LocationPolicy.Off) groupShares = false;
        if (groupRole == GroupRole.Admin && policy == LocationPolicy.Required) groupShares = true;
        UpdateView();
    }

    private void JoinWithoutSharing(object sender, RoutedEventArgs e) => JoinGroup(shares: false);
    private void JoinAndShare(object sender, RoutedEventArgs e) => JoinGroup(shares: true);

    private void JoinGroup(bool shares)
    {
        if (groupRole != GroupRole.Visitor) return;
        groupJoinRefused = policy == LocationPolicy.Required && !shares;
        if (!groupJoinRefused)
        {
            groupRole = GroupRole.Member;
            groupShares = shares && policy != LocationPolicy.Off;
        }
        UpdateView();
    }

    private void SetGroupSharing(object sender, RoutedEventArgs e)
    {
        if (groupRole != GroupRole.Visitor && policy == LocationPolicy.Preferred)
            groupShares = GroupSharing.IsChecked == true;
        UpdateView();
    }

    private void SetDirectLocalSharing(object sender, RoutedEventArgs e)
    {
        if (directStage != DirectStage.Pending)
            localShares = DirectLocalSharing.IsChecked == true;
        UpdateView();
    }

    private void SetDirectPeerSharing(object sender, RoutedEventArgs e)
    {
        if (directStage != DirectStage.Idle)
            peerShares = DirectPeerSharing.IsChecked == true;
        UpdateView();
    }

    private void SendInvite(object sender, RoutedEventArgs e)
    {
        if (directStage != DirectStage.Idle) return;
        directStage = DirectStage.Pending;
        UpdateView();
    }

    private void AcceptInvite(object sender, RoutedEventArgs e)
    {
        if (directStage != DirectStage.Pending) return;
        directStage = DirectStage.Accepted;
        directMessages.Add(new(ConsoleSampleStrings.DirectName, ConsoleSampleStrings.SampleDirectFirst));
        UpdateView();
    }

    private void AddReply(object sender, RoutedEventArgs e)
    {
        if (viewingGroup ? groupRole == GroupRole.Visitor : directStage != DirectStage.Accepted) return;
        var messages = viewingGroup ? groupMessages : directMessages;
        if (messages.Count == MaxMessagesPerThread) messages.RemoveAt(0);
        messages.Add(new(ConsoleSampleStrings.SampleSelf, ConsoleSampleStrings.SampleReply, Outgoing: true));
        UpdateView();
        ThreadScroll.ScrollToEnd();
    }

    private void UpdateView()
    {
        var groupName = groupRole == GroupRole.Admin
            ? ConsoleSampleStrings.CreatedGroupName : ConsoleSampleStrings.GroupName;
        GroupTabButton.Content = groupName;
        GroupTabButton.SetResourceReference(BorderBrushProperty, viewingGroup ? "AccentBrush" : "BorderBrush");
        DirectTabButton.SetResourceReference(BorderBrushProperty, viewingGroup ? "BorderBrush" : "AccentBrush");
        GroupControls.Visibility = VisibleIf(viewingGroup);
        DirectControls.Visibility = VisibleIf(!viewingGroup);
        ConversationTitle.Text = viewingGroup ? groupName : ConsoleSampleStrings.DirectName;
        if (viewingGroup) UpdateGroup();
        else UpdateDirect();
    }

    private void UpdateGroup()
    {
        ConversationStatus.Text = groupRole switch
        {
            GroupRole.Admin => ConsoleSampleStrings.GroupAdmin,
            GroupRole.Member => ConsoleSampleStrings.GroupMember,
            _ => ConsoleSampleStrings.GroupInvitation,
        };
        PolicyOff.IsChecked = policy == LocationPolicy.Off;
        PolicyPreferred.IsChecked = policy == LocationPolicy.Preferred;
        PolicyRequired.IsChecked = policy == LocationPolicy.Required;
        PolicyOff.IsEnabled = PolicyPreferred.IsEnabled = PolicyRequired.IsEnabled = groupRole != GroupRole.Member;
        PolicyDetails.Text = policy switch
        {
            LocationPolicy.Off => ConsoleSampleStrings.OffDetails,
            LocationPolicy.Preferred => ConsoleSampleStrings.PreferredDetails,
            _ => ConsoleSampleStrings.RequiredDetails,
        };
        var canJoin = groupRole == GroupRole.Visitor;
        PolicyTitle.Text = canJoin ? ConsoleSampleStrings.PreviewLocationPolicy : ConsoleSampleStrings.LocationPolicy;
        JoinAndShareButton.Visibility = VisibleIf(canJoin && policy != LocationPolicy.Off);
        JoinWithoutSharingButton.Visibility = VisibleIf(canJoin);
        GroupSharing.Visibility = VisibleIf(!canJoin);
        GroupSharing.IsEnabled = policy == LocationPolicy.Preferred;
        GroupSharing.IsChecked = groupShares;
        JoinFeedback.Text = canJoin
            ? (groupJoinRefused ? ConsoleSampleStrings.RequiredRefusal : string.Empty)
            : (groupShares ? ConsoleSampleStrings.GroupSharingOn : ConsoleSampleStrings.GroupSharingOff);
        JoinFeedback.SetResourceReference(TextBlock.ForegroundProperty, groupJoinRefused ? "WarningBrush" : "MutedBrush");
        SampleReplyButton.IsEnabled = !canJoin;
        ShowMessages(groupMessages, groupRole == GroupRole.Admin ? ConsoleSampleStrings.NewGroupHint : null);
    }

    private void UpdateDirect()
    {
        var accepted = directStage == DirectStage.Accepted;
        ConversationStatus.Text = directStage switch
        {
            DirectStage.Pending => ConsoleSampleStrings.DirectPending,
            DirectStage.Accepted => ConsoleSampleStrings.DirectStarted,
            _ => ConsoleSampleStrings.DirectInvitation,
        };
        DirectHeading.Text = accepted ? ConsoleSampleStrings.DirectLocationSharing : ConsoleSampleStrings.DirectInvitation;
        DirectDetails.Visibility = VisibleIf(!accepted);
        DirectLocalSharing.Content = accepted ? ConsoleSampleStrings.DirectShare : ConsoleSampleStrings.ShareOnAcceptance;
        DirectLocalSharing.IsEnabled = directStage != DirectStage.Pending;
        DirectLocalSharing.IsChecked = localShares;
        DirectPeerSharing.Content = accepted ? ConsoleSampleStrings.PeerDirectShare : ConsoleSampleStrings.PeerShareOnAcceptance;
        DirectPeerSharing.Visibility = VisibleIf(directStage != DirectStage.Idle);
        DirectPeerSharing.IsChecked = peerShares;
        SendInviteButton.Visibility = VisibleIf(directStage == DirectStage.Idle);
        AcceptInviteButton.Visibility = VisibleIf(directStage == DirectStage.Pending);
        SampleReplyButton.IsEnabled = accepted;
        ShowMessages(directMessages, null);
    }

    private void ShowMessages(IEnumerable<SampleMessage> messages, string? emptyHint)
    {
        ThreadMessages.Children.Clear();
        foreach (var message in messages)
        {
            var sender = new TextBlock { Text = message.Sender, FontWeight = FontWeights.SemiBold, FontSize = 13 };
            sender.SetResourceReference(TextBlock.ForegroundProperty, "AccentBrush");
            var body = new TextBlock { Text = message.Text, FontSize = 15, TextWrapping = TextWrapping.Wrap };
            var content = new StackPanel();
            content.Children.Add(sender);
            content.Children.Add(body);
            var bubble = new Border
            {
                Child = content,
                Padding = new Thickness(11, 8, 11, 9),
                CornerRadius = new CornerRadius(7),
                Margin = message.Outgoing ? new Thickness(18, 0, 0, 8) : new Thickness(0, 0, 18, 8),
            };
            bubble.SetResourceReference(Border.BackgroundProperty, message.Outgoing ? "RaisedBrush" : "LcdBrush");
            ThreadMessages.Children.Add(bubble);
        }
        if (ThreadMessages.Children.Count == 0 && emptyHint is not null)
        {
            var hint = new TextBlock { Text = emptyHint, FontSize = 13, TextWrapping = TextWrapping.Wrap };
            hint.SetResourceReference(TextBlock.ForegroundProperty, "MutedBrush");
            ThreadMessages.Children.Add(hint);
        }
    }

    private static Visibility VisibleIf(bool condition) => condition ? Visibility.Visible : Visibility.Collapsed;
}
