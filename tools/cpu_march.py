"""The -march level the game is built for: x86-64-v3 (AVX2, FMA, BMI2) when the CPU of this PC has it, else native.

The build runs on the player's own PC, so the code may use what this CPU offers. A CPU without AVX2/FMA/BMI2 (older
Intel Core up to Ivy Bridge, Pentium / Celeron / Atom, some virtual machines) gets -march=native: whatever that CPU has
(often AVX and F16C), at least x86-64-v2. The FMADD family then runs as a float product in double (runtime/simd.h KFMA),
not as the C library's software fma (issue #15: 1.5-16 FPS). KARPUR_MARCH=x86-64-v2 (or another -march value) forces a
level. The answer is clang's own view of the host (-march=native's predefined macros), so it needs the compiler.
"""

import os
import subprocess


def march(clang):
    forced = os.environ.get('KARPUR_MARCH')
    if forced:
        return forced
    if os.environ.get('KARPUR_PORTABLE_ZIG') == '1' and os.name != 'nt':
        try:
            flags = next(line.split(':', 1)[1].split() for line in open('/proc/cpuinfo') if line.startswith('flags'))
            return 'x86-64-v3' if {'avx2', 'fma', 'bmi2'} <= set(flags) else 'x86-64-v2'
        except (OSError, StopIteration):
            return 'x86-64-v2'
    try:
        out = subprocess.run([str(clang), '-march=native', '-E', '-dM', '-x', 'c', os.devnull],
                             capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return 'x86-64-v3'   # no answer: the level every build so far used
    have = {line.split()[1] for line in out.splitlines() if line.startswith('#define __')}
    if {'__AVX2__', '__FMA__', '__BMI2__'} <= have:
        return 'x86-64-v3'
    return 'native' if {'__SSE4_2__', '__POPCNT__'} <= have else 'x86-64-v2'
