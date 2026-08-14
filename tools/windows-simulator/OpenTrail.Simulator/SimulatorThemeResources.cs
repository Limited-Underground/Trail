using System.Windows;
using System.Windows.Media;

namespace OpenTrail.Simulator;

internal sealed record SimulatorThemePalette(
    string Name,
    IReadOnlyDictionary<string, Brush> Brushes);

internal static class SimulatorThemeResources
{
    internal static readonly string[] BrushKeys =
    [
        "WindowBrush",
        "LcdBrush",
        "PanelBrush",
        "RaisedBrush",
        "TextBrush",
        "MutedBrush",
        "AccentBrush",
        "AccentTextBrush",
        "BorderBrush",
        "FocusBrush",
        "GoodBrush",
        "WarningBrush",
        "CriticalBrush",
        "DisabledBrush",
    ];

    internal static SimulatorThemePalette CreateClassicPalette() =>
        CreatePalette(
            "classic",
            "#151922", "#071018", "#112332", "#1B3446",
            "#F7FAFC", "#B9CAD6", "#45D9FF", "#00141B",
            "#6D8798", "#FFFFFF", "#4DE39B", "#FFD166",
            "#FF6978", "#9BAAB4");

    internal static SimulatorThemePalette CreateSystemHighContrastPalette() =>
        new(
            "system-high-contrast",
            new Dictionary<string, Brush>(StringComparer.Ordinal)
            {
                ["WindowBrush"] = SystemColors.WindowBrush,
                ["LcdBrush"] = SystemColors.WindowBrush,
                ["PanelBrush"] = SystemColors.WindowBrush,
                ["RaisedBrush"] = SystemColors.ControlBrush,
                ["TextBrush"] = SystemColors.WindowTextBrush,
                ["MutedBrush"] = SystemColors.WindowTextBrush,
                ["AccentBrush"] = SystemColors.HighlightBrush,
                ["AccentTextBrush"] = SystemColors.HighlightTextBrush,
                ["BorderBrush"] = SystemColors.WindowTextBrush,
                ["FocusBrush"] = SystemColors.WindowTextBrush,
                ["GoodBrush"] = SystemColors.WindowTextBrush,
                ["WarningBrush"] = SystemColors.WindowTextBrush,
                ["CriticalBrush"] = SystemColors.WindowTextBrush,
                ["DisabledBrush"] = SystemColors.GrayTextBrush,
            });

    internal static SimulatorThemePalette CreateDeterministicHighContrastPalette() =>
        new(
            "deterministic-high-contrast",
            new Dictionary<string, Brush>(StringComparer.Ordinal)
            {
                ["WindowBrush"] = Brushes.Black,
                ["LcdBrush"] = Brushes.Black,
                ["PanelBrush"] = Brushes.Black,
                ["RaisedBrush"] = Brushes.Black,
                ["TextBrush"] = Brushes.White,
                ["MutedBrush"] = Brushes.White,
                ["AccentBrush"] = Brushes.Yellow,
                ["AccentTextBrush"] = Brushes.Black,
                ["BorderBrush"] = Brushes.White,
                ["FocusBrush"] = Brushes.White,
                ["GoodBrush"] = Brushes.White,
                ["WarningBrush"] = Brushes.Yellow,
                ["CriticalBrush"] = Brushes.White,
                ["DisabledBrush"] = Brushes.Yellow,
            });

    internal static void Apply(ResourceDictionary resources, SimulatorThemePalette palette)
    {
        ArgumentNullException.ThrowIfNull(resources);
        ArgumentNullException.ThrowIfNull(palette);
        if (string.IsNullOrWhiteSpace(palette.Name) || palette.Name.Length > 64)
        {
            throw new ArgumentException("Theme name is invalid.", nameof(palette));
        }

        foreach (var key in BrushKeys)
        {
            if (!palette.Brushes.TryGetValue(key, out var brush) || brush is null)
            {
                throw new ArgumentException($"Theme palette is missing {key}.", nameof(palette));
            }
        }

        foreach (var key in BrushKeys)
        {
            resources[key] = palette.Brushes[key];
        }

        resources["SimulatorThemeMode"] = palette.Name;
    }

    private static SimulatorThemePalette CreatePalette(string name, params string[] colors)
    {
        if (colors.Length != BrushKeys.Length)
        {
            throw new ArgumentException("Theme color count is invalid.", nameof(colors));
        }

        var brushes = new Dictionary<string, Brush>(StringComparer.Ordinal);
        for (var index = 0; index < BrushKeys.Length; ++index)
        {
            var brush = new SolidColorBrush(
                (Color)ColorConverter.ConvertFromString(colors[index]));
            brush.Freeze();
            brushes[BrushKeys[index]] = brush;
        }

        return new SimulatorThemePalette(name, brushes);
    }
}
