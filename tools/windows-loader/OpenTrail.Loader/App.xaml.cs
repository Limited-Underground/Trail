using System.Windows;
using System.ComponentModel;
using Microsoft.Win32;

namespace OpenTrail.Loader;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        ApplySystemTheme();
        SystemParameters.StaticPropertyChanged += SystemParameters_StaticPropertyChanged;
        SystemEvents.UserPreferenceChanged += SystemEvents_UserPreferenceChanged;
        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemParameters.StaticPropertyChanged -= SystemParameters_StaticPropertyChanged;
        SystemEvents.UserPreferenceChanged -= SystemEvents_UserPreferenceChanged;
        base.OnExit(e);
    }

    internal void ApplyThemeForAcceptance(LoaderThemePalette palette) =>
        LoaderThemeResources.Apply(Resources, palette);

    private void SystemParameters_StaticPropertyChanged(
        object? sender,
        PropertyChangedEventArgs e)
    {
        if (!string.IsNullOrEmpty(e.PropertyName) &&
            !string.Equals(
                e.PropertyName,
                nameof(SystemParameters.HighContrast),
                StringComparison.Ordinal))
        {
            return;
        }

        QueueSystemThemeRefresh();
    }

    private void SystemEvents_UserPreferenceChanged(
        object sender,
        UserPreferenceChangedEventArgs e)
    {
        if (e.Category is not (
            UserPreferenceCategory.Accessibility or
            UserPreferenceCategory.Color or
            UserPreferenceCategory.General or
            UserPreferenceCategory.VisualStyle or
            UserPreferenceCategory.Window))
        {
            return;
        }

        QueueSystemThemeRefresh();
    }

    private void QueueSystemThemeRefresh()
    {
        if (Dispatcher.CheckAccess())
        {
            ApplySystemTheme();
        }
        else
        {
            _ = Dispatcher.BeginInvoke(ApplySystemTheme);
        }
    }

    private void ApplySystemTheme() =>
        LoaderThemeResources.Apply(
            Resources,
            SystemParameters.HighContrast
                ? LoaderThemeResources.CreateSystemHighContrastPalette()
                : LoaderThemeResources.CreateClassicPalette());
}
