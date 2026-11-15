#!/usr/bin/env python3
"""Extract scuttler animation frames from G6h contact sheet.

The supplied image is a labelled contact sheet, not an atlas.  We use the cell
grid only to isolate artwork, then derive an alpha mask from the near-black
background and fill enclosed body holes.  Frames are tightly cropped, scaled
into a 64x32 nominal sprite, and anchored at bottom-centre.  Turn rows are
manifested but not used by the runtime.
"""
from PIL import Image, ImageDraw, ImageFilter
from collections import deque
import json, os

SRC = 'assets/scuttlers/scuttler_sheet.png'
OUT = 'sprites/scuttlers_g6h'
HDR = 'scuttler_sprites_g6h.h'
W, H = 64, 32
PAD = 2

# Interior cell boundaries measured on the 1536x1024 contact sheet.
COLS = [340, 488, 635, 784, 933, 1082, 1231, 1381, 1530]
TURN_COLS = [255, 414, 573, 732, 891, 1050, 1209, 1368, 1527]
ATTACK_COLS = [255, 467, 679, 891, 1103, 1315, 1527]
HIT_COLS = [255, 567, 879, 1191, 1503]
DYING_COLS = [255, 467, 679, 891, 1103, 1315, 1527]
ROWS = {
    'front': (78, 145),
    'three_quarter_left': (145, 210),
    'side_left': (210, 272),
    'rear': (272, 335),
}
BANDS = {
    'turn_left': (345, 415, TURN_COLS),
    'turn_right': (420, 490, TURN_COLS),
    'attack': (495, 575, ATTACK_COLS),
    'hit': (580, 660, HIT_COLS),
    'dying': (665, 750, DYING_COLS),
}
DEAD_BOX = (180, 758, 410, 830)
CUTS = {'front': 8, 'three_quarter_left': 8, 'side_left': 8, 'rear': 8,
        'turn_left': 8, 'turn_right': 8, 'attack': 6, 'hit': 4,
        'dying': 6, 'dead': 1}

