"""Build the separate Linux launcher and its Type 2 AppImage on Linux x86-64.

Needs .NET 10 SDK, Python, cmake, ninja, clang, make, git and squashfs-tools.
Uses the local source snapshot manifest when this directory is outside a Git checkout.
No game content is packaged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / 'dist' / 'linux'
CACHE = ROOT / 'toolchain' / 'appimage-downloads'
PYTHON_URL = ('https://github.com/astral-sh/python-build-standalone/releases/download/20261110/'
              'cpython-3.14.10%2B20261110-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz')
PYTHON_SHA = 'c2ff2e97422f5d3896a16e0317fd212ba977bbf2688d93a6f7a03d817506bd30'
SDL_URL = 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz'
SDL_SHA = 'f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f'
VULKAN_URL = 'https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.335'
VULKAN_SHA = '8ee39bee575bdccc1ecacd0bdb26f181841de35d7409a3eca468c6b622daa2f1'
RUNTIME_URL = 'https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64'
RUNTIME_SHA = '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d'
ZIG_URL = 'https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz'
ZIG_SHA = '02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239'


def run(*args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def fetch(url, digest, filename):
    CACHE.mkdir(parents=True, exist_ok=True)
    target = CACHE / filename
    if target.is_file():
        with target.open('rb') as f:
            if hashlib.file_digest(f, 'sha256').hexdigest() == digest:
                return target
    temp = target.with_suffix('.download')
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'KarPur-AppImage/0.3.8.1'}), timeout=90) as response, temp.open('wb') as f:
            shutil.copyfileobj(response, f)
        with temp.open('rb') as f:
            if hashlib.file_digest(f, 'sha256').hexdigest() != digest:
                raise RuntimeError(f'Checksum mismatch for {filename}')
        temp.replace(target)
    finally:
        temp.unlink(missing_ok=True)
    return target


def sources():
    manifest = ROOT / 'build' / 'source-files.json'
    if manifest.is_file():
        names = json.loads(manifest.read_text(encoding='utf-8-sig'))
    else:
        r = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT,
                           capture_output=True, check=True)
        names = r.stdout.decode().split('\0')
    names += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'launcher-linux').glob('*.cs')]
    names += ['launcher-linux/KarPurLauncher.Linux.csproj', 'tools/make_appimage.py', 'docs/launcher-linux.md']
    safe = []
    forbidden = {'.xci', '.nsp', '.nca', '.keys', '.nso', '.npdm', '.trpfs', '.trpak', '.bnsh', '.bntx', '.exe', '.dll'}
    for name in sorted(set(names)):
        path = Path(name)
        if not name or path.is_absolute() or '..' in path.parts:
            continue
        if path.parts[0] not in ('tools', 'recomp', 'runtime', 'docs', 'launcher-linux') and name not in ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'config.example.toml'):
            continue
        if any(p in ('bin', 'obj', '__pycache__') for p in path.parts) or path.suffix.lower() in forbidden:
            continue
        if (ROOT / path).is_file():
            safe.append(path)
    return safe


def add_file(z, source, name):
    item = zipfile.ZipInfo(str(name).replace('\\', '/'))
    item.external_attr = (0o100755 if os.access(source, os.X_OK) else 0o100644) << 16
    item.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(item, source.read_bytes())


def prepare_portable():
    def copy_missing(source, target):
        if not Path(target).is_file():
            shutil.copy2(source, target)
        return target

    zig = ROOT / 'toolchain' / 'zig'
    marker = zig / '.karpur-ready'
    if not marker.is_file() or marker.read_text(encoding='ascii').strip() != ZIG_SHA or not (zig / 'zig').is_file():
        zig.mkdir(parents=True, exist_ok=True)
        run('tar', '-xJf', fetch(ZIG_URL, ZIG_SHA, 'zig-0.15.2.tar.xz'), '-C', zig, '--strip-components=1')
        if not (zig / 'zig').is_file() or not (zig / 'LICENSE').is_file():
            raise RuntimeError('Zig bundle is incomplete')
        marker.write_text(ZIG_SHA, encoding='ascii')
    deps = ROOT / 'toolchain' / 'deps-linux'
    include = deps / 'include'
    lib = deps / 'lib'
    for name in ('SDL3', 'vulkan', 'vk_video'):
        shutil.copytree(ROOT / 'toolchain' / 'linux' / 'include' / name, include / name,
                        dirs_exist_ok=True, copy_function=copy_missing)
    for name in ('opus', 'vpx'):
        shutil.copytree(Path('/usr/include') / name, include / name, dirs_exist_ok=True, copy_function=copy_missing)
    vma_url, vma_sha = ('https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.4.0/include/vk_mem_alloc.h',
                        '8487b7995ad3b263eb73bc5b9a77d71aa69b6bef5d58a715c02d2663afd81f1a')
    (include / 'vma').mkdir(parents=True, exist_ok=True)
    copy_missing(fetch(vma_url, vma_sha, 'vk_mem_alloc.h'), include / 'vma' / 'vk_mem_alloc.h')
    for name in ('zstd.h', 'zdict.h'):
        copy_missing(Path('/usr/include') / name, include / name)
    lib.mkdir(parents=True, exist_ok=True)
    for name in ('libopus.a', 'libvpx.a', 'libzstd.a', 'libvulkan.so'):
        copy_missing(Path('/usr/lib/x86_64-linux-gnu') / name, lib / name)
    copy_missing('/usr/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a', lib / 'libclang_rt.builtins-x86_64.a')
    for name in ('libSDL3.so', 'libSDL3.so.0'):
        copy_missing(ROOT / 'toolchain' / 'linux' / 'lib' / name, lib / name)
    ninja = ROOT / 'toolchain' / 'portable' / 'bin' / 'ninja'
    ninja.parent.mkdir(parents=True, exist_ok=True)
    copy_missing('/usr/bin/ninja', ninja)
    license_dir = deps / 'licenses'
    license_dir.mkdir(parents=True, exist_ok=True)
    for package in ('libopus-dev', 'libvpx-dev', 'libzstd-dev', 'libvulkan-dev', 'ninja-build', 'libclang-rt-18-dev'):
        copy_missing(Path('/usr/share/doc') / package / 'copyright', license_dir / (package + '-copyright'))
    for name in ('clang', 'llvm-ar'):
        wrapper = ninja.parent / name
        if not wrapper.is_file():
            raise RuntimeError(f'Portable compiler wrapper missing: {wrapper}')
        wrapper.chmod(0o755)


def prepare():
    python = ROOT / 'toolchain' / 'python' / 'bin' / 'python3'
    runtime_marker = ROOT / 'toolchain' / 'python' / '.karpur-runtime'
    if not runtime_marker.is_file():
        with tarfile.open(fetch(PYTHON_URL, PYTHON_SHA, 'python-linux.tar.gz')) as tar:
            # Terminal aliases are unused by the launcher and can form long link chains on a WSL mounted drive.
            tar.extractall(ROOT / 'toolchain', members=(m for m in tar if '/share/terminfo/' not in m.name), filter='data')
        runtime_marker.write_text(PYTHON_SHA)
    marker = python.parent.parent / '.karpur-packages'
    packages = ['cryptography==46.0.3', 'lz4==4.4.5', 'numpy==2.3.5', 'cffi==2.1.1', 'pycparser==3.12']
    if not marker.is_file() or marker.read_text() != '\n'.join(packages):
        run(python, '-m', 'pip', 'install', '--disable-pip-version-check', *packages)
        marker.write_text('\n'.join(packages))
    prefix = ROOT / 'toolchain' / 'linux'
    if not (prefix / 'include' / 'vulkan' / 'vulkan.h').is_file():
        source = ROOT / 'toolchain' / 'vulkan-headers-src'
        with tarfile.open(fetch(VULKAN_URL, VULKAN_SHA, 'vulkan-headers.tar.gz')) as tar:
            tar.extractall(source, filter='data')
        include = source / 'Vulkan-Headers-1.4.335' / 'include'
        shutil.copytree(include, prefix / 'include', dirs_exist_ok=True)
        shutil.copy2(source / 'Vulkan-Headers-1.4.335' / 'LICENSE.md', prefix / 'Vulkan-Headers-LICENSE.md')
        shutil.copytree(source / 'Vulkan-Headers-1.4.335' / 'LICENSES', prefix / 'share' / 'licenses' / 'Vulkan-Headers', dirs_exist_ok=True)
    if not (prefix / 'include' / 'SDL3' / 'SDL.h').is_file():
        src = ROOT / 'toolchain' / 'sdl-src'
        with tarfile.open(fetch(SDL_URL, SDL_SHA, 'sdl3.tar.gz')) as tar:
            tar.extractall(src, filter='data')
        run('cmake', '-S', src / 'SDL3-3.4.20', '-B', src / 'build', '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}', '-DCMAKE_INSTALL_LIBDIR=lib',
            '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TESTS=OFF')
        run('cmake', '--build', src / 'build', '--parallel', '8')
        run('cmake', '--install', src / 'build')
    prepare_portable()
    hactool = ROOT / 'toolchain' / 'hactool' / 'hactool'
    if not hactool.is_file():
        run(python, 'tools/build_hactool.py')
    xlate = ROOT / 'build' / 'xlate' / 'totk_xlate'
    if not xlate.is_file():
        run(python, 'tools/build_xlate.py')
    return python


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepare-only', action='store_true')
    a = p.parse_args()
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'amd64'):
        p.error('build the AppImage on Linux x86-64 (WSL2 is supported)')
    python = prepare()
    if a.prepare_only:
        return
    DIST.mkdir(parents=True, exist_ok=True)
    appdir = DIST / 'KarPur.AppDir'
    if appdir.exists():
        if not appdir.resolve().is_relative_to(DIST.resolve()):
            raise RuntimeError('AppDir resolves outside the packaging directory')
        shutil.rmtree(appdir)
    publish = appdir / 'usr' / 'lib' / 'karpur'
    publish.mkdir(parents=True, exist_ok=True)
    run('dotnet', 'publish', ROOT / 'launcher-linux' / 'KarPurLauncher.Linux.csproj', '-r', 'linux-x64',
        '-c', 'Release', '--self-contained', 'true', '-o', publish)
    shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', publish / 'THIRD_PARTY_NOTICES.md')
    shutil.copy2(ROOT / 'LICENSE', publish / 'KarPur-LICENSE')
    avalonia_license = fetch('https://raw.githubusercontent.com/AvaloniaUI/Avalonia/11.3.23/licence.md',
                             '213814d306090074d234d760239ff0f67eb9b8d20eefb4d5631bb39dbe0b769b', 'Avalonia-LICENSE')
    shutil.copy2(avalonia_license, publish / 'Avalonia-LICENSE')
    archive = publish / 'payload.zip'
    names = sources()
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in names:
            add_file(z, ROOT / name, name)
        for tree in ('toolchain/python', 'toolchain/linux', 'toolchain/zig', 'toolchain/portable', 'toolchain/deps-linux'):
            for file in sorted((ROOT / tree).rglob('*')):
                if 'terminfo' in file.parts:
                    continue
                if file.is_file() and '__pycache__' not in file.parts and not file.name.endswith('.pyc'):
                    add_file(z, file, file.relative_to(ROOT))
        for name in ('toolchain/hactool/hactool', 'toolchain/hactool/LICENSE', 'build/xlate/totk_xlate', 'build/xlate/totk_xlate.id'):
            add_file(z, ROOT / name, name)
        add_file(z, ROOT / 'toolchain' / 'sdl-src' / 'SDL3-3.4.20' / 'LICENSE.txt', 'toolchain/linux/SDL3-LICENSE.txt')
    (publish / 'START-HERE.txt').write_text(
        'KarPur Linux: Starte ./KarPur-Launcher-Linux aus diesem vollständig entpackten Ordner.\n'
        'payload.zip muss direkt neben der Programmdatei liegen. Einzelne Dateien reichen nicht.\n'
        'Alternativ die separate .AppImage-Datei direkt starten.\n', encoding='utf-8')
    run(publish / 'KarPur-Launcher-Linux', '--self-check', '--require-payload')
    folder_archive = DIST / 'KarPur-Launcher-Linux-x86_64.tar.gz'
    with tarfile.open(folder_archive, 'w:gz') as tar:
        tar.add(publish, arcname='linux-x64')
    with folder_archive.open('rb') as f:
        folder_digest = hashlib.file_digest(f, 'sha256').hexdigest()
    folder_archive.with_name(folder_archive.name + '.sha256').write_text(f'{folder_digest}  {folder_archive.name}\n', encoding='ascii')
    (appdir / 'AppRun').write_text('#!/bin/sh\nAPPDIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\nexport APPDIR\nexec "$APPDIR/usr/lib/karpur/KarPur-Launcher-Linux" "$@"\n', encoding='utf-8')
    (appdir / 'AppRun').chmod(0o755)
    (appdir / 'karpur.desktop').write_text('[Desktop Entry]\nType=Application\nName=KarPur Linux Launcher\nComment=Play KarPur on Linux\nExec=AppRun\nIcon=karpur\nTerminal=false\nCategories=Game;\n', encoding='utf-8')
    shutil.copy2(ROOT / 'launcher' / 'assets' / 'icon-neutral.png', appdir / 'karpur.png')
    shutil.copy2(appdir / 'karpur.png', appdir / '.DirIcon')
    runtime = fetch(RUNTIME_URL, RUNTIME_SHA, 'runtime-x86_64')
    license_file = fetch('https://raw.githubusercontent.com/AppImage/type2-runtime/20251108/LICENSE',
                         'aa154fc9070614bbe7921f89db11efd1dba7a1f3a41685958110e2230f9c0ca1', 'AppImage-runtime-LICENSE')
    shutil.copy2(license_file, publish / 'AppImage-runtime-LICENSE')
    squash = DIST / 'karpur.squashfs'
    run('mksquashfs', appdir, squash, '-noappend', '-all-root', '-comp', 'gzip', '-no-xattrs', '-quiet')
    image = DIST / 'KarPur-Launcher-Linux-x86_64.AppImage'
    with image.open('wb') as f, runtime.open('rb') as stub, squash.open('rb') as filesystem:
        shutil.copyfileobj(stub, f)
        shutil.copyfileobj(filesystem, f)
    image.chmod(0o755)
    run(image, '--appimage-extract-and-run', '--self-check', '--require-payload')
    with image.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    image.with_suffix('.AppImage.sha256').write_text(f'{digest}  {image.name}\n', encoding='ascii')
    deck_image = DIST / 'KarPur-Linux-Launcher-0.3.8.AppImage'
    shutil.copy2(image, deck_image)
    deck_image.with_suffix('.AppImage.sha256').write_text(f'{digest}  {deck_image.name}\n', encoding='ascii')
    print(f'AppImage built: {image} ({image.stat().st_size / 2**20:.1f} MiB)')
    print(f'Steam Deck launcher: {deck_image} ({deck_image.stat().st_size / 2**20:.1f} MiB)')
    print(f'Complete Linux folder built: {folder_archive} ({folder_archive.stat().st_size / 2**20:.1f} MiB)')
    print(f'Payload contains {len(names)} source files and the Linux tool dependencies.')


if __name__ == '__main__':
    main()
