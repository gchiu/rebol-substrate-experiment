#!/usr/bin/env python3
import hashlib, json
from pathlib import Path

BASE=Path(__file__).resolve().parent.parent
ROOT=BASE.parent.parent
B5=ROOT/"experiments/glon-authoring-b5"
B6=ROOT/"experiments/glon-authoring-b6"
B7=ROOT/"experiments/glon-authoring-b7"
SEL={
 "choose":{"A":B7/"trials/AE-seed-20261115/04-choose-A","D":B6/"trials/AD-seed-20261113/12-choose-D",
           "E":B7/"trials/AE-seed-20261115/07-choose-E","B":B5/"trials/followup-BC-seed-20261111/04-choose-B"},
 "max_neg":{"A":B7/"trials/AE-seed-20261115/12-max_neg-A","D":B6/"trials/AD-seed-20261113/03-max_neg-D",
            "E":B7/"trials/AE-seed-20261115/08-max_neg-E","B":B5/"trials/followup-BC-seed-20261111/07-max_neg-B"},
 "ack":{"A":B7/"trials/AE-seed-20261115/15-ack-A","D":B6/"trials/AD-seed-20261113/07-ack-D",
        "E":B7/"trials/AE-seed-20261115/02-ack-E","B":B5/"trials/followup-BC-seed-20261111/09-ack-B"},
}
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
out={}
for task,reps in SEL.items():
 out[task]={}
 for rep,t in reps.items():
  c=json.loads((t/"complete.json").read_text())
  attempts=sorted(t.glob("artifact.attempt*"))
  attempts=[p for p in attempts if not p.name.endswith((".bin",".test",".convert"))]
  diags=[]
  for a in attempts:
   tst=Path(str(a)+".test")
   diags.append({"artifact":str(a.relative_to(ROOT)),"sha256":sha(a),
                 "test":tst.read_text(errors="replace")[-2000:] if tst.exists() else ""})
  art=t/"artifact"
  out[task][rep]={"trial_path":str(t.relative_to(ROOT)),"seed":t.parent.name.split("-seed-")[-1],
                  "first_attempt_success":c["first_attempt_success"],"final_success":c["final_success"],
                  "attempts":c["attempts"],"repairs":c["repairs"],"tokens":c["tokens"],"cost":c["cost"],
                  "model_step_seconds":c["model_step_seconds"],"process_seconds":c["process_seconds"],
                  "final_artifact":str(art.relative_to(ROOT)),"final_sha256":sha(art),
                  "attempts_detail":diags}
(BASE/"data/trials.json").write_text(json.dumps(out,indent=2)+"\n")
print("wrote data/trials.json")
