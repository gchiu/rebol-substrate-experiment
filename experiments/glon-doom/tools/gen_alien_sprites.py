#!/usr/bin/env python3
"""Generate original 96x128 transparent Alien sprite frames.
The supplied sprites/Glon_AvP_Industrial_Texture_Set.png is an environment
contact sheet with no Alien art, so these are original pixel-art assets in the
same restrained industrial palette. No labels/backgrounds/borders are kept.
"""
from PIL import Image, ImageDraw
import pathlib

W, H, S = 96, 128, 4
OUT_W, OUT_H = 64, 128
ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "sprites" / "aliens"
HEADER = ROOT / "alien_sprites.h"

OUTLINE=(6,9,7,255); DEEP=(13,20,14,255); DARK=(25,37,28,255)
BODY=(39,56,42,255); MID=(56,78,56,255); HI=(92,122,82,255)
BONE=(166,175,148,255); TEETH=(222,222,188,255); MOUTH=(97,20,28,255)
EYE=(214,207,76,255); EYE_HI=(246,242,164,255)

def p(v): return tuple(int(x*S) for x in v)
def poly(d, pts, fill, outline=OUTLINE, width=2):
    d.polygon([(int(x*S),int(y*S)) for x,y in pts], fill=fill, outline=outline, width=int(width*S))
def line(d, pts, fill, width=2):
    d.line([(int(x*S),int(y*S)) for x,y in pts], fill=fill, width=int(width*S), joint="curve")
def ell(d, box, fill, outline=OUTLINE, width=2):
    d.ellipse([int(v*S) for v in box], fill=fill, outline=outline, width=int(width*S))

def base(d, walk=0, attack=False, side=False):
    # tail: heavy segmented curve behind the body
    if side:
        tail=[(52,82),(67,88),(80,96),(82,110),(75,116),(68,108),(72,101),(78,101)]
    else:
        tail=[(44,82),(30,88),(17,96),(10,108),(16,116),(25,111),(27,102),(20,100)]
    if walk==1: tail=[(x-4 if x<50 else x+4,y) for x,y in tail]
    if walk==2: tail=[(x+3 if x<50 else x-3,y) for x,y in tail]
    line(d, tail, OUTLINE, 11)
    line(d, tail, DARK, 7)
    line(d, tail, BODY, 4)
    for i in range(1,len(tail)-1):
        a=tail[i]; b=tail[i+1]
        mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2
        line(d, [(mx-2,my-2),(mx+2,my+2)], MID, 1)
    # legs, segmented and dark
    hips=[(39,78),(57,78)]
    feet=[(50,116),(46,116)] if walk==0 else ([(31,113),(55,118)] if walk==1 else [(60,118),(41,113)])
    for hip,foot in zip(hips,feet):
        mid=((hip[0]+foot[0])/2-3, 96)
        poly(d, [(hip[0]-5,hip[1]),(hip[0]+4,hip[1]),(mid[0]+4,98),(mid[0]-3,98)], DARK, OUTLINE, 2)
        poly(d, [(mid[0]-3,96),(mid[0]+4,96),(foot[0]+5,114),(foot[0]-4,114)], BODY, OUTLINE, 2)
        line(d, [(mid[0]-2,99),(mid[0]+2,104)], MID, 1)
        # clawed foot
        for k in (-4,0,4):
            poly(d, [(foot[0]+k-2,112),(foot[0]+k+2,112),(foot[0]+k+4,119),(foot[0]+k,118)], BONE, OUTLINE, 1)
    # torso/ribcage
    poly(d, [(33,48),(63,48),(67,72),(59,84),(37,84),(29,72)], BODY, OUTLINE, 2)
    poly(d, [(37,51),(59,51),(61,70),(55,79),(41,79),(35,70)], MID, OUTLINE, 1)
    # rib plates
    for y in (56,62,68,74):
        line(d, [(36,y),(60,y-1)], OUTLINE, 2)
        line(d, [(38,y+1),(58,y)], DARK, 1)
    # sternum
    line(d, [(48,50),(48,80)], DARK, 3)
    line(d, [(49,52),(49,78)], HI, 1)
    # shoulder plates
    for cx in (33,63):
        ell(d, (cx-8,46,cx+8,57), DARK, OUTLINE, 2)
        line(d, [(cx-5,48),(cx+5,48)], MID, 1)
    # arms: upper/lower segmented, claws
    if attack:
        arms=[((31,51),(17,42),(13,30)),((65,51),(79,42),(83,30))]
    elif side:
        arms=[((34,52),(22,64),(17,78)),((65,52),(70,66),(76,78))]
    else:
        arms=[((31,52),(18,66),(14,82)),((65,52),(78,66),(82,82))]
    if walk==1: arms=[((31,52),(21,70),(19,86)),((65,52),(74,62),(78,78))]
    if walk==2: arms=[((31,52),(22,62),(18,78)),((65,52),(75,70),(77,86))]
    for sh,el,cl in arms:
        line(d, [sh,el], OUTLINE, 8); line(d, [sh,el], DARK, 5)
        line(d, [el,cl], OUTLINE, 7); line(d, [el,cl], BODY, 4)
        line(d, [((sh[0]+el[0])/2,(sh[1]+el[1])/2)], MID, 1)
        # three claws
        dx=1 if cl[0]>el[0] else -1
        for k in (-4,0,4):
            poly(d, [(cl[0],cl[1]+k-1),(cl[0]+dx*7,cl[1]+k*1.5+2),(cl[0]+dx*4,cl[1]+k+4)], BONE, OUTLINE, 1)
    return feet

