#!/usr/bin/env python3
import subprocess, json
from pathlib import Path
BASE=Path(__file__).resolve().parent.parent
ROOT=BASE.parent.parent
src=(BASE/"reference/hf_left.glon").read_text()
vectors=[
 ("all_zero",[0]*16,1,1,32768),
 ("complete_len1_2",[0,2]+[0]*14,1,1,0),
 ("incomplete_len1_1",[0,1]+[0]*14,1,1,16384),
 ("over_len1_3",[0,3]+[0]*14,1,1,-1),
 ("len2_4",[0,0,4]+[0]*13,1,1,0),
 ("len3_8",[0,0,0,8]+[0]*12,1,1,0),
 ("two_codes_len2",[0,0,4]+[0]*13,2,1,-1),
 ("boundary_l16",[0]*16,16,123,123),
 ("zero_left",[0]*16,1,0,0),
 ("partial",[0,1,2]+[0]*13,1,1,0),
]
out=[]
for name,c,l,left,want in vectors:
 p=BASE/"reference"/"production"/f"{name}.glon"
 p.parent.mkdir(exist_ok=True)
 p.write_text(src+f"hf/left [ {' '.join(map(str,c))} ] {l} {left}\n")
 r=subprocess.run([str(ROOT/"glon"),str(ROOT/"jupyter/prelude.glon"),str(p)],cwd=ROOT,capture_output=True,text=True)
 lines=[x for x in r.stdout.strip().splitlines() if x.strip()]
 got=int(lines[-1]) if lines else None
 out.append({"name":name,"expected":want,"got":got,"returncode":r.returncode,"path":str(p.relative_to(ROOT)),
             "stdout":r.stdout[-300:],"stderr":r.stderr[-300:]})
 print(name,got,want,r.returncode)
(BASE/"results/reference.json").write_text(json.dumps(out,indent=2)+"\n")
