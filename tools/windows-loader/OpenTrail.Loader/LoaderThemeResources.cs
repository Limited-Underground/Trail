using System.Windows;
using System.Windows.Media;

namespace OpenTrail.Loader;

internal sealed record LoaderThemePalette(
    string Name,
    IReadOnlyDictionary<string, Brush> Brushes);

internal static class LoaderThemeResources
{
    internal const string ThemeModeResourceKey = "LoaderThemeMode";

    private static readonly string[] RequiredBrushKeys =
    [
        "BackgroundBrush",
        "PanelBrush",
        "PanelRaisedBrush",
        "BorderBrush",
        "TextBrush",
        "MutedTextBrush",
        "AccentBrush",
        "InfoTextBrush",
        "InfoSurfaceBrush",
        "InfoSurfaceTextBrush",
        "SurfaceBrush",
        "AccentTextBrush",
        "CriticalTextBrush",
        "CriticalSurfaceBrush",
        "WarningSurfaceBrush",
        "WarningBorderBrush",
        "SuccessTextBrush",
        "SelectedSurfaceBrush",
        "SelectedBorderBrush",
        "DisabledTextBrush",
        "FocusBrush",
        "ButtonFaceBrush",
        "ButtonTextBrush",
        "ButtonBorderBrush",
        "ButtonHighlightBrush",
        "ButtonShadowBrush",
        "ButtonHoverBrush",
        "ButtonPressedBrush",
    ];

    internal static LoaderThemePalette CreateClassicPalette() =>
        CreatePalette(
            "classic",
            Background: Color("#C0C0C0"),
            Panel: Color("#C0C0C0"),
            PanelRaised: Color("#DFDFDF"),
            Border: Color("#808080"),
            Text: Color("#000000"),
            MutedText: Color("#404040"),
            Accent: Color("#000080"),
            InfoText: Color("#000080"),
            InfoSurface: Color("#000080"),
            InfoSurfaceText: Color("#FFFFFF"),
            Surface: Color("#FFFFFF"),
            AccentText: Color("#FFFFFF"),
            CriticalText: Color("#800000"),
            CriticalSurface: Color("#FFD8D8"),
            WarningSurface: Color("#FFFFE1"),
            WarningBorder: Color("#808000"),
            SuccessText: Color("#006000"),
            SelectedSurface: Color("#DDEBFF"),
            SelectedBorder: Color("#000080"),
            DisabledText: Color("#5A5A5A"),
            Focus: Color("#000000"),
            ButtonFace: Color("#C0C0C0"),
            ButtonText: Color("#000000"),
            ButtonBorder: Color("#000000"),
            ButtonHighlight: Color("#FFFFFF"),
            ButtonShadow: Color("#000000"),
            ButtonHover: Color("#D4D4D4"),
            ButtonPressed: Color("#B0B0B0"));

    internal static LoaderThemePalette CreateSystemHighContrastPalette() =>
        CreatePalette(
            "system-high-contrast",
            Background: SystemColors.WindowBrush,
            Panel: SystemColors.WindowBrush,
            PanelRaised: SystemColors.ControlBrush,
            Border: SystemColors.WindowTextBrush,
            Text: SystemColors.WindowTextBrush,
            MutedText: SystemColors.WindowTextBrush,
            Accent: SystemColors.HighlightBrush,
            InfoText: SystemColors.WindowTextBrush,
            InfoSurface: SystemColors.WindowTextBrush,
            InfoSurfaceText: SystemColors.WindowBrush,
            Surface: SystemColors.WindowBrush,
            AccentText: SystemColors.HighlightTextBrush,
            CriticalText: SystemColors.WindowTextBrush,
            CriticalSurface: SystemColors.WindowBrush,
            WarningSurface: SystemColors.WindowBrush,
            WarningBorder: SystemColors.WindowTextBrush,
            SuccessText: SystemColors.WindowTextBrush,
            SelectedSurface: SystemColors.WindowBrush,
            SelectedBorder: SystemColors.HighlightBrush,
            DisabledText: SystemColors.GrayTextBrush,
            Focus: SystemColors.WindowTextBrush,
            ButtonFace: SystemColors.ControlBrush,
            ButtonText: SystemColors.ControlTextBrush,
            ButtonBorder: SystemColors.ControlTextBrush,
            ButtonHighlight: SystemColors.ControlLightLightBrush,
            ButtonShadow: SystemColors.ControlDarkDarkBrush,
            ButtonHover: SystemColors.ControlBrush,
            ButtonPressed: SystemColors.ControlBrush);

