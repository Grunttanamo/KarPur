"""Builds the game in this folder from the player's own dumps in one run: what the launcher (launcher/) starts.

The steps are those of docs/setup-windows.md, without installing anything: the C toolchain is MSYS2's UCRT64 packages
unpacked into toolchain/msys2 (tools/toolchain_msys2.py), and the shader translator and hactool come prebuilt with the
release (build/xlate, toolchain/hactool). Each finished step is noted in build/setup_state.json, so a run that stopped
goes on where it stopped; changed inputs start it over.

Output for the launcher, one line each: "STEP <n> <total> <key>" when a step begins, "PERCENT <0-100>" within a step,
"PLAN <step,step,...>" first (only the steps this run needs: a
second run redoes what is missing or changed, which is the launcher's Repair and update), "LOG <text>" for everything the tools print (also in build/setup.log), "DONE <path of karpur.exe>" at the end, or
"ERROR <step> <text>" and exit code 1. No key values are ever printed.

  python tools/setup_build.py --base GAME.xci --update UPDATE.nsp [--dlc DLC.nsp] --keys prod.keys
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import toolchain_msys2  # noqa: E402

STATE = ROOT / 'build' / 'setup_state.json'
LOG = ROOT / 'build' / 'setup.log'
EXE = ROOT / 'build' / 'game' / 'karpur.exe'
HACTOOL = ROOT / 'toolchain' / 'hactool' / 'hactool.exe'
XLATE = ROOT / 'build' / 'xlate' / 'totk_xlate.exe'
MSYS = ROOT / 'toolchain' / 'msys2'
FREE_GB = 30   # docs/setup-windows.md: about 30 GB during the setup

STEPS = ['check', 'toolchain', 'extract', 'fonts', 'romfs', 'dlc', 'menu', 'fps', 'icon', 'lift', 'build', 'selftest', 'shaders',
         'cleanup']
UI_LIST = ROOT / 'tools' / 'data' / 'ui_patch_violet_4.0.0.txt'


def out(line):
    print(line, flush=True)


class StepError(Exception):
    pass


def magic(path, offset, want):
    try:
        with open(path, 'rb') as f:
            f.seek(offset)
            return f.read(len(want)) == want
    except OSError:
        return False


def check(a):
    """The inputs are what they claim to be; nothing is read from them beyond a few header bytes."""
    if not a.base.is_file():
        raise StepError(f'base game not found: {a.base}')
    is_xci = a.base.suffix.lower() == '.xci' and magic(a.base, 0x100, b'HEAD')
    is_nsp = a.base.suffix.lower() == '.nsp' and magic(a.base, 0, b'PFS0')
    if a.firmware and not a.firmware.is_dir():
        raise StepError(f'firmware folder not found: {a.firmware}')
    if not is_xci and not (is_nsp and a.firmware):
        raise StepError('the base game must be an XCI dump (its system fonts come from the cartridge image); a base game '
                        'as NSP needs a firmware folder too (the launcher\'s "Firmware folder": the folder of your '
                        'console\'s system NCA files, e.g. an emulator\'s nand/system/Contents/registered)')
    for kind, p in [('update', a.update)] + [('dlc', d) for d in a.dlc]:
        if not p.is_file():
            raise StepError(f'{kind} not found: {p}')
        if p.suffix.lower() != '.nsp' or not magic(p, 0, b'PFS0'):
            raise StepError(f'the {kind} must be an NSP file: {p.name}')
    if not a.keys.is_file():
        raise StepError(f'prod.keys not found: {a.keys}')
    text = a.keys.read_text(encoding='utf-8', errors='replace')
    if not re.search(r'^\s*header_key\s*=', text, re.M):
        raise StepError(f'{a.keys.name} has no header_key: is it prod.keys from your console?')
    free = shutil.disk_usage(ROOT).free / 2**30
    if free < FREE_GB:
        raise StepError(f'only {free:.0f} GB free on the target drive, about {FREE_GB} GB are needed')
    for p, what in [(HACTOOL, 'hactool'), (XLATE, 'the shader translator')]:
        if not p.is_file():
            raise StepError(f'{what} is missing ({p}): the release folder is incomplete')
    q = lambda p: "'" + str(p.resolve()) + "'"
    dlc = ', '.join(q(d) for d in a.dlc)
    (ROOT / 'config.local.toml').write_text(
        f'# Written by tools/setup_build.py\n[game]\nbase = {q(a.base)}\nupdate = {q(a.update)}\ndlc = [{dlc}]\n\n'
        f'[keys]\nprod = {q(a.keys)}\n', encoding='utf-8')


# A line that names the reason a step failed, and the two lines after it: what the compiler or the resource compiler
# says about it comes after the line that announces the failure. The last lines of the output are not it - a build that
# stops at one file still compiles the files that do not depend on it, so its last lines are warnings from elsewhere and
# the report the player sends says nothing about the cause (issue #65).
PROGRESS_LINE = re.compile(r'^\[\d+/\d+\]')   # a compiler progress line says nothing about the failure
FAIL_LINE = re.compile(r"^(FAILED|ninja: error)[: ]|\berror:|fatal error|can't open", re.IGNORECASE)


def run(step, cmd, env, progress=None):
    """cmd with its output in the log; progress(line) may return a percentage."""
    with open(LOG, 'a', encoding='utf-8') as log:
        log.write(f'\n=== {step}: {" ".join(str(c) for c in cmd)}\n')
        p = subprocess.Popen([str(c) for c in cmd], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, encoding='utf-8', errors='replace', bufsize=1)
        tail, why, after = [], [], 0
        for line in p.stdout:
            line = line.rstrip()
            log.write(line + '\n')
            out(f'LOG {line}')
            tail = (tail + [line])[-12:]
            if FAIL_LINE.search(line):
                after = 3
            if after and line and not PROGRESS_LINE.match(line) and len(why) < 6:
                why.append(line[:300])
            after = max(after - 1, 0)
            pct = progress(line) if progress else None
            if pct is not None:
                out(f'PERCENT {pct}')
        code = p.wait()
    if code:
        raise StepError(f'{cmd[1] if len(cmd) > 1 else cmd[0]} ended with exit code {code}: '
                        + ' | '.join(why or tail[-4:]))


def menu():
    """game/ui_patch.txt, the list behind the PC settings entry of the main menu (tools/pc_menu.py makes it from a run
    with TOTK_DATA_WATCH=1; for Violet 4.0.0 the reference copy is the same). A list the player made stays."""
    target = ROOT / 'game' / 'ui_patch.txt'
    if not target.is_file():
        shutil.copy2(UI_LIST, target)


def icon(py, env):
    """The game's own icon for karpur.exe (tools/game_icon.py); without it the exe just has none."""
    try:
        run('icon', [py, 'tools/game_icon.py'], env)
    except StepError as e:
        out(f'LOG warning: no icon for karpur.exe ({e})')


