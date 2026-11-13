#!/usr/bin/env python3
"""Validate G5j original textures: dimensions and seamless edges."""
from PIL import Image
import pathlib, sys
ROOT=pathlib.Path(__file__).resolve().parent.parent
LIMIT=12.0
fail=0
for name in ('panel_v2','service_v2'):
    p=ROOT/'sprites'/'textures_g5j'/f'{name}.png'
    im=Image.open(p).convert('RGB'); w,h=im.size; pix=im.load()
    lr=sum(sum(abs(a-b) for a,b in zip(pix[0,y],pix[w-1,y])) for y in range(h))/h
    tb=sum(sum(abs(a-b) for a,b in zip(pix[x,0],pix[x,h-1])) for x in range(w))/w
    ok=im.size==(64,64) and lr<=LIMIT and tb<=LIMIT
    print(f"{'OK' if ok else 'FAIL'} {name}: {im.size} edge_lr={lr:.2f} edge_tb={tb:.2f}")
    if not ok: fail+=1
sys.exit(1 if fail else 0)