def head(d, attack=False, side=False, walk=0):
    if side:
        # profile head facing right, long swept cranium
        poly(d, [(43,50),(38,38),(42,22),(52,10),(68,8),(79,16),(78,27),(68,33),(60,39),(58,50)], DARK, OUTLINE, 2)
        poly(d, [(46,47),(43,35),(49,22),(58,14),(69,14),(74,21),(72,29),(61,35),(55,43)], BODY, OUTLINE, 1)
        line(d, [(48,27),(67,18)], HI, 2)
        # jaw open profile
        poly(d, [(58,38),(78,35),(82,43),(77,50),(61,50),(55,46)], DARK, OUTLINE, 2)
        poly(d, [(61,43),(77,42),(78,47),(63,48)], MOUTH, OUTLINE, 1)
        for x in (64,69,74): poly(d, [(x,42),(x+3,42),(x+2,46),(x,46)], TEETH, OUTLINE, 1)
        ell(d, (58,27,64,32), DARK, OUTLINE, 1); line(d, [(59,29),(63,29)], EYE, 1)
    else:
        # elongated domed head; carries forward on attack
        top=8 if not attack else 5
        tilt=0 if not attack else 9
        pts=[(49-tilt,49),(39-tilt,39),(35-tilt,25),(40-tilt,12),(48-tilt,top),(58-tilt,top+2),(66-tilt,14),(67-tilt,28),(62-tilt,40),(58-tilt,50)]
        poly(d, pts, DARK, OUTLINE, 2)
        pts2=[(48-tilt,47),(43-tilt,37),(41-tilt,25),(45-tilt,15),(51-tilt,11),(58-tilt,15),(61-tilt,27),(57-tilt,38),(54-tilt,47)]
        poly(d, pts2, BODY, OUTLINE, 1)
        # dome ridges
        line(d, [(45-tilt,16),(51-tilt,12),(57-tilt,16)], HI, 2)
        line(d, [(43-tilt,27),(50-tilt,22),(58-tilt,26)], MID, 1)
        line(d, [(42-tilt,35),(50-tilt,30),(58-tilt,34)], DARK, 1)
        # eye sockets + slit eyes
        for cx in (45-tilt,58-tilt):
            ell(d, (cx-4,29,cx+4,35), DARK, OUTLINE, 1)
            line(d, [(cx-3,31),(cx+2,32)], EYE, 1)
            line(d, [(cx-2,31),(cx,31)], EYE_HI, 1)
        # jaw: open on attack, slight idle
        if attack:
            poly(d, [(43-tilt,42),(59-tilt,39),(63-tilt,49),(53-tilt,57),(43-tilt,53)], DARK, OUTLINE, 2)
            poly(d, [(46-tilt,44),(57-tilt,42),(59-tilt,49),(51-tilt,53),(45-tilt,50)], MOUTH, OUTLINE, 1)
            for x in (47,51,55):
                poly(d, [(x-tilt,44),(x+3-tilt,44),(x+2-tilt,48),(x+1-tilt,48)], TEETH, OUTLINE, 1)
                poly(d, [(x-tilt,50),(x+3-tilt,50),(x+2-tilt,54),(x+1-tilt,54)], TEETH, OUTLINE, 1)
        else:
            poly(d, [(44-tilt,42),(59-tilt,40),(60-tilt,49),(49-tilt,51)], DARK, OUTLINE, 2)
            line(d, [(46-tilt,44),(58-tilt,43)], MID, 1)
            poly(d, [(47-tilt,45),(57-tilt,44),(56-tilt,48),(48-tilt,48)], MOUTH, OUTLINE, 1)
            for x in (48,52,56): poly(d, [(x-tilt,45),(x+2-tilt,45),(x+1-tilt,48),(x-tilt,48)], TEETH, OUTLINE, 1)
        # cheek tendrils
        line(d, [(40-tilt,39),(34-tilt,44),(35-tilt,50)], DARK, 3)
        line(d, [(61-tilt,39),(67-tilt,44),(66-tilt,50)], DARK, 3)

def frame(name, walk=0, attack=False, side=False):
    im=Image.new("RGBA",(W*S,H*S),(0,0,0,0)); d=ImageDraw.Draw(im)
    # soft dark ground contact shadow is omitted: transparent sprite only
    base(d, walk, attack, side)
    head(d, attack, side, walk)
    im=im.resize((W,H), Image.Resampling.LANCZOS).resize((OUT_W,OUT_H), Image.Resampling.LANCZOS)
    OUT.mkdir(parents=True, exist_ok=True)
    im.save(OUT/f"{name}.png")
    return list(im.getdata())

def main():
    frames={
        "idle": frame("idle"),
        "walk1": frame("walk1",1),
        "walk2": frame("walk2",2),
        "attack": frame("attack",0,True),
        "side": frame("side",0,False,True),
    }
    with open(HEADER,"w") as f:
        f.write("/* Generated by tools/gen_alien_sprites.py. Original art; RGBA ARGB words. */\n")
        f.write("#ifndef GLON_AVP_ALIEN_SPRITES_H\n#define GLON_AVP_ALIEN_SPRITES_H\n#include <stdint.h>\n")
        f.write("#define ALIEN_W 64\n#define ALIEN_H 128\n\n")
        for name,vals in frames.items():
            f.write(f"static const uint32_t alien_{name}[ALIEN_W*ALIEN_H] = {{\n")
            for i in range(0,len(vals),8):
                row=[]
                for r,g,b,a in vals[i:i+8]:
                    row.append("0x%08xu"%((a<<24)|(r<<16)|(g<<8)|b))
                f.write("  "+", ".join(row)+",\n")
            f.write("};\n\n")
        f.write("#endif\n")
    print("wrote",len(frames),"frames to",OUT)

if __name__=="__main__": main()
