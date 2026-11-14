#!/usr/bin/env python3
"""Convert selected poses from the G5q reference sheet into runtime sprites.

The supplied sheet is a labelled concept sheet, not a fixed-grid atlas.  Crop
coordinates below isolate five already-approved poses; labels and the info box
are excluded.  All poses share one scale, a bottom-foot baseline and a foot
pivot.  This is a reference-to-sprite conversion, not procedural creature art.
"""
from PIL import Image
import os

SRC = 'assets/alien/alien-animation-reference.png'
OUTDIR = 'assets/alien/frames'
HEADER = 'alien_sprites_g5q.h'
W, H = 192, 144
MARGIN = 6
# name, source crop (x0,y0,x1,y1); selected from isolated alpha components
CROPS = {
    'idle':   (845, 10, 1053, 165),
    'walk1':  (139, 186, 307, 319),
    'walk2':  (480, 186, 641, 319),
    'attack': (618, 500, 770, 643),
    'side':   (811, 186, 967, 319),
}

def alpha_bbox(im, threshold=8):
    a = im.getchannel('A')
    return a.point(lambda p: 255 if p > threshold else 0).getbbox()

def foot_pivot(im):
    """Alpha-weighted x centroid of the bottom 7 opaque rows."""
    a = im.getchannel('A')
    w, h = im.size
    rows = []
    for y in range(h - 1, -1, -1):
        xs = [(x, a.getpixel((x, y))) for x in range(w) if a.getpixel((x, y)) > 32]
        if xs:
            rows.append(xs)
        if len(rows) == 7:
            break
    if not rows:
        return w / 2.0
    total = sum(p for row in rows for _, p in row)
    return sum(x * p for row in rows for x, p in row) / total

def premultiply_resize(im, size):
    """Resize RGBA in premultiplied space to avoid dark transparent fringes."""
    w, h = im.size
    pre = Image.new('RGBA', (w, h))
    src, dst = im.load(), pre.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = src[x, y]
            dst[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    pre = pre.resize(size, Image.Resampling.LANCZOS)
    out = Image.new('RGBA', size)
    src, dst = pre.load(), out.load()
    for y in range(size[1]):
        for x in range(size[0]):
            r, g, b, a = src[x, y]
            if a:
                dst[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
            else:
                dst[x, y] = (0, 0, 0, 0)
    return out

def main():
    sheet = Image.open(SRC).convert('RGBA')
    crops = {}
    for name, box in CROPS.items():
        im = sheet.crop(box)
        bb = alpha_bbox(im)
        if not bb:
            raise SystemExit(f'{name}: empty alpha crop')
        crops[name] = im.crop(bb)
    maxw = max(im.width for im in crops.values())
    maxh = max(im.height for im in crops.values())
    scale = min((W - 2 * MARGIN) / maxw, (H - 2 * MARGIN) / maxh)
    os.makedirs(OUTDIR, exist_ok=True)
    resized = {}
    for name, im in crops.items():
        nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
        resized[name] = premultiply_resize(im, (nw, nh))
    # common baseline and foot pivot placement
    frames = {}
    for name, im in resized.items():
        fx = foot_pivot(crops[name]) * scale
        x = round(W / 2 - fx)
        x = max(MARGIN, min(x, W - MARGIN - im.width))
        y = H - MARGIN - im.height
        canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        canvas.alpha_composite(im, (x, y))
        frames[name] = canvas
        canvas.save(f'{OUTDIR}/{name}.png')
    # runtime ARGB header
    with open(HEADER, 'w') as f:
        f.write('/* Generated from the supplied G5q Alien reference sheet by tools/convert_g5q_alien.py. */\n')
        f.write('#ifndef GLON_AVP_ALIEN_SPRITES_G5Q_H\n#define GLON_AVP_ALIEN_SPRITES_G5Q_H\n#include <stdint.h>\n')
        f.write(f'#define ALIEN_W {W}\n#define ALIEN_H {H}\n\n')
        for name, im in frames.items():
            f.write(f'static const uint32_t alien_{name}[ALIEN_W*ALIEN_H] = {{\n')
            px = im.load()
            for y in range(H):
                vals = []
                for x in range(W):
                    r, g, b, a = px[x, y]
                    vals.append(f'0x{a:02x}{r:02x}{g:02x}{b:02x}u')
                f.write('  ' + ', '.join(vals) + ',\n')
            f.write('};\n\n')
        f.write('#endif\n')
    print(f'wrote {HEADER} and {len(frames)} frames to {OUTDIR}; scale={scale:.4f}')

if __name__ == '__main__':
    main()
