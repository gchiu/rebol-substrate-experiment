#!/usr/bin/env python3
"""Generate runtime arrays for supplied G5o corridor prototype textures.
Source PNGs remain full resolution under assets/corridor. Runtime arrays are
downsampled to 512 to bound executable/source size for this evaluation."""
from PIL import Image
import pathlib
ROOT=pathlib.Path(__file__).resolve().parent.parent
ASSETS=ROOT/'assets'/'corridor'; OUT=ROOT/'corridor_textures.h'
FILES={'wall_bulkhead_steel':'wall_bulkhead_steel','wall_recess_panel':'wall_recess_panel','floor_plate':'floor_plate'}
with open(OUT,'w') as f:
    f.write('/* Generated from supplied G5o prototype textures; runtime 512x512. */\n')
    f.write('#ifndef GLON_AVP_CORRIDOR_TEXTURES_H\n#define GLON_AVP_CORRIDOR_TEXTURES_H\n#include <stdint.h>\n\n')
    for name,file in FILES.items():
        im=Image.open(ASSETS/f'{file}.png').convert('RGB').resize((512,512),Image.Resampling.LANCZOS)
        vals=list(im.getdata())
        f.write(f'static const uint32_t tex_art_{name}[512*512] = {{\n')
        for i in range(0,len(vals),6):
            f.write('  '+', '.join('0x%08xu'%((255<<24)|(r<<16)|(g<<8)|b) for r,g,b in vals[i:i+6])+',\n')
        f.write('};\n\n')
    f.write('#endif\n')
print('wrote',OUT)
