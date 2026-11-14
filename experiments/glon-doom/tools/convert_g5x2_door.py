#!/usr/bin/env python3
"""Prepare the G5x2 high-resolution blast-door leaves.

The supplied G5x2 artwork is two independently generated 1070x1470 front views.
Each is a complete leaf, not a mirrored half of one sheet.  The existing leaf
face is 64 world units wide by 112 high (aspect 0.5714), while the source is
0.728, so the full image cannot be mapped without stretching.

Keep the full height and trim the outer edge to the face aspect:
  left  leaf -> keep the right (seam) edge
  right leaf -> keep the left  (seam) edge
No upscaling, retouching or procedural reconstruction is performed.
"""
from PIL import Image
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "doors" / "reference" / "g5x2"
OUT = ROOT / "assets" / "doors"

SOURCE_SIZE = (1070, 1470)
FACE_W, FACE_H = 64, 112
EXPECTED = {
    "left": "ae3aec0f51ad90f28aa4e3d85aa5839b7ed7afdb65b70e54879fe89d2658343d",
    "right": "53fcdc2e4f21447278b0092c1ea1388933a2ac5a85c12ca60c13d537f6f319e4",
}


def crop_box(name, size):
    width = round(size[1] * FACE_W / FACE_H)
    if width > size[0]:
        sys.exit(f"{name}: source is too narrow for the leaf aspect")
    if name == "left":
        return (size[0] - width, 0, size[0], size[1])
    return (0, 0, width, size[1])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ("left", "right"):
        src = SRC / f"door-{name}.png"
        digest = hashlib.sha256(src.read_bytes()).hexdigest()
        if digest != EXPECTED[name]:
            sys.exit(f"{src}: source artwork changed ({digest})")
        im = Image.open(src).convert("RGB")
        if im.size != SOURCE_SIZE:
            sys.exit(f"{src}: expected {SOURCE_SIZE}, got {im.size}")
        box = crop_box(name, im.size)
        leaf = im.crop(box)
        png = OUT / f"g5x2-door-{name}.png"
        leaf.save(png)
        raw = OUT / f"g5x2-door-{name}.rgb"
        raw.write_bytes(leaf.tobytes())
        print(f"{name}: source {im.size} crop {box} -> {leaf.size} "
              f"raw={raw.stat().st_size} bytes")


if __name__ == "__main__":
    main()
