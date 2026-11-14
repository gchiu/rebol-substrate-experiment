#!/usr/bin/env python3
"""Verify the G5x2 high-resolution blast-door assets and crop geometry."""
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
    if name == "left":
        return (size[0] - width, 0, size[0], size[1])
    return (0, 0, width, size[1])


for name in ("left", "right"):
    src = SRC / f"door-{name}.png"
    digest = hashlib.sha256(src.read_bytes()).hexdigest()
    if digest != EXPECTED[name]:
        sys.exit(f"{src}: source artwork changed ({digest})")
    source = Image.open(src).convert("RGB")
    if source.size != SOURCE_SIZE:
        sys.exit(f"{src}: expected {SOURCE_SIZE}, got {source.size}")

    box = crop_box(name, source.size)
    expected = source.crop(box)
    png = OUT / f"g5x2-door-{name}.png"
    runtime = Image.open(png)
    if runtime.size != expected.size or runtime.mode != "RGB":
        sys.exit(f"{png}: expected RGB {expected.size}, got {runtime.size} {runtime.mode}")
    if runtime.tobytes() != expected.tobytes():
        sys.exit(f"{png}: does not match the aspect-preserving source crop")

    raw = OUT / f"g5x2-door-{name}.rgb"
    if raw.read_bytes() != expected.tobytes():
        sys.exit(f"{raw}: raw pixels do not match the cropped PNG")

print("g5x2 door: OK: two native 840x1470 seam-preserving leaves")