def lift_progress(line):
    m = re.search(r'(\d+)/(\d+) files', line)
    return 100 * int(m.group(1)) // int(m.group(2)) if m else None


SHADER_PHASES = [   # (pattern of a progress line, share of the step where its phase begins, its share)
    (re.compile(r'shader census: (\d+) of (\d+) packs'), 0, 25),
    (re.compile(r'gfx_shader: prepare: (\d+) of (\d+) pairs'), 25, 60),
    (re.compile(r'preparation progress: (\d+) of (\d+) variants'), 85, 15),
]


def shader_progress(line):
    for pat, at, share in SHADER_PHASES:
        m = pat.search(line)
        if m and int(m.group(2)):
            return at + share * int(m.group(1)) // int(m.group(2))
    return None


def shaders(env):
    """Every shader of the game, prepared ahead (docs/progress.md, shader pre-build): the shader census unpacks the
    game's data with its own decoder and writes the seed (all programs of its BNSH containers, the player's own data,
    next to karpur.exe); karpur.exe --prepare-shaders translates them and builds them into the driver's cache, restarting
    itself if the driver crashes on one. The game still runs without it (shaders then build at their first draw), so a
    failure here is logged and the build goes on."""
    try:
        run('shaders', [EXE], dict(env, TOTK_SHADER_CENSUS='1'), shader_progress)
        run('shaders', [EXE, '--prepare-shaders'], env, shader_progress)
    except StepError as e:
        out(f'LOG shader preparation failed, the shaders build while playing instead: {e}')


