#!/usr/bin/env python3
"""Extract original 64x64 game textures from the supplied contact sheet.
The source sheet is preserved unchanged. Crops exclude labels, swatches,
example renders and outer panels where possible."""
from PIL import Image
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHEET = ROOT / "sprites" / "Glon_AvP_Industrial_Texture_Set.png"
OUT = ROOT / "sprites" / "textures"
HEADER = ROOT / "textures.h"

# Interior artwork boxes in the 1536x1024 contact sheet.
BOXES = {
    "wall_panel": (30, 95, 185, 250),
    "wall_service": (590, 95, 745, 250),
    "vent_grille": (410, 95, 565, 250),
    "rib": (980, 95, 1130, 250),
    "pipes": (1170, 95, 1325, 250),
    "floor": (30, 342, 185, 495),
    "ceiling_light": (790, 342, 940, 495),
    "ceiling_panel": (980, 342, 1130, 495),
    "door": (30, 589, 185, 730),
}

def main():
    im = Image.open(SHEET).convert("RGB")
    if im.size != (1536, 1024):
        sys.exit(f"unexpected contact sheet dimensions: {im.size}")
    OUT.mkdir(parents=True, exist_ok=True)
    arrays = {}
    for name, box in BOXES.items():
        crop = im.crop(box).resize((64, 64), Image.Resampling.NEAREST)
        crop.save(OUT / f"{name}.png")
        arrays[name] = list(crop.getdata())
    with open(HEADER, "w") as f:
        f.write("/* Generated from sprites/Glon_AvP_Industrial_Texture_Set.png. */\n")
        f.write("#ifndef GLON_AVP_TEXTURES_H\n#define GLON_AVP_TEXTURES_H\n#include <stdint.h>\n\n")
        for name, vals in arrays.items():
            f.write(f"static const uint32_t tex_{name}[64*64] = {{\n")
            for i in range(0, len(vals), 6):
                f.write("  " + ", ".join("0x%06xu" % ((r << 16) | (g << 8) | b)
                                         for r, g, b in vals[i:i+6]) + ",\n")
            f.write("};\n\n")
        f.write("#endif\n")
    print("wrote", len(arrays), "textures to", OUT)
    for name in arrays: print(name, "64x64")

if __name__ == "__main__":
    main()
