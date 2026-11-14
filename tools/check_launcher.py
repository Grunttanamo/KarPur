"""Check what a launcher build depends on, before a release, without building anything.

The launcher is how the game is played, so a mistake on that path reaches every player at once and usually cannot be
undone from inside the launcher. The checks here are the ones that have actually gone wrong (docs/launcher-checks.md
has the story of each):

  settings   every key the runtime reads with cfg_value / cfg_int / cfg_double / cfg_bool has a row in the settings
             table of runtime/cfg.c. Without one those four kill the game the moment the environment variable is
             unset - which is every launcher start, and never a command-line test.
  keys       every key the launcher writes to karpur.ini, and every row of the table, is in cfg.c's `known` list.
  tools      every script the build runs exists, and imports with Python's isolated mode, which is how a build
             started by the launcher may run: a script that imports a neighbour without saying where it lives
             compiles for an hour and then stops.
  toolchain  the executables the build calls are the ones toolchain_msys2.TOOLS checks for, so an interrupted
             toolchain download is noticed instead of being remembered as complete.
  payload    the files a built game needs are ones tools/make_launcher.py puts into the release.

    .venv\\Scripts\\python tools\\check_launcher.py

It prints one line per check and exits non-zero if any failed. Reads this repository only, no game data.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNTIME = ROOT / 'runtime'
LAUNCHER = ROOT / 'launcher'

# What the runtime's four strict readers are called with, and the three tables of runtime/cfg.c.
STRICT = re.compile(r'\bcfg_(?:value|int|double|bool)\("TOTK_([A-Z_0-9]+)"\)')
META_ROW = re.compile(r'\{"([A-Z_0-9]+)",\s*"[^"]*",\s*(?:"[^"]*"|NULL),\s*(?:LIVE|RESTART)\}')
SECTION_ROW = re.compile(r'\{"([A-Z_0-9]+)",\s*"(display|language|audio|input|performance|diagnostics|compatibility|mods)"\}')
INI_SET = re.compile(r'ini\.Set\("([a-z]+)",\s*"([a-z_0-9]+)"')
# A path under the toolchain's bin: how the build names an executable it calls.
UCRT_EXE = re.compile(r"UCRT\s*/\s*'([A-Za-z0-9._+-]+\.exe)'")
# Named in the build but never by a launcher build: llvm-ar serves --make-lib and llvm-windres --toolchain, both
# developer options. MSYS2's clang package does not even ship llvm-ar, so demanding it would fail a sound toolchain.


def cfg_tables():
    text = (RUNTIME / 'cfg.c').read_text(encoding='utf-8')
    known = re.search(r'static const char \*const known\[\] = \{(.*?)\n\};', text, re.S)
    names = set(re.findall(r'"([A-Z_0-9]+)"', known.group(1))) if known else set()
    metas = re.search(r'static const CfgMeta metas\[\] = \{(.*?)\n\};', text, re.S)
    sections = re.search(r'sections\[\] = \{(.*?)\n\};', text, re.S)
    return (names,
            set(META_ROW.findall(metas.group(1))) if metas else set(),
            dict(SECTION_ROW.findall(sections.group(1))) if sections else {})


def strict_reads():
    """{key: [files]} for every key read with one of the four readers that need a row."""
    out = {}
    for p in sorted(RUNTIME.rglob('*.c')):
        for key in STRICT.findall(p.read_text(encoding='utf-8', errors='replace')):
            out.setdefault(key, []).append(p.name)
    return out


def launcher_keys():
    """{key: section} for every key the launcher writes to karpur.ini."""
    out = {}
    for p in sorted(LAUNCHER.glob('*.cs')):
        for section, key in INI_SET.findall(p.read_text(encoding='utf-8', errors='replace')):
            out[key] = section
    return out


def build_tools():
    """The scripts tools/setup_build.py runs, by path."""
    text = (ROOT / 'tools' / 'setup_build.py').read_text(encoding='utf-8')
    return sorted(set(re.findall(r"'(tools/[a-z_0-9]+\.py)'", text)))


def build_exes():
    """The executables under the toolchain's bin that the build names."""
    out = set()
    for name in ('build_game.py', 'lift.py', 'build_shaders.py'):
        p = ROOT / 'tools' / name
        if not p.is_file():
            continue
        text = p.read_text(encoding='utf-8')
        out |= set(UCRT_EXE.findall(text))
        out |= {f'{t}.exe' for t in re.findall(r"find_tool\('([a-z.+-]+)'", text)}
    return out - {'llvm-ar.exe', 'llvm-windres.exe'}


