using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace KarPurLauncher;

// The launcher window: a sidebar (name, version, Setup / Settings / Update, Play) and one page at a time. The paths and
// the colour choice are remembered in %LOCALAPPDATA%\KarPur\launcher.json (paths only, never file contents).
partial class MainForm : Form
{
    // Setup
    readonly TextBox baseBox = Theme.TextBox(), updateBox = Theme.TextBox(), dlcBox = Theme.TextBox(),
                     keysBox = Theme.TextBox(), targetBox = Theme.TextBox(), firmwareBox = Theme.TextBox();
    readonly Button buildButton = Theme.Button("Build", true, Theme.GlyphBuild),
                    repairButton = Theme.Button("Repair", glyph: Theme.GlyphRepair),
                    cancelButton = Theme.Button("Cancel", glyph: Theme.GlyphStop),
                    playButton = Theme.Button("Play", true, Theme.GlyphPlay);
    readonly Bar progress = new();
    readonly Label status = Theme.Label("Choose your files and a target folder, then press Build.", 9.5f, muted: true);
    readonly CheckBox details = new ChoiceBox { Text = "Show details", AutoSize = true, ForeColor = Theme.Muted, BackColor = Theme.Card };
    readonly TextBox log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Dock = DockStyle.Fill,
        Visible = false, BackColor = Theme.Card, ForeColor = Theme.Muted, BorderStyle = BorderStyle.None,
        Font = new Font("Cascadia Mono", 8.5f),
    };
    // Settings
    readonly CheckBox fullscreen = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, Text = "Start in fullscreen", BackColor = Theme.Card };
    readonly Label windowSizeNote = Theme.Label(
        "Window size applies in windowed mode. Fullscreen uses the monitor; Render scale sets the game's image size.",
        9.5f, muted: true);
    readonly Label renderScaleNote = Theme.Label(
        "New game: keep 1920 x 1080 through character creation, then choose another Render scale and restart. Other sizes can misplace the preview and ID photo.",
        9.5f, muted: true);
    // submit_defer = 0: every GPU submission goes out at once. Players whose game crashed after the character creation
    // reported that TOTK_SUBMIT_DEFER=0 fixes it; far trees may flicker a little and the frame rate can drop.
    readonly CheckBox safeSubmit = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card,
                                           Text = "Compatibility mode (try it if the game crashes after the character creation)" };
    readonly CheckBox keepHotMemory = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card,
                                           Text = "Keep hot memory active (try if compatibility mode does not help)" };
    readonly CheckBox noExclusives = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card,
                                           Text = "No version exclusives (the other edition's wild Pokémon spawn here too)" };
    readonly CheckBox noTradeEvo = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card,
                                           Text = "No trade evolutions (they evolve on a level, holding the item they needed if there was one)" };
    readonly ComboBox windowSize = Combo(), renderScale = Combo(), fpsLimit = Combo(), present = Combo(), upscaler = Combo(), overlay = Combo(),
                      population = Combo(), peopleShown = Combo(), peopleDetail = Combo(), pokemonDetail = Combo(), ao = Combo(), indirectLight = Combo(), colour = Combo(), antiAliasing = Combo(), dlssModel = Combo(), frameGen = Combo(),
                      gpu = Combo(), opMode = Combo(), shadowRes = Combo(), stutter = Combo();
    readonly Label gpuNote = Theme.Label("", 9.5f, muted: true);
    readonly Dictionary<Control, Label> rowLabels = new();
    readonly Button addStreamline = Theme.Button("Add Streamline files", glyph: Theme.GlyphFolder);
    readonly Label fgNote = Theme.Label("", 9.5f, muted: true);
    readonly Label stutterNote = Theme.Label(
        "Experimental timing changes that can smooth out the hitches when the game streams in Pokémon and "
        + "objects. Standard starts the next frame earlier; Aggressive also lets threads share the "
        + "processor cores more freely, which the console does not do. Leave this off if anything looks "
        + "wrong.", 9.5f, muted: true);
    readonly Label modelDetailNote = Theme.Label(
        "Minimum full-detail distance. An active LOD mod may already use a larger distance. This does not change spawning.",
        9.5f, muted: true);
    readonly Label peopleShownNote = Theme.Label(
        "How many static town people can have models at once. Higher counts may need more memory.",
        9.5f, muted: true);
    readonly Label lightingNote = Theme.Label(
        "Screen-space effects on the GPU. They can cost frame time; the game's memory pools stay unchanged.",
        9.5f, muted: true);
    readonly Label compatibilityNote = Theme.Label(
        "Keep hot memory active sets hot_cool = 0; turning it off restores 300. It may reduce performance over longer play.",
        9.5f, muted: true);
    readonly Label resizableBarNote = Theme.Label(
        "Resizable BAR can improve KarPur performance on supported PCs. In your motherboard's UEFI/BIOS, enable "
        + "Above 4G Decoding and Re-Size BAR (sometimes Smart Access Memory), then restart. Use UEFI boot; "
        + "check NVIDIA Control Panel > System Information > Resizable BAR: Yes. Menu names vary by motherboard.",
        9.5f, muted: true);
    readonly Button addDlss = Theme.Button("Download DLSS from NVIDIA", glyph: Theme.GlyphSetup);
    readonly Label dlssNote = Theme.Label("", 9.5f, muted: true);
    readonly TrackBar sharpness = new FocusSlider { Minimum = 0, Maximum = 100, TickFrequency = 10, Width = 280, BackColor = Theme.Card };
    readonly Label sharpnessValue = Theme.Label("80");
    readonly Label settingsNote = Theme.Label("", 9.5f, muted: true);
    readonly Button saveSettings = Theme.Button("Save settings", true, "");
    // Update
    readonly Label updateStatus = Theme.Label("", 10f);
    readonly Button checkUpdate = Theme.Button("Check for updates", true, Theme.GlyphUpdate),
                    installUpdate = Theme.Button("Download and install", glyph: Theme.GlyphSetup);
    readonly TextBox notes = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Theme.Card,
        ForeColor = Theme.Text, BorderStyle = BorderStyle.None, Visible = false,
    };
    Updater.Release? release;

    readonly LogoMark sidebarLogo = new();
    readonly Panel content = new Surface { Dock = DockStyle.Fill, BackColor = Theme.Back, Padding = new Padding(28, 24, 28, 24) };
    readonly Dictionary<string, (NavButton Nav, Control Page)> pages = new();
    string currentPage = "";
    CancellationTokenSource? cancel;
    readonly bool updated;

    static readonly string SettingsFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KarPur", "launcher.json");

    // Values the game's PC settings use (runtime/cfg.c), with the names shown here.
    static readonly (string Value, string Name)[] Sizes =
        { ("1280x720", "1280 × 720"), ("1600x900", "1600 × 900"), ("1920x1080", "1920 × 1080"), ("2560x1440", "2560 × 1440"),
          ("3840x2160", "3840 × 2160"), ("2560x1080", "2560 × 1080 (21:9)"),
          ("3440x1440", "3440 × 1440 (21:9)"), ("5120x1440", "5120 × 1440 (32:9)"),
          ("960x720", "960 × 720 (4:3)"), ("1440x1080", "1440 × 1080 (4:3)"),
          ("1200x800", "1200 × 800 (3:2)"), ("1620x1080", "1620 × 1080 (3:2)"),
          ("1280x800", "1280 × 800 (16:10)"), ("1920x1200", "1920 × 1200 (16:10)"),
          ("720x1280", "720 × 1280 (9:16)"), ("1080x1920", "1080 × 1920 (9:16)") };
    // The game draws its 3D scene and UI at this size, then presents it in the separately chosen window size.
    // 1080p is the game's original mode; the other sizes use host-owned render targets without growing its GPU pool.
    static readonly (string Value, string Name)[] RenderScales =
        { ("800x450", "450p (800 × 450)"), ("854x480", "480p (854 × 480)"), ("1280x720", "720p (1280 × 720)"),
          ("1920x1080", "1080p (original)"), ("2560x1440", "1440p (2560 × 1440)"),
          ("3840x2160", "2160p / 4K (3840 × 2160)"),
          ("2560x1080", "21:9 (2560 × 1080, experimental)"),
          ("3440x1440", "21:9 (3440 × 1440, experimental)"),
          ("5120x1440", "32:9 (5120 × 1440, experimental)"),
          ("1440x1080", "4:3 (1440 × 1080, experimental)"),
          ("1620x1080", "3:2 (1620 × 1080, experimental)"),
          ("1920x1200", "16:10 (1920 × 1200, experimental)"),
          ("1080x1920", "9:16 (1080 × 1920, experimental)") };
    static readonly (string, string)[] Fps =
        { ("30", "30"), ("40", "40"), ("45", "45"), ("60", "60 (recommended)"), ("90", "90 (experimental)"),
          ("120", "120 (experimental)"), ("144", "144 (experimental)"), ("display", "Display refresh rate (experimental)") };
    static readonly (string, string)[] Presents =
        { ("mailbox", "Smooth, no tearing (mailbox)"), ("fifo", "V-Sync (fifo)"), ("immediate", "Off, lowest latency (immediate)") };
    // The picture's way to the window: FSR 1 or bilinear scaling of the game's 1080p image, or NVIDIA DLSS resolving the
    // 3D scene at the window's size (or at 4K, scaled down to the window: supersampling). DLSS turns DLAA on with it.
    static readonly (string, string)[] Upscalers =
        { ("fsr", "AMD FSR 1"), ("bilinear", "Bilinear"), ("dlss", "NVIDIA DLSS (to the window size)"),
          ("dlss4k", "NVIDIA DLSS 4K (supersampling)"), ("fsr2", "AMD FSR 2 (to window)") };
    static readonly (string, string)[] AntiAliasings =
        { ("0", "Game's own"), ("1", "NVIDIA DLAA (RTX)"), ("2", "AMD FSR 2") };
    static readonly (string, string)[] FrameGens =
        { ("0", "Off"), ("2", "2x (DLSS Frame Generation)"), ("3", "3x (RTX 50)"), ("4", "4x (RTX 50)") };
    static readonly (string, string)[] GpuAuto = { ("", "Automatic (best card)") };
    // The console's own two output modes (runtime/ov_system.c): handheld renders at 1280x720, the console's own
    // lower-resolution mode, a real speed gain confirmed by the maintainer, no reverse engineering needed.
    static readonly (string, string)[] OperationModes =
        { ("docked", "Docked (1920x1080, full quality)"), ("handheld", "Handheld (1280x720, saves performance)") };
    // The generic TOTK_RENDER_SCALE percentage does not control Violet's own targets; RenderScales above selects
    // the actual output resolution through Violet's patch set.
    static readonly (string, string)[] DlssModels =
        { ("5", "CNN (steady, recommended)"), ("11", "Transformer (sharper, can smear)") };
    static readonly (string, string)[] Overlays = { ("off", "Off"), ("fps", "FPS only"), ("full", "Full") };
    static readonly (string, string)[] Populations =
        { ("switch", "Switch (15, default)"), ("switch2", "Switch 2 (40)"), ("enhanced", "Enhanced (80)"),
          ("extreme", "Extreme (120, can lower the frame rate)") };
    static readonly (string, string)[] PeopleShown =
        { ("12", "Game's own (12)"), ("24", "24"), ("36", "36"), ("48", "48") };
    static readonly (string, string)[] PeopleDetails =
        { ("1", "Game's own (8 m)"), ("2", "16 m"), ("4", "32 m"), ("6", "48 m"),
          ("8", "64 m"), ("10", "80 m") };
    static readonly (string, string)[] PokemonDetails =
        { ("1", "Game's own (10 m)"), ("2", "20 m"), ("4", "40 m"), ("6", "60 m"),
          ("8", "80 m") };
    static readonly (string, string)[] LightingLevels =
        { ("0", "Off (default)"), ("1", "Low"), ("2", "Medium"), ("3", "High") };
    static readonly (string, string)[] Colours = { ("auto", "Automatic (from your game)"), ("violet", "Violet"), ("scarlet", "Scarlet") };
    // Two runtime switches behind one row (contributed in #49 and #53): both are off by default, and the
    // launcher writes both so a level is unambiguous. Power users can still set either on its own in the ini.
    static readonly (string, string)[] Stutters =
        { ("off", "Off (default)"), ("standard", "Standard (experimental)"),
          ("aggressive", "Aggressive (experimental)") };
    static readonly (string, string)[] ShadowReses =
        { ("0", "Original (1024×1024)"), ("2048", "x2 (2048×2048)"), ("3072", "x3 (3072×3072)"), ("4096", "x4 (4096×4096)"),
          ("off", "Disabled (no sun shadows)") };

    readonly string startTab;

    public MainForm(bool updated, string tab = "Setup")
    {
        this.updated = updated;
        startTab = tab;
        Text = "KarPur Launcher";
        Font = new Font("Segoe UI", 10f);
        BackColor = Theme.Back;
        ForeColor = Theme.Text;
        MinimumSize = new Size(1000, 680);
        Size = new Size(1260, 840);
        DoubleBuffered = true;
        StartPosition = FormStartPosition.CenterScreen;
        try { Icon = Icon.ExtractAssociatedIcon(Environment.ProcessPath!); } catch { }

        Controls.Add(content);
        Controls.Add(Sidebar());
        AddPage("Home", HomePage());
        AddPage("Setup", SetupPage());
        AddPage("Settings", SettingsPage());
        AddPage("Controls", ControlsPage());
        AddPage("Saves", SavesPage());
        AddPage("Mods", ModsPage());
        AddPage("Update", UpdatePage());
        AddPage("Feedback", FeedbackPage());

        buildButton.Click += async (_, _) => await Build(repair: false);
        repairButton.Click += async (_, _) => await Build(repair: true);
        cancelButton.Click += (_, _) => cancel?.Cancel();
        cancelButton.Enabled = false;
        playButton.Click += (_, _) => Play();
        details.CheckedChanged += (_, _) => log.Visible = details.Checked;
        targetBox.TextChanged += (_, _) => Refresh_();
        sharpness.ValueChanged += (_, _) => sharpnessValue.Text = sharpness.Value.ToString();
        saveSettings.Click += (_, _) => SaveGameSettings();
        addDlss.Click += async (_, _) => await DownloadDlss();
        addStreamline.Click += (_, _) => AddStreamline();
        frameGen.SelectedIndexChanged += (_, _) =>
        {
            // frame generation runs on DLAA's picture at the window's size: both follow when it is switched on
            if (!loadingGame && Value(frameGen) != "0") { Select(antiAliasing, "1", "0"); Select(upscaler, "dlss", "fsr"); }
            FgStatus();
        };
        antiAliasing.SelectedIndexChanged += (_, _) =>
        {
            if (!loadingGame)
            {
                string aa = Value(antiAliasing);
                if (aa != "1" && Value(upscaler).StartsWith("dlss")) Select(upscaler, "fsr", "fsr");
                if (aa != "2" && Value(upscaler) == "fsr2") Select(upscaler, "fsr", "fsr");
                if (aa != "1") Select(frameGen, "0", "0");
            }
            DlssStatus();
            UpdateRows();
        };
        upscaler.SelectedIndexChanged += (_, _) =>
        {
            if (!loadingGame && Value(upscaler).StartsWith("dlss")) Select(antiAliasing, "1", "0");
            if (!loadingGame && Value(upscaler) == "fsr2") Select(antiAliasing, "2", "0");
            if (!loadingGame && !Value(upscaler).StartsWith("dlss")) Select(frameGen, "0", "0");   // frame generation needs DLSS's window-sized picture
            UpdateRows();
        };
        colour.SelectedIndexChanged += (_, _) => { ApplyColour(); if (!loading) SaveSettings(); };
        gpu.SelectedIndexChanged += (_, _) => { if (!loadingGame) UpdateGpuLock(); };
        checkUpdate.Click += async (_, _) => await CheckUpdate();
        installUpdate.Click += async (_, _) => await InstallUpdate();
        installUpdate.Visible = false;

        HandleCreated += (_, _) => Theme.DarkTitleBar(this);
        StartPad();   // the controller (PadNav.cs)
        Load += async (_, _) =>
        {
            LoadSettings();
            Refresh_();
            bool builtNow = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
            Show(pages.ContainsKey(startTab) && startTab != "Setup" ? startTab : builtNow ? "Home" : "Setup");
            // After an update, through the Update tab (--updated) or a launcher replaced by hand: the game's files and
            // build follow this launcher's version (only what changed is rebuilt).
            string builtWith = builtNow ? Builder.BuiltWith(targetBox.Text) : "";
            if (builtNow && !Program.DesignCheck && !Builder.IsSourceRoot(Builder.Root(targetBox.Text)) && (this.updated || builtWith != Program.Version))
            {
                status.Text = $"Updated to version {Program.Version}. Rebuilding what changed …";
                await Build(repair: false);
            }
        };
        FormClosing += (_, e) =>
        {
            if (cancel != null && MessageBox.Show(this, "A build is running. Stop it and close?", Text, MessageBoxButtons.YesNo,
                    MessageBoxIcon.Warning) == DialogResult.No)
                e.Cancel = true;
            else cancel?.Cancel();
        };
    }

    // ---- layout

    Control Sidebar()
    {
        var side = new Surface { Dock = DockStyle.Left, Width = 232, BackColor = Theme.Panel };
        var logo = sidebarLogo;
        logo.Size = new Size(46, 46);
        logo.Location = new Point(18, 20);
        var title = Theme.Label("KarPur", 17f, style: FontStyle.Bold);
        title.Location = new Point(70, 20);
        var version = Theme.Label($"Version {Program.Version}", 9f, muted: true);
        version.Location = new Point(72, 48);
        side.Controls.Add(logo);
        side.Controls.Add(title);
        side.Controls.Add(version);
        var library = Theme.Label("KARPUR", 8f, muted: true, style: FontStyle.Bold);
        library.Location = new Point(26, 101);
        side.Controls.Add(library);
        int y = 128;
        foreach (var (name, glyph) in new[] { ("Home", Theme.GlyphHome), ("Setup", Theme.GlyphSetup), ("Settings", Theme.GlyphSettings),
                                               ("Controls", Theme.GlyphGame), ("Saves", Theme.GlyphSave),
                                               ("Mods", ""), ("Update", Theme.GlyphUpdate),
                                               ("Feedback", Theme.GlyphFeedback) })
        {
            var b = new NavButton { Text = name, Glyph = glyph, Width = side.Width, Location = new Point(0, y) };
            b.Click += (_, _) => Show(name);
            side.Controls.Add(b);
            pages[name] = (b, new Panel());
            y += 50;
        }
        playButton.Font = new Font("Segoe UI Semibold", 13f);
        playButton.Size = new Size(184, 54);
        ((RoundButton)playButton).Radius = 12;
        playButton.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
        side.Controls.Add(playButton);
        side.Controls.Add(padBack);
        void PlaceSidebar()
        {
            playButton.Location = new Point(24, side.Height - 88);
            padBack.Location = new Point(26, side.Height - 136);
            int step = Math.Clamp((side.Height - 128 - 164) / pages.Count, 40, 50), row = 0;
            foreach (var (nav, _) in pages.Values)
            {
                nav.Top = 128 + step * row++;
                nav.Height = step - 2;
            }
        }
        side.Resize += (_, _) => PlaceSidebar();
        PlaceSidebar();
        return side;
    }

    void AddPage(string name, Control page)
    {
        page.Dock = DockStyle.Fill;
        if (page is ScrollableControl sc) sc.AutoScroll = true;   // pages taller than the window scroll
        DarkScroll.Apply(page);   // ... with Windows' dark scrollbars
        page.Visible = false;
        content.Controls.Add(page);
        pages[name] = (pages[name].Nav, page);
    }

    void Show(string name)
    {
        if (currentPage == name) { PadMarkSidebar(); return; } // entering with A keeps unsaved edits and scroll position
        currentPage = name;
        using var paint = new PagePaint(content);
        content.Padding = name == "Home" ? Padding.Empty : new Padding(28, 24, 28, 24);
        // Hide the old page before revealing the new one. Home used to paint while Setup still covered it.
        foreach (var (_, (_, page)) in pages) page.Visible = false;
        padInPage = false;
        ActiveControl = null;
        foreach (var (key, (nav, page)) in pages)
        {
            page.Visible = key == name;
            nav.Selected = key == name;
        }
        pages[name].Page.BringToFront();
        if (pages[name].Page is ScrollableControl shown) shown.AutoScrollPosition = Point.Empty;
        int pi = padTabs.IndexOf(name);   // the controller's cursor follows a tab chosen with the mouse
        if (pi >= 0) padIndex = pi;
        if (name == "Home") { RefreshHome(); WatchTeam(); }
        if (name == "Saves") RefreshSaves();
        if (name == "Mods") LoadMods();
        if (name == "Settings") {
            LoadGameSettings();
            // Loading the saved controls can focus a low row before the page finishes laying out.
            BeginInvoke(() => { if (currentPage == "Settings" && pages[name].Page is ScrollableControl settings)
                settings.AutoScrollPosition = Point.Empty; });
        }
        if (name == "Controls") LoadControls();
        if (name == "Feedback") _ = FillFeedback();
        PadMarkSidebar();
    }

    static TableLayoutPanel Grid(int columns) =>
        new() { Dock = DockStyle.Top, AutoSize = true, ColumnCount = columns, BackColor = Theme.Card };

    static Card CardOf(params Control[] children)
    {
        var card = new Card { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink };
        foreach (var c in children.Reverse()) card.Controls.Add(c);
        return card;
    }

    static Control Spacer(int h, Color? back = null) => new Panel { Dock = DockStyle.Top, Height = h, BackColor = back ?? Theme.Back };

    static Control Header(string title, string subtitle)
    {
        var p = new Panel { Dock = DockStyle.Top, Height = 86, BackColor = Theme.Back };
        var t = Theme.Label(title, 22f, style: FontStyle.Bold);
        var s = Theme.Label(subtitle, 9.5f, muted: true);
        s.Location = new Point(3, 50);
        p.Controls.Add(t);
        p.Controls.Add(s);
        return p;
    }

    static ComboBox Combo() => new RoundCombo { Width = 320 };   // rounded, dark (Controls2.cs)

    Control SetupPage()
    {
        var page = new Surface { BackColor = Theme.Back };
        var grid = Grid(3);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        FileRow(grid, "Base game (.xci or .nsp)", baseBox, () => PickFile(baseBox, "Base game (*.xci;*.nsp)|*.xci;*.nsp|XCI cartridge dump|*.xci|NSP base game (needs firmware)|*.nsp"));
        FileRow(grid, "Update (.nsp)", updateBox, () => PickFile(updateBox, "Update as NSP|*.nsp"));
        FileRow(grid, "DLC (.nsp, optional)", dlcBox, () => PickFile(dlcBox, "DLC as NSP|*.nsp"));
        FileRow(grid, "prod.keys", keysBox, () => PickFile(keysBox, "Keys|*.keys|All files|*.*"));
        // Optional: the console's system NCAs for the shared fonts, when the base game is an NSP or an XCI converted from
        // one, which lacks them (issue #18).
        FileRow(grid, "Firmware folder (NSP or converted XCI)", firmwareBox, PickFirmware);
        FileRow(grid, "Target folder", targetBox, PickFolder);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        details.Margin = new Padding(6, 10, 0, 0);
        buttons.Controls.AddRange(new Control[] { buildButton, repairButton, cancelButton, details });
        var statusPanel = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Theme.Card };
        status.Location = new Point(0, 8);
        statusPanel.Controls.Add(status);
        var logPanel = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 4, 0, 0), BackColor = Theme.Back };
        logPanel.Controls.Add(log);

        page.Controls.Add(logPanel);
        page.Controls.Add(Spacer(10));
        page.Controls.Add(CardOf(buttons, Spacer(16, Theme.Card), progress, statusPanel));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Setup", "Choose your own dumps and keys and a folder for the game. Everything stays on your PC."));
        foreach (Control c in new Control[] { buttons, statusPanel }) c.BackColor = Theme.Card;
        return page;
    }

    static void FileRow(TableLayoutPanel grid, string label, TextBox box, Action browse)
    {
        int row = grid.RowCount++;
        var l = Theme.Label(label);
        l.Margin = new Padding(0, 13, 16, 0);
        grid.Controls.Add(l, 0, row);
        grid.Controls.Add(new Field(box), 1, row);
        var b = Theme.Button("Browse", glyph: Theme.GlyphFolder);
        b.Margin = new Padding(0, 3, 0, 3);
        b.Click += (_, _) => browse();
        grid.Controls.Add(b, 2, row);
    }

    Control SettingsPage()
    {
        var page = new Surface { BackColor = Theme.Back };
        var grid = Grid(2);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        Fill(windowSize, Sizes);
        Fill(renderScale, RenderScales);
        Fill(fpsLimit, Fps);
        Fill(present, Presents);
        Fill(upscaler, Upscalers);
        Fill(gpu, GpuAuto);
        Fill(opMode, OperationModes);
        Fill(antiAliasing, AntiAliasings);
        Fill(dlssModel, DlssModels);
        Fill(frameGen, FrameGens);
        Fill(overlay, Overlays);
        Fill(population, Populations);
        Fill(peopleShown, PeopleShown);
        Fill(peopleDetail, PeopleDetails);
        Fill(pokemonDetail, PokemonDetails);
        Fill(ao, LightingLevels);
        Fill(indirectLight, LightingLevels);
        Fill(colour, Colours);
        Fill(shadowRes, ShadowReses);
        Fill(stutter, Stutters);
        var sharp = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        sharpnessValue.Margin = new Padding(6, 6, 0, 0);
        sharp.Controls.Add(sharpness);
        sharp.Controls.Add(sharpnessValue);
        SettingRow(grid, "Display", fullscreen);
        SettingRow(grid, "Window size", windowSize);
        fullscreen.CheckedChanged += (_, _) => windowSize.Enabled = saveSettings.Enabled && !fullscreen.Checked;
        windowSizeNote.MaximumSize = new Size(560, 0);
        SettingRow(grid, "", windowSizeNote);
        SettingRow(grid, "Render scale", renderScale);
        renderScaleNote.MaximumSize = new Size(560, 0);
        SettingRow(grid, "", renderScaleNote);
        SettingRow(grid, "FPS limit", fpsLimit);
        SettingRow(grid, "Frame sync", present);
        SettingRow(grid, "Graphics card", gpu);
        gpuNote.MaximumSize = new Size(560, 0);
        gpuNote.AutoSize = true;
        SettingRow(grid, "", gpuNote);
        SettingRow(grid, "Mode", opMode);
        var aa = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0), WrapContents = false };
        antiAliasing.Margin = new Padding(0, 3, 10, 0);
        addDlss.Margin = new Padding(0);
        aa.Controls.Add(antiAliasing);
        aa.Controls.Add(addDlss);
        SettingRow(grid, "Anti-aliasing", aa);
        dlssNote.MaximumSize = new Size(560, 0);
        dlssNote.AutoSize = true;
        SettingRow(grid, "", dlssNote);
        SettingRow(grid, "Upscaling", upscaler);
        rowLabels[sharp] = SettingRow(grid, "Sharpness", sharp);
        rowLabels[dlssModel] = SettingRow(grid, "DLSS model", dlssModel);
        var fg = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0), WrapContents = false };
        frameGen.Margin = new Padding(0, 3, 10, 0);
        addStreamline.Margin = new Padding(0);
        fg.Controls.Add(frameGen);
        fg.Controls.Add(addStreamline);
        SettingRow(grid, "Frame generation", fg);
        fgNote.MaximumSize = new Size(560, 0);
        fgNote.AutoSize = true;
        SettingRow(grid, "", fgNote);
        SettingRow(grid, "Performance overlay", overlay);
        SettingRow(grid, "Shadow quality", shadowRes);
        SettingRow(grid, "Ambient occlusion", ao);
        SettingRow(grid, "Indirect light", indirectLight);
        lightingNote.MaximumSize = new Size(560, 0);
        lightingNote.AutoSize = true;
        SettingRow(grid, "", lightingNote);
        SettingRow(grid, "Wild Pokémon", population);
        SettingRow(grid, "People shown", peopleShown);
        peopleShownNote.MaximumSize = new Size(560, 0);
        peopleShownNote.AutoSize = true;
        SettingRow(grid, "", peopleShownNote);
        SettingRow(grid, "People detail", peopleDetail);
        SettingRow(grid, "Pokémon detail", pokemonDetail);
        modelDetailNote.MaximumSize = new Size(560, 0);
        modelDetailNote.AutoSize = true;
        SettingRow(grid, "", modelDetailNote);
        SettingRow(grid, "", noExclusives);
        SettingRow(grid, "", noTradeEvo);
        SettingRow(grid, "Stutter reduction", stutter);
        stutterNote.MaximumSize = new Size(560, 0);
        stutterNote.AutoSize = true;
        SettingRow(grid, "", stutterNote);
        SettingRow(grid, "Compatibility", safeSubmit);
        SettingRow(grid, "", keepHotMemory);
        compatibilityNote.MaximumSize = new Size(560, 0);
        compatibilityNote.AutoSize = true;
        SettingRow(grid, "", compatibilityNote);
        resizableBarNote.MaximumSize = new Size(560, 0);
        resizableBarNote.AutoSize = true;
        SettingRow(grid, "Resizable BAR", resizableBarNote);
        var look = Grid(2);
        look.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        look.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        SettingRow(look, "Launcher colour", colour);
        var bottom = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Back };
        settingsNote.Margin = new Padding(6, 10, 0, 0);
        bottom.Controls.Add(saveSettings);
        bottom.Controls.Add(settingsNote);
        page.Controls.Add(bottom);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(look));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Settings", "Used the next time the game starts. The game's own PC settings page changes the same values."));
        return page;
    }

    static Label SettingRow(TableLayoutPanel grid, string label, Control c)
    {
        int row = grid.RowCount++;
        var l = Theme.Label(label);
        l.Margin = new Padding(0, 9, 28, 6);
        l.MinimumSize = new Size(170, 0);
        c.Margin = new Padding(0, 5, 0, 5);
        grid.Controls.Add(l, 0, row);
        grid.Controls.Add(c, 1, row);
        return l;
    }

    void ShowRow(Control c, bool on)   // a setting row that only applies to some choices
    {
        c.Visible = on;
        if (rowLabels.TryGetValue(c, out var l)) l.Visible = on;
    }

    // Sharpness belongs to FSR 1's RCAS; the DLSS model to DLAA / DLSS
    void UpdateRows()
    {
        string up = Value(upscaler);
        ShowRow(sharpness.Parent!, up == "fsr");
        ShowRow(dlssModel, Value(antiAliasing) == "1" || up.StartsWith("dlss"));
    }

    // DLAA, DLSS upscaling and DLSS Frame Generation need an NVIDIA RTX card: locked (not just hidden, so it is clear
    // why) once a specific other card is picked above. "Automatic" leaves every option open, since any installed card
    // could end up running the game.
    void UpdateGpuLock()
    {
        string name = Value(gpu);
        bool locked = name != "" && !name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase);
        string curUp = Value(upscaler), curAa = Value(antiAliasing);
        bool wasLoading = loadingGame;
        loadingGame = true;
        Fill(upscaler, locked ? Upscalers.Where(u => !u.Item1.StartsWith("dlss")).ToArray() : Upscalers);
        Select(upscaler, locked && curUp.StartsWith("dlss") ? "fsr" : curUp, "fsr");
        Fill(antiAliasing, locked ? AntiAliasings.Where(a => a.Item1 != "1").ToArray() : AntiAliasings);
        Select(antiAliasing, locked && curAa == "1" ? "0" : curAa, "0");
        loadingGame = wasLoading;
        if (locked && Value(frameGen) != "0") Select(frameGen, "0", "0");
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        antiAliasing.Enabled = built;
        dlssModel.Enabled = frameGen.Enabled = addStreamline.Enabled = built && !locked;
        addDlss.Enabled = built && !locked && !dlssDownloading;
        gpuNote.Text = locked
            ? $"DLAA, DLSS and Frame Generation need an NVIDIA card: locked while \"{name}\" is selected above. AMD FSR 2 remains available."
            : "";
        UpdateRows();
    }

    // The graphics cards karpur.exe finds (vk_device.c pick_device logs "vk: gpu N: name (type T, ...)" on every
    // start): run once per built exe (cached) through --probe-vkdevice, which only enumerates devices, no game files
    // needed. wantName is the card saved in karpur.ini (TOTK_GPU_NAME), selected once the list is in.
    (string Value, string Name)[]? gpuCacheItems;
    string? gpuCacheExe;

    async Task DetectGpus(string exe, string? wantName)
    {
        if (exe == "" || !File.Exists(exe)) { Fill(gpu, GpuAuto); Select(gpu, "", ""); gpu.Enabled = false; UpdateGpuLock(); return; }
        if (gpuCacheExe == exe && gpuCacheItems != null)
        {
            Fill(gpu, gpuCacheItems);
            Select(gpu, wantName, "");
            UpdateGpuLock();
            return;
        }
        Fill(gpu, GpuAuto);
        Select(gpu, "", "");
        gpu.Enabled = false;
        gpuNote.Text = "Detecting graphics cards …";
        var found = new List<(string, string)>();
        try
        {
            var psi = new ProcessStartInfo(exe, "--probe-vkdevice")
            {
                WorkingDirectory = Path.GetDirectoryName(exe)!, RedirectStandardError = true, RedirectStandardOutput = true,
                UseShellExecute = false, CreateNoWindow = true,
            };
            using var p = Process.Start(psi);
            if (p != null)
            {
                string err = await p.StandardError.ReadToEndAsync();
                await p.StandardOutput.ReadToEndAsync();
                await p.WaitForExitAsync();
                foreach (Match m in Regex.Matches(err, @"vk: gpu \d+: (.+?) \(type (\d+)"))
                {
                    string name = m.Groups[1].Value.Trim();
                    string kind = m.Groups[2].Value == "1" ? " (integrated)" : m.Groups[2].Value == "2" ? " (discrete)" : "";
                    if (!found.Any(f => f.Item1 == name)) found.Add((name, name + kind));
                }
            }
        }
        catch { }
        // the exe or target may have changed while the probe ran: only apply a result for what is still selected
        if (targetBox.Text == "" || Builder.Exe(targetBox.Text) != exe) return;
        var items = GpuAuto.Concat(found).ToArray();
        gpuCacheExe = exe;
        gpuCacheItems = items;
        Fill(gpu, items);
        Select(gpu, wantName, "");
        gpu.Enabled = true;
        UpdateGpuLock();
    }

    static void Fill(ComboBox c, (string Value, string Name)[] items)
    {
        c.Items.Clear();
        foreach (var (_, name) in items) c.Items.Add(name);
        c.Tag = items;
    }

    static void Select(ComboBox c, string? value, string fallback)
    {
        var items = ((string Value, string Name)[])c.Tag!;
        int i = Array.FindIndex(items, x => x.Value.Equals(value ?? fallback, StringComparison.OrdinalIgnoreCase));
        c.SelectedIndex = i >= 0 ? i : Math.Max(0, Array.FindIndex(items, x => x.Value == fallback));
    }

    static string? CapDetailSetting(string? value, int maximum) =>
        int.TryParse(value, out int saved) && saved > maximum ? maximum.ToString() : value;

    static string Value(ComboBox c) => ((string Value, string Name)[])c.Tag! is var items && c.SelectedIndex >= 0
        ? items[c.SelectedIndex].Value : "";

    Control UpdatePage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Theme.Card };
        var installed = Theme.Label($"Installed: version {Program.Version}", 11f);
        installed.Margin = new Padding(0, 0, 0, 14);
        var row = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        row.Controls.Add(checkUpdate);
        row.Controls.Add(installUpdate);
        updateStatus.Margin = new Padding(0, 14, 0, 0);
        top.Controls.Add(installed);
        top.Controls.Add(row);
        top.Controls.Add(updateStatus);
        var notesPanel = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 4, 0, 0), BackColor = Theme.Back };
        notesPanel.Controls.Add(notes);
        page.Controls.Add(notesPanel);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(top));
        page.Controls.Add(Header("Update", "New versions come from the KarPur releases on GitHub. Only what changed is rebuilt."));
        return page;
    }

    // ---- Feedback (Feedback.cs; a GitHub issue the player submits in their browser)

    readonly TextBox fbLauncher = Theme.TextBox(), fbGpu = Theme.TextBox(), fbCpu = Theme.TextBox(), fbRam = Theme.TextBox();
    readonly ComboBox fbGame = Combo(), fbUpdate = Combo(), fbDlc = Combo(), fbWindows = Combo();
    readonly TextBox fbReport = new()
    {
        Multiline = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Theme.Field, ForeColor = Theme.Text,
        BorderStyle = BorderStyle.None, MaxLength = Feedback.ReportLimit, Font = new Font("Segoe UI", 10f),
        PlaceholderText = "How does it run? What went wrong? Where? Logs are welcome.",
    };
    readonly Label fbCount = Theme.Label($"0 / {Feedback.ReportLimit}", 9f, muted: true);
    readonly Label fbNote = Theme.Label("", 9.5f, muted: true);
    readonly Button fbSend = Theme.Button("Open on GitHub", true, ""), fbCopy = Theme.Button("Copy report", glyph: ""),
                    fbLog = Theme.Button("Attach log", glyph: "");
    bool fbDetected;
    static readonly (string, string)[] Games = { ("Violet", "Violet"), ("Scarlet", "Scarlet") };
    static readonly (string, string)[] YesNo = { ("Yes", "Yes"), ("No", "No") };
    static readonly (string, string)[] Windows = { ("Windows 11", "Windows 11"), ("Windows 10", "Windows 10") };

    Control FeedbackPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        Fill(fbGame, Games);
        Fill(fbUpdate, YesNo);
        Fill(fbDlc, YesNo);
        Fill(fbWindows, Windows);
        fbLauncher.ReadOnly = true;
        fbLauncher.Text = Program.Version;
        var grid = Grid(4);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        void Pair(string a, Control ca, string b, Control cb)
        {
            int row = grid.RowCount++;
            foreach (var (label, c, col) in new[] { (a, ca, 0), (b, cb, 2) })
            {
                var l = Theme.Label(label);
                l.Margin = new Padding(col == 0 ? 0 : 18, 12, 14, 0);
                grid.Controls.Add(l, col, row);
                Control box = c is TextBox t ? new Field(t) : c;
                box.Margin = new Padding(0, 4, 0, 4);
                if (c is ComboBox cbx) { cbx.Width = 10; cbx.Dock = DockStyle.Fill; cbx.Margin = new Padding(0, 8, 0, 4); }
                grid.Controls.Add(box, col + 1, row);
            }
        }
        Pair("Launcher version", fbLauncher, "Game", fbGame);
        Pair("Update 4.0.0", fbUpdate, "DLC", fbDlc);
        Pair("GPU", fbGpu, "CPU", fbCpu);
        Pair("RAM (type, size)", fbRam, "Windows", fbWindows);

        var reportCard = new Card { Dock = DockStyle.Fill };
        var reportField = new Field(fbReport) { Dock = DockStyle.Fill, Padding = new Padding(10, 8, 4, 8) };
        var reportHead = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Theme.Card };
        var rl = Theme.Label("Report");
        reportHead.Controls.Add(rl);
        fbCount.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        reportHead.Controls.Add(fbCount);
        reportHead.Resize += (_, _) => fbCount.Location = new Point(reportHead.Width - fbCount.Width, 2);
        reportCard.Controls.Add(reportField);
        reportCard.Controls.Add(reportHead);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, BackColor = Theme.Back, Padding = new Padding(0, 12, 0, 0) };
        fbNote.Margin = new Padding(6, 10, 0, 0);
        buttons.Controls.AddRange(new Control[] { fbSend, fbCopy, fbLog, fbNote });

        page.Controls.Add(reportCard);
        page.Controls.Add(buttons);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Feedback", "Tell us how KarPur runs on your PC. It opens as a GitHub issue that you check and submit."));

        fbReport.TextChanged += (_, _) => fbCount.Text = $"{fbReport.TextLength} / {Feedback.ReportLimit}";
        fbLog.Click += (_, _) =>
        {
            string tail = Feedback.LogTail(targetBox.Text, Feedback.ReportLimit - fbReport.TextLength - 2);
            if (tail == "") { fbNote.Text = "No game log found (start the game once), or the report is full."; return; }
            fbReport.AppendText((fbReport.TextLength > 0 ? "\r\n\r\n" : "") + tail.Replace("\n", "\r\n"));
            fbNote.Text = "Log attached. Folder names of your user account are left out.";
        };
        fbCopy.Click += (_, _) => { Clipboard.SetText(Feedback.Text(FeedbackFields())); fbNote.Text = "Report copied to the clipboard."; };
        fbSend.Click += (_, _) =>
        {
            if (fbReport.Text.Trim() == "") { fbNote.Text = "Please write a few words in the report first."; return; }
            var f = FeedbackFields();
            string title = $"[Feedback] {f["game"]}, {f["gpu"]}, launcher {f["launcher"]}";
            fbNote.Text = Feedback.Open(f, title)
                ? "Opened in your browser: check it and press Submit new issue (a free GitHub account is needed)."
                : "Opened in your browser. The report was too long for the link: paste it from the clipboard (Ctrl+V).";
        };
        return page;
    }

    Dictionary<string, string> FeedbackFields() => new()
    {
        ["launcher"] = fbLauncher.Text, ["game"] = Value(fbGame), ["update"] = Value(fbUpdate), ["dlc"] = Value(fbDlc),
        ["gpu"] = fbGpu.Text.Trim(), ["cpu"] = fbCpu.Text.Trim(), ["ram"] = fbRam.Text.Trim(), ["windows"] = Value(fbWindows),
        ["report"] = fbReport.Text.Replace("\r\n", "\n").Trim(),
    };

    // Filled in once, the first time the tab opens: the game from the build, the PC from Windows.
    async Task FillFeedback()
    {
        if (fbDetected) return;
        fbDetected = true;
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        Select(fbGame, built && Builder.Game(targetBox.Text) == "scarlet" ? "Scarlet" : "Violet", "Violet");
        Select(fbUpdate, built || updateBox.Text != "" ? "Yes" : "No", "Yes");
        Select(fbDlc, dlcBox.Text != "" ? "Yes" : "No", "No");
        Select(fbWindows, Feedback.WindowsName(), "Windows 11");
        fbNote.Text = "Reading your PC's hardware …";
        var s = await Feedback.Detect();
        if (fbGpu.Text == "") fbGpu.Text = s.Gpu;
        if (fbCpu.Text == "") fbCpu.Text = s.Cpu;
        if (fbRam.Text == "") fbRam.Text = s.Ram;
        fbNote.Text = "Filled in from your PC. Change anything that is wrong.";
    }

    // ---- Setup

    void PickFile(TextBox box, string filter)
    {
        using var d = new OpenFileDialog { Filter = filter, CheckFileExists = true };
        if (File.Exists(box.Text)) d.InitialDirectory = Path.GetDirectoryName(box.Text);
        if (d.ShowDialog(this) == DialogResult.OK) box.Text = d.FileName;
    }

    void PickFirmware()
    {
        using var d = new FolderBrowserDialog
        {
            Description = "The folder with your console's system files (.nca), e.g. an emulator's nand\\system\\Contents\\registered. " +
                          "Only needed when your base game is an .nsp or was converted from one.",
            UseDescriptionForTitle = true,
        };
        if (Directory.Exists(firmwareBox.Text)) d.InitialDirectory = firmwareBox.Text;
        if (d.ShowDialog(this) == DialogResult.OK) firmwareBox.Text = d.SelectedPath;
    }

    void PickFolder()
    {
        using var d = new FolderBrowserDialog { Description = "Where should the game go?", UseDescriptionForTitle = true };
        if (Directory.Exists(targetBox.Text)) d.InitialDirectory = targetBox.Text;
        if (d.ShowDialog(this) == DialogResult.OK) targetBox.Text = d.SelectedPath;
    }

    // Messages for what the player can fix before anything runs.
    string? CheckInputs()
    {
        if (!File.Exists(baseBox.Text)) return "Please choose your base game (.xci or .nsp).";
        bool nsp = baseBox.Text.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase);
        if (!baseBox.Text.EndsWith(".xci", StringComparison.OrdinalIgnoreCase) && !nsp)
            return "The base game must be an .xci file (or an .nsp together with a firmware folder).";
        if (firmwareBox.Text != "" && !Directory.Exists(firmwareBox.Text))
            return "The firmware folder was not found. Leave the field empty if your base game is a cartridge dump (.xci).";
        if (nsp && firmwareBox.Text == "")
            return "A base game as .nsp lacks the system fonts that a cartridge dump (.xci) carries. Choose the folder with your " +
                   "console's system files (.nca) as \"Firmware folder\", or use an .xci.";
        if (!File.Exists(updateBox.Text) || !updateBox.Text.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase))
            return "Please choose the update (.nsp).";
        if (dlcBox.Text != "" && (!File.Exists(dlcBox.Text) || !dlcBox.Text.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase)))
            return "The DLC file was not found or is not an .nsp. Leave the field empty if you have no DLC.";
        if (!File.Exists(keysBox.Text)) return "Please choose your prod.keys.";
        if (targetBox.Text == "") return "Please choose a target folder.";
        return null;
    }

    async Task Build(bool repair)
    {
        string? problem = CheckInputs();
        if (problem != null)
        {
            Show("Setup");
            MessageBox.Show(this, problem, Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        SaveSettings();
        var inputs = new Inputs(baseBox.Text, updateBox.Text, dlcBox.Text, keysBox.Text, targetBox.Text, firmwareBox.Text);
        cancel = new CancellationTokenSource();
        SetBusy(true);
        log.Clear();
        status.ForeColor = Theme.Muted;
        var b = new Builder
        {
            Status = s => BeginInvoke(() => status.Text = s),
            Percent = p => BeginInvoke(() => progress.Value = p),
            Log = s => BeginInvoke(() => { if (log.TextLength > 2_000_000) log.Clear(); log.AppendText(s + Environment.NewLine); }),
        };
        string? error;
        try { error = await b.Run(inputs, repair, cancel.Token); }
        catch (OperationCanceledException) { error = "Stopped. The next Build goes on where it stopped."; }
        catch (Exception e) { error = e.Message; }
        cancel = null;
        SetBusy(false);
        Refresh_();
        if (error == null)
        {
            Builder.NoteBuilt(targetBox.Text);
            status.Text = "Done! Press Play, or use the shortcut in your target folder.";
            status.ForeColor = Theme.Text;
        }
        else
        {
            status.Text = "Error: " + error;
            status.ForeColor = Theme.Error;
            MessageBox.Show(this, "The build did not work:\n\n" + error + "\n\nThe full log is in " +
                            Path.Combine(Builder.Root(inputs.Target), "launcher.log"), Text, MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
        }
    }

    void SetBusy(bool busy)
    {
        buildButton.Enabled = repairButton.Enabled = checkUpdate.Enabled = installUpdate.Enabled = !busy;
        cancelButton.Enabled = busy;
        playButton.Enabled = !busy && File.Exists(Builder.Exe(targetBox.Text));
        foreach (var t in new[] { baseBox, updateBox, dlcBox, keysBox, targetBox }) t.ReadOnly = busy;
    }

    // Play, Repair and the colour follow what the target folder holds.
    void Refresh_()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        playButton.Enabled = cancel == null && built;
        repairButton.Enabled = cancel == null && built;
        buildButton.Enabled = cancel == null;
        buildButton.Text = built ? "Rebuild" : "Build";
        if (cancel == null && status.ForeColor == Theme.Muted)
            status.Text = built ? "Ready to play. Rebuild or Repair only when something is missing or after a change."
                                : "Choose your files and a target folder, then press Build.";
        ApplyColour();
        RefreshHome();
    }

    void ApplyColour()
    {
        string choice = colour.SelectedIndex >= 0 ? Value(colour) : "auto";
        if (Program.PreviewColour != null) choice = Program.PreviewColour;   // --colour=scarlet: a look at a palette (design checks)
        string game = choice == "auto" ? (targetBox.Text != "" ? Builder.Game(targetBox.Text) : "unknown") : choice;
        Theme.SetPalette(Theme.PaletteFor(game), this);
        sidebarLogo.Edition = File.Exists(Builder.Exe(targetBox.Text)) ? Theme.Current.Name : "neutral";
        sidebarLogo.Invalidate();
        hero.Invalidate(true);
    }

    void Play()
    {
        string exe = Builder.Exe(targetBox.Text);
        if (!File.Exists(exe)) return;
        DisableOrphanedModList();
        var start = Builder.GameStartInfo(targetBox.Text);
        var game = Process.Start(start);
        if (game != null)
        {
            // Record successful starts for Home and distinguish installations in local reports.
            try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "launcher-start.log"),
                $"{DateTime.Now:O} exe={start.FileName} cwd={start.WorkingDirectory}{Environment.NewLine}"); } catch { }
            // the Home page follows the game: its save and team when it ends (the watcher covers saves while it runs)
            game.EnableRaisingEvents = true;
            game.Exited += (_, _) => BeginInvoke(() => { RefreshHome(); RefreshSaves(); });
        }
        WatchTeam();
    }

    // <profile>	eam.json changes whenever the game saves: the Home page shows the new team at once, without a tab switch.
    FileSystemWatcher? teamWatch;
    void WatchTeam()
    {
        string? profile = ProfileDir();
        if (profile == null || !Directory.Exists(profile)) return;
        if (teamWatch != null && teamWatch.Path == profile) return;
        teamWatch?.Dispose();
        teamWatch = new FileSystemWatcher(profile, "team.json") { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName };
        FileSystemEventHandler changed = (_, _) => BeginInvoke(async () =>
        {
            await Task.Delay(300);   // the game writes the file and renames it into place
            RefreshHome();
        });
        teamWatch.Changed += changed;
        teamWatch.Created += changed;
        teamWatch.Renamed += (s, e) => changed(s, e);
        teamWatch.EnableRaisingEvents = true;
    }

    // ---- Settings (karpur.ini next to karpur.exe)

    void LoadGameSettings()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        var ini = new Ini(built ? Builder.Ini(targetBox.Text) : "");
        fullscreen.Checked = (built ? ini.Get("fullscreen") : null) == "1";
        Select(windowSize, built ? ini.Get("window_size") : null, "1920x1080");
        Select(renderScale, built ? ini.Get("output_res") : null, "1920x1080");
        Select(fpsLimit, built ? ini.Get("fps_limit") : null, "60");
        Select(present, built ? ini.Get("vk_present") : null, "mailbox");
        loadingGame = true;
        bool fsr2On = built && ini.Get("fsr2") == "1";
        Select(antiAliasing, fsr2On ? "2" : built ? ini.Get("dlaa") : null, "0");
        string dlssOut = built ? ini.Get("dlss_out") ?? "game" : "game";
        Select(upscaler, fsr2On && ini.Get("fsr2_out") == "window" ? "fsr2"
            : !fsr2On && dlssOut == "window" ? "dlss" : !fsr2On && dlssOut == "3840x2160" ? "dlss4k"
            : built ? ini.Get("upscaler") : null, "fsr");
        Select(opMode, built ? ini.Get("operation_mode") : null, "docked");
        Select(dlssModel, built ? ini.Get("dlaa_preset") : null, "5");
        Select(frameGen, built ? ini.Get("framegen") : null, "0");
        loadingGame = false;
        UpdateRows();
        Select(overlay, built ? ini.Get("perf_overlay") : null, "off");
        Select(population, built ? ini.Get("population") : null, "switch");
        Select(peopleShown, built ? CapDetailSetting(ini.Get("people_shown"), 48) : null, "12");
        Select(peopleDetail, built ? CapDetailSetting(ini.Get("people_detail"), 10) : null, "1");
        Select(pokemonDetail, built ? CapDetailSetting(ini.Get("pokemon_detail"), 8) : null, "1");
        Select(ao, built ? ini.Get("ao") : null, "0");
        Select(indirectLight, built ? ini.Get("indirect_light") : null, "0");
        Select(shadowRes, built ? ini.Get("shadow_res") : null, "0");
        string ahead = (built ? ini.Get("acquire_ahead") : null) ?? "0", lazy = (built ? ini.Get("core_lazy") : null) ?? "0";
        Select(stutter, lazy != "0" ? "aggressive" : ahead != "0" ? "standard" : "off", "off");
        safeSubmit.Checked = (built ? ini.Get("submit_defer") : null) == "0";
        keepHotMemory.Checked = (built ? ini.Get("hot_cool") : null) == "0";
        noExclusives.Checked = (built ? ini.Get("no_exclusives") : null) == "1";
        noTradeEvo.Checked = (built ? ini.Get("no_trade_evo") : null) == "1";
        sharpness.Value = int.TryParse(built ? ini.Get("sharpness") : null, out int s) ? Math.Clamp(s, 0, 100) : 80;
        foreach (Control c in new Control[] { fullscreen, windowSize, fpsLimit, present, opMode, upscaler, antiAliasing, addDlss, dlssModel, frameGen, addStreamline, overlay, population, peopleShown, peopleDetail, pokemonDetail, ao, indirectLight, noExclusives, noTradeEvo, shadowRes, stutter, safeSubmit, keepHotMemory, sharpness, saveSettings })
            c.Enabled = built;
        windowSize.Enabled = built && !fullscreen.Checked;
        renderScale.Enabled = built && Builder.HasRenderScale(targetBox.Text);
        settingsNote.Text = !built ? "Build the game first: the settings belong to its folder."
            : !renderScale.Enabled ? "Render scale needs a Violet 4.0.0 build with the resolution patch." : "";
        DlssStatus();
        FgStatus();
        _ = DetectGpus(built ? Builder.Exe(targetBox.Text) : "", built ? ini.Get("gpu_name") : null);
    }

    // ---- DLSS Frame Generation: NVIDIA Streamline's files are NVIDIA's; the player adds their own copies next to
    // karpur.exe (games with DLSS Frame Generation ship them). Windows' hardware-accelerated GPU scheduling must be on.

    static readonly string[] StreamlineFiles =
        { "sl.interposer.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.reflex.dll", "sl.pcl.dll", "nvngx_dlssg.dll" };
    bool loadingGame;

    static bool HagsOn()
    {
        try
        {
            return Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers",
                                                     "HwSchMode", null) is int v && v == 2;
        }
        catch { return false; }
    }

    void FgStatus()
    {
        if (targetBox.Text == "" || !File.Exists(Builder.Exe(targetBox.Text))) { fgNote.Text = ""; return; }
        string dir = Path.GetDirectoryName(Builder.Exe(targetBox.Text))!;
        var missing = StreamlineFiles.Where(f => !File.Exists(Path.Combine(dir, f))).ToList();
        var notes = new List<string>();
        if (missing.Count > 0) notes.Add("Missing next to karpur.exe: " + string.Join(", ", missing) + ". Add them with the button "
                                         + "(a game with DLSS Frame Generation has them; KarPur cannot ship them).");
        if (!File.Exists(DlssPath())) notes.Add("Frame generation also needs nvngx_dlss.dll (Anti-aliasing above).");
        if (!HagsOn()) notes.Add("Turn on Windows' hardware-accelerated GPU scheduling (Settings > System > Display > Graphics) "
                                 + "and restart the PC.");
        fgNote.Text = notes.Count > 0 ? string.Join(" ", notes)
                    : "Ready: frame generation shows 2 to 4 frames for each one the game renders (NVIDIA RTX 40/50; 3x and 4x: RTX 50). "
                      + "Needs V-Sync off (Frame sync).";
    }

    void AddStreamline()
    {
        using var dlg = new FolderBrowserDialog { Description = "Choose the folder that holds sl.interposer.dll and the other Streamline files" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        string dir = Path.GetDirectoryName(Builder.Exe(targetBox.Text))!;
        int copied = 0;
        var notFound = new List<string>();
        try
        {
            foreach (string f in StreamlineFiles)
            {
                string src = Path.Combine(dlg.SelectedPath, f);
                if (!File.Exists(src)) { notFound.Add(f); continue; }
                // a check against picking the wrong files, not a trust decision (see AddDlss): NVIDIA's certificate name
                string signer;
#pragma warning disable SYSLIB0057
                try { signer = System.Security.Cryptography.X509Certificates.X509Certificate.CreateFromSignedFile(src).Subject; }
                catch { signer = ""; }
#pragma warning restore SYSLIB0057
                if (!signer.Contains("NVIDIA Corporation")) { notFound.Add(f + " (not signed by NVIDIA)"); continue; }
                File.Copy(src, Path.Combine(dir, f), true);
                copied++;
            }
            FgStatus();
            settingsNote.Text = $"Copied {copied} Streamline file(s)." + (notFound.Count > 0 ? " Not in that folder: " + string.Join(", ", notFound) + "." : "");
        }
        catch (Exception e) { settingsNote.Text = "Could not copy: " + e.Message; }
    }

    // NVIDIA's release DLL is fetched only when the player presses the button. It is not bundled with KarPur.
    // Pin the version, size and SHA-256 together so a changed or incomplete download never replaces a working DLL.
    const string DlssVersion = "310.9.16";
    const string DlssUrl = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Windows_x86_64/rel/nvngx_dlss.dll";
    const long DlssSize = 58956912;
    const string DlssSha256 = "D00B5B0AB29C3CA101B70C3A5ECB3A01B7EF33766A3383A28A18623083E6123E";
    bool dlssDownloading;

    string DlssPath() => Path.Combine(Path.GetDirectoryName(Builder.Exe(targetBox.Text))!, "nvngx_dlss.dll");

    void DlssStatus()
    {
        if (dlssDownloading) return;
        if (targetBox.Text == "" || !File.Exists(Builder.Exe(targetBox.Text))) { dlssNote.Text = ""; return; }
        if (Value(antiAliasing) == "2")
        {
            dlssNote.Text = "AMD FSR 2 is included with KarPur. Moving Pokémon, foliage and effects may leave trails until their motion is tracked separately.";
            return;
        }
        string dll = DlssPath();
        if (File.Exists(dll))
            dlssNote.Text = $"nvngx_dlss.dll {FileVersionInfo.GetVersionInfo(dll).FileVersion?.Replace(',', '.')} is in place. "
                          + "DLAA smooths edges on NVIDIA RTX cards (about 1 ms per frame); with a DLSS output above "
                          + "1080p, DLSS also sharpens the 3D world to that size (the HUD stays at 1080p).";
        else
            dlssNote.Text = Value(antiAliasing) == "1"
                ? "DLAA needs NVIDIA's nvngx_dlss.dll. Download it with the button above. "
                  + "Without it the game starts with its own anti-aliasing."
                : "NVIDIA DLAA needs an RTX card and NVIDIA's nvngx_dlss.dll. Download it directly from NVIDIA above.";
    }

    async Task DownloadDlss()
    {
        if (dlssDownloading || targetBox.Text == "" || !File.Exists(Builder.Exe(targetBox.Text))) return;
        string destination = DlssPath();
        var existing = File.Exists(destination) ? FileVersionInfo.GetVersionInfo(destination) : null;
        if (existing != null && new Version(existing.FileMajorPart, existing.FileMinorPart, existing.FileBuildPart, existing.FilePrivatePart)
            > new Version(310, 9, 16, 0))
        {
            dlssNote.Text = "A newer DLSS version is already installed. Kept that file.";
            return;
        }
        string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".download";
        dlssDownloading = true;
        addDlss.Enabled = false;
        try
        {
            dlssNote.Text = $"Downloading NVIDIA DLSS {DlssVersion} …";
            using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("KarPur-Launcher/" + Program.Version);
            using var response = await http.GetAsync(DlssUrl, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, true);
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            byte[] buffer = new byte[1024 * 1024];
            long received = 0;
            int shown = -5;
            int count;
            while ((count = await source.ReadAsync(buffer)) != 0)
            {
                received += count;
                if (received > DlssSize) throw new InvalidDataException("NVIDIA's download is larger than expected.");
                hash.AppendData(buffer, 0, count);
                await output.WriteAsync(buffer.AsMemory(0, count));
                int percent = (int)(received * 100 / DlssSize);
                if (percent >= shown + 5) { dlssNote.Text = $"Downloading NVIDIA DLSS {DlssVersion}: {percent}%"; shown = percent; }
            }
            await output.FlushAsync();
            if (received != DlssSize || !Convert.ToHexString(hash.GetHashAndReset()).Equals(DlssSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("NVIDIA's download did not match its pinned size and SHA-256. The existing DLL was kept.");
            output.Close();
            File.Move(temporary, destination, true);
            dlssDownloading = false;
            DlssStatus();
            FgStatus();
            settingsNote.Text = $"NVIDIA DLSS {DlssVersion} is ready. Choose DLAA or DLSS above and save settings to use it.";
        }
        catch (Exception e) { dlssNote.Text = "Could not download DLSS: " + e.Message; }
        finally
        {
            try { if (File.Exists(temporary)) File.Delete(temporary); } catch { }
            dlssDownloading = false;
            UpdateGpuLock();
        }
    }

    void SaveGameSettings()
    {
        try
        {
            var ini = new Ini(Builder.Ini(targetBox.Text));
            ini.Set("display", "fullscreen", fullscreen.Checked ? "1" : "0");
            ini.Set("display", "window_size", Value(windowSize));
            if (Builder.HasRenderScale(targetBox.Text)) {
                string output = Value(renderScale);
                ini.Set("display", "output_res", output);
                ini.Set("display", "svhost_test", "1"); // also bypasses the unrelated generic HeapDef growth at 1080p
            }
            ini.Set("frame rate", "fps_limit", Value(fpsLimit));
            ini.Set("display", "vk_present", Value(present));
            ini.Set("display", "gpu_name", Value(gpu));
            ini.Set("display", "gpu_index", "");   // clears a stale manual TOTK_GPU_INDEX: both set at once can match no device
            string up = Value(upscaler);
            ini.Set("display", "upscaler", up.StartsWith("dlss") || up == "fsr2" ? "fsr" : up);   // present scaler when temporal upscaling is off
            ini.Set("display", "operation_mode", Value(opMode));
            ini.Set("display", "dlaa", Value(antiAliasing) == "1" ? "1" : "0");
            ini.Set("display", "fsr2", Value(antiAliasing) == "2" ? "1" : "0");
            ini.Set("display", "fsr2_out", up == "fsr2" ? "window" : "game");
            ini.Set("display", "dlss_out", up == "dlss" ? "window" : up == "dlss4k" ? "3840x2160" : "game");
            ini.Set("display", "dlaa_preset", Value(dlssModel));
            ini.Set("display", "framegen", Value(frameGen));
            ini.Set("display", "sharpness", sharpness.Value.ToString());
            ini.Set("display", "perf_overlay", Value(overlay));
            ini.Set("display", "population", Value(population));
            ini.Set("display", "people_shown", Value(peopleShown));
            ini.Set("display", "people_detail", Value(peopleDetail));
            ini.Set("display", "pokemon_detail", Value(pokemonDetail));
            ini.Set("display", "ao", Value(ao));
            ini.Set("display", "indirect_light", Value(indirectLight));
            ini.Set("display", "shadow_res", Value(shadowRes));
            ini.Set("display", "no_exclusives", noExclusives.Checked ? "1" : "0");
            ini.Set("display", "no_trade_evo", noTradeEvo.Checked ? "1" : "0");
            string level = Value(stutter);
            ini.Set("performance", "acquire_ahead", level == "off" ? "0" : "1");
            ini.Set("performance", "core_lazy", level == "aggressive" ? "1" : "0");
            ini.Set("compatibility", "submit_defer", safeSubmit.Checked ? "0" : "1");
            ini.Set("compatibility", "hot_cool", keepHotMemory.Checked ? "0" : "300");
            ini.Save();
            settingsNote.Text = "Saved. They apply the next time the game starts.";
        }
        catch (Exception e) { settingsNote.Text = "Could not save: " + e.Message; }
    }

    // ---- Update

    async Task CheckUpdate()
    {
        checkUpdate.Enabled = false;
        updateStatus.Text = "Checking …";
        try
        {
            release = await Updater.Latest(CancellationToken.None);
            if (release == null) updateStatus.Text = "No releases published yet.";
            else if (Updater.Newer(release))
            {
                updateStatus.Text = $"Version {release.Version.ToString(3)} is available.";
                installUpdate.Visible = true;
                notes.Text = release.Notes.Replace("\n", Environment.NewLine);
                notes.Visible = release.Notes.Trim() != "";
            }
            else updateStatus.Text = $"You have the newest version ({Program.Version}).";
        }
        catch (Exception e) { updateStatus.Text = "Could not check for updates: " + e.Message; }
        checkUpdate.Enabled = true;
    }

    async Task InstallUpdate()
    {
        if (release == null) return;
        installUpdate.Enabled = checkUpdate.Enabled = false;
        updateStatus.Text = "Downloading …";
        try
        {
            SaveSettings();
            await Updater.Install(release, CancellationToken.None);
            Close();
        }
        catch (Exception e)
        {
            updateStatus.Text = "The update failed: " + e.Message;
            installUpdate.Enabled = checkUpdate.Enabled = true;
        }
    }

    // ---- remembered choices

    record Saved(string Base, string Update, string Dlc, string Keys, string Target, string? Colour, string? Firmware = null);

    bool loading;   // LoadSettings fills the controls: their change events must not save half-filled settings

    void LoadSettings()
    {
        loading = true;
        Select(colour, "auto", "auto");
        try
        {
            var s = JsonSerializer.Deserialize<Saved>(File.ReadAllText(SettingsFile));
            if (s != null)
            {
                (baseBox.Text, updateBox.Text, dlcBox.Text, keysBox.Text, targetBox.Text) = (s.Base, s.Update, s.Dlc, s.Keys, s.Target);
                firmwareBox.Text = s.Firmware ?? "";
                Select(colour, s.Colour, "auto");
            }
        }
        catch { }
        loading = false;
        // Started from the target folder (the copy a build puts there): that folder is the target.
        string here = Path.GetDirectoryName(Environment.ProcessPath ?? "") ?? "";
        string? repo = Program.FindRepository(AppContext.BaseDirectory);
        if (repo != null) targetBox.Text = repo; // preview2 uses the current repo; changing Target folder still works
        else if (here != "" && File.Exists(Builder.Exe(here))) targetBox.Text = here;
    }

    void SaveSettings()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile)!);
            File.WriteAllText(SettingsFile, JsonSerializer.Serialize(new Saved(baseBox.Text, updateBox.Text, dlcBox.Text,
                                                                                 keysBox.Text, targetBox.Text, Value(colour),
                                                                                 firmwareBox.Text)));
        }
        catch { }
    }
}
