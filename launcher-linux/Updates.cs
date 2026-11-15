using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text.Json;

namespace KarPurLinux;

sealed record LinuxRelease(Version Version, string AssetName, string DownloadUrl, string? ChecksumUrl, string PageUrl);

static class Updates
{
    static HttpClient Client(TimeSpan timeout)
    {
        var http = new HttpClient { Timeout = timeout };
        http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("KarPur-Linux", Program.Version));
        return http;
    }

    public static async Task<LinuxRelease?> Latest(CancellationToken token = default)
    {
        using var http = Client(TimeSpan.FromSeconds(25));
        using var reply = await http.GetAsync("https://api.github.com/repos/Grunttanamo/KarPur/releases?per_page=20", token);
        reply.EnsureSuccessStatusCode();
        using var json = JsonDocument.Parse(await reply.Content.ReadAsStringAsync(token));
        foreach (var release in json.RootElement.EnumerateArray())
        {
            if (release.GetProperty("draft").GetBoolean() || release.GetProperty("prerelease").GetBoolean()) continue;
            string tag = release.GetProperty("tag_name").GetString() ?? "";
            if (!Version.TryParse(tag.TrimStart('v', 'V'), out var version)) continue;
            var assets = release.GetProperty("assets").EnumerateArray().ToArray();
            var appImage = assets.FirstOrDefault(a =>
            {
                string name = a.GetProperty("name").GetString() ?? "";
                return name.EndsWith(".AppImage", StringComparison.OrdinalIgnoreCase) &&
                    (name.StartsWith("KarPur-Linux-Launcher", StringComparison.OrdinalIgnoreCase) ||
                     name.StartsWith("KarPur-Launcher-Linux", StringComparison.OrdinalIgnoreCase) ||
                     name.StartsWith("KarPur-SteamDeck-Installer", StringComparison.OrdinalIgnoreCase));
            });
            if (appImage.ValueKind == JsonValueKind.Undefined) continue;
            string name = appImage.GetProperty("name").GetString()!;
            string? checksum = assets.Where(a => a.GetProperty("name").GetString() == name + ".sha256")
                .Select(a => a.GetProperty("browser_download_url").GetString()).FirstOrDefault();
            return new(version, name, appImage.GetProperty("browser_download_url").GetString()!, checksum,
                release.GetProperty("html_url").GetString()!);
        }
        return null;
    }

    public static async Task<string> Download(LinuxRelease release, string root, byte[] icon,
        Action<int> progress, CancellationToken token = default)
    {
        if (!OperatingSystem.IsLinux()) throw new PlatformNotSupportedException();
        root = Path.GetFullPath(root);
        if (root == "/") throw new IOException("Choose a KarPur installation folder first.");
        Directory.CreateDirectory(root);
        string target = Path.Combine(root, "KarPur-Linux-Launcher.AppImage");
        string temporary = target + ".download";
        using var http = Client(TimeSpan.FromMinutes(20));
        try
        {
            using var response = await http.GetAsync(release.DownloadUrl, HttpCompletionOption.ResponseHeadersRead, token);
            response.EnsureSuccessStatusCode();
            long? length = response.Content.Headers.ContentLength;
            await using (var input = await response.Content.ReadAsStreamAsync(token))
            await using (var output = File.Create(temporary))
            {
                byte[] buffer = new byte[1024 * 1024];
                long received = 0;
                int count;
                while ((count = await input.ReadAsync(buffer, token)) > 0)
                {
                    await output.WriteAsync(buffer.AsMemory(0, count), token);
                    received += count;
                    if (length > 0) progress((int)Math.Clamp(received * 100 / length.Value, 0, 100));
                }
            }
            if (new FileInfo(temporary).Length < 1024 * 1024) throw new InvalidDataException("The downloaded AppImage is incomplete.");
            byte[] header = new byte[4];
            using (var file = File.OpenRead(temporary)) file.ReadExactly(header);
            if (!header.SequenceEqual(new byte[] { 0x7f, (byte)'E', (byte)'L', (byte)'F' }))
                throw new InvalidDataException("The download is not an AppImage.");
            if (release.ChecksumUrl is { } checksumUrl)
            {
                string expected = (await http.GetStringAsync(checksumUrl, token)).Split(' ', '\n', '\r', '\t')[0];
                if (expected.Length != 64 || !expected.All(Uri.IsHexDigit))
                    throw new InvalidDataException("The release checksum is invalid.");
                await using var file = File.OpenRead(temporary);
                string actual = Convert.ToHexString(await SHA256.HashDataAsync(file, token));
                if (!actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The AppImage checksum does not match the release.");
            }
            File.SetUnixFileMode(temporary, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute |
                UnixFileMode.GroupRead | UnixFileMode.GroupExecute | UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
            File.Move(temporary, target, true);
            File.WriteAllText(Path.Combine(root, ".karpur-linux-launcher-version"), release.Version.ToString());
            Install.RegisterLauncher(root, icon);
            progress(100);
            return target;
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
