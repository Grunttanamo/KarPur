"""Write game/exclusives.txt: the bytes that make a version-exclusive wild Pokemon spawn in both editions.

Scarlet and Violet ship the same spawn data. An entry of the species tables in
world/data/encount (arc/worlddataencountsettingsettingdata.bin) carries

    versiontable { A: bool, B: bool }

and an exclusive does not store `false` for the other edition: that field is simply **absent**, so FlatBuffers
answers with the default, which is "no". Larvitar has `A = 1` and no `B`, Bagon has `B = 1` and no `A`, and
everything else has both (docs/progress.md 2026-11-13).

An absent field needs no rewritten file. A FlatBuffer table reaches its fields through a vtable, where an absent
field's entry is 0; writing the present flag's vtable entry into the absent one's makes both flags read the same
byte, which is the stored 1. That is two bytes per shape of entry, and runtime/data_patch.c applies them right
after the game's Oodle decode (TOTK_NO_EXCLUSIVES=1).

vtables are shared between tables of the same shape, which is wanted here - all exclusives of one edition share
one - but this tool patches a vtable only when every table in the file that uses it is a versiontable.

Needs the dumped tables and their schema (both from tools/trpfs.py):

    .venv\\Scripts\\python tools\\trpfs.py --watch "encount"
    <run the game once with TOTK_DATA_WATCH=1>
    .venv\\Scripts\\python tools\\exclusives.py

It prints counts and species numbers only, never file contents.

Standard library only.
"""

import argparse
import collections
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DUMP = ROOT / 'game' / 'dump'
WATCH = ROOT / 'game' / 'data_watch.txt'
OUT = ROOT / 'game' / 'exclusives.txt'

VECTOR, OBJ = 14, 15   # FlatBuffer reflection base types, as tools/trpfs.py names them


