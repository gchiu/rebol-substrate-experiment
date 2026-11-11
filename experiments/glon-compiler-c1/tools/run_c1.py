#!/usr/bin/env python3
import json, subprocess, hashlib
from pathlib import Path
BASE=Path(__file__).resolve().parent.parent
ROOT=BASE.parent.parent

def run(cmd, cwd=BASE):
 p=subprocess.run(cmd,cwd=cwd,capture_output=True,text=True)
 try: return json.loads(p.stdout)
 except Exception: raise RuntimeError(f"{cmd}: {p.stdout} {p.stderr}")

def main():
 configs={}
 for name in ("candidate","none"):
  meas=run([str(BASE/"build/c1-count"),"--measure",name])
  bench=run([str(BASE/"build/c1-bench"),"--bench",name])
  configs[name]={"measure":meas,"bench":bench,
    "sum_instructions":sum(v["instructions"] for v in meas["vectors"]),
    "sum_exec_us":round(sum(v["exec_median_us"] for v in bench["vectors"]),3),
    "max_ds":max(v["ds"] for v in meas["vectors"]),
    "max_rs":max(v["rs"] for v in meas["vectors"]),
    "all_pass":all(v["pass"] for v in meas["vectors"]),
  }
 out={"configs":configs}
 c=configs["candidate"]; n=configs["none"]
 out["ratios"]={"instructions_none_over_candidate":round(n["sum_instructions"]/c["sum_instructions"],4),
                "exec_none_over_candidate":round(n["sum_exec_us"]/c["sum_exec_us"],4),
                "function_cells_none_over_candidate":round(n["measure"]["function_cells"]/c["measure"]["function_cells"],4)}
 (BASE/"results/compiled.json").write_text(json.dumps(out,indent=2)+"\n")
 print(json.dumps(out["ratios"],indent=2)); print("all_pass",c["all_pass"],n["all_pass"])

if __name__=="__main__": main()
