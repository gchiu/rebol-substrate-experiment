#!/usr/bin/env python3
"""Original 128x192 biomechanical horror creature sprites for G5m.
Not derived from or copying any Alien game/movie frame. Layered procedural
silhouette, anatomical shading and segmented machinery; transparent RGBA."""
from PIL import Image, ImageDraw
import pathlib, math, random
ROOT=pathlib.Path(__file__).resolve().parent.parent; OUT=ROOT/'sprites'/'aliens_g5m'; OUT.mkdir(parents=True,exist_ok=True)
HDR=ROOT/'alien_sprites_g5m.h'; W,H,S=128,192,4
K=(5,7,7,255); D=(11,14,14,255); B=(19,25,24,255); M=(30,39,36,255); L=(54,66,59,255)
HIL=(96,110,99,255); BONE=(174,171,145,255); MOUTH=(49,7,10,255); CLAW=(136,139,122,255)
def p(v): return tuple(int(x*S) for x in v)
def poly(d,pts,c,out=K,w=2): d.polygon([(int(x*S),int(y*S)) for x,y in pts],fill=c,outline=out,width=int(w*S))
def line(d,pts,c,w=2): d.line([(int(x*S),int(y*S)) for x,y in pts],fill=c,width=max(1,int(w*S)),joint='curve')
def ell(d,b,c,out=K,w=2): d.ellipse([int(v*S) for v in b],fill=c,outline=out,width=int(w*S))
def seg(d,a,b,w1,w2,c=B,out=K):
    dx=b[0]-a[0]; dy=b[1]-a[1]; ln=max(0.001,math.hypot(dx,dy)); nx=-dy/ln; ny=dx/ln
    pts=[(a[0]+nx*w1/2,a[1]+ny*w1/2),(b[0]+nx*w2/2,b[1]+ny*w2/2),
         (b[0]-nx*w2/2,b[1]-ny*w2/2),(a[0]-nx*w1/2,a[1]-ny*w1/2)]
    poly(d,pts,c,out,2)

