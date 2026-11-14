#!/usr/bin/env python3
"""Extract the two blast-door leaves from the supplied G5x reference sheet.

The supplied image is a concept sheet: the large front view on the left is the
only resolution suitable for close-up use.  The two leaf crops exclude the
outer frame and header but retain the central interlock seam.  No upscaling or
procedural reconstruction is performed.
"""
from PIL import Image
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "doors" / "reference" / "blast-door-reference.png"
OUT = ROOT / "assets" / "doors"

# Large front view in the concept sheet.  Inner edges meet at x=660.
CROPS = {
    "left": (210, 145, 660, 930),
    "right": (660, 145, 1070, 930),
}


def load_source():
    im = Image.open(SRC).convert("RGB")
    if im.size != (1536, 1024):
        sys.exit(f"{SRC}: expected 1536x1024 concept sheet, got {im.size}")
    return im


def main():
    im = load_source()
    OUT.mkdir(parents=True, exist_ok=True)
    leaves = {}
    for name, box in CROPS.items():
        leaf = im.crop(box)
        leaf.save(OUT / f"blast-door-{name}.png")
        leaves[name] = leaf
        print(f"{name}: {leaf.size}")

    for name, leaf in leaves.items():
        raw = OUT / f"blast-door-{name}.rgb"
        raw.write_bytes(leaf.tobytes())
        print(f"{raw}: {raw.stat().st_size} bytes")
    print(f"sheet sha256={hashlib.sha256(SRC.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
