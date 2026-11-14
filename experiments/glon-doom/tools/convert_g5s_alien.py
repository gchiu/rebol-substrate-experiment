#!/usr/bin/env python3
"""Extract the G5s directional Alien set at native source resolution.

Source sheets are reference contact sheets, not clean atlases. Rectangles below
are the alpha-component bounds measured at alpha > 64. The script keeps every
selected pose at 1:1, removes low-alpha background noise, and records the foot
baseline / pivot. It never resamples a pose.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "assets/alien/directional/glon-alien-g5s-pack"
OUT = ROOT / "assets/alien/directional/extracted"
HEADER = ROOT / "alien_sprites_g5s.h"

# role, PNG, sheet, source rectangle (left, top, right, bottom), source label
POSES = [
    ("idle",    "alien-directional-sheet-a.png", (48,   8,  181,  196), "sheet A frontal idle"),
    ("front1",  "alien-directional-sheet-b.png", (911,  11, 1047, 234), "sheet B frontal approach A"),
    ("front2",  "alien-directional-sheet-b.png", (1234, 13, 1365, 230), "sheet B frontal approach B"),
    ("threeq",  "alien-directional-sheet-b.png", (287,  255, 504, 469), "sheet B three-quarter"),
    ("side",    "alien-directional-sheet-b.png", (288,  501, 542, 690), "sheet B right-facing side"),
    ("rear",    "alien-directional-sheet-b.png", (179,  732, 318, 1000), "sheet B rear"),
    ("attack",  "alien-directional-sheet-b.png", (1090, 737, 1299, 986), "sheet B frontal attack"),
]

ALPHA_FLOOR = 40
MARGIN = 2


def segment(sheet):
    """Label alpha-connected poses so a crop cannot include a neighbouring tail."""
    w, h = sheet.size
    px = sheet.load()
    labels = [0] * (w * h)
    comps = [None]
    for y in range(h):
        for x in range(w):
            idx = y * w + x
            if labels[idx] or px[x, y][3] <= 64:
                continue
            cid = len(comps)
            stack = [(x, y)]
            labels[idx] = cid
            minx = maxx = x
            miny = maxy = y
            count = 0
            while stack:
                cx, cy = stack.pop()
                count += 1
                if cx < minx: minx = cx
                if cx > maxx: maxx = cx
                if cy < miny: miny = cy
                if cy > maxy: maxy = cy
                for nx in (cx - 1, cx, cx + 1):
                    if nx < 0 or nx >= w:
                        continue
                    for ny in (cy - 1, cy, cy + 1):
                        if ny < 0 or ny >= h:
                            continue
                        ni = ny * w + nx
                        if not labels[ni] and px[nx, ny][3] > 64:
                            labels[ni] = cid
                            stack.append((nx, ny))
            comps.append((count, (minx, miny, maxx + 1, maxy + 1)))
    return labels, comps


def find_component(comps, rect):
    for cid in range(1, len(comps)):
        count, bbox = comps[cid]
        if count < 200:
            continue
        if all(abs(bbox[i] - rect[i]) <= 2 for i in range(4)):
            return cid
    raise RuntimeError(f"component not found for {rect}")


def clean_crop(sheet, labels, cid, crop_box):
    x0, y0, x1, y1 = crop_box
    sw = sheet.width
    im = sheet.crop(crop_box).convert("RGBA")
    px = im.load()
    # Keep the selected component and its 1-pixel antialias fringe only.
    keep = set()
    for ly in range(y0 - 1, y1 + 1):
        for lx in range(x0 - 1, x1 + 1):
            if 0 <= lx < sheet.width and 0 <= ly < sheet.height and labels[ly * sw + lx] == cid:
                for yy in range(max(y0, ly - 1), min(y1 - 1, ly + 1) + 1):
                    for xx in range(max(x0, lx - 1), min(x1 - 1, lx + 1) + 1):
                        keep.add((xx - x0, yy - y0))
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < ALPHA_FLOOR or (x, y) not in keep:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = (r, g, b, a)
    return im


def measure(sheet, labels, comps, rect):
    left, top, right, bottom = rect
    cid = find_component(comps, rect)
    crop = clean_crop(sheet, labels, cid, rect)
    px = crop.load()
    w, h = crop.size
    # Feet baseline: central 50% of the component, excluding an extended tail.
    cx0, cx1 = int(w * 0.25), int((w * 0.75) + 0.999)
    ground = max(y for y in range(h) for x in range(cx0, cx1) if px[x, y][3] > 64)
    px0, px1 = int(w * 0.20), int((w * 0.80) + 0.999)
    xs = [x for y in range(max(0, ground - 8), ground + 1)
          for x in range(px0, px1) if px[x, y][3] > 64]
    if not xs:
        raise RuntimeError(f"no foot pixels in {rect}")
    pivot = sum(xs) / len(xs)
    # Component bounds and output crop include a small antialias margin.
    bbox = crop.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError(f"empty component in {rect}")
    x0 = max(0, left + bbox[0] - MARGIN)
    y0 = max(0, top + bbox[1] - MARGIN)
    x1 = min(sheet.width, left + bbox[2] + MARGIN)
    y1 = min(sheet.height, top + bbox[3] + MARGIN)
    out = clean_crop(sheet, labels, cid, (x0, y0, x1, y1))
    return {
        "rect": (left, top, right, bottom),
        "bbox": (left + bbox[0], top + bbox[1], left + bbox[2], top + bbox[3]),
        "crop": (x0, y0, x1, y1),
        "image": out,
        "w": out.width,
        "h": out.height,
        "effective_w": bbox[2] - bbox[0],
        "effective_h": bbox[3] - bbox[1],
        "pivot_x": pivot - bbox[0] + (left + bbox[0] - x0),
        "anchor_y": ground - bbox[1] + (top + bbox[1] - y0) + 1,
        "source_ground_y": top + ground,
        "source_bottom_y": top + bbox[3],
    }


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheets = {}
    segs = {}
    rows = []
    for role, png, rect, label in POSES:
        if png not in sheets:
            sheets[png] = Image.open(PACK / png).convert("RGBA")
            segs[png] = segment(sheets[png])
        labels, comps = segs[png]
        m = measure(sheets[png], labels, comps, rect)
        path = OUT / f"{role}.png"
        m["image"].save(path)
        rows.append((role, png, label, m))
        print(f"{role:8s} src={m['rect']} effective={m['effective_w']}x{m['effective_h']} "
              f"crop={m['crop']} out={m['w']}x{m['h']} pivot={m['pivot_x']:.1f} "
              f"anchor={m['anchor_y']}")

    lines = []
    lines.append("/* Generated by tools/convert_g5s_alien.py. Native-resolution crops. */")
    lines.append("#ifndef GLON_AVP_ALIEN_SPRITES_G5S_H")
    lines.append("#define GLON_AVP_ALIEN_SPRITES_G5S_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    for i, (role, _, _, m) in enumerate(rows):
        lines.append(f"#define ALIEN_DIR_{role.upper()} {i}")
    lines.append(f"#define ALIEN_DIR_COUNT {len(rows)}")
    lines.append("")
    lines.append("static const int alien_dir_w[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(m["w"]) for _, _, _, m in rows))
    lines.append("};")
    lines.append("static const int alien_dir_h[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(m["h"]) for _, _, _, m in rows))
    lines.append("};")
    lines.append("static const int alien_dir_pivot_x[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(round(m["pivot_x"])) for _, _, _, m in rows))
    lines.append("};")
    lines.append("static const int alien_dir_anchor_y[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(m["anchor_y"]) for _, _, _, m in rows))
    lines.append("};")
    lines.append("static const int alien_dir_eff_w[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(m["effective_w"]) for _, _, _, m in rows))
    lines.append("};")
    lines.append("static const int alien_dir_eff_h[ALIEN_DIR_COUNT] = {")
    lines.append("    " + ", ".join(str(m["effective_h"]) for _, _, _, m in rows))
    lines.append("};")
    lines.append("")
    for role, png, label, m in rows:
        lines.append(f"/* {label}; source {m['rect']}; effective {m['effective_w']}x{m['effective_h']}; "
                     f"output {m['w']}x{m['h']}; source sheet {png}. */")
        lines.append(f"static const uint32_t alien_dir_{role}[{m['w']*m['h']}] = {{")
        vals = []
        for y in range(m["h"]):
            for x in range(m["w"]):
                r, g, b, a = m["image"].getpixel((x, y))
                vals.append(f"0x{a:02x}{r:02x}{g:02x}{b:02x}u")
            lines.append("    " + ", ".join(vals) + ("," if y != m["h"] - 1 else ""))
            vals = []
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    HEADER.write_text("\n".join(lines) + "\n")
    print(f"wrote {HEADER} ({HEADER.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
