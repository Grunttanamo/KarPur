using System.Reflection;

namespace KarPurLauncher;

static class Program
{
    // The launcher's version (KarPurLauncher.csproj <Version>), shown top left and compared with GitHub releases.
    public static readonly Version Current = Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 1, 0);
    public static string Version => $"{Current.Major}.{Current.Minor}.{Current.Build}";

    public static string? PreviewColour;
    public static bool DesignCheck;   // --shot: a picture of the window only, nothing else runs
    // A preview built inside the checkout starts its existing local game by default. This only chooses the initial
    // target: every Setup path stays editable and Builder.Root follows the target the user chooses.
    public static string? FindRepository(string directory)
    {
        for (var p = new DirectoryInfo(directory); p != null; p = p.Parent)
            if (Builder.IsSourceRoot(p.FullName)) return p.FullName;
        return null;
    }

    // Build from an external launcher using the same installer as the Setup tab.
    // Paths are passed as individual arguments and may contain spaces.
    static int Prepare(string[] args)
    {
        string? Opt(string name)
        {
            int i = Array.IndexOf(args, name);
            return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        }
        static int Fail(string message)
        {
            Console.Error.WriteLine(message.Replace("\r", " ").Replace("\n", " "));
            return 1;
        }
        string target = Opt("--target") ?? Path.GetDirectoryName(Environment.ProcessPath) ?? "";
        if (target != "") target = Path.TrimEndingDirectorySeparator(target);
        var inp = new Inputs(Opt("--game") ?? "", Opt("--update") ?? "", Opt("--dlc") ?? "", Opt("--keys") ?? "", target,
                             Opt("--firmware") ?? "");
        if (!File.Exists(inp.Base)) return Fail("The base game (.xci or .nsp) given with --game was not found.");
        bool nsp = inp.Base.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase);
        if (!nsp && !inp.Base.EndsWith(".xci", StringComparison.OrdinalIgnoreCase))
            return Fail("The base game must be an .xci or .nsp file.");
        if (!File.Exists(inp.Update)) return Fail("The update (.nsp) given with --update was not found.");
        if (!File.Exists(inp.Keys)) return Fail("The prod.keys given with --keys was not found.");
        if (inp.Dlc != "" && !File.Exists(inp.Dlc)) return Fail("The DLC file given with --dlc was not found.");
        if (inp.Firmware != "" && !Directory.Exists(inp.Firmware)) return Fail("The firmware folder given with --firmware was not found.");
        if (nsp && inp.Firmware == "") return Fail("An NSP base game needs --firmware <folder> with system .nca files.");
        if (inp.Target == "") return Fail("No target folder.");

        string status = "Starting";
        int percent = 0;
        void Report() => Console.WriteLine($"progress {(percent / 100.0).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture)} {status}");
        var builder = new Builder
        {
            Status = s => { status = s; Report(); },
            Percent = p => { p = Math.Clamp(p, 0, 100); if (p == percent) return; percent = p; Report(); },
        };
        string? error;
        try { error = builder.Run(inp, false, CancellationToken.None).GetAwaiter().GetResult(); }
        catch (Exception e) { error = e.Message; }
        if (error != null) return Fail(error);
        Builder.NoteBuilt(inp.Target);
        percent = 100;
        status = "Ready";
        Report();
        return 0;
    }

    [STAThread]
    static int Main(string[] args)
    {
        if (args.Contains("--prepare")) return Prepare(args);
        Updater.CleanUpOld();
        ApplicationConfiguration.Initialize();
        PreviewColour = args.FirstOrDefault(a => a.StartsWith("--colour="))?[9..];
        string tab = args.FirstOrDefault(a => a.StartsWith("--tab="))?[6..] ?? "Setup";   // --tab=Settings: open on that page
        var form = new MainForm(updated: args.Contains("--updated"), tab);
        // --shot=<png>: a picture of the window a moment after it opened, then exit (a check of the design)
        string? shot = args.FirstOrDefault(a => a.StartsWith("--shot="))?[7..];
        DesignCheck = shot != null;
        if (shot != null)
            form.Shown += async (_, _) =>
            {
                await Task.Delay(1500);
                // the window's own painting (not the screen: another window may lie on top)
                using var bmp = new Bitmap(form.Width, form.Height);
                form.DrawToBitmap(bmp, new Rectangle(0, 0, form.Width, form.Height));
                bmp.Save(shot);
                form.Close();
            };
        Application.Run(form);
        return 0;
    }
}
