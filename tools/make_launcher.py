"""Maintainer tool: builds dist/KarPur-Launcher.exe, a single file for the GitHub releases page.

The launcher (launcher/, C# WinForms, self-contained) carries payload.zip: every file of this repository that git tracks
or would track, the shader translator (build/xlate/totk_xlate.exe and .id, from tools/build_xlate.py) and hactool
(toolchain/hactool, from tools/build_hactool.py). None of these hold anything from the game; the player's dumps and keys
never leave their PC. Needs the .NET 10 SDK.

  .venv\\Scripts\\python tools\\make_launcher.py
"""

import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / 'dist'
PREBUILT = ['build/xlate/totk_xlate.exe', 'build/xlate/totk_xlate.id', 'toolchain/hactool/hactool.exe',
            'toolchain/hactool/LICENSE']


def repo_files():
    r = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT,
                       capture_output=True, check=True)
    names = [n for n in r.stdout.decode('utf-8').split('\0') if n]
    return [n for n in names if not n.startswith(('launcher/', '.github/')) and (ROOT / n).is_file()]


def main():
    missing = [p for p in PREBUILT if not (ROOT / p).is_file()]
    if missing:
        sys.exit(f'missing: {", ".join(missing)} (run tools/build_xlate.py and tools/build_hactool.py)')
    if not (ROOT / 'launcher' / 'pins.json').is_file():
        sys.exit('launcher/pins.json is missing: run tools/launcher_pins.py')
    DIST.mkdir(exist_ok=True)
    payload = DIST / 'payload.zip'
    files = repo_files() + PREBUILT
    with zipfile.ZipFile(payload, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in files:
            z.write(ROOT / n, n)
    print(f'{payload}: {len(files)} files, {payload.stat().st_size / 2**20:.1f} MiB')
    r = subprocess.run(['dotnet', 'publish', str(ROOT / 'launcher' / 'KarPurLauncher.csproj'), '-c', 'Release',
                        f'-p:PayloadZip={payload}', '-o', str(DIST / 'publish')], cwd=ROOT)
    if r.returncode:
        sys.exit('dotnet publish failed')
    exe = DIST / 'publish' / 'KarPur-Launcher.exe'
    target = DIST / 'KarPur-Launcher.exe'
    target.write_bytes(exe.read_bytes())
    print(f'{target}: {target.stat().st_size / 2**20:.1f} MiB')
    (DIST / 'recomplauncher.json').write_bytes((ROOT / 'recomplauncher.json').read_bytes())   # next to the exe, for RecompLauncher


if __name__ == '__main__':
    main()
