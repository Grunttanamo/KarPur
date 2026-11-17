using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace KarPurLinux;

static class Program
{
    public const string Version = "0.4.0";
    // The internal revision also replaces earlier Deck test launchers when pinning the AppImage.
    public const string BuildRevision = "0.4.0.1";
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--self-check")) return SelfCheck.Run(args.Contains("--require-payload"));
        try { return AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace().StartWithClassicDesktopLifetime(args); }
        catch (Exception e)
        {
            string path = Path.Combine(UserState.DirectoryPath, "launcher-start.log");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, e.ToString());
            Console.Error.WriteLine($"KarPur could not start. Details in {path}\n{e.Message}");
            return 1;
        }
    }
}

sealed class App : Application
{
    public override void Initialize()
    {
        RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
        Styles.Add(new FluentTheme());
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.MainWindow = new MainWindow(desktop.Args?.Contains("--smoke-test") == true);
        base.OnFrameworkInitializationCompleted();
    }
}
