"""Milestone M2: lift the game's code to C and compile every file, then print a report.

    .venv\\Scripts\\python tools\\lift.py

1. recomp/gen.py turns main, subsdk0 and sdk from game/extracted/update/exefs (written by tools/extract.py) into C
   under generated/.
2. Every generated file is compiled with MSYS2's clang into build/lift/obj, through Ninja, all CPU cores in parallel.
   Nothing is linked yet: that needs the runtime (milestone M3).
3. The report counts functions, instructions, jump tables, unsupported instructions and compile errors. It names
   instruction kinds and files, never code; compiler messages are printed without source lines.

Takes a while: expect 10-40 minutes and about 5 GB of disk for generated C and objects.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIN = os.name == 'nt'
# MSYS2's UCRT64 tree; KARPUR_UCRT names another one (tools/setup_build.py unpacks the packages into the install
# folder instead of installing MSYS2)
MSYS_BIN = Path(os.environ.get('KARPUR_UCRT') or r'C:\msys64\ucrt64') / 'bin'
MODULES = ('main', 'subsdk0', 'sdk')

# Flags for generated code, as TotK Recomp and LAS Recomp build it.
GEN_FLAGS = ['-O1', '-fno-strict-aliasing', '-Wno-return-type', '-Wno-unused-label', '-fvisibility=hidden',
             '-march=x86-64-v3']
# Error messages without the offending source line, which would be lifted game code.
QUIET_FLAGS = ['-fno-caret-diagnostics', '-fno-diagnostics-show-option', '-w']


def find_tool(name, given):
    if given:
        return Path(given)
    if WIN and (MSYS_BIN / f'{name}.exe').is_file():
        return MSYS_BIN / f'{name}.exe'
    found = shutil.which(name)
    if not found:
        # A launcher build has its own toolchain and never asks the player to install one: that it is incomplete
        # here means the toolchain step was stopped part way through (issue #62).
        if os.environ.get('KARPUR_UCRT'):
            sys.exit(f'error: {name} is missing from the toolchain. Press Build again: the launcher downloads the '
                     'packages the stopped run did not get.')
        sys.exit(f'error: {name} not found. Install it with MSYS2 (docs/setup-windows.md, step 8).')
    return Path(found)


def ninja_path(p):
    """A path for a build statement in a Ninja file: relative to the repository, with Ninja's specials escaped."""
    rel = Path(os.path.relpath(p, ROOT)).as_posix()
    return rel.replace('$', '$$').replace(' ', '$ ').replace(':', '$:')


def generate(exefs, out, modules, patches=None):
    cmd = [sys.executable, str(ROOT / 'recomp' / 'gen.py'), '--exefs', str(exefs), '--out', str(out),
           '--modules', ','.join(modules)] + (['--patches', str(patches)] if patches else [])
    print('Lifting (recomp/gen.py) ...', flush=True)
    t = time.perf_counter()
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode:
        sys.exit(f'error: recomp/gen.py failed with exit code {r.returncode}')
    return time.perf_counter() - t


