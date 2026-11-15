using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using System.Text.RegularExpressions;
using System.Text.Json;
using KarPurLauncher;

namespace KarPurLinux;

sealed class MainWindow : Window
{
    readonly UserState state;
    readonly ContentControl page = new();
    readonly TextBlock status = Text("Ready", 13, "#AFA9C9");
    readonly TextBox details = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 280 };
    readonly ProgressBar progress = new() { Height = 4, Minimum = 0, Maximum = 100 };
    readonly List<Button> navigation = [];
    readonly List<string> log = [];
    readonly List<Control> operations = [];
    readonly Dictionary<string, TextBox> inputs = [];
    readonly Image sidebarLogo = new() { Height = 78, Stretch = Stretch.Uniform, HorizontalAlignment = HorizontalAlignment.Left };
    readonly TextBlock brandTitle = Text("KarPur", 25);
    readonly Button cancel = new() { Content = "Cancel", IsVisible = false };
    CancellationTokenSource? buildCancellation;
    bool busy;
    bool buildComplete;

    static TextBlock Text(string text, double size = 15, string color = "#F0ECFF") => new()
    {
        Text = text, FontSize = size, Foreground = Brush.Parse(color), TextWrapping = TextWrapping.Wrap,
    };
    static StackPanel Stack(params Control[] controls) => new StackPanel { Spacing = 16 }.With(controls);
    static Border Card(Control child, string background = "#211E32") => new()
    {
        Background = Brush.Parse(background), CornerRadius = new CornerRadius(16), Padding = new Thickness(24), Child = child,
    };
    static Bitmap Art(string name) => new(AssetLoader.Open(new Uri("avares://KarPur-Launcher-Linux/Assets/" + name)));
    Button Action(string title, Func<Task> action, bool primary = false)
    {
        var button = new Button { Content = title, Padding = new Thickness(20, 12), CornerRadius = new CornerRadius(9),
            Background = Brush.Parse(primary ? (Scarlet() ? "#E26077" : "#9C75F4") : "#302A46"), Foreground = Brushes.White,
            HorizontalContentAlignment = HorizontalAlignment.Center };
        operations.Add(button);
        button.IsEnabled = !busy;
        button.Click += async (_, _) => { try { await action(); } catch (Exception e) { Report(e.Message); } };
        return button;
    }

    public MainWindow(bool smoke)
    {
        try { state = smoke ? new UserState { Colour = Environment.GetEnvironmentVariable("KARPUR_SMOKE_COLOUR") ?? "auto" } : UserState.Load(); }
        catch { state = new UserState(); }
        Title = "KarPur Linux Launcher";
        Icon = new WindowIcon(AssetLoader.Open(new Uri("avares://KarPur-Launcher-Linux/Assets/icon-" + (Scarlet() ? "scarlet" : "violet") + ".png")));
        Width = 1120; Height = 820; MinWidth = 780; MinHeight = 650;
        Background = Brush.Parse("#14121F");
        status.MaxHeight = 52;
        FontFamily = new FontFamily("DejaVu Sans, sans-serif");
        var sidebar = new StackPanel { Spacing = 10, Margin = new Thickness(22, 30), Width = 194 };
        sidebarLogo.Source = Art("icon-" + (Scarlet() ? "scarlet" : "violet") + ".png");
        sidebarLogo.Width = 48; sidebarLogo.Height = 48;
        var brand = new Grid { ColumnDefinitions = new ColumnDefinitions("54,*"), ColumnSpacing = 4 };
        brand.Children.Add(sidebarLogo);
        brandTitle.Foreground = Brush.Parse(Scarlet() ? "#FFA9B4" : "#C9ADFF");
        var brandText = Stack(brandTitle,
            Text("Linux Launcher · " + Program.Version, 11, "#B1A6C8"));
        brandText.Spacing = 2;
        Grid.SetColumn(brandText, 1); brand.Children.Add(brandText);
        sidebar.Children.Add(brand);
        sidebar.Children.Add(new Border { Height = 26 });
        foreach (var (title, render) in new (string, Action)[] {
            ("Home", Home), ("Setup", Setup), ("Settings", Settings), ("Controls", Controls),
            ("Saves", Saves), ("Update", UpdatePage), ("Logs", Logs) })
        {
            var button = new Button { Content = title, HorizontalAlignment = HorizontalAlignment.Stretch,
                Padding = new Thickness(16, 13), Background = Brushes.Transparent, CornerRadius = new CornerRadius(9) };
            button.Click += (_, _) => { SelectNavigation(navigation.IndexOf(button)); render(); };
            navigation.Add(button); sidebar.Children.Add(button);
        }
        sidebar.Children.Add(new Border { Height = 24 });
        sidebar.Children.Add(Text("AppImage · x86_64", 12, "#9D94AF"));
        var root = new Grid { ColumnDefinitions = new ColumnDefinitions("238,*"), RowDefinitions = new RowDefinitions("*,Auto") };
        root.Children.Add(new Border { Background = Brush.Parse("#1B1728"), Child = sidebar });
        var scroll = new ScrollViewer { Content = page, Margin = new Thickness(32, 30, 32, 20), HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled };
        Grid.SetColumn(scroll, 1); root.Children.Add(scroll);
        cancel.Click += (_, _) => buildCancellation?.Cancel();
        var footer = Stack(progress, status, cancel);
        footer.Margin = new Thickness(270, 12, 32, 20);
        Grid.SetRow(footer, 1); Grid.SetColumnSpan(footer, 2); root.Children.Add(footer);
        Content = root;
        Closing += (_, e) => { if (busy) { e.Cancel = true; Report("Cancel the build and wait for it to stop before closing the launcher."); } };
        navigation[0].Background = Brush.Parse(Scarlet() ? "#4A2635" : "#34284D");
        Home();
        if (!smoke) Opened += async (_, _) =>
        {
            if (!File.Exists(Install.Game(state.Target))) return;
            try
            {
                byte[] icon = EditionIcon();
                await Task.Run(() => Install.PinLauncher(state.Target, icon));
            }
            catch (Exception e) { Report("Could not create the launcher shortcut: " + e.Message); }
        };
        if (smoke) Opened += async (_, _) =>
        {
            foreach (var (name, show) in new (string, Action)[] { ("home", Home), ("setup", Setup),
                ("settings", Settings), ("controls", Controls), ("update", UpdatePage), ("saves", Saves),
                ("complete", () => { buildComplete = true; progress.IsVisible = false; Logs(); }) })
            {
                show();
                await Task.Delay(250);
                using var image = new RenderTargetBitmap(new PixelSize((int)Bounds.Width, (int)Bounds.Height), new Vector(96, 96));
                image.Render(this);
                image.Save(Path.Combine(Environment.CurrentDirectory, "linux-launcher-" + name + ".png"));
            }
            Close();
        };
    }

    void Page(string eyebrow, string title, params Control[] children)
    {
        var body = Stack(Text(eyebrow.ToUpperInvariant(), 12, "#B49CE1"), Text(title, 30));
        foreach (Control control in children) body.Children.Add(control);
        page.Content = body;
    }
    void SelectNavigation(int index)
    {
        foreach (var button in navigation) button.Background = Brushes.Transparent;
        if (index >= 0 && index < navigation.Count)
            navigation[index].Background = Brush.Parse(Scarlet() ? "#4A2635" : "#34284D");
    }
    bool Scarlet()
    {
        if (state.Colour == "scarlet") return true;
        if (state.Colour == "violet") return false;
        try { return File.ReadAllText(Path.Combine(state.Target, "build", "setup_info.json")).Contains("scarlet"); }
        catch { return false; }
    }
    byte[] EditionIcon()
    {
        using var stream = AssetLoader.Open(new Uri("avares://KarPur-Launcher-Linux/Assets/icon-" +
            (Scarlet() ? "scarlet" : "violet") + ".png"));
        using var bytes = new MemoryStream();
        stream.CopyTo(bytes);
        return bytes.ToArray();
    }
    void ApplyEdition()
    {
        string name = Scarlet() ? "scarlet" : "violet";
        Icon = new WindowIcon(AssetLoader.Open(new Uri("avares://KarPur-Launcher-Linux/Assets/icon-" + name + ".png")));
        sidebarLogo.Source = Art("icon-" + name + ".png");
        brandTitle.Foreground = Brush.Parse(Scarlet() ? "#FFA9B4" : "#C9ADFF");
        foreach (var button in navigation)
            if (button.Background != Brushes.Transparent)
                button.Background = Brush.Parse(Scarlet() ? "#4A2635" : "#34284D");
    }
    void Home()
    {
        bool installed = File.Exists(Install.Game(state.Target));
        var play = Action(installed ? "▶  Play" : "Set up now", () =>
        {
            if (!File.Exists(Install.Game(state.Target))) { Setup(); Report("Set up the game or select its installation folder first."); return Task.CompletedTask; }
            if (Install.GameRunning) throw new IOException("KarPur is already running.");
            ProcessGame(); return Task.CompletedTask;
        }, true);
        var heroBody = new Grid { RowDefinitions = new RowDefinitions("*,Auto") };
        var banner = new Image { Source = Art("banner-" + (Scarlet() ? "scarlet" : "violet") + ".jpg"),
            Stretch = Stretch.UniformToFill };
        Grid.SetRowSpan(banner, 2); heroBody.Children.Add(banner);
        var logo = new Image { Source = Art("logo-" + (Scarlet() ? "scarlet" : "violet") + ".png"),
            Width = 230, Height = 125, Stretch = Stretch.Uniform,
            HorizontalAlignment = Scarlet() ? HorizontalAlignment.Left : HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = Scarlet() ? new Thickness(24, 12, 0, 0) : new Thickness(0, 12, 24, 0) };
        heroBody.Children.Add(logo);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12,
            HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(18) };
        actions.Children.Add(play);
        actions.Children.Add(Action("Open game folder", () =>
        {
            if (!Directory.Exists(state.Target)) throw new IOException("Set up the game first.");
            Install.Open(state.Target); return Task.CompletedTask;
        }));
        Grid.SetRow(actions, 1); heroBody.Children.Add(actions);
        if (installed)
        {
            string dlc = Directory.Exists(Path.Combine(state.Target, "game", "dlc")) &&
                Directory.EnumerateDirectories(Path.Combine(state.Target, "game", "dlc")).Any() ? "DLC installed" : "No DLC";
            var chips = Text("✓  Native Linux game     ·     " + dlc, 13, "#FFFFFF");
            chips.Background = Brush.Parse("#AA1B1728");
            chips.Padding = new Thickness(10, 7);
            chips.HorizontalAlignment = HorizontalAlignment.Left;
            chips.VerticalAlignment = VerticalAlignment.Bottom;
            chips.Margin = new Thickness(18);
            Grid.SetRow(chips, 1); heroBody.Children.Add(chips);
        }
        var hero = new Border { Height = 315, CornerRadius = new CornerRadius(16), ClipToBounds = true, Child = heroBody };
        var content = Stack(hero);
        content.Children.Add(new Border
        {
            Background = Brush.Parse("#352735"), BorderBrush = Brush.Parse("#D78C89"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(14), Padding = new Thickness(20),
            Child = Stack(Text("Experimental Linux / Steam Deck release", 19, "#FFD3D0"),
                Text("Missing trees, disappearing scenery and performance problems are still being investigated. Linux and Vulkan contributors: please report what you see, include your device and graphics driver, and share logs if you can. Pull requests are welcome.", 14, "#F1DAE5"),
                Action("Report an issue on GitHub", () => { Install.Open("https://github.com/Grunttanamo/KarPur/issues/new/choose"); return Task.CompletedTask; }),
                Action("Contribute on GitHub", () => { Install.Open("https://github.com/Grunttanamo/KarPur/pulls"); return Task.CompletedTask; }))
        });
        Control? team = TeamCards();
        if (team != null) content.Children.Add(team);
        content.Children.Add(Card(Stack(Text(installed ? "Game ready to play" : "Your next adventure starts here", 20),
                Text(state.Target, 13, "#B9AECF"),
                Text(installed ? "Your installed edition is ready in the Linux launcher." :
                    "Build KarPur from your own game files in Setup.", 14, "#C1B7D1"))));
        content.Children.Add(Card(Stack(Text("Your edition. Your PC.", 20),
                Text("Manage graphics, controller settings and saves from the sidebar.", 14, "#C1B7D1"),
                Action(installed ? "Repair installation" : "Set up installation", () => { Setup(); return Task.CompletedTask; }))));
        page.Content = content;
    }
    Control? TeamCards()
    {
        string file = Path.Combine(Install.Profile(state.Target), "team.json");
        if (!File.Exists(file)) return null;
        try
        {
            var icons = new Dictionary<(int, int), string>();
            string directory = Path.Combine(state.Target, "build", "game", "icons");
            string catalog = Path.Combine(directory, "catalog.txt");
            if (File.Exists(catalog)) foreach (string line in File.ReadLines(catalog))
            {
                string[] parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length == 3 && int.TryParse(parts[0], out int species) && int.TryParse(parts[1], out int form))
                    icons.TryAdd((species, form), Path.Combine(directory, parts[2] + ".png"));
            }
            using var json = JsonDocument.Parse(File.ReadAllText(file));
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
            foreach (var member in json.RootElement.GetProperty("team").EnumerateArray().Take(6))
            {
                int species = member.GetProperty("species").GetInt32();
                int form = member.TryGetProperty("form", out var f) ? f.GetInt32() : 0;
                string nick = member.GetProperty("nickname").GetString() ?? "?";
                int level = member.GetProperty("level").GetInt32();
                bool shiny = member.TryGetProperty("shiny", out var shine) && shine.GetBoolean();
                var box = new StackPanel { Spacing = 5, HorizontalAlignment = HorizontalAlignment.Center };
                if ((icons.TryGetValue((species, form), out string? path) || icons.TryGetValue((species, 0), out path)) && File.Exists(path))
                    box.Children.Add(new Image { Source = new Bitmap(path), Width = 92, Height = 92, Stretch = Stretch.Uniform });
                else box.Children.Add(Text(nick.Length > 0 ? nick[..1].ToUpperInvariant() : "?", 34,
                    Scarlet() ? "#FFA9B4" : "#C9ADFF"));
                box.Children.Add(Text((shiny ? "★  " : "") + nick, 13));
                box.Children.Add(Text("Lv. " + level, 12, "#88D6DF"));
                row.Children.Add(new Border { Width = 120, MinHeight = 158, Padding = new Thickness(8),
                    Background = Brush.Parse("#25203C"), CornerRadius = new CornerRadius(12), Child = box });
            }
            return row.Children.Count == 0 ? null : Stack(Text("YOUR TEAM", 12, "#B49CE1"), row);
        }
        catch { return null; }
    }
    void ProcessGame()
    {
        var process = System.Diagnostics.Process.Start(Install.StartInfo(state.Target, state.DeckControls)) ?? throw new IOException("Could not start the game.");
        Report("KarPur started.");
        process.EnableRaisingEvents = true;
        process.Exited += (_, _) => Dispatcher.UIThread.Post(() => Report($"KarPur exited (code {process.ExitCode})."));
    }

    Control Pick(string key, string title, string value, bool folder, Action<string> save, params string[] extensions)
    {
        var box = new TextBox { Text = value, Watermark = title, IsEnabled = !busy, MinWidth = 100 };
        inputs[key] = box;
        box.LostFocus += (_, _) =>
        {
            try { save(box.Text?.Trim() ?? ""); state.Save(); }
            catch (IOException e) { Report(e.Message); }
            catch (UnauthorizedAccessException e) { Report(e.Message); }
        };
        var browse = Action("Browse …", async () =>
        {
            string? path;
            if (folder)
            {
                var selected = await StorageProvider.OpenFolderPickerAsync(new() { Title = title, AllowMultiple = false });
                path = selected.FirstOrDefault()?.Path.LocalPath;
            }
            else
            {
                var selected = await StorageProvider.OpenFilePickerAsync(new() { Title = title, AllowMultiple = false,
                    FileTypeFilter = [new(title) { Patterns = extensions.Select(e => "*." + e).ToArray() }] });
                path = selected.FirstOrDefault()?.Path.LocalPath;
            }
            if (path != null) { box.Text = path; save(path); state.Save(); }
        });
        var row = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 10 };
        row.Children.Add(box); Grid.SetColumn(browse, 1); row.Children.Add(browse);
        return Stack(Text(title, 13, "#C1B7D1"), row);
    }
    void Setup()
    {
        SelectNavigation(1);
        inputs.Clear();
        Page("Setup", "Build your game on Linux",
            Card(Stack(Text("Installation folder", 20), Pick("target", "Folder for KarPur", state.Target, true, v => state.Target = v))),
            Card(Stack(Text("Your game files", 20),
                Pick("base", "Base game (.xci or .nsp)", state.Base, false, v => state.Base = v, "xci", "nsp"),
                Pick("update", "Update (.nsp)", state.Update, false, v => state.Update = v, "nsp"),
                Pick("keys", "Your console's prod.keys", state.Keys, false, v => state.Keys = v, "keys"),
                Pick("firmware", "Firmware folder (required for NSP base games)", state.Firmware, true, v => state.Firmware = v),
                Pick("dlc", "DLC (.nsp, optional)", state.Dlc, false, v => state.Dlc = v, "nsp"))),
            Card(Stack(Text("Ready to build", 20),
                Text("The AppImage includes the compiler, Python, and required development files. Select your own game files and allow at least 30 GB of free disk space. The first build can take a while.", 14, "#C1B7D1"))),
            Action("Install / update / repair", Build, true));
    }
    async Task Build()
    {
        if (busy) return;
        buildComplete = false;
        state.Target = inputs["target"].Text?.Trim() ?? "";
        state.Base = inputs["base"].Text?.Trim() ?? "";
        state.Update = inputs["update"].Text?.Trim() ?? "";
        state.Keys = inputs["keys"].Text?.Trim() ?? "";
        state.Firmware = inputs["firmware"].Text?.Trim() ?? "";
        state.Dlc = inputs["dlc"].Text?.Trim() ?? "";
        if (!File.Exists(state.Base) || !File.Exists(state.Update) || !File.Exists(state.Keys))
            throw new IOException("Select existing files for the base game, update, and prod.keys.");
        if (string.IsNullOrWhiteSpace(state.Target)) throw new IOException("Choose an installation folder.");
        state.Save();
        busy = true;
        foreach (Control c in operations) c.IsEnabled = false;
        foreach (TextBox c in inputs.Values) c.IsEnabled = false;
        buildCancellation = new CancellationTokenSource();
        cancel.IsVisible = true;
        progress.IsVisible = true;
        progress.IsIndeterminate = true;
        Logs();
        try
        {
            await Task.Run(() => Install.Build(state, s => Dispatcher.UIThread.Post(() => Report(s)), buildCancellation.Token));
            await Dispatcher.UIThread.InvokeAsync(() => { }); // drain queued build messages before showing the final state
            try
            {
                byte[] icon = EditionIcon();
                string? installed = await Task.Run(() => Install.PinLauncher(state.Target, icon));
                if (installed != null) Report("Launcher shortcut created: " + installed);
            }
            catch (Exception e) { Report("Game built, but launcher shortcut could not be created: " + e.Message); }
            buildComplete = true;
            ApplyEdition();
            Report("Build complete. KarPur is ready to play.");
            progress.Value = 100;
            progress.IsVisible = false;
            Logs();
        }
        catch (OperationCanceledException) { Report("Build cancelled. The next run resumes after completed steps."); }
        finally
        {
            busy = false;
            foreach (Control c in operations) c.IsEnabled = true;
            cancel.IsVisible = false;
            progress.IsIndeterminate = false;
            if (buildComplete) progress.IsVisible = false;
            buildCancellation.Dispose(); buildCancellation = null;
        }
    }

    void Settings()
    {
        var ini = new Ini(Path.Combine(state.Target, "build", "game", "karpur.ini"));
        ComboBox Choice(string[] choices, string value) => new() { ItemsSource = choices, SelectedItem = choices.Contains(value) ? value : choices[0], HorizontalAlignment = HorizontalAlignment.Stretch };
        var fps = Choice(["30", "40", "45", "60", "90", "120", "144", "display"], ini.Get("fps_limit") ?? "60");
        var size = Choice(["1280x720", "1280x800", "1600x900", "1920x1080", "1920x1200", "2560x1080", "2560x1440", "3840x2160"], ini.Get("window_size") ?? "1280x800");
        var scale = Choice(["fsr", "bilinear"], ini.Get("upscaler") ?? "fsr");
        var shadow = Choice(["0", "2048", "3072", "4096", "off"], ini.Get("shadow_res") ?? "0");
        var population = Choice(["switch", "switch2", "enhanced", "extreme"], ini.Get("population") ?? "switch");
        var stutter = Choice(["off", "standard", "aggressive"], ini.Get("core_lazy") == "1" ? "aggressive" : ini.Get("acquire_ahead") == "1" ? "standard" : "off");
        var present = Choice(["mailbox", "fifo", "immediate"], ini.Get("vk_present") ?? "mailbox");
        var overlay = Choice(["off", "fps", "full"], ini.Get("perf_overlay") ?? "off");
        var gpu = Choice([""], ini.Get("gpu_name") ?? "");
        var colour = Choice(["auto", "violet", "scarlet"], state.Colour);
        colour.SelectionChanged += (_, _) =>
        {
            if (colour.SelectedItem is not string selected) return;
            state.Colour = selected;
            state.Save();
            ApplyEdition();
        };
        var sharpness = new Slider { Minimum = 0, Maximum = 100, Value = int.TryParse(ini.Get("sharpness"), out int value) ? Math.Clamp(value, 0, 100) : 80 };
        var handheld = new CheckBox { Content = "Handheld mode (720p, lower GPU load)", IsChecked = ini.Get("operation_mode") == "handheld" };
        var fullscreen = new CheckBox { Content = "Fullscreen", IsChecked = ini.Get("fullscreen") == "1" };
        var noExclusives = new CheckBox { Content = "All version-exclusive Pokémon", IsChecked = ini.Get("no_exclusives") == "1" };
        var noTradeEvo = new CheckBox { Content = "Trade evolutions without trading", IsChecked = ini.Get("no_trade_evo") == "1" };
        var safeSubmit = new CheckBox { Content = "Submit GPU work immediately (compatibility)", IsChecked = ini.Get("submit_defer") == "0" };
        Page("Settings", "Display and performance", Card(Stack(
            Text("FPS limit", 14), fps, Text("Window size", 14), size,
            Text("Window size controls output. The game scene uses docked or handheld resolution.", 13, "#B9AECF"),
            Text("Frame sync", 14), present, Text("Graphics card (blank = automatic)", 14), gpu,
            Text("Upscaling", 14), scale, Text("FSR sharpness", 14), sharpness, fullscreen, handheld,
            Text("Performance overlay", 14), overlay, Text("Shadow quality (0 = original)", 14), shadow,
            Text("Wild Pokémon", 14), population, Text("Stutter reduction", 14), stutter,
            safeSubmit, noExclusives, noTradeEvo,
            Text("Launcher colour", 14), colour,
            Action("Save settings", () =>
            {
                if (File.Exists(Install.Game(state.Target)))
                    Install.SaveSettings(state.Target, (string)fps.SelectedItem!, (string)size.SelectedItem!, handheld.IsChecked == true,
                        (string)scale.SelectedItem!, (string)present.SelectedItem!, (string)overlay.SelectedItem!, fullscreen.IsChecked == true,
                        (string)shadow.SelectedItem!, (string)population.SelectedItem!, noExclusives.IsChecked == true,
                        noTradeEvo.IsChecked == true, (string)stutter.SelectedItem!, (int)sharpness.Value,
                        (string)gpu.SelectedItem!, safeSubmit.IsChecked == true);
                state.Colour = (string)colour.SelectedItem!;
                state.Save();
                ApplyEdition();
                Report(File.Exists(Install.Game(state.Target)) ? "Settings saved. They apply the next time the game starts." :
                    "Launcher settings saved. Game settings can be saved after installation."); return Task.CompletedTask;
            }, true))));
        _ = DetectGpus(gpu, ini.Get("gpu_name") ?? "");
    }

    void Controls()
    {
        var ini = new Ini(Path.Combine(state.Target, "build", "game", "karpur.ini"));
        var deck = new CheckBox { Content = "Steam Deck controls", IsChecked = state.DeckControls };
        var layout = new ComboBox { ItemsSource = new[] { "auto", "nintendo", "xbox" },
            SelectedItem = ini.Get("pad_layout") is "nintendo" or "xbox" ? ini.Get("pad_layout") : "auto",
            HorizontalAlignment = HorizontalAlignment.Stretch };
        var swap = new CheckBox { Content = "Swap movement and camera sticks", IsChecked = ini.Get("stick_swap") == "1" };
        var invertX = new CheckBox { Content = "Invert camera horizontally", IsChecked = ini.Get("camera_invert_x") == "1" };
        var invertY = new CheckBox { Content = "Invert camera vertically", IsChecked = ini.Get("camera_invert_y") == "1" };
        var dead = new Slider { Minimum = 0, Maximum = 50,
            Value = int.TryParse(ini.Get("stick_deadzone"), out int value) ? Math.Clamp(value, 0, 50) : 8 };
        Page("Controls", "Controller setup",
            Card(Stack(Text("Steam Deck preset", 20), deck,
                Text("Nintendo positions: Deck B → Switch A, A → B, Y → X, X → Y. Left stick moves; right stick controls the camera. L4 opens the PC menu; R4 is + and R5 is − when Steam exposes the rear buttons to the game.", 14, "#C1B7D1"),
                Text("Steam Deck mode ignores Steam's duplicate keyboard and mouse gameplay input while a gamepad is connected.", 13, "#B9AECF"))),
            Card(Stack(Text("Button layout", 14), layout, Text("Stick dead zone (%)", 14), dead,
                swap, invertX, invertY,
                Action("Save controls", () =>
                {
                    state.DeckControls = deck.IsChecked == true;
                    state.Save();
                    if (File.Exists(Install.Game(state.Target)))
                    {
                        if (Install.GameRunning) throw new IOException("Close the game before saving controls.");
                        var settings = new Ini(Path.Combine(state.Target, "build", "game", "karpur.ini"));
                        settings.Set("input", "pad_layout", (string)layout.SelectedItem!);
                        settings.Set("input", "stick_swap", swap.IsChecked == true ? "1" : "0");
                        settings.Set("input", "camera_invert_x", invertX.IsChecked == true ? "1" : "0");
                        settings.Set("input", "camera_invert_y", invertY.IsChecked == true ? "1" : "0");
                        settings.Set("input", "stick_deadzone", ((int)dead.Value).ToString());
                        settings.Save();
                    }
                    Report("Controls saved. Restart the game to apply the Steam Deck mode.");
                    return Task.CompletedTask;
                }, true))));
    }
    async Task DetectGpus(ComboBox combo, string selected)
    {
        string exe = Install.Game(state.Target);
        if (!File.Exists(exe)) { combo.IsEnabled = false; return; }
        combo.IsEnabled = false;
        try
        {
            var start = new System.Diagnostics.ProcessStartInfo(exe, "--probe-vkdevice")
            {
                WorkingDirectory = state.Target, RedirectStandardError = true,
                RedirectStandardOutput = true, UseShellExecute = false,
            };
            using var process = System.Diagnostics.Process.Start(start);
            if (process == null) return;
            var error = process.StandardError.ReadToEndAsync();
            var output = process.StandardOutput.ReadToEndAsync();
            await Task.WhenAll(error, output, process.WaitForExitAsync());
            var names = new List<string> { "" };
            foreach (Match match in Regex.Matches(error.Result, @"vk: gpu \d+: (.+?) \(type \d+"))
            {
                string name = match.Groups[1].Value.Trim();
                if (!names.Contains(name)) names.Add(name);
            }
            if (selected.Length > 0 && !names.Contains(selected)) names.Add(selected);
            combo.ItemsSource = names;
            combo.SelectedItem = names.Contains(selected) ? selected : "";
        }
        catch { if (selected.Length > 0) { combo.ItemsSource = new[] { "", selected }; combo.SelectedItem = selected; } }
        finally { combo.IsEnabled = true; }
    }
    void Saves()
    {
        Page("Saves", "Take your progress with you", Card(Stack(
            Text("Import and export", 20), Text("Close the game before transferring saves. Import backs up the current save to save-backup.", 14, "#C1B7D1"),
            Action("Export save as ZIP", async () =>
            {
                var file = await StorageProvider.SaveFilePickerAsync(new() { Title = "Export save", SuggestedFileName = $"KarPur-save-{DateTime.Now:yyyyMMdd-HHmmss}.zip", DefaultExtension = "zip" });
                if (file != null) { await Task.Run(() => Install.ExportSave(state.Target, file.Path.LocalPath)); Report("Save exported."); }
            }),
            Action("Import save from ZIP", async () =>
            {
                var files = await StorageProvider.OpenFilePickerAsync(new() { Title = "Import save", AllowMultiple = false,
                    FileTypeFilter = [new("ZIP save") { Patterns = ["*.zip"] }] });
                if (files.Count == 0) return;
                var yes = new Button { Content = "Import and back up current save" };
                var no = new Button { Content = "Cancel" };
                var dialog = new Window { Title = "Replace save?", Width = 480, Height = 210,
                    WindowStartupLocation = WindowStartupLocation.CenterOwner,
                    Content = Card(Stack(Text("The selected save will load next time. Your current save is backed up first."), yes, no)) };
                yes.Click += (_, _) => dialog.Close(true); no.Click += (_, _) => dialog.Close(false);
                if (await dialog.ShowDialog<bool>(this)) { await Task.Run(() => Install.ImportSave(state.Target, files[0].Path.LocalPath)); Report("Save imported and previous save backed up."); }
            }),
            Action("Open save folder", () => { Install.Open(Install.Profile(state.Target)); return Task.CompletedTask; }))));
    }
    void UpdatePage()
    {
        var result = Text("Checking GitHub for a Linux release …", 16, "#C1B7D1");
        var actions = new StackPanel { Spacing = 12 };
        Page("Update", "Launcher updates",
            Card(Stack(Text("Installed launcher: " + Program.Version, 20), result, actions)));
        async Task Check()
        {
            result.Text = "Checking GitHub for a Linux release …";
            actions.Children.Clear();
            try
            {
                LinuxRelease? release = await Updates.Latest();
                if (release == null)
                {
                    result.Text = "No Linux AppImage has been published on GitHub yet. This launcher can check and install future Linux releases here.";
                    return;
                }
                if (release.Version <= Version.Parse(Program.Version))
                {
                    result.Text = "You have the latest Linux launcher (" + Program.Version + ").";
                    return;
                }
                result.Text = "Linux launcher " + release.Version + " is available.";
                actions.Children.Add(Action("Download and install update", async () =>
                {
                    if (!File.Exists(Install.Game(state.Target)))
                        throw new IOException("Set up the game or select its installation folder first.");
                    byte[] icon = EditionIcon();
                    progress.IsVisible = true;
                    progress.IsIndeterminate = false;
                    progress.Value = 0;
                    try
                    {
                        string target = await Updates.Download(release, state.Target, icon,
                            percent => Dispatcher.UIThread.Post(() => progress.Value = percent));
                        result.Text = "Update installed. Close this launcher and open KarPur Linux Launcher again.";
                        actions.Children.Clear();
                        Report("Updated launcher: " + target);
                    }
                    finally { progress.IsVisible = false; }
                }, true));
                actions.Children.Add(Action("View release on GitHub", () =>
                { Install.Open(release.PageUrl); return Task.CompletedTask; }));
            }
            catch (Exception e) { result.Text = "Could not check for updates: " + e.Message; }
            finally { actions.Children.Add(Action("Check again", Check)); }
        }
        _ = Check();
    }
    void Logs()
    {
        SelectNavigation(6);
        if (details.Parent is Border old) old.Child = null;
        if (buildComplete)
            Page("Logs", "Installation complete",
                Card(Stack(Text("✓  Ready to play", 24, "#8CE3B5"),
                    Text("The game and launcher are installed. Use Play in Home or the KarPur Linux Launcher shortcut.", 14, "#C1B7D1"),
                    Action("Play now", () => { ProcessGame(); return Task.CompletedTask; }, true))),
                Card(details));
        else Page("Logs", "Current activity", Card(details));
    }
    void Report(string message)
    {
        if (!message.StartsWith("PERCENT ") && !message.StartsWith("DONE "))
            status.Text = message.StartsWith("LOG ") ? message[4..] : message;
        log.Add(message);
        if (log.Count > 600) log.RemoveRange(0, log.Count - 600);
        details.Text = string.Join(Environment.NewLine, log);
        details.CaretIndex = details.Text.Length;
        if (message.StartsWith("PERCENT ") && int.TryParse(message[8..], out int pct))
        { progress.IsIndeterminate = false; progress.Value = Math.Clamp(pct, 0, 100); }
        if (message.StartsWith("STEP ")) progress.IsIndeterminate = true;
    }
}

static class LayoutExtensions
{
    public static StackPanel With(this StackPanel panel, IEnumerable<Control> controls)
    { foreach (Control control in controls) panel.Children.Add(control); return panel; }
}
