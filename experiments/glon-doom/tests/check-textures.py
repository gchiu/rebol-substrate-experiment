#!/usr/bin/env python3
from PIL import Image
import hashlib, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
sheet = ROOT / "sprites" / "Glon_AvP_Industrial_Texture_Set.png"
assets = ROOT / "sprites" / "textures"
expected = "b677ed632f117d60a5cab21246af27db72a384df4b2a5d53d1529aec0dfde412"
h = hashlib.sha256(sheet.read_bytes()).hexdigest()
if h != expected:
    sys.exit("contact sheet changed: " + h)
names = ["wall_panel", "wall_service", "vent_grille", "rib", "pipes",
         "floor", "ceiling_light", "ceiling_panel", "door"]
for n in names:
    im = Image.open(assets / (n + ".png"))
    if im.size != (64, 64):
        sys.exit(f"{n}: expected 64x64, got {im.size}")
    if im.mode != "RGB":
        sys.exit(f"{n}: expected RGB, got {im.mode}")
print(f"textures: OK: {len(names)} assets, 64x64, sheet unchanged")
