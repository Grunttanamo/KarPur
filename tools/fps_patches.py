"""Write the patch table for Violet's frame-rate and LOD patches (recomp/patches.py format) from the game's own main.

The table goes to build/ (it holds instruction words of the game, so it stays out of the repository). Every site is
checked against the instruction it expects (register and immediate), so a different game version stops here with a
report instead of producing a broken build.

Sets (switched with their TOTK_ variable):

  fps_dt   (on by default) main+0x92dd2c: the thunk main+0x92dd20 (tail call of vtable slot 2 of the object in x0) gets x2 = the
           frame time in ns from the patch page +64, which runtime/frame_dt.c fills with the measured frame time
           (the game passes 33333334 there at 30 fps; the third-party mod sets the same register). With it on, the
           runtime paces frames to the menu's frame limit (TOTK_FPS_LIMIT, 30..360 or display) and the game keeps
           its speed at any rate (confirmed at 60 fps, docs/progress.md 2026-11-02).

  lod_bias (off by default; TOTK_LOD=1) main+0xc07dc4 and main+0x7f1a6c: each loads a float (ldr s0, [x19, #0x1c] /
           [x20, #0x10]) that a virtual call right after receives as its argument; a third-party mod for Violet 3.0.1
           replaces both loads with fmov s0, #-15.0 and calls it "LodBias" (read for behaviour only). The set loads s0
           from the patch page +72 instead (adrp + ldr s0 through a scratch register from x9-x17 that no instruction
           between the load and the call names; the call clobbers it), which runtime/lod.c fills from TOTK_LOD_BIAS
           (default -15).

  data_watch (off by default; TOTK_DATA_WATCH=1) every direct call (bl) of main+0xcc8750, the game's Oodle decoder
           (OodleLZ_Decompress: the function names that string), goes to a host function at patch page +0xfd0
           (runtime/data_watch.c), which runs the decoder and then looks at what it unpacked: the game's data files
           (arc/data.trpfs) are Oodle Kraken data. Checked: the function at that offset must reference the string
           "OodleLZ_Decompress"; tail calls (b) to it are counted and left alone.

  step_ten (off by default; TOTK_STEP_TEN=1, test) main+0x170c408: movz w10, #1.0f (stored to [x9, #0x90]) becomes
           10.0f, a fixed patch of the third-party mod at the same offset in 3.0.1 (meaning unknown).

  indoor_fix (off by default; TOTK_INDOOR_FIX=1, test) main+0x20a8d3c: the float 50000.0 built there for a
           comparison becomes 96.15625 (top half 0x42c0), as in a third-party 3.0.1 patch that fixes stutter indoors.

  dynres_full (off by default; TOTK_DYNRES_FULL=1, test) main+0xbd7014 and +0xbd7020: the pair 1280x720 (the lower
           bound of the dynamic resolution, it seems) becomes 1920x1080, as in a third-party 3.0.1 quality patch.

  interval_one (off by default; TOTK_INTERVAL_ONE=1, test) main+0x2b7a874: `add w1, w20, #1` becomes `movz w1, #1`,
           the argument of a virtual call in the frame-mode function, as in a third-party 3.0.1 60 fps patch.

  lua_mod  (on by default; TOTK_LUA_MOD=0 turns it off) main+0xe37f94: the call (bl) of lua_pcallk (main+0xb5ddd0) that runs the
           game's main script, right after main+0xe37dac loaded it with luaL_loadbufferx (bl main+0xe382a0 at
           main+0xe37f70), goes to a host function at patch page +0xfc0 (runtime/lua_mod.c), which makes the call and
           then loads and runs the runtime's own Lua in the same state. Checked: both calls are there, and
           main+0xe382a0 calls lua_load (main+0x232e0), whose parser function references "attempt to load a %s chunk".

Tried and dropped: the 30 fps frame time and vsync count in main+0x2b79f60 (no effect on the interval or the speed).

tools/lift.py uses build/fps_patches.txt by itself when the file exists.
Background: docs/progress.md 2026-11-02 (a third-party frame-rate mod for Violet, read for behaviour only, and
tools/consts.py / calls.py --insn reports of 4.0.0).

  .venv\\Scripts\\python tools\\fps_patches.py
  .venv\\Scripts\\python tools\\lift.py --patches build\\fps_patches.txt --gen-only
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'recomp'))

from layout import layout  # noqa: E402
from module import Module  # noqa: E402

EXEFS = ROOT / 'game' / 'extracted' / 'update' / 'exefs'


def describe(w):
    """Short text of a 64-bit or SIMD 32-bit LDR (immediate), move-wide or ADD (immediate) word for the report."""
    if (w & 0xFFC00000) == 0xBD400000:
        return f'ldr s{w & 31}, [x{(w >> 5) & 31}, #{((w >> 10) & 0xFFF) * 4:#x}]'
    if (w & 0xFFC00000) == 0xF9400000:
        return f'ldr x{w & 31}, [x{(w >> 5) & 31}, #{((w >> 10) & 0xFFF) * 8:#x}]'
    if (w & 0x1F800000) == 0x12800000:
        name = {0: 'movn', 2: 'movz', 3: 'movk'}.get((w >> 29) & 3, 'move-wide')
        return f'{name} {"x" if w >> 31 else "w"}{w & 31}, #{(w >> 5) & 0xFFFF:#x} lsl {16 * ((w >> 21) & 3)}'
    if (w & 0x7F800000) == 0x11000000:
        return f'add {"x" if w >> 31 else "w"}{w & 31}, {"x" if w >> 31 else "w"}{(w >> 5) & 31}, #{(w >> 10) & 0xFFF:#x}'
    return 'another kind of instruction'


def ldr_x(rt, rn, off):
    return 0xF9400000 | (off // 8) << 10 | rn << 5 | rt


def ldr_w(rt, rn, off):
    return 0xB9400000 | (off // 4) << 10 | rn << 5 | rt


PAGE_FRAME_NS = 64   # patch page byte offset of the frame time in ns (runtime/frame_dt.c; recomp/patches.py layout)
PAGE_LOD_BIAS = 72   # patch page byte offset of the LOD bias float (runtime/lod.c)
PAGE_SHADOW_RES = 76   # patch page byte offset of the shadow map side in pixels (runtime/resolution.c)


def movz_w(rd, imm16, shift):
    return 0x52800000 | (shift // 16) << 21 | imm16 << 5 | rd


def movk_w(rd, imm16, shift):
    return 0x72800000 | (shift // 16) << 21 | imm16 << 5 | rd


def movz_x(rd, imm16, shift):
    return 0xD2800000 | (shift // 16) << 21 | imm16 << 5 | rd


def ldr_s(rt, rn, off):
    return 0xBD400000 | (off // 4) << 10 | rn << 5 | rt


SCRATCH = (17, 16, 15, 14, 13, 12, 11, 10, 9)   # registers a call clobbers that are no argument (x9-x17)


def free_reg(m, a):
    """A register from SCRATCH that no instruction after a names (any register field) before the next call, or None.
    The call clobbers it, so the patch may use it; the field check is coarse: an immediate that looks like the register
    only makes it pick another one."""
    window = []
    for k in range(1, 12):
        w = m.word(a + 4 * k)
        if (w & 0xFC000000) == 0x94000000 or (w & 0xFFFFFC1F) == 0xD63F0000:
            break
        window.append(w)
    else:
        return None   # no call within reach: nothing is known to be free
    for r in SCRATCH:
        if not any(((w >> sh) & 31) == r for w in window for sh in (0, 5, 10, 16)):
            return r
    return None


# (set, switch, default, module offset, expected word, replacement word or words; '@page:x2' is adrp x2 of the
# patch page)
SITES = [
    ('fps_dt', 'TOTK_FPS_DT', 1, 0x92dd2c, ldr_x(8, 0, 0), [ldr_x(8, 0, 0), '@page:x2', ldr_x(2, 2, PAGE_FRAME_NS)]),
    # 'scratch': adrp into a free register from SCRATCH, then ldr s0 from the patch page through it (main picks it)
    ('lod_bias', 'TOTK_LOD', 0, 0xc07dc4, ldr_s(0, 19, 0x1c), 'scratch'),
    ('lod_bias', 'TOTK_LOD', 0, 0x7f1a6c, ldr_s(0, 20, 0x10), 'scratch'),
    # step_ten (test): float 1.0 that main+0x170c408 stores to [x9, #0x90] becomes 10.0, as in the third-party mod
    # (meaning unknown; tried for the player character's stutter indoors at 60 fps, docs/progress.md 2026-11-02)
    ('step_ten', 'TOTK_STEP_TEN', 0, 0x170c408, movz_w(10, 0x3F80, 16), [movz_w(10, 0x4120, 16)]),
    # indoor_fix (test): main+0x20a8d38/3c build w8 = 50000.0f, compared right after (fmov s1, w8; fcmp; b.cond);
    # the third-party 60 fps patch for 3.0.1 writes the top half 0x42c0 at the same offset (96.15625f) and names
    # "stutter indoors" among its fixes (docs/progress.md 2026-11-02)
    ('indoor_fix', 'TOTK_INDOOR_FIX', 0, 0x20a8d3c, movk_w(8, 0x4743, 16), [movk_w(8, 0x42C0, 16)]),
    # dynres_full (test): main+0xbd7014..28 store two 64-bit words, widths (1280, 1920) and heights (720, 1080), which
    # look like the lower and upper bound of the dynamic resolution; the third-party quality patch raises the lower
    # bound to 1920x1080
    ('dynres_full', 'TOTK_DYNRES_FULL', 0, 0xbd7014, movz_x(8, 0x500, 0), [movz_x(8, 0x780, 0)]),
    ('dynres_full', 'TOTK_DYNRES_FULL', 0, 0xbd7020, movz_x(8, 0x2D0, 0), [movz_x(8, 0x438, 0)]),
    # shadow (TOTK_SHADOW_RES, 1024/2048/3072/4096; found this session, independently, by counting instructions from
    # a known label in the lifted C): main+0x16dc200 `ldrsw x13, [x19, #0x3d4]` sign-extends a field of the renderer
    # object into x13, the side in pixels of the sun shadow map's render target (the game's own is 1024); the field
    # is recomputed from elsewhere every time, so changing it at runtime does not stick. Read the patch page instead.
    ('shadow', 'TOTK_SHADOW_RES', 0, 0x16dc200, 0xb983d66d, ['@page:x13', ldr_w(13, 13, PAGE_SHADOW_RES)]),
    # interval_one (test): main+0x2b7a874 `add w1, w20, #1` (w20 read from [x10, #0x20]) is the argument of a virtual
    # call in the frame-mode function main+0x2b79f60; the third-party 3.0.1 60 fps patch sets w1 = 1 at this offset
    ('interval_one', 'TOTK_INTERVAL_ONE', 0, 0x2b7a874, 0x11000000 | 1 << 10 | 20 << 5 | 1, [movz_w(1, 1, 0)]),
]
OPTIONAL = {'lod_bias', 'data_watch', 'step_ten', 'indoor_fix', 'dynres_full', 'interval_one', 'lua_mod', 'shadow'}
OODLE_DECOMPRESS = 0xcc8750   # main offset of OodleLZ_Decompress in 4.0.0 (calls.py --string oodle, 2026-11-02)
PAGE_OODLE_HOST = 0xfd0       # patch page offset of the host wrapper (runtime/data_watch.c)
# Lua (docs/progress.md 2026-11-03, calls.py --string / --refs on 4.0.0)
LUA_RUN_SITE = 0xe37f94       # bl lua_pcallk that runs the main script
LUA_LOAD_SITE = 0xe37f70      # bl luaL_loadbufferx that loads it, just before
LUA_PCALLK = 0xb5ddd0
LUA_LOADBUFFERX = 0xe382a0
LUA_LOAD = 0x232e0
LUA_F_PARSER = 0x28580        # references "attempt to load a %s chunk (mode is '%s')"
PAGE_LUA_HOST = 0xfc0         # patch page offset of the host function (runtime/lua_mod.c)


def bl_target(m, a):
    """Target of the bl at a, or None."""
    w = m.word(a)
    if (w & 0xFC000000) != 0x94000000:
        return None
    off = w & 0x03FFFFFF
    return a + 4 * (off - 0x4000000 if off & 0x2000000 else off)


def calls_in(m, func, target, span=0x400):
    """True when a bl in the first span bytes of func goes to target."""
    return any(bl_target(m, func + k) == target for k in range(0, span, 4) if m.in_text(func + k))


def names_string(m, base, func, text, span=0x600):
    """True when an ADRP+ADD in the first span bytes of the function at base+func computes the address of the
    NUL-terminated string text."""
    img = m.image
    at = img.find(b'\0' + text.encode() + b'\0')
    if at < 0:
        return False
    target = base + at + 1
    for k in range(0, span, 4):
        a = base + func + k
        if not m.in_text(a + 4):
            break
        w = m.word(a)
        if (w & 0x9F000000) != 0x90000000:
            continue
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2)
        imm -= (1 << 21) if imm & (1 << 20) else 0
        if (a & ~0xFFF) + (imm << 12) != target & ~0xFFF:
            continue
        for j in range(1, 6):
            x = m.word(a + 4 * j)
            if (x & 0xFFC00000) == 0x91000000 and ((x >> 5) & 31) == (w & 31) and ((x >> 10) & 0xFFF) == target & 0xFFF:
                return True
    return False


def direct_calls(m, target):
    """(bl sites, b sites) in main's .text that branch to target."""
    import numpy as np
    w = m.words
    idx = np.arange(len(w), dtype=np.int64)
    off = (w & 0x03FFFFFF).astype(np.int64)
    off = np.where(off & 0x2000000, off - 0x4000000, off)
    hit = (m.lo + (idx + off) * 4) == target
    bl = np.flatnonzero(hit & ((w & 0xFC000000) == 0x94000000))
    b = np.flatnonzero(hit & ((w & 0xFC000000) == 0x14000000))
    return [m.lo + 4 * int(i) for i in bl], [m.lo + 4 * int(i) for i in b]   # a mismatch drops the set with a note instead of stopping


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'fps_patches.txt')
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    base = next(b for n, b, _ in layout(a.exefs) if n == 'main')
    m = Module('main', a.exefs / 'main', base, 'c')
    # Check every site first: an optional set with any failing site is left out whole.
    dropped, bad, words_of = set(), 0, {}
    for name, env, default, off, want, repl in SITES:
        have = m.word(base + off)
        ok = have == want
        note = 'ok' if ok else 'MISMATCH'
        if ok and repl == 'scratch':
            r = free_reg(m, base + off)
            if r is None:
                ok, note = False, 'no free scratch register before the call'
            else:
                repl, note = [f'@page:x{r}', ldr_s(0, r, PAGE_LOD_BIAS)], f'ok (scratch x{r})'
        words_of[off] = repl
        print(f'main+{off:#x}: expected {describe(want)}, found {describe(have)}: {note}')
        if not ok:
            if name in OPTIONAL:
                dropped.add(name)
            else:
                bad += 1
    # data_watch: every bl of the Oodle decoder
    if names_string(m, base, OODLE_DECOMPRESS, 'OodleLZ_Decompress'):
        calls, tails = direct_calls(m, base + OODLE_DECOMPRESS)
        print(f'main+{OODLE_DECOMPRESS:#x}: references "OodleLZ_Decompress": ok; {len(calls)} calls (bl), '
              f'{len(tails)} tail calls (b, left alone)')
        for site in calls:
            off = site - base
            SITES.append(('data_watch', 'TOTK_DATA_WATCH', 0, off, m.word(site), [f'@pagebl:{PAGE_OODLE_HOST:x}']))
            words_of[off] = SITES[-1][5]
        if not calls:
            dropped.add('data_watch')
    else:
        print(f'main+{OODLE_DECOMPRESS:#x}: does not reference "OodleLZ_Decompress": set data_watch left out')
        dropped.add('data_watch')
    # lua_mod: the call that runs the main script
    lua_ok = (bl_target(m, base + LUA_RUN_SITE) == base + LUA_PCALLK and
              bl_target(m, base + LUA_LOAD_SITE) == base + LUA_LOADBUFFERX and
              calls_in(m, base + LUA_LOADBUFFERX, base + LUA_LOAD, 0x40) and
              names_string(m, base, LUA_F_PARSER, "attempt to load a %s chunk (mode is '%s')"))
    print(f'main+{LUA_RUN_SITE:#x}: bl lua_pcallk after luaL_loadbufferx: {"ok" if lua_ok else "MISMATCH"}')
    if lua_ok:
        SITES.append(('lua_mod', 'TOTK_LUA_MOD', 1, LUA_RUN_SITE, m.word(base + LUA_RUN_SITE),
                      [f'@pagebl:{PAGE_LUA_HOST:x}']))
        words_of[LUA_RUN_SITE] = SITES[-1][5]
    else:
        dropped.add('lua_mod')
    if bad:
        sys.exit(f'{bad} sites differ from what this table expects (another game version?): nothing written')
    for name in sorted(dropped):
        print(f'set {name} left out (one of its sites failed the check above)')
    lines, sets, n = ['# frame-rate and LOD patch sets from tools/fps_patches.py (holds instruction words: not for the '
                      'repository)'], [], 0
    for name, env, default, off, want, _ in SITES:
        if name in dropped:
            continue
        if name not in sets:
            sets.append(name)
            lines.append(f'set  {name} {env} {default}')
        repl = words_of[off]
        words = repl if isinstance(repl, list) else [repl]
        text = ' '.join(r if isinstance(r, str) else f'{r:08x}' for r in words)
        lines.append(f'site {name} main {off:x} {want:08x} {text}')
        n += 1
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n')
    print(f'patch table: {len(sets)} sets, {n} sites written to {a.out}')


if __name__ == '__main__':
    main()
