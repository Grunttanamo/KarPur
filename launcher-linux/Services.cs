using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using KarPurLauncher;

namespace KarPurLinux;

sealed class UserState
{
    public static bool IsSteamDeck
    {
        get
        {
            if (Environment.GetEnvironmentVariable("SteamDeck") == "1") return true;
            try
            {
                if (File.ReadAllLines("/etc/os-release").Any(line =>
                    line.Trim().Equals("ID=steamos", StringComparison.OrdinalIgnoreCase) ||
                    line.Trim().Equals("ID=\"steamos\"", StringComparison.OrdinalIgnoreCase))) return true;
                return File.ReadAllText("/sys/class/dmi/id/board_vendor").Trim() == "Valve" &&
                    (File.ReadAllText("/sys/class/dmi/id/product_name").Trim() is "Jupiter" or "Galileo");
            }
            catch { return false; }
        }
    }
    public static string DirectoryPath => Path.Combine(
        Environment.GetEnvironmentVariable("XDG_CONFIG_HOME") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config"), "karpur-linux");
    static string FilePath => Path.Combine(DirectoryPath, "launcher.json");
    public string Target { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share", "KarPur");
    public string Base { get; set; } = "";
    public string Update { get; set; } = "";
    public string Keys { get; set; } = "";
    public string Firmware { get; set; } = "";
    public string Dlc { get; set; } = "";
    public string Colour { get; set; } = "auto";
    public bool DeckControls { get; set; } = IsSteamDeck;
    public static UserState Load()
    {
        var state = File.Exists(FilePath) ? JsonSerializer.Deserialize<UserState>(File.ReadAllText(FilePath)) ?? new() : new();
        string? appImage = Environment.GetEnvironmentVariable("APPIMAGE");
        string? adjacent = string.IsNullOrWhiteSpace(appImage) ? null : Path.GetDirectoryName(appImage);
        if (adjacent != null && File.Exists(Path.Combine(adjacent, "build", "game", "karpur")) &&
            !File.Exists(Path.Combine(state.Target, "build", "game", "karpur")))
            state.Target = adjacent;
        return state;
    }
    public void Save()
    {
        Directory.CreateDirectory(DirectoryPath);
        string temporary = FilePath + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporary, FilePath, true);
    }
}

static class BundledFiles
{
    public static string Payload()
    {
        var roots = new[]
        {
            AppContext.BaseDirectory,
            Path.GetDirectoryName(Environment.ProcessPath),
            Environment.GetEnvironmentVariable("APPDIR") is { Length: > 0 } appDir ? Path.Combine(appDir, "usr", "lib", "karpur") : null,
        };
        foreach (string? root in roots.Distinct())
        {
            if (string.IsNullOrWhiteSpace(root)) continue;
            string path = Path.Combine(root, "payload.zip");
            if (File.Exists(path)) return path;
        }
        string location = Environment.GetEnvironmentVariable("APPDIR") is { Length: > 0 }
            ? "The source package is missing from this AppImage."
            : "The Linux executable has no source package beside it. Run the .AppImage file or extract the complete Linux folder package.";
        throw new FileNotFoundException(location + " Checked: " +
            string.Join(", ", roots.Where(r => !string.IsNullOrWhiteSpace(r)).Distinct().Select(r => Path.Combine(r!, "payload.zip"))));
    }
}

static class Install
{
    public static string Game(string root) => Path.Combine(root, "build", "game", "karpur");
    public static string Profile(string root) => Path.Combine(root, "build", "profile");
    public static string Python(string root) => Path.Combine(root, "toolchain", "python", "bin", "python3");
    public static bool GameRunning => Process.GetProcessesByName("karpur").Any();
    public static string? PinLauncher(string root, byte[] icon)
    {
        string? source = Environment.GetEnvironmentVariable("APPIMAGE");
        if (string.IsNullOrWhiteSpace(source) || !File.Exists(source)) return null;
        if (root.Contains('\n') || root.Contains('\r')) throw new InvalidDataException("Invalid installation folder name.");
        string target = Path.Combine(root, "KarPur-Linux-Launcher.AppImage");
        Directory.CreateDirectory(root);
        string marker = Path.Combine(root, ".karpur-linux-launcher-version");
        bool current = File.Exists(target) && File.Exists(marker) &&
            Version.TryParse(File.ReadAllText(marker), out Version? installed) &&
            installed >= Version.Parse(Program.BuildRevision);
        if (!Path.GetFullPath(source).Equals(Path.GetFullPath(target), StringComparison.Ordinal) && !current)
        {
            string temporary = target + ".new";
            try
            {
                File.Copy(source, temporary, true);
                if (OperatingSystem.IsLinux())
                    File.SetUnixFileMode(temporary, File.GetUnixFileMode(source) | UnixFileMode.UserExecute);
                File.Move(temporary, target, true);
                File.WriteAllText(marker, Program.BuildRevision);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
        else if (!File.Exists(marker)) File.WriteAllText(marker, Program.BuildRevision);
        RegisterLauncher(root, icon);
        return target;
    }

    internal static void RegisterLauncher(string root, byte[] icon)
    {
        string target = Path.Combine(root, "KarPur-Linux-Launcher.AppImage");
        string image = Path.Combine(root, "karpur-launcher-icon.png");
        File.WriteAllBytes(image, icon);
        string dataHome = Environment.GetEnvironmentVariable("XDG_DATA_HOME") ??
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share");
        string application = Path.Combine(dataHome, "applications", "KarPur-Linux-Launcher.desktop");
        Directory.CreateDirectory(Path.GetDirectoryName(application)!);
        static string Quoted(string path) => "\"" + path.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("$", "\\$")
            .Replace("`", "\\`").Replace("%", "%%") + "\"";
        string entry = "[Desktop Entry]\nType=Application\nName=KarPur Linux Launcher\nComment=Play KarPur on Linux\n" +
            "Exec=" + Quoted(target) + "\nIcon=" + image + "\nTerminal=false\nCategories=Game;\n";
        File.WriteAllText(application, entry);
        string desktop = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Desktop");
        if (Directory.Exists(desktop))
        {
            string shortcut = Path.Combine(desktop, "KarPur Linux Launcher.desktop");
            File.WriteAllText(shortcut, entry);
            if (OperatingSystem.IsLinux())
                File.SetUnixFileMode(shortcut, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        }
    }
    public static ProcessStartInfo StartInfo(string root, bool deckControls = false)
    {
        var start = new ProcessStartInfo(Game(root)) { WorkingDirectory = root, UseShellExecute = false };
        start.ArgumentList.Add("--exefs");
        start.ArgumentList.Add(Path.Combine(root, "game", "extracted", "update", "exefs"));
        start.ArgumentList.Add("--romfs");
        start.ArgumentList.Add(Path.Combine(root, "game", "romfs"));
        start.ArgumentList.Add("--profile");
        start.ArgumentList.Add(Profile(root));
        start.Environment.Remove("APPIMAGE");
        start.Environment.Remove("APPDIR");
        if (deckControls) start.Environment["KARPUR_DECK_CONTROLS"] = "1";
        return start;
    }

    public static string SafePath(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || relative.Contains('\\') || relative.Contains('\0') || Path.IsPathRooted(relative))
            throw new InvalidDataException("Invalid path in archive.");
        string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string full = Path.GetFullPath(Path.Combine(fullRoot, relative));
        if (!full.StartsWith(fullRoot, StringComparison.Ordinal)) throw new InvalidDataException("Archive path escapes the installation folder.");
        string? parent = Path.GetDirectoryName(full);
        while (parent != null && parent.Length >= fullRoot.Length - 1)
        {
            if (new DirectoryInfo(parent).LinkTarget != null) throw new InvalidDataException("An installation folder is a symbolic link.");
            parent = Path.GetDirectoryName(parent);
        }
        if (new FileInfo(full).LinkTarget != null) throw new InvalidDataException("An installation file is a symbolic link.");
        return full;
    }

    public static void Unpack(string archive, string root, CancellationToken cancel, Action<int, int>? report = null)
    {
        using var zip = ZipFile.OpenRead(archive);
        // Validate every entry before touching the installation.
        foreach (var entry in zip.Entries)
        {
            if ((entry.ExternalAttributes >> 16 & 0xf000) == 0xa000) throw new InvalidDataException("Symbolic link in source package.");
            if (!entry.FullName.EndsWith('/')) SafePath(root, entry.FullName);
        }
        int total = zip.Entries.Count(e => !e.FullName.EndsWith('/'));
        int prepared = 0;
        foreach (var entry in zip.Entries)
        {
            cancel.ThrowIfCancellationRequested();
            if (entry.FullName.EndsWith('/')) continue;
            string dest = SafePath(root, entry.FullName);
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            string tmp = dest + ".linux-update-" + Guid.NewGuid().ToString("N");
            try
            {
                entry.ExtractToFile(tmp);
                if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(tmp, (UnixFileMode)((entry.ExternalAttributes >> 16) & 0x1ff));
                File.SetLastWriteTimeUtc(tmp, DateTime.UtcNow);
                bool unchanged = File.Exists(dest) && new FileInfo(dest).Length == entry.Length &&
                    System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(dest)).AsSpan().SequenceEqual(
                        System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(tmp)));
                if (!unchanged) File.Move(tmp, dest, true);
            }
            finally { if (File.Exists(tmp)) File.Delete(tmp); }
            prepared++;
            if (prepared % 500 == 0 || prepared == total) report?.Invoke(prepared, total);
        }
    }

