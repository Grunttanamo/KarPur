using System.ComponentModel;
using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace KarPurLauncher;

// Full-width edition artwork, cached once per size and palette to keep tab switches clean.
class Hero : Panel
{
    static readonly Random Stars = new(7);
    static readonly Dictionary<string, Image?> Banners = new();

    static Image? Banner(string edition)
    {
        if (Banners.TryGetValue(edition, out var img)) return img;
        try
        {
            using var s = typeof(Hero).Assembly.GetManifestResourceStream($"banner-{edition}.jpg");
            img = s != null ? new Bitmap(s) : null;
        }
        catch { img = null; }
        return Banners[edition] = img;
    }
    readonly (float X, float Y, float R, int A)[] stars;
    Bitmap? scene;
    Theme.Palette? scenePalette;

    public Hero()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
                 ControlStyles.UserPaint, true);
        BackColor = Theme.Back;
        stars = Enumerable.Range(0, 70).Select(_ => ((float)Stars.NextDouble(), (float)Stars.NextDouble() * 0.6f,
                                                      0.6f + (float)Stars.NextDouble() * 1.4f, 60 + Stars.Next(160))).ToArray();
    }

    Bitmap? Scene()
    {
        if (Width < 2 || Height < 2) return null;
        if (scene == null || scene.Size != ClientSize || scenePalette != Theme.Current)
        {
            scene?.Dispose();
            scene = new Bitmap(Width, Height);
            scenePalette = Theme.Current;
            using var g = Graphics.FromImage(scene);
            PaintScene(g);
        }
        return scene;
    }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        if (Scene() is Bitmap bitmap) e.Graphics.DrawImageUnscaled(bitmap, 0, 0);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) { scene?.Dispose(); scene = null; }
        base.Dispose(disposing);
    }

    // The scene under a child control (buttons, chips): drawn by the child itself, shifted to its place, so its rounded
    // corners show the scene and not a stale or black rectangle (Windows' transparency through nested panels).
    public void PaintBehind(Graphics g, Control child)
    {
        var at = PointToClient(child.Parent!.PointToScreen(child.Location));
        // Copy only the child's part. Repainting the scene here used ResetClip, which discarded the invalidated
        // region during a tab switch and let banner fragments escape over neighbouring controls.
        if (Scene() is Bitmap bitmap) g.DrawImageUnscaled(bitmap, -at.X, -at.Y);
    }

    void PaintScene(Graphics g)
    {
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Back);
        if (Width < 40 || Height < 40) { g.Restore(state); return; }
        var p = Theme.Current;
        var r = new RectangleF(0, 0, Width - 1, Height - 1);
        using var clip = new GraphicsPath();
        clip.AddRectangle(r);
        g.SetClip(clip, CombineMode.Intersect);
        var banner = p.Name == "neutral" ? null : Banner(p.Name);
        if (banner != null)
        {
            // Violet anchors near the top of the source so Miraidon's head stays in view.
            // Scarlet keeps its original centred crop.
            float scale = Math.Max(r.Width / banner.Width, r.Height / banner.Height);
            float w = banner.Width * scale, h = banner.Height * scale;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            float y = (r.Height - h) * (p.Name == "violet" ? 0.08f : 0.5f);
            g.DrawImage(banner, (r.Width - w) / 2, y, w, h);
        }
        else
        {
            using (var sky = new LinearGradientBrush(r, p.Sky1, p.Sky2, 90f)) g.FillRectangle(sky, r);
            if (p.Name == "scarlet") PaintScarlet(g, r, p);
            else PaintViolet(g, r, p);
        }
        // a dark veil on the left, where the text sits
        using (var veil = new LinearGradientBrush(r, Theme.Alpha(p.Back, 125), Theme.Alpha(p.Back, 0), 0f))
        {
            var blend = new Blend { Positions = new[] { 0f, 0.45f, 1f }, Factors = new[] { 0f, 0.55f, 1f } };
            veil.Blend = blend;
            g.FillRectangle(veil, r);
        }
        g.Restore(state);
        // The logo stays inside the cached scene, so transparent children share the exact same pixels.
        void Logo(string edition, Rectangle box)
        {
            if (Artwork.Get($"logo-{edition}.png") is not Image image) return;
            float scale = Math.Min((float)box.Width / image.Width, (float)box.Height / image.Height);
            g.DrawImage(image, box.Left, box.Top, image.Width * scale, image.Height * scale);
        }
        Logo(p.Name == "neutral" ? "scarlet" : p.Name, new Rectangle(p.Name == "violet" ? Width - 312 : 28, 26, 284, 154));
        if (p.Name == "neutral") Logo("violet", new Rectangle(332, 26, 284, 154));
    }

    void PaintViolet(Graphics g, RectangleF r, Theme.Palette p)
    {
        foreach (var (x, y, rad, a) in stars)
        {
            using var b = new SolidBrush(Color.FromArgb(a, 255, 255, 255));
            g.FillEllipse(b, x * r.Width, y * r.Height, rad, rad);
        }
        // the ringed moon
        float mx = r.Width * 0.78f, my = r.Height * 0.30f, mr = r.Height * 0.16f;
        using (var halo = new GraphicsPath())
        {
            halo.AddEllipse(mx - mr * 2.4f, my - mr * 2.4f, mr * 4.8f, mr * 4.8f);
            using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(p.Accent, 90), SurroundColors = new[] { Theme.Alpha(p.Accent, 0) } };
            g.FillPath(hb, halo);
        }
        using (var moon = new LinearGradientBrush(new RectangleF(mx - mr, my - mr, mr * 2, mr * 2), Theme.Mix(p.Accent, Color.White, 0.55f), p.Accent, 60f))
            g.FillEllipse(moon, mx - mr, my - mr, mr * 2, mr * 2);
        using (var ring = new Pen(Theme.Alpha(p.Accent2, 200), 2.2f))
            g.DrawEllipse(ring, mx - mr * 1.9f, my - mr * 0.45f, mr * 3.8f, mr * 0.9f);
        // the horizon glow and the neon grid below it
        float hy = r.Height * 0.68f;
        using (var glow = new LinearGradientBrush(new RectangleF(0, hy - 60, r.Width, 120), Theme.Alpha(p.Glow, 0), Theme.Alpha(p.Glow, 110), 90f))
        {
            glow.Blend = new Blend { Positions = new[] { 0f, 0.5f, 1f }, Factors = new[] { 0f, 1f, 0f } };
            g.FillRectangle(glow, 0, hy - 60, r.Width, 120);
        }
        using (var ground = new LinearGradientBrush(new RectangleF(0, hy, r.Width, r.Height - hy + 1), Theme.Mix(p.Sky2, p.Back, 0.4f), p.Back, 90f))
            g.FillRectangle(ground, 0, hy, r.Width, r.Height - hy + 1);
        using var line = new Pen(Theme.Alpha(p.Accent2, 120), 1.2f);
        float vx = r.Width * 0.62f;
        for (int i = -14; i <= 14; i++) g.DrawLine(line, vx, hy, vx + i * r.Width * 0.09f, r.Height + 4);
        for (int i = 1; i <= 7; i++)
        {
            float t = i / 7f, y = hy + (r.Height - hy) * t * t;
            using var l = new Pen(Theme.Alpha(p.Accent2, (int)(40 + 120 * t)), 1.2f);
            g.DrawLine(l, 0, y, r.Width, y);
        }
        using var horizon = new Pen(Theme.Alpha(p.Accent2, 230), 2f);
        g.DrawLine(horizon, 0, hy, r.Width, hy);
        // circuit lines in the sky
        using var trace = new Pen(Theme.Alpha(p.Accent, 110), 1.4f);
        for (int k = 0; k < 4; k++)
        {
            float y0 = r.Height * (0.14f + k * 0.11f), x0 = r.Width * (0.42f + k * 0.04f);
            g.DrawLines(trace, new[] { new PointF(x0, y0), new PointF(x0 + 60, y0), new PointF(x0 + 80, y0 + 20), new PointF(x0 + 150, y0 + 20) });
            using var dot = new SolidBrush(Theme.Alpha(p.Accent2, 220));
            g.FillEllipse(dot, x0 + 147, y0 + 17, 6, 6);
        }
    }

    void PaintScarlet(Graphics g, RectangleF r, Theme.Palette p)
    {
        float sx = r.Width * 0.74f, sy = r.Height * 0.62f, sr = r.Height * 0.2f;
        // rays
        var state = g.Save();
        g.TranslateTransform(sx, sy);
        for (int i = 0; i < 18; i++)
        {
            using var ray = new GraphicsPath();
            ray.AddPolygon(new[] { new PointF(0, 0), new PointF(-14, -r.Width), new PointF(14, -r.Width) });
            using var rb = new SolidBrush(Theme.Alpha(p.Accent2, 18));
            g.FillPath(rb, ray);
            g.RotateTransform(20);
        }
        g.Restore(state);
        using (var halo = new GraphicsPath())
        {
            halo.AddEllipse(sx - sr * 2.6f, sy - sr * 2.6f, sr * 5.2f, sr * 5.2f);
            using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(p.Accent2, 150), SurroundColors = new[] { Theme.Alpha(p.Accent2, 0) } };
            g.FillPath(hb, halo);
        }
        using (var sun = new LinearGradientBrush(new RectangleF(sx - sr, sy - sr, sr * 2, sr * 2), Theme.Mix(p.Accent2, Color.White, 0.5f), p.Accent, 90f))
            g.FillEllipse(sun, sx - sr, sy - sr, sr * 2, sr * 2);
        // three mountain ridges, darker towards the front
        for (int layer = 0; layer < 3; layer++)
        {
            float baseY = r.Height * (0.66f + layer * 0.1f);
            var pts = new List<PointF> { new(0, r.Height + 2) };
            var rnd = new Random(11 + layer);
            for (float x = 0; x <= r.Width + 40; x += 40 + layer * 10)
                pts.Add(new PointF(x, baseY - (float)rnd.NextDouble() * r.Height * (0.16f - layer * 0.03f)));
            pts.Add(new PointF(r.Width + 2, r.Height + 2));
            using var b = new SolidBrush(Theme.Mix(Theme.Mix(p.Sky2, p.Back, 0.45f + layer * 0.22f), p.Accent, 0.08f));
            g.FillPolygon(b, pts.ToArray());
        }
        // embers
        foreach (var (x, y, rad, a) in stars.Take(40))
        {
            using var b = new SolidBrush(Color.FromArgb(a / 2, p.Accent2));
            g.FillEllipse(b, x * r.Width, (0.15f + y) * r.Height, rad * 1.4f, rad * 1.4f);
        }
    }
}

