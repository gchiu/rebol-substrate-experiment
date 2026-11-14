#!/usr/bin/env python3
from PIL import Image
import hashlib, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
src = ROOT / "assets" / "doors" / "reference" / "blast-door-reference.png"
expected = "2c690cf91294b8124a30bffc85a4227d7d7d8b20de8c86050baf67f3ce879a96"
if hashlib.sha256(src.read_bytes()).hexdigest() != expected:
    sys.exit("G5x blast-door reference sheet changed")
for name, size in (("left", (450, 785)), ("right", (410, 785))):
    im = Image.open(ROOT / "assets" / "doors" / f"blast-door-{name}.png")
    if im.size != size or im.mode != "RGB":
        sys.exit(f"blast-door-{name}: expected RGB {size}, got {im.size} {im.mode}")
for name, size in (("left", (450, 785)), ("right", (410, 785))):
    raw = ROOT / "assets" / "doors" / f"blast-door-{name}.rgb"
    if raw.stat().st_size != size[0]*size[1]*3:
        sys.exit(f"{raw}: wrong raw size")
print("g5x door: OK: reference sheet unchanged, two native-resolution leaves")
