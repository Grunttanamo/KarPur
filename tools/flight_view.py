"""Read a flight recorder dump (runtime/flight.c) and say what the frame it was triggered by was spent on.

TOTK_FLIGHT=1 keeps the last 600 presents in a ring: for each one the interval, how long the render thread was
blocked and in what, and the counts of the work in it. A dump is written on F8, on a frame over
TOTK_FLIGHT_SPIKE_MS, and at the frames of TOTK_FLIGHT_AT. This reads its frames.tsv and prints

  - the trigger and the frames around it, with the blocked times and counts that are not the usual ones;
  - what the slow frames cost over the quiet ones, per blocked kind, which is the answer to "why did it stutter":
    a shader translated or a pipeline built at that moment, a texture uploaded, waiting for the GPU, or simply
    more work in the frame than the frame rate allows.

    .venv\\Scripts\\python tools\\flight_view.py                      the newest dump under build/flight
    .venv\\Scripts\\python tools\\flight_view.py <dump folder>         that one
    .venv\\Scripts\\python tools\\flight_view.py --around 40           40 frames before the trigger, not 20

A recording made with TOTK_FLIGHT_THUMBS=0 holds numbers only; with thumbnails it also holds pictures of the game,
which stay on the maintainer's PC. This prints numbers only either way.

Standard library only.
"""

import argparse
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The costs a frame can be made of, in the order they are reported. `replay` is not a wait: it is the work of turning
# the game's NVN commands into Vulkan ones, and the one thing a long frame can consist of while waiting for nothing.
BLOCKED = [
    ('replay_ms', 'turning the frame\'s NVN commands into Vulkan commands'),
    ('xlate_ms', 'translating a shader the game had not used before'),
    ('pipe_ms', 'building a graphics pipeline'),
    ('tex_ms', 'uploading a texture'),
    ('astc_ms', 'decoding an ASTC texture'),
    ('fence_ms', 'waiting for the GPU to finish an earlier frame'),
    ('drain_ms', 'waiting for the submit queue to drain'),
    ('pace_ms', 'held back by the frame limit'),
]
# Counts worth naming when they rise in the slow frames: each one is work that frame did and its quiet neighbours
# did not. draws/passes/dispatches say the frame was simply heavier.
COUNTS = ['shaders_xlated', 'pipe_built', 'pipe_waited', 'oneshot_waited', 'tex_up', 'tex_new', 'tex_reup',
          'draws_skip_shader', 'draws_skip_pipe', 'passes', 'draws', 'dispatches', 'copies', 'clears', 'submits']


def newest_dump(base):
    """The most recently written dump folder under base (<YYYYMMDD-HHMMSS>-f<frame>/frames.tsv)."""
    dumps = [p.parent for p in base.glob('*/frames.tsv')]
    return max(dumps, key=lambda p: p.name) if dumps else None


def read(path):
    """(the dump's own first line, [{column: value}] oldest frame first) from a frames.tsv."""
    head, rows, cols = '', [], []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('#'):
            head = head or line.lstrip('# ').strip()
            continue
        f = line.split('\t')
        if not cols:
            cols = f
            continue
        row = {}
        for name, value in zip(cols, f):
            try:
                row[name] = float(value) if '.' in value else int(value)
            except ValueError:
                row[name] = value
        if 'frame' in row:
            rows.append(row)
    if not rows:
        sys.exit(f'error: {path} holds no frame records')
    return head, rows


def column(rows, name):
    return [r[name] for r in rows if name in r]


def fmt(v):
    return f'{v:.1f}' if isinstance(v, float) else str(v)