// A small rounded label on the hero: a glyph and a text on a translucent field.
class Chip : Control
{
    public string CompactText = "";
    public bool Compact;
    string DisplayText => Compact && CompactText != "" ? CompactText : Text;
    public void Fit(bool compact)
    {
        Compact = compact;
        Width = TextRenderer.MeasureText(DisplayText, Font).Width + (Compact || Glyph == "" ? 24 : 44);
        Invalidate();
    }
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public string Glyph { get; set; } = "";
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public bool Good { get; set; }

    public Chip()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        Font = new Font("Segoe UI Semibold", 9.5f);
        Height = 32;
        Margin = new Padding(0, 0, 8, 0);
    }

    protected override void OnTextChanged(EventArgs e)
    {
        base.OnTextChanged(e);
        Fit(Compact);
    }

    protected override void OnPaintBackground(PaintEventArgs e) { }
    Hero? FindHero() { for (var p = Parent; p != null; p = p.Parent) if (p is Hero h) return h; return null; }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        if (FindHero() is Hero hero) hero.PaintBehind(g, this);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Height / 2f);
        using (var b = new SolidBrush(Theme.Alpha(Theme.Back, 170))) g.FillPath(b, path);
        using (var pen = new Pen(Theme.Alpha(Good ? Theme.Good : Theme.Border, 200))) g.DrawPath(pen, path);
        int x = 10;
        if (Glyph != "" && !Compact)
        {
            using var gf = new Font(Theme.IconFont, 10f);
            TextRenderer.DrawText(g, Glyph, gf, new Rectangle(x, 0, 20, Height), Good ? Theme.Good : Theme.Accent2,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            x += 24;
        }
        TextRenderer.DrawText(g, DisplayText, Font, new Rectangle(x, 0, Width - x, Height), Theme.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
    }
}