    public static async Task Run(string exe, IEnumerable<string> args, string cwd, Action<string> log, CancellationToken token)
    {
        var start = new ProcessStartInfo(exe)
        {
            WorkingDirectory = cwd, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false,
        };
        foreach (string arg in args) start.ArgumentList.Add(arg);
        start.Environment["PYTHONUNBUFFERED"] = "1";
        start.Environment["PYTHONIOENCODING"] = "utf-8";
        using var process = Process.Start(start) ?? throw new IOException("Could not start the build process.");
        using var registration = token.Register(() => { try { process.Kill(entireProcessTree: true); } catch (InvalidOperationException) { } });
        async Task Read(StreamReader reader)
        {
            while (await reader.ReadLineAsync() is { } line) log(line);
        }
        await Task.WhenAll(Read(process.StandardOutput), Read(process.StandardError));
        await process.WaitForExitAsync(token);
        token.ThrowIfCancellationRequested();
        if (process.ExitCode != 0) throw new IOException($"{Path.GetFileName(exe)} exited with code {process.ExitCode}. See Logs for details.");
    }

    public static async Task Build(UserState input, Action<string> log, CancellationToken cancel)
    {
        if (!OperatingSystem.IsLinux()) throw new PlatformNotSupportedException("The game build requires Linux.");
        string root = Path.GetFullPath(input.Target);
        if (root == "/" || root == Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))
            throw new IOException("Choose a dedicated KarPur installation folder.");
        if (GameRunning) throw new IOException("Close the running game first.");
        string archive = BundledFiles.Payload();
        Directory.CreateDirectory(root);
        using var buildLock = new FileStream(Path.Combine(root, ".linux-build.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        log("Preparing project files …");
        await Task.Run(() => Unpack(archive, root, cancel, (done, total) => log($"Project files: {done:N0} of {total:N0} prepared")), cancel);
        string python = Python(root);
        if (!File.Exists(python)) throw new IOException("Bundled Linux Python is missing.");
        var arguments = new List<string> { "tools/setup_build.py", "--base", input.Base, "--update", input.Update, "--keys", input.Keys, "--keep" };
        if (input.Firmware.Length > 0) arguments.AddRange(["--firmware", input.Firmware]);
        if (input.Dlc.Length > 0) arguments.AddRange(["--dlc", input.Dlc]);
        using var writer = new StreamWriter(Path.Combine(root, "launcher-linux.log"), append: true) { AutoFlush = true };
        void Report(string line) { lock (writer) writer.WriteLine(line); log(line); }
        await Run(python, arguments, root, Report, cancel);
        if (!File.Exists(Game(root))) throw new IOException("The build did not create a game executable.");
        File.WriteAllText(Path.Combine(root, ".karpur-linux-version"), Program.Version);
    }

    public static void SaveSettings(string root, string fps, string window, bool handheld, string upscaler, string present, string overlay,
        bool fullscreen = false, string shadow = "0", string population = "switch", bool noExclusives = false,
        bool noTradeEvo = false, string stutter = "off", int sharpness = 80, string gpu = "", bool safeSubmit = false)
    {
        if (!File.Exists(Game(root))) throw new IOException("Select or create a Linux game installation first.");
        if (GameRunning) throw new IOException("Close the game before saving settings.");
        var ini = new Ini(Path.Combine(root, "build", "game", "karpur.ini"));
        ini.Set("frame rate", "fps_limit", fps);
        ini.Set("display", "window_size", window);
        ini.Set("display", "fullscreen", fullscreen ? "1" : "0");
        ini.Set("display", "operation_mode", handheld ? "handheld" : "docked");
        ini.Set("display", "upscaler", upscaler);
        ini.Set("display", "vk_present", present);
        ini.Set("display", "gpu_name", gpu);
        ini.Set("display", "gpu_index", "");
        ini.Set("display", "perf_overlay", overlay);
        ini.Set("display", "shadow_res", shadow);
        ini.Set("display", "population", population);
        ini.Set("display", "no_exclusives", noExclusives ? "1" : "0");
        ini.Set("display", "no_trade_evo", noTradeEvo ? "1" : "0");
        ini.Set("display", "sharpness", sharpness.ToString());
        ini.Set("performance", "acquire_ahead", stutter == "off" ? "0" : "1");
        ini.Set("performance", "core_lazy", stutter == "aggressive" ? "1" : "0");
        ini.Set("compatibility", "submit_defer", safeSubmit ? "0" : "1");
        ini.Save();
    }

    public static string SaveDirectory(string root)
    {
        string profile = Profile(root);
        string saves = Path.Combine(profile, "save");
        if (Directory.Exists(saves))
        {
            var existing = Directory.GetDirectories(saves).Where(d => Path.GetFileName(d).Length == 32 &&
                Path.GetFileName(d).All(Uri.IsHexDigit) && File.Exists(Path.Combine(d, "main"))).ToArray();
            if (existing.Length == 1) return existing[0];
        }
        byte[] uid = File.ReadAllBytes(Path.Combine(profile, "account.uid"));
        if (uid.Length != 16) throw new InvalidDataException("No valid user ID yet. Start the game once.");
        return Path.Combine(saves, Convert.ToHexString(uid.Reverse().ToArray()).ToLowerInvariant());
    }

    public static void ExportSave(string root, string destination)
    {
        if (GameRunning) throw new IOException("Close the game first.");
        string profile = Profile(root);
        if (Directory.Exists(Path.Combine(profile, "save-tx")) && Directory.EnumerateDirectories(Path.Combine(profile, "save-tx"), "ready-*").Any())
            throw new IOException("A pending save needs to be applied. Start and close the game once.");
        string save = SaveDirectory(root);
        if (!File.Exists(Path.Combine(save, "main"))) throw new IOException("No completed save found.");
        ZipFile.CreateFromDirectory(save, destination);
    }

    public static void ImportSave(string root, string archive)
    {
        if (GameRunning) throw new IOException("Close the game first.");
        string dest = SaveDirectory(root);
        string stage = Path.Combine(Profile(root), "import-" + Guid.NewGuid().ToString("N"));
        try
        {
            using (var zip = ZipFile.OpenRead(archive))
            {
                if (!zip.Entries.Any(e => e.FullName == "main")) throw new InvalidDataException("The ZIP must contain main at the top level.");
                if (zip.Entries.Sum(e => e.Length) > 128 * 1024 * 1024) throw new InvalidDataException("Save archive is too large.");
            }
            Unpack(archive, stage, CancellationToken.None);
            string backup = Path.Combine(Profile(root), "save-backup", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-linux-" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(backup);
            bool moved = false;
            try
            {
                if (Directory.Exists(dest)) { Directory.Move(dest, Path.Combine(backup, "current")); moved = true; }
                string tx = Path.Combine(Profile(root), "save-tx");
                if (Directory.Exists(tx)) foreach (string pending in Directory.GetDirectories(tx, "ready-*"))
                    Directory.Move(pending, Path.Combine(backup, Path.GetFileName(pending)));
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                Directory.Move(stage, dest);
            }
            catch
            {
                if (moved && !Directory.Exists(dest)) Directory.Move(Path.Combine(backup, "current"), dest);
                foreach (string pending in Directory.GetDirectories(backup, "ready-*"))
                    Directory.Move(pending, Path.Combine(Profile(root), "save-tx", Path.GetFileName(pending)));
                throw;
            }
        }
        finally { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
    }

    public static void Open(string path) => Process.Start(new ProcessStartInfo("xdg-open") { ArgumentList = { path }, UseShellExecute = false });

}
