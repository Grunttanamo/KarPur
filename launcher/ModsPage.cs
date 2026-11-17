using System.ComponentModel;
using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.IO.Compression;
using System.Text.Json;

namespace KarPurLauncher;

// The Mods tab: the player's own community mods (a folder with romfs/<path> files, the Trinity Mod Loader layout, as
// NexusMods and others ship them), each switched on or off. Nothing of the game or of a mod is replaced or copied over
// another file: the runtime gives the game a mod's file when the game unpacks the original (runtime/data_patch.c,
// TOTK_MOD_FILES), from the list tools/mod_files.py writes to game\mod_files.txt for the mods switched on. Switched off,
// the list leaves them out and the game reads its own files again.
//   <root>\mods.json        the mods in their order (a file two mods change comes from the later one), on or off, and
//                           what the analysis found: files it replaces, adds (not supported yet), changes unpacked
//                           (not supported) and whether it carries code (exefs: needs a separate recompilation path)
//   <root>\mods\<name>\     a mod added from a .zip (a folder added as a folder stays where it is)
class ModEntry
{
    public string Path { get; set; } = "";
    public bool Enabled { get; set; }
    public int Replaces { get; set; }
    public int Adds { get; set; }
    public int Unpacked { get; set; }
    public bool Code { get; set; }
    public bool Analysed { get; set; }
}

class ModFilterSettings
{
    public int MaxSizeMiB { get; set; }
    public bool BntxOnly { get; set; }
}

// An on/off switch in the launcher's colours.
class Toggle : Control
{
    bool on;
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public bool On { get => on; set { on = value; Invalidate(); } }
    public event EventHandler? Switched;

    public Toggle()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.Selectable, true);
        Size = new Size(64, 36);
        Cursor = Cursors.Hand;
        TabStop = true;
    }

    public void PerformToggle()
    {
        if (!Enabled) return;
        On = !On;
        Switched?.Invoke(this, EventArgs.Empty);
    }

    protected override void OnClick(EventArgs e)
    {
        PerformToggle();
        base.OnClick(e);
    }

    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Card);
        using var track = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Height / 2f);
        Color fill = !Enabled ? Theme.Mix(Theme.Field, Theme.Back, 0.4f) : on ? Theme.Accent : Theme.Field;
        using (var b = new SolidBrush(fill)) g.FillPath(b, track);
        using (var pen = new Pen(on ? Theme.Mix(Theme.Accent, Color.White, 0.2f) : Theme.Border)) g.DrawPath(pen, track);
        if (Focused && Pad.Active)
        {
            using var ring = new Pen(Theme.Accent2, 2f);
            g.DrawPath(ring, track);
        }
        float d = Height - 8, x = on ? Width - d - 4 : 4;
        using var knob = new SolidBrush(Color.White);
        g.FillEllipse(knob, x, 4, d, d);
    }
}

partial class MainForm
{
    readonly FlowLayoutPanel modList = new() { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Theme.Back };
    readonly Label modNote = Theme.Label("", 9.5f, muted: true), modEmpty = Theme.Label("No mods yet. Add a mod folder or a .zip from NexusMods or another site.", 10f, muted: true);
    readonly Button addModFolder = Theme.Button("Add mod folder…", true, Theme.GlyphFolder), addModZip = Theme.Button("Add .zip…", glyph: Theme.GlyphImport),
                    openMods = Theme.Button("Open mods folder", glyph: Theme.GlyphFolder);
    readonly ComboBox modMaxSize = Combo();
    readonly CheckBox modBntxOnly = new ChoiceBox { AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card,
                                                    Text = "Only .bntx texture replacements" };
    List<ModEntry> mods = new();
    bool modsBusy, loadingModFilters;

