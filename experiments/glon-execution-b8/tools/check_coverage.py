#!/usr/bin/env python3
import json, subprocess
from pathlib import Path
BASE=Path(__file__).resolve().parent.parent
ROOT=BASE.parent.parent
FILES=[
 ("prelude","jupyter/prelude.glon"),
 ("strings","demo/shop/strings.glon"),
 ("deflate-inflate","experiments/glon-deflate/inflate.glon"),
 ("deflate-app","experiments/glon-deflate/app.glon"),
 ("traffic","demo/shop/demos/traffic.glon"),
 ("kaka-lib","demos/kaka/kaka-lib.glon"),
 ("kaka-main","demos/kaka/kaka.glon"),
 ("kaka-fighter","demos/kaka/kaka-fighter.glon"),
]
out=[]
for name,rel in FILES:
 p=subprocess.run([str(BASE/"build/bench-count"),"--check",str(ROOT/rel)],cwd=ROOT,capture_output=True,text=True)
 try: j=json.loads(p.stdout)
 except Exception: j={"ok":False,"error":p.stdout+p.stderr}
 j.update({"name":name,"path":rel,"returncode":p.returncode})
 out.append(j); print(name,p.returncode,j.get("error",""))
(BASE/"results/coverage.json").write_text(json.dumps(out,indent=2)+"\n")