def ninja_progress(line):
    m = re.match(r'\[(\d+)/(\d+)\]', line)
    return 100 * int(m.group(1)) // int(m.group(2)) if m else None


# What each step reads from this folder (globs below ROOT): a release that changes them reruns the step, and the steps
# that use its result (DEPENDENTS). What each step leaves: missing, the step runs again (the launcher's Repair).
STEP_INPUTS = {
    'extract': ['tools/extract.py', 'tools/switchfs.py', 'tools/nso.py'],
    'fonts': ['tools/extract_fonts.py'],
    'romfs': ['tools/extract_romfs.py', 'tools/romfs_index.py'],
    'dlc': ['tools/extract_dlc.py'],
    'menu': ['tools/data/ui_patch_violet_4.0.0.txt'],
    'fps': ['tools/fps_patches.py'],
    'icon': ['tools/game_icon.py'],
    'lift': ['recomp/**/*.py', 'tools/lift.py'],
    'build': ['runtime/**/*', 'tools/build_game.py', 'tools/romfs_index.py'],
}
STEP_OUTPUTS = {
    'extract': ['game/extracted/update/exefs/main', 'game/report/m1-report.txt'],
    'fonts': ['game/firmware/shared-fonts'],
    'romfs': ['game/romfs/romfs.bin', 'game/romfs/index.json'],
    'dlc': ['game/dlc'],
    'menu': ['game/ui_patch.txt'],
    'fps': ['build/fps_patches.txt'],
    'build': ['build/game/karpur.exe'],
    'shaders': ['build/game/shader-prepared.bin'],
}
DEPENDENTS = {
    'extract': ['fps', 'lift', 'build', 'selftest'],
    'romfs': ['build', 'selftest'],
    'fps': ['lift', 'build', 'selftest'],
    'icon': ['build', 'selftest'],
    'lift': ['build', 'selftest'],
    'build': ['selftest', 'shaders'],
}
INFO = ROOT / 'build' / 'setup_info.json'
GAMES = {'01008F6008C5E000': 'violet', '0100A3D008C5C000': 'scarlet'}


def inputs_hash(step):
    h = hashlib.sha256()
    for pattern in STEP_INPUTS.get(step, []):
        for f in sorted(ROOT.glob(pattern)):
            if f.is_file() and '__pycache__' not in f.parts:
                h.update(f.relative_to(ROOT).as_posix().encode() + b'\0' + f.read_bytes())
    return h.hexdigest()


def outputs_present(step):
    return all((ROOT / o).exists() for o in STEP_OUTPUTS.get(step, []))


def write_info():
    """build/setup_info.json for the launcher: which game this is (its colours), from the extract report."""
    report = ROOT / 'game' / 'report' / 'm1-report.txt'
    text = report.read_text(encoding='utf-8', errors='replace') if report.is_file() else ''
    game = next((g for tid, g in GAMES.items() if tid in text), 'unknown')
    INFO.write_text(json.dumps({'game': game}), encoding='utf-8')


def cleanup():
    """What only the first build needed: the toolchain (fetched again in half a minute) and temporary game files. The
    generated C and its objects stay, so an update that changes the runtime only relinks in minutes."""
    for p in [ROOT / 'game' / 'tmp', ROOT / 'game' / 'tmp-icon', MSYS]:
        shutil.rmtree(p, ignore_errors=True)


def fingerprint(a):
    parts = [str(p.resolve()) + ':' + str(p.stat().st_size) for p in [a.base, a.update, *a.dlc, a.keys] if p.is_file()]
    if a.firmware:
        parts.append('firmware:' + str(a.firmware.resolve()))
    return hashlib.sha256('\n'.join(parts).encode()).hexdigest()