// A card with a glyph, a title, a big value and a note: one fact about the build.
// One party member: a monogram in the edition's gradient (no game art), the name the game shows, the level, a star when
// shiny.
class TeamCard : Control
{
    public string Nick = "", Level = "";
    public bool Shiny;
    public Image? Icon;   // the Pokemon's icon from the player's own game files (build\game\icons), else the monogram

    public TeamCard()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Size = new Size(142, 164);
        Margin = new Padding(0, 0, 12, 12);
        Font = new Font("Segoe UI Semibold", 10f);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) { Icon?.Dispose(); Icon = null; }
        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Back);
        using (var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), 12))
        {
            using var b = new LinearGradientBrush(ClientRectangle, Theme.Mix(Theme.Card, Theme.Accent, 0.09f), Theme.Card, 90f);
            g.FillPath(b, path);
            using var pen = new Pen(Shiny ? Theme.Mix(Theme.Accent2, Theme.Card, 0.2f) : Theme.Border);
            g.DrawPath(pen, path);
        }
        var circle = new RectangleF(Width / 2f - 30, 24, 60, 60);
        if (Icon != null)
        {
            // a soft glow in the edition's colours behind the icon, the icon a little larger than the circle
            using (var halo = new System.Drawing.Drawing2D.GraphicsPath())
            {
                halo.AddEllipse(circle.X - 6, circle.Y - 6, circle.Width + 12, circle.Height + 12);
                using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(Theme.Accent, 110), SurroundColors = new[] { Theme.Alpha(Theme.Accent2, 0) } };
                g.FillPath(hb, halo);
            }
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.DrawImage(Icon, new RectangleF(Width / 2f - 56, 0, 112, 112));   // the icons carry a margin of their own
        }
        else
        {
            using (var grad = new LinearGradientBrush(circle, Theme.Accent, Theme.Accent2, 45f)) g.FillEllipse(grad, circle);
            string letter = Nick.Length > 0 ? Nick[..1].ToUpperInvariant() : "?";
            using var lf = new Font("Segoe UI", 20f, FontStyle.Bold);
            TextRenderer.DrawText(g, letter, lf, Rectangle.Round(circle), Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }
        if (Shiny)
        {
            using var sf = new Font(Theme.IconFont, 11f);
            TextRenderer.DrawText(g, "", sf, new Rectangle(Width - 28, 6, 22, 22), Theme.Accent2, TextFormatFlags.NoPadding);
        }
        TextRenderer.DrawText(g, Nick, Font, new Rectangle(4, 111, Width - 8, 22), Theme.Text,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.EndEllipsis);
        using var small = new Font("Segoe UI", 9f);
        TextRenderer.DrawText(g, Level, small, new Rectangle(4, 135, Width - 8, 20), Theme.Accent2, TextFormatFlags.HorizontalCenter);
    }
}