def table(rows, trig, around, limit):
    """The frames around the trigger, the quiet runs between them collapsed into one line."""
    at = next((i for i, r in enumerate(rows) if r['frame'] == trig), len(rows) - 1)
    first = max(0, at - around)
    print('frame      interval  what the render thread was blocked in (ms)')
    hidden = 0
    for i, r in enumerate(rows[first:], first):
        blocked = [f'{n.removesuffix("_ms")} {r[n]:.1f}' for n, _ in BLOCKED if r.get(n, 0) >= 0.5]
        slow = r['interval_ms'] > limit
        # Every slow frame, every frame that waited for something, and the trigger with its two neighbours either way.
        if not (slow or abs(i - at) <= 2 or any(r.get(n, 0) >= 2.0 for n, _ in BLOCKED)):
            hidden += 1
            continue
        if hidden:
            print(f'{"":>9}            ({hidden} frames of the usual length)')
            hidden = 0
        mark = '  <-- trigger' if r['frame'] == trig else ''
        print(f'{r["frame"]:>9} {"*" if slow else " "}{r["interval_ms"]:>8.1f}  {", ".join(blocked) or "-"}{mark}')
    if hidden:
        print(f'{"":>9}            ({hidden} frames of the usual length)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('dump', type=Path, nargs='?', help='a dump folder, or a folder of them (default build/flight)')
    ap.add_argument('--around', type=int, default=20, help='frames before the trigger to list (default 20)')
    ap.add_argument('--slow', type=float, default=2.0, help='a frame is slow at this multiple of the quiet interval')
    a = ap.parse_args()

    base = a.dump or ROOT / 'build' / 'flight'
    path = base / 'frames.tsv'
    if not path.is_file():
        if not base.is_dir():
            sys.exit(f'error: {base} is not there. Run the game with TOTK_FLIGHT=1 TOTK_FLIGHT_THUMBS=0 and press F8.')
        found = newest_dump(base)
        if not found:
            sys.exit(f'error: no dump under {base} (a dump is a folder with a frames.tsv in it)')
        path = found / 'frames.tsv'
    head, rows = read(path)

    print(f'{path}')
    print(f'{head}')
    print(f'{len(rows)} frames recorded, {rows[0]["frame"]} to {rows[-1]["frame"]}\n')

    trig = max((int(w) for w in head.replace(',', ' ').split() if w.isdigit()), default=rows[-1]['frame'])
    intervals = sorted(column(rows, 'interval_ms'))
    quiet = statistics.median(intervals)
    slow = [r for r in rows if r['interval_ms'] > a.slow * quiet]
    calm = [r for r in rows if r['interval_ms'] <= a.slow * quiet]
    print(f'the usual frame here is {quiet:.1f} ms ({1000 / quiet:.0f} fps); {len(slow)} of {len(rows)} frames took '
          f'more than {a.slow * quiet:.1f} ms, the worst {max(intervals):.1f} ms\n')

    table(rows, trig, a.around, a.slow * quiet)

    if not slow:
        print('\nNo frame here is slow enough to explain a stutter: the trigger may have been F8 pressed too late,\n'
              'or the stutter is in the game\'s own timing rather than in a frame that took too long.')
        return
    print(f'\nWhat the {len(slow)} slow frames spent that the other {len(calm)} did not:')
    total = sum(r['interval_ms'] for r in slow) - len(slow) * quiet
    named = 0.0
    for name, what in BLOCKED:
        over = sum(r.get(name, 0) for r in slow) - len(slow) * (statistics.median(column(calm, name)) if calm else 0)
        if over >= 0.5:
            named += over
            print(f'  {over:>8.1f} ms  {name.removesuffix("_ms"):<7} {what}')
    print(f'  {max(total - named, 0):>8.1f} ms  rest    not waiting on anything: the work in the frame itself')
    print(f'  {total:>8.1f} ms  over {quiet:.1f} ms per frame, all {len(slow)} together')

    print('\nWork counted in those frames, against a quiet frame:')
    for name in COUNTS:
        if name not in rows[0]:
            continue
        hot = statistics.median(column(slow, name))
        cold = statistics.median(column(calm, name)) if calm else 0
        if hot or cold:
            arrow = '  <-- only in the slow frames' if hot and not cold else ''
            print(f'  {name:<18} {fmt(hot):>9} vs {fmt(cold):>9}{arrow}')


if __name__ == '__main__':
    main()