def compile_all(clang, ninja, gen, build, modules):
    sources = [p for m in modules for p in sorted((gen / m).glob('*.c'))]
    build.mkdir(parents=True, exist_ok=True)
    flags = ' '.join(GEN_FLAGS + QUIET_FLAGS + ['-I', ninja_path(ROOT / 'runtime'), '-I', ninja_path(gen)])
    lines = ['# Written by tools/lift.py. Compiles the generated C; links nothing.',
             f'builddir = {ninja_path(build)}',
             f'cc = "{clang}"' if ' ' in str(clang) else f'cc = {clang}',
             f'flags = {flags}', '',
             'rule cc', '  command = $cc $flags -c $in -o $out', '  description = $in', '']
    objs = []
    for src in sources:
        obj = build / 'obj' / src.parent.name / (src.stem + '.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        objs.append(obj)
        lines.append(f'build {ninja_path(obj)}: cc {ninja_path(src)}')
    ninja_file = build / 'build.ninja'
    ninja_file.write_text('\n'.join(lines) + '\n', encoding='utf-8')

    print(f'Compiling {len(sources)} files with {clang.name} ...', flush=True)
    t = time.perf_counter()
    env = dict(os.environ)
    if WIN:   # clang's own DLLs and the assembler/linker it may call live next to it
        env['PATH'] = str(clang.parent) + os.pathsep + env.get('PATH', '')
    proc = subprocess.Popen([str(ninja), '-f', ninja_path(ninja_file), '-k', '0'], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                            errors='replace')
    log, done = [], 0
    for line in proc.stdout:
        log.append(line)
        m = re.match(r'\[(\d+)/(\d+)\]', line)
        if m:
            done = int(m.group(1))
            if done % 25 == 0 or done == int(m.group(2)):
                print(f'  {done}/{m.group(2)} files, {time.perf_counter() - t:.0f}s', flush=True)
    proc.wait()
    (build / 'compile.log').write_text(''.join(log), encoding='utf-8')
    ok = sum(1 for o in objs if o.is_file())
    errors = [line.strip() for line in log if ': error:' in line]
    return len(sources), ok, errors, time.perf_counter() - t


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exefs', type=Path, default=ROOT / 'game' / 'extracted' / 'update' / 'exefs')
    ap.add_argument('--gen', type=Path, default=ROOT / 'generated', help='where the C goes')
    ap.add_argument('--build', type=Path, default=ROOT / 'build' / 'lift', help='where the objects go')
    ap.add_argument('--modules', default=','.join(MODULES))
    ap.add_argument('--clang', help='clang executable (default: MSYS2 UCRT64, else PATH)')
    ap.add_argument('--ninja', help='ninja executable (default: MSYS2 UCRT64, else PATH)')
    ap.add_argument('--skip-gen', action='store_true', help='compile what generated/ already holds')
    ap.add_argument('--patches', type=Path, help='code patch table for gen.py (recomp/patches.py), e.g. the one '
                    'tools/consts.py --patch-file writes; default build/fps_patches.txt when it exists')
    ap.add_argument('--gen-only', action='store_true',
                    help='only lift (tools/build_game.py compiles what changed and links)')
    args = ap.parse_args()

    modules = args.modules.split(',')
    if not (args.exefs / 'main').is_file():
        sys.exit(f'error: {args.exefs} has no main: run tools\\extract.py first')
    clang, ninja = find_tool('clang', args.clang), find_tool('ninja', args.ninja)
    sys.path.insert(0, str(ROOT / 'tools'))
    from cpu_march import march   # the CPU level of this PC (tools/cpu_march.py)
    level = march(clang)
    GEN_FLAGS[:] = [f'-march={level}' if f.startswith('-march=') else f for f in GEN_FLAGS]
    free = shutil.disk_usage(ROOT).free
    if free < 8 * 2**30:
        sys.exit(f'error: only {free / 2**30:.1f} GiB free on the drive of {ROOT}; lifting needs about 8 GiB')

    default = ROOT / 'build' / 'fps_patches.txt'   # tools/fps_patches.py: Violet's frame-rate set
    if args.patches is None and default.is_file():
        args.patches = default
        print(f'patch table: {default} (tools/fps_patches.py)')
    if args.patches and not args.patches.is_file():
        sys.exit(f'error: patch table {args.patches} not found')
    gen_s = 0.0 if args.skip_gen else generate(args.exefs, args.gen, modules, args.patches)
    if args.gen_only:
        print(f'lifted in {gen_s:.0f} s; next: tools\\build_game.py')
        return
    total, ok, errors, cc_s = compile_all(clang, ninja, args.gen, args.build, modules)

    lines = ['=' * 27 + ' KarPur M2 report ' + '=' * 27]
    census = Counter()
    lines.append(f'  {"module":<8} {"functions":>9} {"instructions":>12} {"C MB":>6} {"jump tbl":>8} '
                 f'{"fallback":>8} {"br disp":>7} {"traps":>6} {"unsupp":>6}')
    for m in modules:
        man = json.loads((args.gen / m / 'manifest.json').read_text())
        s = man['stats']
        census.update(man.get('unsupported_census', {}))
        lines.append(f'  {m:<8} {s["emitted_functions"]:>9} {s["instructions"]:>12} {s["c_bytes"] / 1e6:>6.0f} '
                     f'{s["jump_tables"]:>8} {s["all_label_fallbacks"]:>8} {s["br_dispatch"]:>7} {s["traps"]:>6} '
                     f'{s["unsupported"]:>6}')
    lines.append(f'Unsupported instruction kinds ({sum(census.values())} sites):')
    for kind, n in census.most_common(30):
        lines.append(f'  {n:6}  {kind}')
    if not census:
        lines.append('  none')
    lines.append(f'Compiled: {ok} of {total} files ({len(errors)} errors)')
    for e in errors[:20]:
        lines.append(f'  {Path(e.split(":")[0]).name}: {e.split(": error:", 1)[1].strip()}' if ': error:' in e else f'  {e}')
    lines.append(f'Time: lifting {gen_s / 60:.1f} min, compiling {cc_s / 60:.1f} min ({os.cpu_count()} CPU threads)')
    lines.append('=' * 72)
    report = '\n'.join(lines)
    print('\n' + report)
    (ROOT / 'game' / 'report').mkdir(parents=True, exist_ok=True)
    (ROOT / 'game' / 'report' / 'm2-report.txt').write_text(report + '\n', encoding='utf-8')
    print(f'\nSaved: {ROOT / "game" / "report" / "m2-report.txt"}')
    return 0 if ok == total else 1


if __name__ == '__main__':
    sys.exit(main())