def draw(frame,walk=0,attack=False,side=False,death=False):
    im=Image.new('RGBA',(W*S,H*S),(0,0,0,0)); d=ImageDraw.Draw(im)
    if death:
        # collapsed, long silhouette on the deck
        poly(d,[(16,160),(32,142),(64,136),(96,142),(114,160),(100,173),(63,176),(28,172)],D,K,3)
        poly(d,[(25,158),(40,148),(64,144),(88,148),(104,158),(92,167),(64,169),(38,166)],B,K,2)
        for x in (42,56,70,84): line(d,[(x,150),(x,164)],M,1)
        poly(d,[(95,148),(115,132),(124,140),(109,156)],D,K,2)
        for x in (101,107,113): poly(d,[(x,137),(x+3,137),(x+2,143),(x,143)],BONE,K,1)
        line(d,[(28,161),(10,174),(6,166),(18,156)],D,5); line(d,[(30,163),(12,170)],M,1)
        im=im.resize((W,H),Image.Resampling.LANCZOS); im.save(OUT/f'{frame}.png'); return list(im.getdata())
    # tail: long segmented, behind body
    if side:
        tail=[(72,118),(90,126),(112,140),(121,158),(113,174),(102,165),(108,151),(116,150)]
    else:
        tail=[(63,116),(43,124),(26,140),(18,162),(24,180),(38,176),(42,162),(34,153)]
        if walk==1: tail=[(x-4 if x<60 else x+4,y) for x,y in tail]
        if walk==2: tail=[(x+4 if x<60 else x-4,y) for x,y in tail]
    for i in range(len(tail)-1): seg(d,tail[i],tail[i+1],10-i*0.8,8-i*0.8,D if i%2==0 else B,K)
    for a,b in zip(tail[:-1],tail[1:]):
        mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2; line(d,[(mx-3,my-3),(mx+3,my+3)],L,1)
    # digitigrade legs
    if side:
        legs=[((62,118),(54,144),(66,160),(58,181)),((67,118),(75,146),(66,166),(74,183))]
    elif walk==1:
        legs=[((58,116),(48,143),(39,164),(34,181)),((70,116),(78,145),(76,166),(82,182))]
    elif walk==2:
        legs=[((58,116),(70,143),(61,162),(58,181)),((70,116),(61,145),(52,166),(46,182))]
    else:
        legs=[((58,116),(52,144),(54,164),(49,181)),((70,116),(76,144),(74,164),(79,181))]
    for hip,knee,ankle,toe in legs:
        seg(d,hip,knee,10,7,B); seg(d,knee,ankle,7,5,D); seg(d,ankle,toe,5,4,B)
        line(d,[(knee[0]-2,knee[1]),(ankle[0]-1,ankle[1])],M,1)
        for k in (-3,0,3): line(d,[(toe[0],toe[1]),(toe[0]+k,toe[1]+8)],CLAW,2)
    # torso: narrow waist and ribbed chest
    poly(d,[(48,64),(80,64),(88,92),(78,120),(50,120),(40,92)],B,K,3)
    poly(d,[(52,68),(76,68),(82,91),(72,112),(56,112),(46,91)],M,K,2)
    for y in range(76,113,8):
        line(d,[(48,y),(80,y-2)],K,2); line(d,[(50,y+1),(78,y-1)],D,1)
    line(d,[(64,67),(64,116)],K,3); line(d,[(66,70),(66,112)],HIL,1)
    # shoulders and long arms
    if attack:
        arms=[((49,66),(30,56),(18,38),(10,26)),((79,66),(98,56),(110,38),(118,26))]
    elif side:
        arms=[((52,68),(36,86),(24,108),(16,126)),((82,68),(92,90),(94,112),(91,130))]
    elif walk==1:
        arms=[((49,66),(32,84),(25,108),(18,124)),((79,66),(94,86),(100,108),(105,124))]
    elif walk==2:
        arms=[((49,66),(34,88),(30,109),(24,126)),((79,66),(92,84),(94,106),(99,122))]
    else:
        arms=[((49,66),(34,88),(28,112),(24,130)),((79,66),(94,88),(100,112),(104,130))]
    for sh,el,wr,cl in arms:
        seg(d,sh,el,11,7,B); seg(d,el,wr,7,5,D); seg(d,wr,cl,5,3,B)
        line(d,[(sh[0]+1,sh[1]+2),(el[0],el[1])],HIL,1)
        # elongated fingers
        dx=1 if cl[0]>wr[0] else -1
        for k in (-5,-2,2,5): line(d,[(cl[0],cl[1]),(cl[0]+dx*11,cl[1]+k)],CLAW,2)
    # long eyeless cranial dome and jaw
    if side:
        poly(d,[(54,64),(49,49),(55,29),(69,13),(85,9),(101,17),(101,32),(88,42),(76,50),(72,65)],D,K,3)
        poly(d,[(58,61),(55,47),(61,32),(72,20),(86,17),(95,23),(94,33),(81,41),(70,50)],B,K,2)
        line(d,[(61,28),(83,19),(94,24)],HIL,2); line(d,[(59,40),(76,31)],L,1)
        poly(d,[(76,48),(99,44),(105,54),(99,62),(78,63),(72,57)],D,K,2)
        poly(d,[(80,53),(99,51),(100,57),(81,59)],MOUTH,K,1)
        for x in (84,90,96): poly(d,[(x,50),(x+4,50),(x+2,56),(x,56)],BONE,K,1)
        line(d,[(56,48),(46,56),(47,66)],D,3); line(d,[(102,36),(112,48)],D,3)
    else:
        tilt=7 if attack else 0
        poly(d,[(64-tilt,62),(50-tilt,48),(47-tilt,28),(54-tilt,11),(67-tilt,3),(83-tilt,7),(92-tilt,22),(88-tilt,42),(80-tilt,58),(74-tilt,66)],D,K,3)
        poly(d,[(64-tilt,58),(57-tilt,43),(56-tilt,29),(63-tilt,14),(73-tilt,10),(84-tilt,16),(85-tilt,31),(79-tilt,45),(75-tilt,58)],B,K,2)
        line(d,[(60-tilt,18),(72-tilt,11),(84-tilt,20)],HIL,2)
        line(d,[(57-tilt,31),(70-tilt,23),(82-tilt,30)],L,1)
        line(d,[(55-tilt,42),(69-tilt,35),(80-tilt,41)],M,1)
        # no eyes; dark recessed brow only
        line(d,[(57-tilt,35),(72-tilt,32)],K,3)
        # jaw, teeth
        if attack:
            poly(d,[(55-tilt,48),(83-tilt,44),(90-tilt,57),(77-tilt,69),(55-tilt,64)],D,K,3)
            poly(d,[(59-tilt,52),(82-tilt,48),(85-tilt,57),(74-tilt,64),(58-tilt,60)],MOUTH,K,1)
            for x in (61,66,71,76): poly(d,[(x-tilt,51),(x+4-tilt,51),(x+2-tilt,58),(x-tilt,58)],BONE,K,1)
            for x in (62,68,74): poly(d,[(x-tilt,60),(x+4-tilt,60),(x+2-tilt,66),(x-tilt,66)],BONE,K,1)
        else:
            poly(d,[(57-tilt,46),(82-tilt,42),(86-tilt,55),(76-tilt,62),(57-tilt,60)],D,K,2)
            poly(d,[(60-tilt,50),(81-tilt,47),(82-tilt,55),(73-tilt,59),(60-tilt,57)],MOUTH,K,1)
            for x in (62,67,72,77): poly(d,[(x-tilt,49),(x+3-tilt,49),(x+2-tilt,56),(x-tilt,56)],BONE,K,1)
        # cheek/neck biomech tendrils
        line(d,[(49-tilt,50),(40-tilt,57),(41-tilt,69)],D,4); line(d,[(89-tilt,48),(103-tilt,56)],D,4)
    # restrained surface highlights/noise
    for _ in range(70):
        x=random.randrange(16,112); y=random.randrange(20,180)
        if im.getpixel((x*S,y*S))[3]>0: d.point((x*S,y*S),fill=(70,80,73,130))
    im=im.resize((W,H),Image.Resampling.LANCZOS)
    px=im.load()
    for x in range(W):
        px[x,0]=(0,0,0,0); px[x,H-1]=(0,0,0,0)
    for y in range(H):
        px[0,y]=(0,0,0,0); px[W-1,y]=(0,0,0,0)
    im.save(OUT/f'{frame}.png'); return list(im.getdata())

def main():
    random.seed(5)
    frames={'idle':draw('idle'),'walk1':draw('walk1',1),'walk2':draw('walk2',2),
            'attack':draw('attack',0,True),'side':draw('side',0,False,True),
            'death':draw('death',death=True)}
    with open(HDR,'w') as f:
        f.write('/* G5m original biomechanical creature sprites; generated, transparent. */\n')
        f.write('#ifndef GLON_AVP_ALIEN_SPRITES_G5M_H\n#define GLON_AVP_ALIEN_SPRITES_G5M_H\n#include <stdint.h>\n#define ALIEN_W 128\n#define ALIEN_H 192\n\n')
        for n,v in frames.items():
            f.write(f'static const uint32_t alien_{n}[ALIEN_W*ALIEN_H] = {{\n')
            for i in range(0,len(v),6):
                f.write('  '+', '.join('0x%08xu'%((a<<24)|(r<<16)|(g<<8)|b) for r,g,b,a in v[i:i+6])+',\n')
            f.write('};\n\n')
        f.write('#endif\n')
    print('wrote',len(frames),'frames')
if __name__=='__main__': main()