def trpfs():
    """tools/trpfs.py as a module: its Schema reader and FlatBuffer helpers."""
    spec = importlib.util.spec_from_file_location('trpfs', ROOT / 'tools' / 'trpfs.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def u16(buf, at):
    return struct.unpack_from('<H', buf, at)[0]


def vtable_of(buf, pos):
    """A table's vtable position: the table starts with a signed offset back to it."""
    return pos - struct.unpack_from('<i', buf, pos)[0]


def slot_offset(buf, pos, slot):
    """The field's offset from the table's start, or 0 when the field is absent."""
    vt = vtable_of(buf, pos)
    size = u16(buf, vt)
    at = 4 + 2 * slot
    return u16(buf, vt + at) if at + 2 <= size else 0


def tables(m, sc, oi, buf, pos, out):
    """(type name, table position) of every table reachable from pos, so a vtable's users are known."""
    o = sc.objects[oi]
    if not o['struct']:
        out.append((o['name'].split('.')[-1], pos))
    for f in o['fields']:
        b = f['base']
        if b not in (OBJ, VECTOR):
            continue
        if o['struct']:
            at = pos + f['offset']
        else:
            off = slot_offset(buf, pos, (f['offset'] - 4) // 2)
            if not off:
                continue
            at = pos + off
        if b == OBJ:
            sub = sc.objects[f['index']]
            tables(m, sc, f['index'], buf, at if sub['struct'] else at + struct.unpack_from('<I', buf, at)[0], out)
        elif f['elem'] == OBJ:
            sub = sc.objects[f['index']]
            v = at + struct.unpack_from('<I', buf, at)[0]
            for k in range(struct.unpack_from('<I', buf, v)[0]):
                el = v + 4 + (k * sub['size'] if sub['struct'] else 4 * k)
                tables(m, sc, f['index'], buf, el if sub['struct'] else el + struct.unpack_from('<I', buf, el)[0], out)


def species_schemas(m):
    """Every schema of the dump whose root is a vector of encounter species entries."""
    out = []
    for p in sorted(DUMP.iterdir()):
        if not p.is_file():
            continue
        buf = p.read_bytes()
        if buf[4:8] != b'BFBS':
            continue
        try:
            sc = m.Schema(buf)
        except Exception:  # noqa: BLE001
            continue
        if sc.objects[sc.root]['name'].split('.')[-1].startswith('EncountPokeData'):
            out.append(sc)
    return out


def entry_slots(sc):
    """The element type of the root's vector, and the slots of devid and versiontable in it."""
    root = sc.objects[sc.root]
    vec = next((f for f in root['fields'] if f['base'] == VECTOR and f['elem'] == OBJ), None)
    if not vec:
        return None
    sub = sc.objects[vec['index']]
    slot = {f['name']: (f['offset'] - 4) // 2 for f in sub['fields']}
    if 'devid' not in slot or 'versiontable' not in slot:
        return None
    vt = next(f for f in sub['fields'] if f['name'] == 'versiontable')
    vslot = {f['name']: (f['offset'] - 4) // 2 for f in sc.objects[vt['index']]['fields']}
    if 'A' not in vslot or 'B' not in vslot:
        return None
    return (vec['offset'] - 4) // 2, slot, vslot, sc.objects[vt['index']]['name'].split('.')[-1]


def patch_file(m, buf, sc):
    """(offset, type, value) for every byte that frees the exclusives, and how many entries were freed.

    The same steps runtime/data_patch.c excl_fix takes: the vtable that has room for both flags gets its absent
    slot pointed at the offset where the stored flag sits, and the entries whose vtable is two bytes short are
    pointed at that vtable instead. Nothing is emitted unless the file looks exactly like that.
    """
    shape = entry_slots(sc)
    if not shape:
        return None
    vslot_root, slot, vslot, vt_name = shape
    off0 = slot_offset(buf, root := struct.unpack_from('<I', buf, 0)[0], vslot_root)
    if not off0:
        return None
    v = root + off0
    v += struct.unpack_from('<I', buf, v)[0]
    count = struct.unpack_from('<I', buf, v)[0]
    users = collections.defaultdict(set)
    seen = []
    tables(m, sc, sc.root, buf, root, seen)
    for name, pos in seen:
        users[vtable_of(buf, pos)].add(name)
    one, both, flag, species = [], 0, 0, []
    for k in range(count):
        e = v + 4 + 4 * k
        e += struct.unpack_from('<I', buf, e)[0]
        vo = slot_offset(buf, e, slot['versiontable'])
        if not vo:
            continue
        vp = e + vo
        vp += struct.unpack_from('<I', buf, vp)[0]
        a, b = slot_offset(buf, vp, vslot['A']), slot_offset(buf, vp, vslot['B'])
        if a and b:
            both += 1
            continue
        if not a and not b:
            continue
        if flag and (a or b) != flag:
            print('  the stored flag sits at more than one offset: left alone')
            return None
        flag = a or b
        do = slot_offset(buf, e, slot['devid'])
        species.append(u16(buf, e + do) if do else 0)
        one.append((vp, vtable_of(buf, vp)))
    if not one or not both:
        return None
    target = next((vt for _, vt in one if u16(buf, vt) >= 8), None)
    if target is None:
        print('  no versiontable vtable with room for both flags: left alone')
        return None
    if users[target] != {vt_name}:
        print(f'  vtable at {target:#x} is used by {sorted(users[target])} too: left alone')
        return None
    a, b = u16(buf, target + 4), u16(buf, target + 6)
    if (a and a != flag) or (b and b != flag) or (a and b):
        print(f'  vtable at {target:#x} does not hold one absent flag: left alone')
        return None
    out = [(target + (6 if a else 4), 'u16', flag)]
    for vp, vt in one:
        if vt != target:
            out.append((vp, 'i32', vp - target))
    return out, count, species, len(one)

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--dump', type=Path, default=DUMP)
    ap.add_argument('--out', type=Path, default=OUT)
    a = ap.parse_args()
    if not a.dump.is_dir():
        sys.exit(f'error: {a.dump} is empty: run tools\\trpfs.py --watch "encount" and the game with '
                 'TOTK_DATA_WATCH=1 first')
    m = trpfs()
    schemas = species_schemas(m)
    if not schemas:
        sys.exit(f'error: no encounter-species schema in {a.dump} (tools/trpfs.py --watch "encount")')
    watch = {}
    if WATCH.is_file():
        for line in WATCH.read_text(encoding='utf-8').splitlines():
            f = line.split()
            if len(f) == 4 and not line.startswith('#'):
                watch[f[3]] = (int(f[0]), int(f[1]), f[2])
    lines = ['# oodle <packed> <unpacked> <fnv1a64 of the first 4 KiB packed> <offset> u16 mod=<value> <name>',
             '# the vtable slot of the flag an exclusive Pokemon leaves absent, pointed at the stored one, and the',
             '# entries whose vtable is too short pointed at the vtable that has room (tools/exclusives.py; the same',
             '# steps runtime/data_patch.c excl_fix takes for TOTK_NO_EXCLUSIVES=1)']
    files = exclusives = 0
    for p in sorted(a.dump.iterdir()):
        if not p.is_file():
            continue
        buf = p.read_bytes()
        if buf[4:8] == b'BFBS' or p.name not in watch:
            continue
        for sc in schemas:
            try:
                got = patch_file(m, buf, sc)
            except Exception:  # noqa: BLE001
                continue       # another pack's schema, or not this layout
            if not got or not got[1]:
                continue
            patches, n, species, freed = got
            if not patches:
                break
            packed, unpacked, print_ = watch[p.name]
            for at, kind, value in patches:
                lines.append(f'oodle {packed} {unpacked} {print_} {at:x} {kind} mod={value} versiontable')
            files += 1
            exclusives += freed
            print(f'{p.name[-28:]:30} {n:>4} entries, {freed:>3} exclusive, {len(patches)} patches')
            print(f'{"":30} species {sorted(set(species))}')
            break
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'\n{a.out}: {len(lines) - 3} patches in {files} tables, {exclusives} exclusive entries')
    if not files:
        print('nothing found: the tables may not be in the dump (run the game with TOTK_DATA_WATCH=1 after --watch)')


if __name__ == '__main__':
    main()
