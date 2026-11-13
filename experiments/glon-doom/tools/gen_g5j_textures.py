#!/usr/bin/env python3
"""Generate two original seamless 64x64 industrial wall textures for G5j."""
from PIL import Image
import pathlib, math
ROOT=pathlib.Path(__file__).resolve().parent.parent
OUT=ROOT/'sprites'/'textures_g5j'; OUT.mkdir(parents=True,exist_ok=True)
HEADER=ROOT/'g5j_textures.h'

def clamp(v): return max(0,min(255,int(v)))
def rgb(r,g,b): return (clamp(r),clamp(g),clamp(b))
def h(x,y,s=0):
    n=(x*374761393+y*668265263+s*69069+0x9e3779b9)&0xffffffff
    n=(n^(n>>13))*1274126177&0xffffffff
    return (n^(n>>16))&255
def moddist(a,b,n): d=abs(a-b); return min(d,n-d)

def panel(x,y):
    # seamless panel: seams at the tile border, inset plate, four bolts, subtle vent
    n=h(x,y,11)%7-3
    r,g,b=74+n,83+n,90+n
    # outer seam and inner bevel (same on opposite edges)
    if x<2 or y<2 or x>61 or y>61: r,g,b=30,37,43
    elif x<4 or y<4 or x>59 or y>59: r,g,b=112,120,124
    # central recessed plate, edges toroidal
    if 7<=x<=56 and 9<=y<=54:
        r,g,b=67+n,77+n,84+n
        if x in (7,8,55,56) or y in (9,10,53,54): r,g,b=40,48,54
    # four rivets, edge-distance aware (positions safely interior)
    for cx,cy in ((12,14),(51,14),(12,49),(51,49)):
        if (x-cx)**2+(y-cy)**2<=2: r,g,b=146,152,151
    # vent slots, horizontal and seamless within plate
    if 18<=x<=45 and 30<=y<=40:
        r,g,b=48,57,63
        if y%5 in (0,1) and 20<=x<=43: r,g,b=19,25,30
        if y in (30,40) and x in (18,45): r,g,b=98,108,110
    # faint vertical brushed lines, toroidal
    if h(x,y,23)%23==0: r,g,b=r+11,g+10,b+7
    # yellow service stripe segment, away from tile seam
    if 8<=x<=15 and 20<=y<=44: r,g,b=134,111,49
    if 9<=x<=14 and 25<=y<=39: r,g,b=184,157,73
    return rgb(r,g,b)

def service(x,y):
    # seamless ribbed/piped service wall
    n=h(x,y,29)%6-3
    r,g,b=63+n,71+n,79+n
    # repeating vertical ribs with seamless wrap
    # rib is centered on the tile seam: x=63 and x=0 are both dark
    if x in (0,1,62,63): r,g,b=28,35,41
    elif x in (2,3,60,61): r,g,b=105,113,117
    elif x in (8,9,54,55): r,g,b=47,56,63
    # horizontal pipe bands
    if 12<=y<=20 or 43<=y<=51:
        r,g,b=48,57,64
        if y in (12,20,43,51): r,g,b=112,120,123
        if y in (15,18,46,49): r,g,b=29,36,42
    # panel fasteners near vertical seams
    for cx,cy in ((5,28),(21,28),(37,28),(53,28),(5,57),(21,57),(37,57),(53,57)):
        if (x-cx)**2+(y-cy)**2<=2: r,g,b=136,145,146
    # subtle amber indicator, one tile cell only
    if 24<=x<=30 and 32<=y<=36: r,g,b=188,137,48
    # low-frequency wear, toroidal
    if h(x,y,31)%41==0: r,g,b=r-12,g-10,b-8
    return rgb(r,g,b)

def make(fn,name):
    im=Image.new('RGB',(64,64))
    for y in range(64):
        for x in range(64): im.putpixel((x,y),fn(x,y))
    im.save(OUT/f'{name}.png')
    return list(im.getdata())

def main():
    a=make(panel,'panel_v2'); b=make(service,'service_v2')
    with open(HEADER,'w') as f:
        f.write('/* Original seamless G5j textures; no contact-sheet crops. */\n')
        f.write('#ifndef GLON_AVP_G5J_TEXTURES_H\n#define GLON_AVP_G5J_TEXTURES_H\n#include <stdint.h>\n\n')
        for name,vals in (('panel_v2',a),('service_v2',b)):
            f.write(f'static const uint32_t tex_{name}[64*64] = {{\n')
            for i in range(0,len(vals),6):
                f.write('  '+', '.join('0x%06xu'%((r<<16)|(g<<8)|b) for r,g,b in vals[i:i+6])+',\n')
            f.write('};\n\n')
        f.write('#endif\n')
    print('wrote panel_v2.png service_v2.png and g5j_textures.h')
if __name__=='__main__': main()