partial class MainForm
{
    readonly Hero hero = new() { Dock = DockStyle.Top, Height = 340 };
    readonly Label heroText = Theme.Label("", 10.5f, muted: true);
    readonly FlowLayoutPanel heroChips = new() { AutoSize = true, BackColor = Color.Transparent, WrapContents = false };
    readonly Button heroPlay = Theme.Button("Play", true, Theme.GlyphPlay), heroFolder = Theme.Button("Open game folder", glyph: Theme.GlyphFolder),
                    heroSetup = Theme.Button("Go to Setup", true, Theme.GlyphSetup);
    readonly FlowLayoutPanel teamRow = new() { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Back, WrapContents = true };
    readonly Label teamTitle = Theme.Label("YOUR TEAM", 8.5f, muted: true, style: FontStyle.Bold);

    // AMD graphics cards crash or run very poorly right now, and it keeps coming back as separate issues from
    // different people hitting the same thing. Shown to everyone on Home, not just detected AMD users, since the
    // point is also to reach someone who could help fix it, not only to warn the one person about to hit it.
    Control AmdNotice()
    {
        var title = Theme.Label("Known issue: AMD graphics cards", 10.5f, style: FontStyle.Bold);
        title.ForeColor = Theme.Error;
        var body = Theme.Label(
            "KarPur currently crashes or runs very poorly on AMD GPUs. If you know Vulkan and AMD drivers and could " +
            "help track this down, please get in touch — it would help a lot of people at once.", 9.5f, muted: true);
        body.MaximumSize = new Size(700, 0);
        body.Margin = new Padding(0, 6, 0, 10);
        var link = new LinkLabel { Text = "Open the GitHub issues", AutoSize = true, LinkColor = Theme.Error, BackColor = Color.Transparent };
        link.Click += (_, _) => Process.Start(new ProcessStartInfo("https://github.com/Grunttanamo/KarPur/issues") { UseShellExecute = true });
        var stack = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, BackColor = Color.Transparent, WrapContents = false };
        stack.Controls.Add(title);
        stack.Controls.Add(body);
        stack.Controls.Add(link);
        var card = new Card { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(16, 14, 16, 14) };
        card.Controls.Add(stack);
        return card;
    }

    Control HomePage()
    {
        var page = new Surface { BackColor = Theme.Back };
        heroText.BackColor = Color.Transparent;
        hero.Height = 350;
        heroText.Location = new Point(30, 194);
        heroText.MaximumSize = new Size(560, 0);
        heroChips.AutoSize = false;
        heroChips.Height = 40;
        heroChips.WrapContents = false;
        heroPlay.Font = new Font("Segoe UI Semibold", 13f);
        heroPlay.Size = new Size(164, 50);
        heroSetup.Size = new Size(180, 50);
        heroFolder.Size = new Size(204, 50);
        foreach (var button in new[] { heroPlay, heroSetup, heroFolder })
        {
            button.BackColor = Color.Transparent;
            ((RoundButton)button).Radius = 12;
        }
        hero.Controls.AddRange(new Control[] { heroText, heroChips, heroPlay, heroSetup, heroFolder });
        void PlaceButtons()
        {
            bool compact = hero.Width < 950;
            heroPlay.Width = compact ? 120 : 164;
            heroFolder.Width = compact ? 164 : 204;
            var buttons = new[] { heroPlay, heroSetup, heroFolder }.Where(b => b.Visible).ToArray();
            int width = buttons.Sum(b => b.Width + 12) - 12;
            int x = Math.Max(28, hero.ClientSize.Width - width - 28);
            foreach (var button in buttons) { button.Location = new Point(x, hero.Height - 74); x += button.Width + 12; }
            foreach (var chip in heroChips.Controls.OfType<Chip>()) chip.Fit(compact);
            // Chips and actions share a bottom edge; compact labels keep that row usable at minimum width.
            heroChips.SetBounds(24, hero.Height - 56, Math.Max(1, hero.Width - width - 68), 32);
        }
        hero.Resize += (_, _) => PlaceButtons();
        heroChips.ControlAdded += (_, _) => PlaceButtons();
        foreach (var button in new[] { heroPlay, heroSetup, heroFolder }) button.VisibleChanged += (_, _) => PlaceButtons();
        var body = new Surface { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(28, 24, 28, 20), BackColor = Theme.Back };
        teamTitle.Dock = DockStyle.Top;
        teamTitle.Padding = new Padding(2, 0, 0, 12);
        body.Controls.Add(teamRow);
        body.Controls.Add(teamTitle);
        body.Controls.Add(Spacer(16, Theme.Back));
        body.Controls.Add(AmdNotice());
        page.Controls.Add(body);
        page.Controls.Add(hero);
        heroPlay.Click += (_, _) => Play();
        heroSetup.Click += (_, _) => Show("Setup");
        heroFolder.Click += (_, _) =>
        {
            string root = Builder.Root(targetBox.Text);
            if (Directory.Exists(root)) Process.Start(new ProcessStartInfo(root) { UseShellExecute = true });
        };
        teamRow.SizeChanged += (_, _) => LayoutTeam();
        return page;
    }

    void LayoutTeam()
    {
        int n = teamRow.Controls.Count;
        if (n == 0) return;
        int width = Math.Max(100, (teamRow.ClientSize.Width - 12 * (n - 1)) / n);
        for (int i = 0; i < n; i++)
        {
            teamRow.Controls[i].Width = width;
            teamRow.Controls[i].Margin = new Padding(0, 0, i == n - 1 ? 0 : 12, 12);
        }
    }

    // What the Home page shows: the edition, whether the game is built, DLC and the most recent play or save.
    void RefreshHome()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        bool dlc = built && Builder.HasDlc(targetBox.Text);
        sidebarLogo.Edition = built ? Theme.Current.Name : "neutral";
        sidebarLogo.Invalidate();
        heroText.Visible = !built;
        heroText.Text = cancel != null ? "Building your PC version…" : "Choose your game files in Setup to get started.";
        heroPlay.Visible = built;
        heroFolder.Visible = built;
        heroSetup.Visible = !built;
        heroPlay.Enabled = built && cancel == null;

        while (heroChips.Controls.Count > 0) heroChips.Controls[0].Dispose();
        if (built)
        {
            heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphCheck, Good = true, CompactText = "v4.0.0", Text = "Game version 4.0.0" });
            heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphGame, CompactText = dlc ? "DLC installed" : "No DLC", Text = dlc ? "DLC installed" : "DLC not installed" });
            var played = LastPlayed();
            var save = NewestSave();
            if (played != null) heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphPlay, CompactText = $"Played {played.Value:dd MMM, HH:mm}", Text = $"Last played {played.Value:yyyy-MM-dd, HH:mm}" });
            else if (save != null) heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphSave, CompactText = $"Save {save.Value:dd MMM, HH:mm}", Text = $"Last save {save.Value:yyyy-MM-dd, HH:mm}" });
        }

        RefreshTeam(built);
        hero.Invalidate(true);
    }

    // <profile>	eam.json, which the game writes when it saves (runtime/team.c): name, level, shiny per party member
    void RefreshTeam(bool built)
    {
        teamRow.SuspendLayout();
        while (teamRow.Controls.Count > 0) teamRow.Controls[0].Dispose();
        var team = new List<TeamCard>();
        try
        {
            string? profile = ProfileDir();
            string file = profile != null ? Path.Combine(profile, "team.json") : "";
            if (built && File.Exists(file))
            {
                var icons = IconCatalog();
                using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(file));
                foreach (var m in doc.RootElement.GetProperty("team").EnumerateArray())
                {
                    int species = m.GetProperty("species").GetInt32(), form = m.TryGetProperty("form", out var fm) ? fm.GetInt32() : 0;
                    team.Add(new TeamCard
                    {
                        Nick = m.GetProperty("nickname").GetString() ?? "", Level = m.GetProperty("level").GetInt32() is int lv && lv > 0 ? $"Lv. {lv}" : "",
                        Shiny = m.TryGetProperty("shiny", out var sh) && sh.GetBoolean(),
                        Icon = LoadIcon(icons, species, form),
                    });
                }
            }
        }
        catch { foreach (var card in team) card.Dispose(); team.Clear(); }
        teamTitle.Visible = teamRow.Visible = team.Count > 0;
        teamRow.Controls.AddRange(team.ToArray());
        LayoutTeam();
        teamRow.ResumeLayout(true);
    }

    // build\game\icons\catalog.txt (the shader census: "species form folder" per line) and the folder's PNG
    Dictionary<(int, int), string> IconCatalog()
    {
        var map = new Dictionary<(int, int), string>();
        try
        {
            string dir = Path.Combine(Builder.Root(targetBox.Text), "build", "game", "icons");
            foreach (var line in File.ReadAllLines(Path.Combine(dir, "catalog.txt")))
            {
                var p = line.Split(' ');
                if (p.Length == 3 && int.TryParse(p[0], out int sp) && int.TryParse(p[1], out int fo))
                    map.TryAdd((sp, fo), Path.Combine(dir, p[2] + ".png"));
            }
        }
        catch { }
        return map;
    }

    static Image? LoadIcon(Dictionary<(int, int), string> icons, int species, int form)
    {
        if (!icons.TryGetValue((species, form), out var file) && !icons.TryGetValue((species, 0), out file)) return null;
        try
        {
            if (!File.Exists(file)) return null;
            using var img = Image.FromFile(file);
            return new Bitmap(img);   // a copy: the file stays free for the next build
        }
        catch { return null; }
    }

    DateTime? LastPlayed()
    {
        try
        {
            string suffix = $" exe={Builder.Exe(targetBox.Text)} cwd={Builder.Root(targetBox.Text)}";
            return File.ReadLines(Path.Combine(AppContext.BaseDirectory, "launcher-start.log"))
                .Where(line => line.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
                .Select(line => DateTime.TryParse(line[..line.IndexOf(" exe=", StringComparison.Ordinal)], out var time) ? (DateTime?)time : null)
                .Where(time => time != null).LastOrDefault();
        }
        catch { return null; }
    }
}