    internal static LoaderThemePalette CreateDeterministicHighContrastPalette() =>
        CreatePalette(
            "deterministic-high-contrast",
            Background: Brushes.Black,
            Panel: Brushes.Black,
            PanelRaised: Brushes.Black,
            Border: Brushes.White,
            Text: Brushes.White,
            MutedText: Brushes.White,
            Accent: Brushes.White,
            InfoText: Brushes.White,
            InfoSurface: Brushes.White,
            InfoSurfaceText: Brushes.Black,
            Surface: Brushes.Black,
            AccentText: Brushes.Black,
            CriticalText: Brushes.White,
            CriticalSurface: Brushes.Black,
            WarningSurface: Brushes.Black,
            WarningBorder: Brushes.White,
            SuccessText: Brushes.White,
            SelectedSurface: Brushes.Black,
            SelectedBorder: Brushes.Yellow,
            DisabledText: Brushes.Yellow,
            Focus: Brushes.Yellow,
            ButtonFace: Brushes.Black,
            ButtonText: Brushes.White,
            ButtonBorder: Brushes.White,
            ButtonHighlight: Brushes.White,
            ButtonShadow: Brushes.White,
            ButtonHover: Brushes.Black,
            ButtonPressed: Brushes.Black);

    internal static void Apply(
        ResourceDictionary resources,
        LoaderThemePalette palette)
    {
        ArgumentNullException.ThrowIfNull(resources);
        ArgumentNullException.ThrowIfNull(palette);
        if (string.IsNullOrWhiteSpace(palette.Name) ||
            palette.Name.Length > 64 ||
            palette.Brushes.Count != RequiredBrushKeys.Length)
        {
            throw new ArgumentException("Theme palette is incomplete.", nameof(palette));
        }

        foreach (var key in RequiredBrushKeys)
        {
            if (!palette.Brushes.TryGetValue(key, out var brush) || brush is null)
            {
                throw new ArgumentException(
                    $"Theme palette is missing {key}.",
                    nameof(palette));
            }
        }

        foreach (var key in RequiredBrushKeys)
        {
            resources[key] = palette.Brushes[key];
        }
        resources[ThemeModeResourceKey] = palette.Name;
    }

    private static LoaderThemePalette CreatePalette(
        string name,
        Brush Background,
        Brush Panel,
        Brush PanelRaised,
        Brush Border,
        Brush Text,
        Brush MutedText,
        Brush Accent,
        Brush InfoText,
        Brush InfoSurface,
        Brush InfoSurfaceText,
        Brush Surface,
        Brush AccentText,
        Brush CriticalText,
        Brush CriticalSurface,
        Brush WarningSurface,
        Brush WarningBorder,
        Brush SuccessText,
        Brush SelectedSurface,
        Brush SelectedBorder,
        Brush DisabledText,
        Brush Focus,
        Brush ButtonFace,
        Brush ButtonText,
        Brush ButtonBorder,
        Brush ButtonHighlight,
        Brush ButtonShadow,
        Brush ButtonHover,
        Brush ButtonPressed) =>
        new(
            name,
            new Dictionary<string, Brush>(StringComparer.Ordinal)
            {
                ["BackgroundBrush"] = Background,
                ["PanelBrush"] = Panel,
                ["PanelRaisedBrush"] = PanelRaised,
                ["BorderBrush"] = Border,
                ["TextBrush"] = Text,
                ["MutedTextBrush"] = MutedText,
                ["AccentBrush"] = Accent,
                ["InfoTextBrush"] = InfoText,
                ["InfoSurfaceBrush"] = InfoSurface,
                ["InfoSurfaceTextBrush"] = InfoSurfaceText,
                ["SurfaceBrush"] = Surface,
                ["AccentTextBrush"] = AccentText,
                ["CriticalTextBrush"] = CriticalText,
                ["CriticalSurfaceBrush"] = CriticalSurface,
                ["WarningSurfaceBrush"] = WarningSurface,
                ["WarningBorderBrush"] = WarningBorder,
                ["SuccessTextBrush"] = SuccessText,
                ["SelectedSurfaceBrush"] = SelectedSurface,
                ["SelectedBorderBrush"] = SelectedBorder,
                ["DisabledTextBrush"] = DisabledText,
                ["FocusBrush"] = Focus,
                ["ButtonFaceBrush"] = ButtonFace,
                ["ButtonTextBrush"] = ButtonText,
                ["ButtonBorderBrush"] = ButtonBorder,
                ["ButtonHighlightBrush"] = ButtonHighlight,
                ["ButtonShadowBrush"] = ButtonShadow,
                ["ButtonHoverBrush"] = ButtonHover,
                ["ButtonPressedBrush"] = ButtonPressed,
            });

    private static SolidColorBrush Color(string value)
    {
        var brush = new SolidColorBrush(
            (System.Windows.Media.Color)ColorConverter.ConvertFromString(value));
        brush.Freeze();
        return brush;
    }
}