def alpha_mask(cell):
    """Segment bright/coloured creature from the near-black sheet background."""
    m = Image.new('L', cell.size, 0); p = cell.load(); q = m.load()
    for y in range(cell.height):
        for x in range(cell.width):
            if max(p[x, y]) > 10:
                q[x, y] = 255
    # Close single-pixel gaps between body/leg highlights.
    m = m.filter(ImageFilter.MaxFilter(3))
    # Do not fill large enclosed dark gaps: on the side frames those are
    # background between the legs, not body. The thresholded highlights and
    # body already provide the opaque silhouette.
    # Remove presentation grid lines: rows that are mostly opaque across the
    # crop are borders, not creature geometry.
    mp = m.load()
    for y in range(cell.height):
        if sum(1 for x in range(cell.width) if mp[x,y]) > cell.width*4//5:
            for x in range(cell.width): mp[x,y] = 0
    # Remove tiny disconnected specks, retaining components > 18 px.
    seen = set(); keep = Image.new('L', cell.size, 0); kp = keep.load()
    for y in range(cell.height):
        for x in range(cell.width):
            if mp[x,y] and (x,y) not in seen:
                qd = deque([(x,y)]); seen.add((x,y)); comp=[]
                while qd:
                    ax,ay = qd.popleft(); comp.append((ax,ay))
                    for nx,ny in ((ax-1,ay),(ax+1,ay),(ax,ay-1),(ax,ay+1)):
                        if 0 <= nx < cell.width and 0 <= ny < cell.height and mp[nx,ny] and (nx,ny) not in seen:
                            seen.add((nx,ny)); qd.append((nx,ny))
                if len(comp) >= 18:
                    cw = max(a for a,b in comp) - min(a for a,b in comp) + 1
                    ch = max(b for a,b in comp) - min(b for a,b in comp) + 1
                    # Drop presentation grid lines (wide, thin components).
                    if not (cw > cell.width*4//5 and ch < 7):
                        for ax,ay in comp: kp[ax,ay] = 255
    return keep

def extract_cell(cell):
    mask = alpha_mask(cell)
    bbox = mask.getbbox()
    if not bbox: raise RuntimeError('empty alpha cell')
    x0,y0,x1,y1 = bbox
    rgba = cell.convert('RGBA')
    rgba.putalpha(mask)
    crop = rgba.crop(bbox)
    # Fit inside a 64x32 frame with transparent padding; preserve aspect.
    aw,ah = crop.size
    scale = min((W-2*PAD)/aw, (H-2*PAD)/ah)
    nw,nh = max(1,round(aw*scale)), max(1,round(ah*scale))
    crop = crop.resize((nw,nh), Image.Resampling.LANCZOS)
    out = Image.new('RGBA',(W,H),(0,0,0,0))
    ox=(W-nw)//2; oy=H-nh-PAD
    out.alpha_composite(crop,(ox,oy))
    # Remove resampling haze outside the mask; keep a clean transparent field.
    op=out.load()
    for yy in range(H):
        for xx in range(W):
            r,g,b,a=op[xx,yy]
            if a < 8: op[xx,yy]=(0,0,0,0)
    return out,(x0,y0,x1-x0,y1-y0),(ox,oy,nw,nh)

def main():
    os.makedirs(OUT,exist_ok=True)
    im=Image.open(SRC).convert('RGB')
    frames=[]; manifest=[]
    for name,(ya,yb) in ROWS.items():
        for i in range(8):
            box=(COLS[i]+4,ya+4,COLS[i+1]-3,yb-3)
            out,srcbox,dst=extract_cell(im.crop(box))
            fn=f'{name}_{i}.png'; out.save(os.path.join(OUT,fn))
            frames.append((fn,out)); manifest.append(dict(name=fn,animation=name,index=i,direction=name,
                source=dict(x=box[0],y=box[1],w=box[2]-box[0],h=box[3]-box[1]),
                source_alpha_bbox=dict(x=srcbox[0]+box[0],y=srcbox[1]+box[1],w=srcbox[2],h=srcbox[3]),
                output=dict(x=dst[0],y=dst[1],w=dst[2],h=dst[3],frame_w=W,frame_h=H),
                anchor=dict(x=W//2,y=H-PAD), alpha='RGBA; transparent outside extracted creature'))
    for name,(ya,yb,cols) in BANDS.items():
        count=CUTS[name]
        for i in range(count):
            box=(cols[i]+4,ya+4,cols[i+1]-3,yb-3)
            out,srcbox,dst=extract_cell(im.crop(box))
            fn=f'{name}_{i}.png'; out.save(os.path.join(OUT,fn))
            frames.append((fn,out)); manifest.append(dict(name=fn,animation=name,index=i,direction='front',
                source=dict(x=box[0],y=box[1],w=box[2]-box[0],h=box[3]-box[1]),
                source_alpha_bbox=dict(x=srcbox[0]+box[0],y=srcbox[1]+box[1],w=srcbox[2],h=srcbox[3]),
                output=dict(x=dst[0],y=dst[1],w=dst[2],h=dst[3],frame_w=W,frame_h=H),
                anchor=dict(x=W//2,y=H-PAD), alpha='RGBA; transparent outside extracted creature'))
    # Static dead frame is not in a normal cell.
    box=(DEAD_BOX[0]+4,DEAD_BOX[1]+4,DEAD_BOX[2]-3,DEAD_BOX[3]-3); out,srcbox,dst=extract_cell(im.crop(box))
    fn='dead_0.png'; out.save(os.path.join(OUT,fn)); frames.append((fn,out))
    manifest.append(dict(name=fn,animation='dead',index=0,direction='front',
        source=dict(x=box[0],y=box[1],w=box[2]-box[0],h=box[3]-box[1]),
        source_alpha_bbox=dict(x=srcbox[0]+box[0],y=srcbox[1]+box[1],w=srcbox[2],h=srcbox[3]),
        output=dict(x=dst[0],y=dst[1],w=dst[2],h=dst[3],frame_w=W,frame_h=H),
        anchor=dict(x=W//2,y=H-PAD), alpha='RGBA; transparent outside extracted creature'))
    # Runtime-used frames only: 4 x scuttle + attack/hit/dying/dead.
    runtime=[]
    for name in ['front','three_quarter_left','side_left','rear']:
        runtime += [f'{name}_{i}.png' for i in range(8)]
    for name,count in [('attack',6),('hit',4),('dying',6),('dead',1)]:
        runtime += [f'{name}_{i}.png' for i in range(count)]
    byname=dict(frames)
    with open(HDR,'w') as f:
        f.write('/* Generated by tools/extract_scuttler_g6h.py; 64x32 RGBA extracted frames. */\n')
        f.write('#ifndef SCUTTLER_SPRITES_G6H_H\n#define SCUTTLER_SPRITES_G6H_H\n#include <stdint.h>\n')
        f.write('#define SCUTTLER_FRAME_W 64\n#define SCUTTLER_FRAME_H 32\n#define SCUTTLER_FRONT_BASE 0\n#define SCUTTLER_THREEQ_BASE 8\n#define SCUTTLER_SIDE_BASE 16\n#define SCUTTLER_REAR_BASE 24\n#define SCUTTLER_ATTACK_BASE 32\n#define SCUTTLER_HIT_BASE 38\n#define SCUTTLER_DYING_BASE 42\n#define SCUTTLER_DEAD_FRAME 48\n#define SCUTTLER_FRAME_COUNT 49\n')
        f.write('static const uint32_t scuttler_pixels[SCUTTLER_FRAME_COUNT][SCUTTLER_FRAME_W*SCUTTLER_FRAME_H] = {\n')
        for fn in runtime:
            img=byname[fn]; px=img.load(); f.write('  {\n')
            for y in range(H):
                f.write('    ')
                for x in range(W):
                    r,g,b,a=px[x,y]; f.write(f'0x{a:02x}{r:02x}{g:02x}{b:02x}u,')
                f.write('\n')
            f.write('  },\n')
        f.write('};\n#endif\n')
    with open(os.path.join(OUT,'manifest.json'),'w') as f: json.dump(manifest,f,indent=2)
    # Contact sheet of extracted runtime frames for visual QA.
    cols=8; rows=(len(runtime)+cols-1)//cols
    sheet=Image.new('RGB',(cols*W,rows*H),(24,26,30)); d=ImageDraw.Draw(sheet)
    for i,fn in enumerate(runtime):
        img=byname[fn]; x=(i%cols)*W; y=(i//cols)*H
        sheet.paste(img,(x,y),img); d.text((x+2,y+1),fn,fill=(255,255,255))
    sheet.save('/tmp/scuttler_extracted_contact.png')
    print(f'extracted {len(frames)} frames, runtime {len(runtime)}, wrote {HDR} and manifest')

if __name__=='__main__': main()