def left_out(paths):
    """The paths of the list that tools/make_launcher.py would not put into payload.zip.

    It packs what git tracks or would track, except everything under launcher/ and .github/ - the launcher's own
    files are compiled or embedded into the executable instead (pins.json, the banners), so they belong there.
    """
    out = [p for p in paths if p.startswith(('launcher/', '.github/'))]
    if paths:
        r = subprocess.run(['git', 'check-ignore', '--stdin'], cwd=ROOT, input='\n'.join(paths),
                           capture_output=True, text=True)
        out += [p for p in r.stdout.splitlines() if p and p not in out]
    return out


def main():
    bad = []

    def check(name, problems, detail=''):
        if problems:
            bad.append(name)
            print(f'FAIL {name}: {problems if isinstance(problems, str) else ", ".join(problems)}')
            if detail:
                print(f'     {detail}')
        else:
            print(f'ok   {name}{" " + detail if detail else ""}')

    known, metas, sections = cfg_tables()
    if not known or not metas:
        sys.exit('error: could not read the tables of runtime/cfg.c (did their shape change?)')

    reads = strict_reads()
    no_row = sorted(f'{k} ({reads[k][0]})' for k in reads if k not in metas)
    check('settings', no_row, f'{len(reads)} keys read strictly, {len(metas)} rows in the table')

    unknown = sorted((metas | set(sections)) - known)
    keys = launcher_keys()
    # bind_<button> is written per button from one place; cfg.c lists the buttons themselves.
    written = {k.upper() for k in keys if k != 'bind_'}
    check('keys', unknown + sorted(k for k in written if k not in known),
          f'{len(written)} keys written by the launcher, {len(known)} known to the runtime')

    missing_tools = [t for t in build_tools() if not (ROOT / t).is_file()]
    isolated = []
    for t in build_tools():
        if (ROOT / t).is_file():
            # -I is isolated mode: no script directory on sys.path, as PYTHONSAFEPATH and -P also leave it off.
            r = subprocess.run([sys.executable, '-I', '-c', f'import importlib.util as u, sys; '
                                f'spec = u.spec_from_file_location("m", r"{ROOT / t}"); '
                                'm = u.module_from_spec(spec); spec.loader.exec_module(m)'],
                               cwd=ROOT, capture_output=True, text=True)
            if r.returncode:
                last = [l for l in r.stderr.strip().splitlines() if l.strip()]
                isolated.append(f'{t}: {last[-1] if last else "failed"}')
    check('tools', missing_tools + isolated, f'{len(build_tools())} scripts the build runs')

    sys.path.insert(0, str(ROOT / 'tools'))
    import toolchain_msys2
    exes = build_exes()
    check('toolchain', sorted(exes - set(toolchain_msys2.TOOLS)),
          f'{len(exes)} executables called, {len(toolchain_msys2.TOOLS)} checked after unpacking')

    need = build_tools() + ['tools/data', 'tools/vendor', 'tools/requirements.txt', 'recomp/patches.py',
                            'recomp/gen.py', 'runtime/cfg.c', 'docs/launcher-checks.md']
    check('payload', left_out(need), f'{len(need)} files the launcher needs on the player\'s PC')

    # Build properties that are wrong for a WinForms program the player types into.
    proj = (LAUNCHER / 'KarPurLauncher.csproj').read_text(encoding='utf-8')
    wrong = []
    if re.search(r'<InvariantGlobalization>\s*true\s*</InvariantGlobalization>', proj):
        wrong.append('InvariantGlobalization is true: switching keyboard layout over a text box throws')
    if not re.search(r'<SelfContained>\s*true\s*</SelfContained>', proj):
        wrong.append('SelfContained is not true: the player would need the .NET runtime installed')
    check('project', wrong, 'the build properties of the launcher')

    print()
    if bad:
        sys.exit(f'{len(bad)} check(s) failed: {", ".join(bad)}')
    print('every check passed')


if __name__ == '__main__':
    main()
