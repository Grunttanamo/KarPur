"""Exercise the launcher's actual payload unpacker against issue #66, without game files.

Needs the .NET 10 SDK. Builds the launcher with a small synthetic embedded payload in an isolated build directory,
then calls its private unpacker through reflection. No window opens and no installed build is changed.

    .venv\\Scripts\\python tools\\check_launcher_payload.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
using System.Reflection;
using System.Text;

var builder = Assembly.Load("KarPur-Launcher").GetType("KarPurLauncher.Builder", throwOnError: true)!;
var unpack = builder.GetMethod("UnpackPayload", BindingFlags.NonPublic | BindingFlags.Static)!;
string root = Path.Combine(AppContext.BaseDirectory, "installed");
Directory.CreateDirectory(Path.Combine(root, "runtime"));
Directory.CreateDirectory(Path.Combine(root, "build", "game", "obj", "runtime"));
string source = Path.Combine(root, "runtime", "resolution.c");
string obj = Path.Combine(root, "build", "game", "obj", "runtime", "resolution.o");
DateTime oldSource = new(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
DateTime oldObject = oldSource.AddDays(1);
void Put(string path, string text, DateTime date) {
    File.WriteAllText(path, text, new UTF8Encoding(false));
    File.SetLastWriteTimeUtc(path, date);
}
void Check(bool ok, string message) {
    if (!ok) throw new Exception(message);
    Console.WriteLine("ok   " + message);
}
void Unpack(bool updated) => unpack.Invoke(null, new object[] { root, updated });

// The release carries old dates; an older build already has a newer object. Different source, same byte length.
Put(source, "old source\n", oldSource);
Put(obj, "old object\n", oldObject);
Unpack(false);
Check(File.ReadAllText(source) == "new source\n", "changed source replaced, including equal-length content");
Check(File.GetLastWriteTimeUtc(source) > File.GetLastWriteTimeUtc(obj), "changed source is newer than cached object");
Check(File.ReadAllText(obj) == "old object\n", "unpacking preserves build outputs");
Check(File.GetLastWriteTimeUtc(Path.Combine(root, "runtime", "host.h")) > oldObject, "new file receives current date");
Check(!File.Exists(Path.Combine(Directory.GetParent(root)!.FullName, "escaped.txt")), "archive traversal entry is rejected");

// Recovery from 0.3.7: the right source bytes are already installed but its object is still from the earlier release.
File.SetLastWriteTimeUtc(source, oldSource);
Unpack(true);
Check(File.GetLastWriteTimeUtc(source) > File.GetLastWriteTimeUtc(obj), "version change invalidates identical source with stale object");

// A second build on the same version must remain incremental, including after a failed build is retried.
DateTime before = File.GetLastWriteTimeUtc(source);
Unpack(false);
Check(File.GetLastWriteTimeUtc(source) == before, "same version and identical bytes preserve source date");

// Repair restores a deleted or modified source even without a version change.
File.Delete(source);
Unpack(false);
Check(File.ReadAllText(source) == "new source\n" && File.GetLastWriteTimeUtc(source) > oldObject,
      "deleted source is restored with current date");
Put(source, "bad source\n", oldSource);
Unpack(false);
Check(File.ReadAllText(source) == "new source\n" && File.GetLastWriteTimeUtc(source) > oldObject,
      "modified source is restored with current date");
Console.WriteLine("every payload check passed");
'''


def main():
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='launcher-payload-check-', dir=ROOT / 'build') as tmp:
        work = Path(tmp)
        payload = work / 'payload.zip'
        with zipfile.ZipFile(payload, 'w') as archive:
            for name, text in [('runtime/resolution.c', 'new source\n'), ('runtime/host.h', '// fixture\n'),
                               ('../escaped.txt', 'must not be extracted\n')]:
                entry = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
                archive.writestr(entry, text)
        project = escape(str(ROOT / 'launcher' / 'KarPurLauncher.csproj'), {'"': '&quot;'})
        (work / 'Check.csproj').write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <UseWindowsForms>true</UseWindowsForms>
    <RuntimeIdentifier>win-x64</RuntimeIdentifier>
    <SelfContained>true</SelfContained>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
  <ItemGroup><ProjectReference Include="{project}" /></ItemGroup>
</Project>
''', encoding='utf-8')
        (work / 'Program.cs').write_text(HARNESS, encoding='utf-8')
        env = dict(os.environ, DOTNET_NOLOGO='1', DOTNET_CLI_TELEMETRY_OPTOUT='1')
        subprocess.run(['dotnet', 'run', '--project', str(work / 'Check.csproj'), '-c', 'Release',
                        f'-p:PayloadZip={payload}', f'-p:ArtifactsPath={work / "artifacts"}',
                        '-p:NuGetAudit=false'],
                       cwd=ROOT, env=env, check=True)


if __name__ == '__main__':
    main()
