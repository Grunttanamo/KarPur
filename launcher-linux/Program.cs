using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;
using System.Diagnostics;
using System.Globalization;

namespace KarPurLinux;

static class Program
{
    public const string Version = "0.4.5";
    // The internal revision also replaces earlier Deck test launchers when pinning the AppImage.
    public const string BuildRevision = "0.4.5.0";
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--self-check")) return SelfCheck.Run(args.Contains("--require-payload"));
        if (args.Contains("--prepare")) return Prepare(args);
        if (args.Contains("--play")) return Play(args);
        try { return AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace().StartWithClassicDesktopLifetime(args); }
        catch (Exception e)
        {
            string path = Path.Combine(UserState.DirectoryPath, "launcher-start.log");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, e.ToString());
            Console.Error.WriteLine($"PokiNative could not start. Details in {path}\n{e.Message}");
            return 1;
        }
    }

    static string? Option(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        if (i >= 0 && i + 1 < args.Length) return args[i + 1];
        string prefix = name + "=";
        return args.FirstOrDefault(a => a.StartsWith(prefix, StringComparison.Ordinal)) is { } value
            ? value[prefix.Length..] : null;
    }

    static int Fail(string message)
    {
        Console.Error.WriteLine(message.Replace("\r", " ").Replace("\n", " "));
        return 1;
    }

    static int Prepare(string[] args)
    {
        var state = new UserState
        {
            Target = Option(args, "--target") ?? "",
            Base = Option(args, "--game") ?? "",
            Update = Option(args, "--update") ?? "",
            Keys = Option(args, "--keys") ?? "",
            Dlc = Option(args, "--dlc") ?? "",
            Firmware = Option(args, "--firmware") ?? "",
        };
        if (string.IsNullOrWhiteSpace(state.Target)) return Fail("The --target installation folder is required.");
        if (!File.Exists(state.Base)) return Fail("The base game given with --game was not found.");
        string extension = Path.GetExtension(state.Base);
        if (!extension.Equals(".xci", StringComparison.OrdinalIgnoreCase) &&
            !extension.Equals(".nsp", StringComparison.OrdinalIgnoreCase))
            return Fail("The base game must be an .xci or .nsp file.");
        if (!File.Exists(state.Update)) return Fail("The update given with --update was not found.");
        if (!File.Exists(state.Keys)) return Fail("The prod.keys file given with --keys was not found.");
        if (state.Dlc.Length > 0 && !File.Exists(state.Dlc)) return Fail("The DLC file was not found.");
        if (state.Firmware.Length > 0 && !Directory.Exists(state.Firmware))
            return Fail("The firmware folder was not found.");
        if (extension.Equals(".nsp", StringComparison.OrdinalIgnoreCase) && state.Firmware.Length == 0)
            return Fail("An NSP base game needs --firmware <folder> with system .nca files.");

        void Report(double fraction, string text) =>
            Console.WriteLine($"progress {Math.Clamp(fraction, 0, 1).ToString("0.00", CultureInfo.InvariantCulture)} {text}");
        try
        {
            Install.Build(state, _ => { }, CancellationToken.None, Report).GetAwaiter().GetResult();
            return 0;
        }
        catch (Exception e) { return Fail(e.Message); }
    }

    static int Play(string[] args)
    {
        string? target = Option(args, "--target");
        if (string.IsNullOrWhiteSpace(target)) return Fail("The --target installation folder is required.");
        target = Path.GetFullPath(target);
        string exe = Install.Game(target);
        if (!File.Exists(exe)) return Fail("The PokiNative game executable was not found. Run setup first.");
        try
        {
            using var game = Process.Start(Install.StartInfo(target, UserState.Load().DeckControls))
                ?? throw new InvalidOperationException("Linux could not start the PokiNative game executable.");
            game.WaitForExit();
            return game.ExitCode;
        }
        catch (Exception e) { return Fail("Could not start PokiNative: " + e.Message); }
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
