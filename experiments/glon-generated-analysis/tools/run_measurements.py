#!/usr/bin/env python3
"""Collect selected final artifacts, hashes, model metadata and VM measurements."""
import hashlib, json, subprocess, sys
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
ROOT = BASE.parent.parent
B5 = ROOT / "experiments/glon-authoring-b5"
B6 = ROOT / "experiments/glon-authoring-b6"
B7 = ROOT / "experiments/glon-authoring-b7"

# Final artifacts selected for the three tasks. Seeds differ by representation
# and are recorded explicitly; no comparison pretends they are the same seed.
SELECTED = {
 "choose": {
  "A": B7/"trials/AE-seed-20261115/04-choose-A",
  "D": B6/"trials/AD-seed-20261113/12-choose-D",
  "E": B7/"trials/AE-seed-20261115/07-choose-E",
  "B": B5/"trials/followup-BC-seed-20261111/04-choose-B",
 },
 "max_neg": {
  "A": B7/"trials/AE-seed-20261115/12-max_neg-A",
  "D": B6/"trials/AD-seed-20261113/03-max_neg-D",
  "E": B7/"trials/AE-seed-20261115/08-max_neg-E",
  "B": B5/"trials/followup-BC-seed-20261111/07-max_neg-B",
 },
 "ack": {
  "A": B7/"trials/AE-seed-20261115/15-ack-A",
  "D": B6/"trials/AD-seed-20261113/07-ack-D",
  "E": B7/"trials/AE-seed-20261115/02-ack-E",
  "B": B5/"trials/followup-BC-seed-20261111/09-ack-B",
 },
}

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def run(cmd):
    p=subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{cmd}: {p.stdout} {p.stderr}")
    return p.stdout

def complete(trial):
    return json.loads((trial/"complete.json").read_text())

def measure(rep, artifact, work):
    if rep == "B":
        # B5 B artifacts are whitespace-separated hex; convert to the binary
        # word stream expected by the B4 decoder.
        binp = work / (artifact.parent.name + ".bin")
        run([sys.executable, str(B5/"tools/hex_to_bin.py"), str(artifact), str(binp)])
        mode="tagged"; path=binp
    else:
        mode="text"; path=artifact
    count=json.loads(run([str(BASE/"build/measure-count"), mode, str(path)]))
    bench=json.loads(run([str(BASE/"build/measure-bench"), mode, str(path)]))
    return count, bench

def main():
    work=Path("/tmp/glon-generated-analysis-bins"); work.mkdir(exist_ok=True)
    out={"selected":{}, "tasks":{}}
    for task, reps in SELECTED.items():
        out["tasks"][task]={}
        for rep, trial in reps.items():
            c=complete(trial)
            art=trial/"artifact"
            count,bench=measure(rep,art,work)
            rec={
              "trial_path": str(trial.relative_to(ROOT)),
              "artifact_path": str(art.relative_to(ROOT)),
              "artifact_sha256": sha(art),
              "seed": trial.parent.name.split("-seed-")[-1],
              "first_attempt_success": c["first_attempt_success"],
              "final_success": c["final_success"],
              "attempts": c["attempts"],
              "repairs": c["repairs"],
              "tokens": c["tokens"],
              "cost": c["cost"],
              "model_step_seconds": c["model_step_seconds"],
              "process_seconds": c["process_seconds"],
              "vm": count, "bench": bench,
            }
            out["tasks"][task][rep]=rec
            print(task, rep, count["cells"], count["instructions"], bench["exec_median_us"])
    p=BASE/"data/measurements.json"
    p.parent.mkdir(exist_ok=True)
    p.write_text(json.dumps(out, indent=2)+"\n")
    print("wrote",p)

if __name__ == "__main__": main()
