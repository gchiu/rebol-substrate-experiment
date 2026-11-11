#!/usr/bin/env python3
import json, subprocess, sys
from pathlib import Path
BASE=Path(__file__).resolve().parent.parent
ROOT=BASE.parent.parent
COUNT=BASE/"build/bench-count"
BENCH=BASE/"build/bench-fast"
WORKLOADS={
 "fib_10":55,"fib_15":610,"fib_18":2584,
 "ack_22":7,"ack_23":9,"ack_33":61,
 "sum_200":20100,"sum_500":125250,
 "choose_6_2":15,"choose_10_5":252,
}
# B3 flags: CALL 1, RET 2, >R 4, R> 8, R@ 16, BRANCH 32
CALL,RET,TOR,FROMR,RFETCH,BRANCH=1,2,4,8,16,32
CONFIGS=[
 ("none",0),
 ("call",CALL),("ret",RET),(">r",TOR),("r>",FROMR),("r@",RFETCH),("branch",BRANCH),
 (">r+r>",TOR|FROMR),("call+ret",CALL|RET),
 ("call+ret+branch",CALL|RET|BRANCH),
 ("call+ret+>r+r>",CALL|RET|TOR|FROMR),
 ("call+ret+>r+r>+r@",CALL|RET|TOR|FROMR|RFETCH),
 ("all six",CALL|RET|TOR|FROMR|RFETCH|BRANCH),
]
def run(cmd):
 p=subprocess.run(cmd,capture_output=True,text=True)
 try: out=json.loads(p.stdout)
 except Exception: raise RuntimeError(f"{cmd}: {p.stdout} {p.stderr}")
 return out
def main():
 results=[]
 for w,expected in WORKLOADS.items():
  f=BASE/"workloads"/(w+".glon")
  for name,flags in CONFIGS:
   c=run([str(COUNT),"bench",str(f),str(flags)])
   if "error" in c:
    results.append({"workload":w,"config":name,"flags":flags,"error":c["error"]}); continue
   ok=(c["result"]==expected and c["stack_ok"])
   b=run([str(BENCH),"bench",str(f),str(flags)])
   results.append({"workload":w,"config":name,"flags":flags,"expected":expected,
                   "correct":ok,"vm":c,"bench":b})
   print(w,name,c["cells"],c["instructions"],c["result"],f'{b["exec_median_us"]:.3f}',flush=True)
 out={"configs":[{"name":n,"flags":f} for n,f in CONFIGS],
      "workloads":WORKLOADS,"results":results}
 (BASE/"results/measurements.json").write_text(json.dumps(out,indent=2)+"\n")
 # Aggregate by configuration.
 agg=[]
 for name,flags in CONFIGS:
  rs=[r for r in results if r["config"]==name and "error" not in r]
  if len(rs)!=len(WORKLOADS):
   agg.append({"config":name,"flags":flags,"complete":False}); continue
  agg.append({"config":name,"flags":flags,"complete":True,
    "correct":all(r["correct"] for r in rs),
    "sum_cells":sum(r["vm"]["cells"] for r in rs),
    "sum_instructions":sum(r["vm"]["instructions"] for r in rs),
    "sum_exec_us":round(sum(r["bench"]["exec_median_us"] for r in rs),3),
    "median_compile_us":round(sorted(r["bench"]["compile_median_us"] for r in rs)[len(rs)//2],3),
    "worst_exec_spread_pct":max(r["bench"]["exec_spread_pct"] for r in rs),
    "max_ds":max(r["vm"]["ds"] for r in rs),"max_rs":max(r["vm"]["rs"] for r in rs)})
 base=next(a for a in agg if a["config"]=="none")
 for a in agg:
  if a.get("complete"):
   a["instruction_ratio_vs_none"]=round(base["sum_instructions"]/a["sum_instructions"],4)
   a["cell_ratio_vs_none"]=round(base["sum_cells"]/a["sum_cells"],4)
   a["exec_ratio_vs_none"]=round(base["sum_exec_us"]/a["sum_exec_us"],4)
 out["aggregate"]=agg
 (BASE/"results/aggregate.json").write_text(json.dumps(agg,indent=2)+"\n")
 print("wrote results/measurements.json and results/aggregate.json")
if __name__=="__main__": main()
