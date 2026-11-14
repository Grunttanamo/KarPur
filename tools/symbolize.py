"""Name the functions behind the addresses of a crash report: our own and the game's SDK.

A fault report prints two kinds of address:

  karpur+0x...                       an offset in karpur.exe, named from the linker map build/game/karpur.map,
                                     which tools/build_game.py writes (runtime/platform/win32.c prints these).
  main+0x... / subsdk0+0x... / sdk+0x...  and the bare guest addresses of `pc`, `lr` and the registers. The game's
                                     own code (main) is stripped, but nnSdk and the subsdk keep their dynamic
                                     symbol tables, and a crash inside a library function is named from those.
                                     Naming one such frame has already decided an investigation
                                     (docs/progress.md 2026-11-14, nn::os::SignalLightEvent on the map screen).

Name the addresses, or paste a whole report:

  .venv\\Scripts\\python tools\\symbolize.py karpur+0xc7a4607 sdk+0x3726f0
  .venv\\Scripts\\python tools\\symbolize.py < crash.txt

Output: symbol names and offsets only, never game bytes. The guest side reads the NSOs of the maintainer's own
extracted update and the lift's generated/layout.json.
"""

import argparse
import bisect
import re
import sys
from bisect import bisect_right
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP = ROOT / 'build' / 'game' / 'karpur.map'
BASE = 0x140000000   # PE32+ default image base (lld); used only when the map lists absolute addresses

# lld's own map format: "<address> [<load address>] <size> <align> <name>", indented by level (output section, input
# section "file:(section)", symbol); the MSVC-style format has "<section>:<offset> <symbol> <address> ...". Both are
# read: a line of numbers followed by one name is a symbol unless the name is a section or an input section.
MS_LINE = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s')
HEX = re.compile(r'^[0-9a-fA-F]+$')

# The guest side: the modules of the lift, their symbol tables, and how a report writes an address.
MODULES = ('main', 'subsdk0', 'sdk')
EXEFS = [ROOT / 'game' / 'extracted' / 'update' / 'exefs', ROOT / 'game' / 'extracted' / 'base' / 'exefs']
LAYOUT = ROOT / 'generated' / 'layout.json'
HOST_ADDR = re.compile(r'\bkarpur\+0x([0-9a-fA-F]+)')
GUEST_ADDR = re.compile(r'\b(main|subsdk0|sdk)\+0x([0-9a-fA-F]+)')
GUEST_RAW = re.compile(r'\b0*(7[0-9a-fA-F]{9,10})\b')   # pc, lr and the registers are printed bare


def load(path):
    syms = []
    for line in path.read_text(errors='replace').splitlines():
        m = MS_LINE.match(line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
            continue
        t = line.split()
        if len(t) < 4 or not all(HEX.match(x) for x in t[:-1]) or len(t[0]) < 8:
            continue
        name = t[-1]
        if name.startswith('.') or ':(' in name or name.endswith(('.o', '.obj', ')')):
            continue
        syms.append((int(t[0], 16), name))
    syms.sort()
    return syms


def guest_tables(exefs):
    """{module: [(address, size, name)]} from the dump's own NSOs; empty for a module that is not there."""
    out = {}
    for m in MODULES:
        for folder in exefs:
            if (folder / m).is_file():
                import nso
                out[m] = sorted((a, s or 1, n) for n, _i, _sh, a, s in nso.Nso(folder / m).exports() if a)
                break
    return out


def guest_modules():
    """[{name, base, size}] of the lift, so a bare guest address can be placed; empty without a lift."""
    import json
    try:
        return json.loads(LAYOUT.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return []


def guest_look(tab, off):
    """(name, offset into it) for the symbol that covers off, or None."""
    i = bisect.bisect_right(tab, (off, 1 << 62, '')) - 1
    if i < 0:
        return None
    a, size, name = tab[i]
    return (name, off - a) if off < a + size else None


def host_side(text, map_path, base):
    offs = [int(h, 16) for h in HOST_ADDR.findall(text)]
    if not offs:
        return
    if not map_path.is_file():
        print(f'karpur+...: no {map_path} (run tools\\build_game.py to write the map)')
        return
    syms = load(map_path)
    if not syms:
        print(f'karpur+...: no symbols read from {map_path}')
        return
    addrs = [s[0] for s in syms]
    # lld's MinGW map lists image-relative addresses (all below the default image base): offsets are used as they are.
    if base is None:
        base = BASE if addrs[-1] >= BASE else 0
    print(f'{len(syms)} symbols in {map_path.name}, image base {base:#x}')
    for off in dict.fromkeys(offs):
        va = base + off
        i = bisect_right(addrs, va) - 1
        if i < 0:
            print(f'karpur+{off:#x}: before the first symbol')
        else:
            print(f'karpur+{off:<12x} {syms[i][1]}+{va - syms[i][0]:#x}')


def guest_side(text, exefs):
    found = []
    for m, off in GUEST_ADDR.findall(text):
        if (m, int(off, 16)) not in found:
            found.append((m, int(off, 16)))
    mods = guest_modules()
    for raw in GUEST_RAW.findall(text):
        v = int(raw, 16)
        for mod in mods:
            off = v - int(mod['base'], 16)
            if 0 <= off < int(mod['size'], 16) and (mod['name'], off) not in found:
                found.append((mod['name'], off))
    if not found:
        return
    sys.path.insert(0, str(ROOT / 'recomp'))
    tabs = guest_tables(exefs)
    if not tabs:
        print(f'main/sdk+...: no NSO in {", ".join(str(p) for p in exefs)} (the build extracts them)')
        return
    stripped = 0
    for m, off in found:
        hit = guest_look(tabs[m], off) if m in tabs else None
        if hit:
            print(f'{m}+0x{off:<10x} {hit[0]} +{hit[1]:#x}')
        else:
            stripped += 1
            print(f'{m}+0x{off:<10x} -')
    if stripped:
        print(f'{stripped} of {len(found)} guest addresses have no symbol: the game\'s own code is stripped, so '
              'only nnSdk and the subsdk can be named.')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addresses', nargs='*', help='karpur+0x..., sdk+0x..., as a report prints them (default: stdin)')
    ap.add_argument('--map', type=Path, default=MAP)
    ap.add_argument('--base', type=lambda s: int(s, 16), default=None, help='image base (hex); default: detected')
    ap.add_argument('--exefs', type=Path, action='append', help='an exefs folder holding main/subsdk0/sdk')
    a = ap.parse_args()

    text = ' '.join(a.addresses) if a.addresses else sys.stdin.read()
    if not (HOST_ADDR.search(text) or GUEST_ADDR.search(text) or GUEST_RAW.search(text)):
        sys.exit('error: no karpur+0x..., main/subsdk0/sdk+0x... or bare guest address found')
    host_side(text, a.map, a.base)
    guest_side(text, a.exefs or EXEFS)


if __name__ == '__main__':
    main()