    Control ModsPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var actions = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Card };
        actions.Controls.AddRange(new Control[] { addModFolder, addModZip, openMods });
        var how = Theme.Label("Experimental: only a few mods have been tried so far. A mod can make the game crash or behave " +
                              "oddly; switch it off and tell us in Feedback which mod it was. " +
                              "Mods are used while the game loads its files: nothing of your game is replaced, and a mod switched off " +
                              "is gone from the game at its next start. Code mods (exefs) are not supported yet; " +
                              "supported data files can be used. When two mods change the same file, the lower one in the list wins.", 9.5f, muted: true);
        how.MaximumSize = new Size(720, 0);
        how.Dock = DockStyle.Top;
        how.Padding = new Padding(0, 0, 0, 10);
        modNote.Dock = DockStyle.Top;
        modNote.Padding = new Padding(0, 10, 0, 0);
        modNote.MaximumSize = new Size(720, 0);
        modEmpty.Margin = new Padding(4, 8, 0, 0);
        Fill(modMaxSize, new (string Value, string Name)[] {
            ("0", "No size limit"), ("1", "1 MiB"), ("2", "2 MiB"), ("4", "4 MiB"),
            ("8", "8 MiB"), ("16", "16 MiB"), ("32", "32 MiB"), ("64", "64 MiB") });
        Select(modMaxSize, "0", "0");
        modMaxSize.Width = 220;
        var filters = Grid(2);
        SettingRow(filters, "Maximum file size", modMaxSize);
        SettingRow(filters, "File types", modBntxOnly);
        var filterNote = Theme.Label("Optional for large texture packs: the reporter of #74 tested 16 MiB together with " +
            "BNTX-only. Excluded files stay in their original mod folders. This does not change the game's memory limits.",
            9.5f, muted: true);
        filterNote.MaximumSize = new Size(720, 0);
        filterNote.Dock = DockStyle.Top;
        var filterTitle = Theme.Label("Large-pack filters", 11f, style: FontStyle.Bold);
        filterTitle.Dock = DockStyle.Top;
        filterTitle.Padding = new Padding(0, 0, 0, 8);
        page.Controls.Add(modList);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(filterTitle, filters, filterNote));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(how, actions, modNote));
        page.Controls.Add(Header("Mods (experimental)", "Your own mods for the game, each switched on or off. Changes apply at the next start of the game."));
        actions.BackColor = Theme.Card;
        modMaxSize.SelectedIndexChanged += async (_, _) => { if (!loadingModFilters && SaveModFilters()) await ApplyMods(); };
        modBntxOnly.CheckedChanged += async (_, _) => { if (!loadingModFilters && SaveModFilters()) await ApplyMods(); };

        addModFolder.Click += async (_, _) =>
        {
            using var d = new FolderBrowserDialog { Description = "The mod's folder (it holds a romfs folder)", UseDescriptionForTitle = true };
            if (d.ShowDialog(this) == DialogResult.OK) await AddMod(d.SelectedPath);
        };
        addModZip.Click += async (_, _) =>
        {
            using var d = new OpenFileDialog { Filter = "Mod archive|*.zip" };
            if (d.ShowDialog(this) != DialogResult.OK) return;
            try
            {
                string dir = Path.Combine(Builder.Root(targetBox.Text), "mods", Path.GetFileNameWithoutExtension(d.FileName));
                if (Directory.Exists(dir)) { ModMessage("A mod of that name is already in the mods folder.", true); return; }
                ZipFile.ExtractToDirectory(d.FileName, dir);
                // an archive whose only top folder holds the mod: use that folder
                var inner = Directory.GetDirectories(dir);
                string modDir = !Directory.Exists(Path.Combine(dir, "romfs")) && inner.Length == 1 ? inner[0] : dir;
                await AddMod(modDir);
            }
            catch (Exception e) { ModMessage("Could not unpack it: " + e.Message, true); }
        };
        openMods.Click += (_, _) =>
        {
            string dir = Path.Combine(Builder.Root(targetBox.Text), "mods");
            Directory.CreateDirectory(dir);
            Process.Start(new ProcessStartInfo(dir) { UseShellExecute = true });
        };
        return page;
    }

    string ModsFile => Path.Combine(Builder.Root(targetBox.Text), "mods.json");
    string ModFiltersFile => Path.Combine(Builder.Root(targetBox.Text), "build", "mod_filters.json");

    // A copied installation can retain mod_files=1 and an old mod_files.txt even
    // when its Mods page is empty. The registered switches are the source of truth.
    void DisableOrphanedModList()
    {
        var ini = new Ini(Builder.Ini(targetBox.Text));
        if (ini.Get("mod_files") != "1") return;
        List<ModEntry> registered;
        try { registered = JsonSerializer.Deserialize<List<ModEntry>>(File.ReadAllText(ModsFile)) ?? new(); }
        catch { registered = new(); }
        if (registered.Any(m => m.Enabled && Directory.Exists(m.Path))) return;
        ini.Set("mods", "mod_files", "0");
        ini.Save();
    }

    void LoadMods()
    {
        LoadModFilters();
        try { mods = JsonSerializer.Deserialize<List<ModEntry>>(File.ReadAllText(ModsFile)) ?? new(); }
        catch { mods = new(); }
        FollowIni();
        RenderMods();
    }

    // karpur.ini is the game's actual switch. It can change outside this page,
    // so reconcile the displayed list whenever the Mods page is opened.
    void FollowIni()
    {
        if (targetBox.Text == "") return;
        string? state = new Ini(Builder.Ini(targetBox.Text)).Get("mod_files");
        if (state == "0" && mods.Any(m => m.Enabled))
        {
            foreach (var mod in mods) mod.Enabled = false;
            SaveMods();
            ModMessage("karpur.ini starts the game without mods. All mods are shown off; switch one on to use it again.");
        }
        else if (state != null && state != "0" && mods.Count > 0 && !mods.Any(m => m.Enabled))
            ModMessage("karpur.ini enables mod files, but none is selected here. Select the mods you want to use.");
    }

    void LoadModFilters()
    {
        loadingModFilters = true;
        try
        {
            ModFilterSettings? saved = null;
            if (targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text)))
                try { saved = JsonSerializer.Deserialize<ModFilterSettings>(File.ReadAllText(ModFiltersFile)); }
                catch (IOException) { }
                catch (JsonException) { }
            Select(modMaxSize, saved?.MaxSizeMiB.ToString(), "0");
            modBntxOnly.Checked = saved?.BntxOnly == true;
        }
        finally { loadingModFilters = false; }
    }

    bool SaveModFilters()
    {
        if (targetBox.Text == "" || !File.Exists(Builder.Exe(targetBox.Text))) return false;
        var saved = new ModFilterSettings {
            MaxSizeMiB = int.TryParse(Value(modMaxSize), out int size) ? size : 0,
            BntxOnly = modBntxOnly.Checked };
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ModFiltersFile)!);
            string temp = ModFiltersFile + ".tmp";
            File.WriteAllText(temp, JsonSerializer.Serialize(saved, new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temp, ModFiltersFile, true);
            return true;
        }
        catch (IOException e) { ModMessage("Could not save the mod filters: " + e.Message, true); return false; }
    }

    void SaveMods()
    {
        try { File.WriteAllText(ModsFile, JsonSerializer.Serialize(mods, new JsonSerializerOptions { WriteIndented = true })); }
        catch { }
    }

    void ModMessage(string text, bool error = false)
    {
        modNote.ForeColor = error ? Theme.Error : Theme.Muted;
        modNote.Text = text;
    }

    // info.toml (the Trinity Mod Loader's): display_name and version, when the mod has one
    static (string Name, string Version) ModInfo(string dir)
    {
        string name = Path.GetFileName(dir.TrimEnd('\\', '/')), version = "";
        try
        {
            foreach (var line in File.ReadAllLines(Path.Combine(dir, "info.toml")))
            {
                var t = line.Trim();
                if (t.StartsWith("display_name=") || t.StartsWith("display_name =")) name = t[(t.IndexOf('=') + 1)..].Trim().Trim('"');
                if (t.StartsWith("version=") || t.StartsWith("version =")) version = t[(t.IndexOf('=') + 1)..].Trim().Trim('"');
            }
        }
        catch { }
        return (name, version);
    }

    void RenderMods()
    {
        modList.SuspendLayout();
        modList.Controls.Clear();
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        foreach (var b in new[] { addModFolder, addModZip, openMods }) b.Enabled = built && !modsBusy;
        modMaxSize.Enabled = modBntxOnly.Enabled = built && !modsBusy;
        if (!built) ModMessage("Build the game first: mods belong to its folder.");
        if (mods.Count == 0) modList.Controls.Add(modEmpty);
        foreach (var m in mods.ToList())
        {
            var (name, version) = ModInfo(m.Path);
            var card = new Card { Width = 760, Height = 92, Margin = new Padding(0, 0, 0, 10) };
            const int textW = 760 - 18 - 220;   // the text keeps clear of the switch and the remove button
            var title = Theme.Label(name + (version != "" ? "  " + version : ""), 11f, style: FontStyle.Bold);
            title.Location = new Point(18, 14);
            title.BackColor = Theme.Card;
            string state = !m.Analysed ? "Not checked yet."
                : $"{m.Replaces:N0} file{(m.Replaces == 1 ? "" : "s")} used" +
                  (m.Adds > 0 ? $", {m.Adds:N0} new files not supported yet" : "") +
                  (m.Unpacked > 0 ? $", {m.Unpacked:N0} unsupported" : "") +
                  (m.Code ? ", code (exefs) left out" : "") +
                  (m.Adds > 0 || m.Code ? ": the mod may not work fully" : "");
            var info = Theme.Label(state, 9.5f, muted: true);
            info.ForeColor = m.Analysed && (m.Adds > 0 || m.Code) ? Theme.Mix(Theme.Accent2, Theme.Muted, 0.3f) : Theme.Muted;
            info.Location = new Point(18, 42);
            info.BackColor = Theme.Card;
            info.MaximumSize = new Size(textW, 0);   // wraps instead of running under the switch
            var where = Theme.Label(m.Path, 8.5f, muted: true);
            where.BackColor = Theme.Card;
            where.MaximumSize = new Size(textW, 18);
            title.MaximumSize = new Size(textW, 0);
            int infoH = TextRenderer.MeasureText(info.Text, info.Font, new Size(textW, 0), TextFormatFlags.WordBreak).Height;
            where.Location = new Point(18, 44 + infoH + 4);
            card.Height = Math.Max(92, where.Bottom + where.Font.Height + 14);
            var toggle = new Toggle { On = m.Enabled, Location = new Point(card.Width - 200, 29), Enabled = built && !modsBusy,
                                      AccessibleName = $"Enable {name}" };
            var remove = Theme.Button("Remove");
            remove.Location = new Point(card.Width - 116, 25);
            remove.Enabled = built && !modsBusy;
            remove.AccessibleName = $"Remove {name} from the list";
            toggle.Switched += async (_, _) => { m.Enabled = toggle.On; SaveMods(); await ApplyMods(); };
            remove.Click += async (_, _) =>
            {
                if (modsBusy) return;
                mods.Remove(m);
                SaveMods();
                await ApplyMods();
            };
            card.Controls.AddRange(new Control[] { title, info, where, toggle, remove });
            modList.Controls.Add(card);
        }
        modList.ResumeLayout();
    }

    async Task AddMod(string dir)
    {
        // the usual layout is <mod>\romfs\<game path>; some mods start at the game path itself (world\...): mod_files.py
        // matches those by the longest path the game knows
        if (!Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories).Any())
        {
            ModMessage("That folder is empty.", true);
            return;
        }
        if (mods.Any(m => string.Equals(m.Path, dir, StringComparison.OrdinalIgnoreCase))) { ModMessage("That mod is already in the list.", true); return; }
        mods.Add(new ModEntry { Path = dir, Enabled = true });
        SaveMods();
        await ApplyMods();
    }

    // tools/mod_files.py for the mods switched on, in their order: game\mod_files.txt, and "mod_files" in karpur.ini. Mods
    // not checked yet are checked first (on or off), for their line in the list.
    async Task ApplyMods()
    {
        if (modsBusy) return;
        modsBusy = true;
        RenderMods();
        try
        {
            string root = Builder.Root(targetBox.Text), python = Path.Combine(root, "python", "python.exe");
            if (Builder.IsSourceRoot(root)) python = Path.Combine(root, ".venv", "Scripts", "python.exe");
            if (!File.Exists(python)) { ModMessage("Python is missing in the game folder: press Repair in Setup.", true); return; }
            foreach (var m in mods.Where(m => !m.Analysed).ToList())
            {
                ModMessage($"Checking {ModInfo(m.Path).Name} against your game …");
                var r = await RunModFiles(python, root, new[] { m.Path }, Path.Combine(root, "build", "mod_check.txt"));
                if (r != null && r.Mods.Count == 1) (m.Replaces, m.Adds, m.Unpacked, m.Code, m.Analysed) =
                    (r.Mods[0].Replaces, r.Mods[0].Adds, r.Mods[0].Unpacked, r.Mods[0].Code, true);
                if (r != null && r.Mods.Count == 1 && r.Mods[0].Replaces == 0)
                    ModMessage($"{ModInfo(m.Path).Name}: none of its files is a file of your game (another game or version?).", true);
            }
            var on = mods.Where(m => m.Enabled && Directory.Exists(m.Path)).Select(m => m.Path).ToArray();
            var ini = new Ini(Builder.Ini(targetBox.Text));
            if (on.Length == 0)
            {
                ini.Set("mods", "mod_files", "0");
                ini.Save();
                ModMessage(mods.Count == 0 ? "" : "No mod is switched on: the game uses its own files.");
            }
            else
            {
                ModMessage("Preparing the mods for the game …");
                int maxSize = int.TryParse(Value(modMaxSize), out int selectedMax) ? selectedMax : 0;
                var r = await RunModFiles(python, root, on, Path.Combine(root, "game", "mod_files.txt"),
                                          maxSize, modBntxOnly.Checked);
                ini.Set("mods", "mod_files", r != null ? "1" : "0");
                ini.Save();
                ModMessage(r != null ? $"{r.Listed:N0} replacement files selected from {on.Length} mod{(on.Length == 1 ? "" : "s")}; " +
                    $"{r.SizeSkipped:N0} over the size limit, {r.TypeSkipped:N0} non-BNTX excluded. Used at the next start."
                                     : "Could not prepare the mods (see launcher.log); the game uses its own files.", r == null);
            }
            SaveMods();
        }
        catch (Exception e) { ModMessage("Could not apply the mods: " + e.Message, true); }
        finally
        {
            modsBusy = false;
            RenderMods();
        }
    }

    record ModResult(int Replaces, int Adds, int Unpacked, bool Code);
    record ModRunResult(List<ModResult> Mods, int Listed, int SizeSkipped, int TypeSkipped);

    static async Task<ModRunResult?> RunModFiles(string python, string root, string[] dirs, string outFile,
                                                 int maxSizeMiB = 0, bool bntxOnly = false)
    {
        var psi = new ProcessStartInfo(python) { WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
                                                 RedirectStandardOutput = true, RedirectStandardError = true };
        psi.ArgumentList.Add(Path.Combine(root, "tools", "mod_files.py"));
        foreach (var d in dirs) psi.ArgumentList.Add(d);
        psi.ArgumentList.Add("--out");
        psi.ArgumentList.Add(outFile);
        psi.ArgumentList.Add("--quiet-files");
        if (maxSizeMiB > 0) { psi.ArgumentList.Add("--max-size-mib"); psi.ArgumentList.Add(maxSizeMiB.ToString()); }
        if (bntxOnly) psi.ArgumentList.Add("--bntx-only");
        using var p = Process.Start(psi);
        if (p == null) return null;
        var outTask = p.StandardOutput.ReadToEndAsync();
        var errTask = p.StandardError.ReadToEndAsync();
        await p.WaitForExitAsync();
        string text = await outTask, err = await errTask;
        try { File.AppendAllText(Path.Combine(root, "launcher.log"), $"=== mods {DateTime.Now:yyyy-MM-dd HH:mm:ss}\n{text}{err}\n"); } catch { }
        var line = text.Split('\n').LastOrDefault(l => l.StartsWith("SUMMARY "));
        var filter = text.Split('\n').LastOrDefault(l => l.StartsWith("FILTER "));
        if (p.ExitCode != 0 || line == null || filter == null) return null;
        try
        {
            using var doc = JsonDocument.Parse(line[8..]);
            using var counts = JsonDocument.Parse(filter[7..]);
            var mods = doc.RootElement.EnumerateArray().Select(e => new ModResult(e.GetProperty("replaces").GetInt32(),
                e.GetProperty("adds").GetInt32(), e.GetProperty("unpacked").GetInt32(), e.GetProperty("code").GetBoolean())).ToList();
            var totals = counts.RootElement;
            return new ModRunResult(mods, totals.GetProperty("listed").GetInt32(),
                totals.GetProperty("size_skipped").GetInt32(), totals.GetProperty("type_skipped").GetInt32());
        }
        catch (JsonException) { return null; }
    }
}
