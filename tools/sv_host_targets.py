"""Make Violet 4.0.0 host render-target patches without changing its GPU pool.

The MIT patch instructions are in tools/data/violet_host_targets_v400.txt.
Original game instruction words are read only from the player's update,
verified by a digest, and written only to the ignored build patch table.
"""

import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "recomp"), str(ROOT / "tools")]
from layout import layout  # noqa: E402
from module import Module  # noqa: E402

POOL_SITE = 0x146E770
ORIGINAL_SITES_SHA256 = "3539aa93c542160fcc698ad7b848ea7a5fd150a434a1a0942f299dd8f130bf9e"
CAMERA_SITES_SHA256 = "51c8b2a648de7e53de45f5c7ac017417d0c19f7adee9737d0683d9840ecc2efd"
CAMERA_SITES = ((0xCF11E8, "@pagebl:fa0"), (0xCF18A4, "@pagebl:fb0"))
FOV_SITES = (0xCF1168, 0xCF182C)


def main():
    source = ROOT / "tools" / "data" / "violet_host_targets_v400.txt"
    baseline = ROOT / "build" / "fps_patches.txt"
    output = ROOT / "build" / "sv_host_targets.txt"
    supported = ROOT / "build" / "svhost_supported.txt"
    exefs = ROOT / "game" / "extracted" / "update" / "exefs"
    for path in (source, baseline, exefs / "main"):
        if not path.is_file():
            sys.exit(f"host targets: missing {path}")
    rows = []
    for line in source.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            off, patch = line.split(maxsplit=1)
            rows.append((int(off, 16), patch))
    if len(rows) != 29 or len({off for off, _ in rows}) != 29 or POOL_SITE in dict(rows):
        sys.exit("host targets: expected 29 unique image-size sites and no pool-size site")
    base = next(addr for name, addr, _ in layout(exefs) if name == "main")
    module = Module("main", exefs / "main", base, "c")
    words = [(off, module.word(base + off)) for off, _ in rows]
    words.append((POOL_SITE, module.word(base + POOL_SITE)))
    digest = hashlib.sha256(b"".join(struct.pack("<II", off, word) for off, word in words)).hexdigest()
    camera = [(off, module.word(base + off)) for off in sorted((*FOV_SITES, *(off for off, _ in CAMERA_SITES)))]
    camera_digest = hashlib.sha256(b"".join(struct.pack("<II", off, word) for off, word in camera)).hexdigest()
    original = baseline.read_text(encoding="utf-8")
    if "set svhost " in original:
        sys.exit("host targets: baseline patch table is not clean")
    if digest != ORIGINAL_SITES_SHA256 or camera_digest != CAMERA_SITES_SHA256:
        output.write_text(original, encoding="utf-8")
        supported.write_text("0\n", encoding="utf-8")
        print("host targets: update does not match Violet 4.0.0; ordinary patches retained, render scale unavailable")
        return
    expected = dict(words)
    selected = [f"site svhost main {off:x} {expected[off]:08x} {patch}" for off, patch in rows]
    camera_words = dict(camera)
    camera_sites = [f"site svwide main {off:x} {camera_words[off]:08x} {camera_words[off]:08x} {patch}"
                    for off, patch in CAMERA_SITES]
    # ADD x19, x19, #0 leaves the camera unchanged but makes the lifter sync
    # its local x19 into Cpu before the host call, which reads the camera aspect.
    fov_sites = [f"site svtall main {off:x} {camera_words[off]:08x} {camera_words[off]:08x} 91000273 @pagebl:f80"
                 for off in FOV_SITES]
    output.write_text(original + "\n# Host-owned Violet render targets; Tera-recomp ca4e2bd (MIT).\n" +
                      "set svhost TOTK_SVHOST_TEST 0\n" + "\n".join(selected) + "\n" +
                      "# Perspective camera aspect and tall-output FOV; PokeV-Recomp PR 14 (MIT).\n" +
                      "set svwide TOTK_SVWIDE 0\n" + "\n".join(camera_sites) + "\n" +
                      "set svtall TOTK_SVTALL 0\n" + "\n".join(fov_sites) + "\n", encoding="utf-8")
    supported.write_text("1\n", encoding="utf-8")
    print("Violet 4.0.0 sites verified: 34/34")
    print("host targets: wrote 29 image-size and 4 camera sites; 0 pool-size and 0 shadow sites")


if __name__ == "__main__":
    main()
