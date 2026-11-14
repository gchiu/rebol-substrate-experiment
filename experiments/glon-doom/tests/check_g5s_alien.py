#!/usr/bin/env python3
"""Validate the G5s native-resolution directional Alien crops."""
from PIL import Image
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NAMES = ["idle", "front1", "front2", "threeq", "side", "rear", "attack"]
fail = 0
for name in NAMES:
    p = ROOT / "assets" / "alien" / "directional" / "extracted" / f"{name}.png"
    im = Image.open(p)
    ok = im.mode == "RGBA"
    a = im.getchannel("A")
    corners = [a.getpixel(xy) for xy in
               ((0, 0), (im.width - 1, 0), (0, im.height - 1), (im.width - 1, im.height - 1))]
    hist = a.histogram()
    opaque = hist[255] + sum(hist[65:255])
    clean_low = sum(hist[1:40]) == 0
    ok = ok and corners == [0, 0, 0, 0] and opaque > 3000 and clean_low
    ok = ok and im.width > 100 and im.height > 150
    print(f"{'OK' if ok else 'FAIL'} {name}: {im.size} {im.mode} "
          f"lit_alpha={opaque} low_alpha={sum(hist[1:40])} corners={corners}")
    if not ok:
        fail += 1

hdr = (ROOT / "alien_sprites_g5s.h").read_text()
for macro in ("ALIEN_DIR_COUNT 7", "alien_dir_eff_h", "alien_dir_anchor_y",
              "alien_dir_pivot_x", "alien_dir_w", "alien_dir_h"):
    if macro not in hdr:
        print(f"FAIL header missing {macro}")
        fail += 1

src_rects = re.findall(r"source \((\d+, \d+, \d+, \d+)\)", hdr)
if len(src_rects) != len(NAMES):
    print(f"FAIL expected {len(NAMES)} source rectangles, found {len(src_rects)}")
    fail += 1
else:
    print(f"OK header source rectangles: {len(src_rects)}")

sys.exit(1 if fail else 0)