def plan(state):
    """The steps to run, in order: not done yet, their output gone, or their inputs changed since they ran; and every
    step that uses the result of one of those."""
    done, hashes = state['done'], state.setdefault('hashes', {})
    work = set()
    for step in STEPS:
        if step in ('check', 'toolchain', 'cleanup', 'selftest'):
            continue
        current = inputs_hash(step)
        if step not in done or not outputs_present(step) or hashes.get(step, current) != current:
            work.add(step)
    for step in list(work):
        work.update(DEPENDENTS.get(step, []))
    if 'build' in work and not (ROOT / 'generated' / 'layout.json').is_file():
        work.update(['lift', 'fps'])   # build_game.py compiles from generated/: an older install's cleanup removed it
    if 'selftest' not in done:
        work.add('selftest')
    if work & {'lift', 'build'} and toolchain_msys2.missing(MSYS):
        work.add('toolchain')   # also when a stopped run left it part way through: fetch() carries on where it stopped
    return ['check'] + [s for s in STEPS if s in work and s not in ('check', 'cleanup')] + ['cleanup']


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--base', type=Path, required=True, help='base game XCI')
    ap.add_argument('--update', type=Path, required=True, help='update NSP')
    ap.add_argument('--dlc', type=Path, action='append', default=[], help='DLC NSP (repeatable, optional)')
    ap.add_argument('--keys', type=Path, required=True, help='prod.keys')
    ap.add_argument('--firmware', type=Path, help="folder of the console's system NCAs, for the shared fonts: needed with "
                    'a base game as NSP or an XCI without them (a converted eShop dump), optional otherwise')
    ap.add_argument('--keep', action='store_true', help='keep the toolchain and the temporary files')
    a = ap.parse_args()

    (ROOT / 'build').mkdir(exist_ok=True)
    fp = fingerprint(a)
    state = json.loads(STATE.read_text(encoding='utf-8')) if STATE.is_file() else {}
    if state.get('inputs') != fp:
        state = {'inputs': fp, 'done': [], 'hashes': {}}
    done, hashes = state['done'], state.setdefault('hashes', {})
    ucrt = MSYS / 'ucrt64'
    env = dict(os.environ, KARPUR_UCRT=str(ucrt), PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8')
    py = sys.executable
    actions = {
        'check': lambda: check(a),
        'toolchain': lambda: toolchain_msys2.fetch(MSYS, log=out),
        'extract': lambda: run('extract', [py, 'tools/extract.py'], env),
        'fonts': lambda: run('fonts', [py, 'tools/extract_fonts.py'] + (['--firmware', a.firmware] if a.firmware else []), env),
        'romfs': lambda: run('romfs', [py, 'tools/extract_romfs.py'], env),
        'dlc': lambda: run('dlc', [py, 'tools/extract_dlc.py'], env),
        'menu': menu,
        'fps': lambda: run('fps', [py, 'tools/fps_patches.py'], env),
        'icon': lambda: icon(py, env),
        'lift': lambda: run('lift', [py, 'tools/lift.py'], env, lift_progress),
        'build': lambda: run('build', [py, 'tools/build_game.py'], env, ninja_progress),
        'selftest': lambda: run('selftest', [EXE, '--selftest'], env),
        'shaders': lambda: shaders(env),
        'cleanup': lambda: None if a.keep else cleanup(),
    }
    steps = plan(state)
    out('PLAN ' + ','.join(steps))
    for n, step in enumerate(steps, 1):
        out(f'STEP {n} {len(steps)} {step}')
        if step in done and step not in ('check', 'cleanup'):
            done.remove(step)   # running again: not done until it ends
            STATE.write_text(json.dumps(state), encoding='utf-8')
        try:
            actions[step]()
        except (StepError, RuntimeError, OSError) as e:
            out(f'ERROR {step} {e}')
            sys.exit(1)
        if step not in done:
            done.append(step)
        if step in STEP_INPUTS:
            hashes[step] = inputs_hash(step)
        STATE.write_text(json.dumps(state), encoding='utf-8')
        if step == 'extract' or not INFO.is_file():
            write_info()
    out(f'DONE {EXE}')


if __name__ == '__main__':
    main()
