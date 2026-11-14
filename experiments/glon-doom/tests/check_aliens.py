#!/usr/bin/env python3
"""Validate generated Alien sprite assets: clean transparency and usable alpha."""
from PIL import Image
import pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
NAMES = ["idle", "walk1", "walk2", "attack", "side", "death"]
fail = 0
for name in NAMES:
    p = ROOT / "sprites" / "aliens_g5m" / f"{name}.png"
    im = Image.open(p)
    ok = im.size == (128, 192) and im.mode == "RGBA"
    a = im.getchannel("A")
    corners = [a.getpixel(xy) for xy in ((0,0),(im.width-1,0),(0,im.height-1),(im.width-1,im.height-1))]
    hist = a.histogram()
    opaque = hist[255]
    transparent = hist[0]
    ok = ok and corners == [0,0,0,0] and opaque > 400 and transparent > 1000
    print(f"{'OK' if ok else 'FAIL'} {name}: {im.size} {im.mode} opaque={opaque} transparent={transparent} corners={corners}")
    if not ok: fail += 1
sys.exit(1 if fail else 0)
