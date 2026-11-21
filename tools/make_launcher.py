"""Builds the release launcher in dist/ or a local test launcher in launcher/bin/preview2/.

The launcher (launcher/, C# WinForms, self-contained) carries payload.zip: every file of this repository that git tracks
or would track, the shader translator (build/xlate/totk_xlate.exe and .id, from tools/build_xlate.py) and hactool
(toolchain/hactool, from tools/build_hactool.py). None of these hold anything from the game; the player's dumps and keys
never leave their PC. Needs the .NET 10 SDK.

  .venv\\Scripts\\python tools\\make_launcher.py
  .venv\\Scripts\\python tools\\make_launcher.py --test
"""

import argparse
import hashlib
import json
import re
import shutil
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
                       capture_output=True)
    if r.returncode == 0:
        names = [n for n in r.stdout.decode('utf-8').split('\0') if n]
    else:
        manifest = ROOT / 'build' / 'source-files.json'
        if not manifest.is_file():
            raise RuntimeError('git metadata and build/source-files.json are both missing')
        names = json.loads(manifest.read_text(encoding='utf-8-sig'))
    return [n for n in names if not n.startswith(('launcher/', '.github/', '.staging/')) and (ROOT / n).is_file()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', action='store_true', help='build the local preview launcher without writing to dist')
    parser.add_argument('--out', type=Path, help='external output directory for a test launcher (implies --test)')
    args = parser.parse_args()
    if args.test and args.out:
        parser.error('--test and --out cannot be combined')
    missing = [p for p in PREBUILT if not (ROOT / p).is_file()]
    if missing:
        sys.exit(f'missing: {", ".join(missing)} (run tools/build_xlate.py and tools/build_hactool.py)')
    if not (ROOT / 'launcher' / 'pins.json').is_file():
        sys.exit('launcher/pins.json is missing: run tools/launcher_pins.py')
    test_build = args.test or args.out is not None
    if args.out:
        output_dir = args.out.resolve()
        build_dir = output_dir / '.build'
    else:
        build_dir = ROOT / 'build' / 'launcher-test' if test_build else DIST
        output_dir = ROOT / 'launcher' / 'bin' / 'preview2' if test_build else DIST / 'publish'
    build_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    payload = build_dir / 'payload.zip'
    files = repo_files() + PREBUILT
    with zipfile.ZipFile(payload, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in files:
            z.write(ROOT / n, n)
    print(f'{payload}: {len(files)} files, {payload.stat().st_size / 2**20:.1f} MiB')
    command = ['dotnet', 'publish', str(ROOT / 'launcher' / 'KarPurLauncher.csproj'), '-c', 'Release']
    if args.out:
        command.extend(['--artifacts-path', str(build_dir / 'artifacts')])
    command.extend([f'-p:PayloadZip={payload}', f'-p:OutputPath={build_dir / "launcher-build"}',
                    '-o', str(output_dir)])
    r = subprocess.run(command, cwd=ROOT)
    if r.returncode:
        sys.exit('dotnet publish failed')
    exe = output_dir / 'KarPur-Launcher.exe'
    target = exe if test_build else DIST / 'KarPur-Launcher.exe'
    if target != exe:
        target.write_bytes(exe.read_bytes())
    print(f'{target}: {target.stat().st_size / 2**20:.1f} MiB')
    manifest = output_dir / 'recomplauncher.json' if test_build else DIST / 'recomplauncher.json'
    manifest.write_bytes((ROOT / 'recomplauncher.json').read_bytes())
    print(f'{manifest}: RecompLauncher manifest')
    windows_asset = target.with_name('KarPur-Launcher-Windows-x86_x64.exe')
    shutil.copyfile(target, windows_asset)
    with windows_asset.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    checksum = windows_asset.with_name(windows_asset.name + '.sha256')
    checksum.write_text(f'{digest}  {windows_asset.name}\n', encoding='ascii')
    print(f'{windows_asset}: Windows x86-64 launcher')
    print(f'{checksum}: SHA-256')
    if not test_build:
        project = (ROOT / 'launcher' / 'KarPurLauncher.csproj').read_text(encoding='utf-8')
        version = re.search(r'<Version>([^<]+)</Version>', project)
        if not version:
            sys.exit('launcher/KarPurLauncher.csproj has no <Version>')
        release = DIST / f'PokiNative-{version.group(1)}-Windows-x64.zip'
        with zipfile.ZipFile(release, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            archive.write(target, 'KarPur-Launcher.exe')
            archive.write(manifest, 'recomplauncher.json')
        print(f'{release}: Windows release package with RecompLauncher manifest')


if __name__ == '__main__':
    main()
