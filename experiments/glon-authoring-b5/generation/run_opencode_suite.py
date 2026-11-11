#!/usr/bin/env python3
"""Run B5 authoring trials through OpenCode and preserve raw evidence.

Each trial uses the existing generation/run_trial.sh with the OpenCode adapter
as LLM_CMD. The adapter creates a fresh tool-free temporary project per model
invocation. This script only schedules trials, measures verification time and
summarises the files produced by run_trial.sh.
"""
import argparse
import glob
import json
import os
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
TASKS = ["tri", "pow2", "min3", "clamp", "sign_neg", "range_sum",
         "choose", "is_even", "ack", "quad", "median3", "max_neg"]
REPS = ["A", "B", "C"]
RUN_TRIAL = BASE / "generation" / "run_trial.sh"
ADAPTER = BASE / "generation" / "opencode_adapter.sh"
B5BIN = BASE / "build" / "b5"


def timed_verify(task, rep, candidate):
    t0 = time.perf_counter()
    p = subprocess.run([str(B5BIN), "--verify", task, rep, str(candidate)],
                       cwd=BASE, capture_output=True, text=True)
    elapsed = time.perf_counter() - t0
    return {"returncode": p.returncode, "seconds": round(elapsed, 6),
            "stdout": p.stdout[-4000:], "stderr": p.stderr[-4000:]}


def attempt_files(trial_dir, task, rep):
    raw = sorted(glob.glob(str(trial_dir / "artifact.attempt*")))
    raw = [p for p in raw if not p.endswith((".bin", ".test", ".convert"))]
    out = []
    for p in raw:
        n = p.rsplit("attempt", 1)[1]
        cand = p
        if rep in ("B", "C"):
            b = p + ".bin"
            if os.path.exists(b):
                cand = b
        out.append((int(n), p, cand))
    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tasks", default="all", help="comma-separated task list or all")
    ap.add_argument("--reps", default="A,B,C")
    ap.add_argument("--model", default="deepseek/deepseek-flash")
    ap.add_argument("--limit", type=int, default=0, help="run at most N remaining trials")
    args = ap.parse_args()

    if not B5BIN.exists():
        print(f"missing {B5BIN}; build B5 first", file=sys.stderr)
        return 2
    outdir = Path(args.out)
    if not outdir.is_absolute():
        outdir = BASE / outdir

    if outdir.exists():
        cfg_path = outdir / "run-config.json"
        if not cfg_path.exists():
            print(f"existing outdir has no run-config.json: {outdir}", file=sys.stderr)
            return 2
        config = json.loads(cfg_path.read_text())
        if args.model != config.get("model"):
            print(f"resume model mismatch: {args.model} vs {config.get('model')}", file=sys.stderr)
            return 2
        pairs = [(o["task"], o["representation"]) for o in config["order"]]
        print(f"resuming {outdir} with seed {config.get('seed')} model {config.get('model')}", flush=True)
    else:
        outdir.mkdir(parents=True, exist_ok=False)
        tasks = TASKS if args.tasks == "all" else [x.strip() for x in args.tasks.split(",") if x.strip()]
        reps = [x.strip() for x in args.reps.split(",") if x.strip()]
        pairs = [(t, r) for t in tasks for r in reps]
        rng = random.Random(args.seed)
        rng.shuffle(pairs)
        order = [{"task": t, "representation": r} for t, r in pairs]
        config = {
            "seed": args.seed,
            "model": args.model,
            "variant": "default",
            "temperature": "provider default (not overridden)",
            "tasks": tasks,
            "representations": reps,
            "order": order,
            "generated_at": time.time(),
        }
        (outdir / "run-config.json").write_text(json.dumps(config, indent=2) + "\n")

    summary = []
    ran = 0
    for idx, (task, rep) in enumerate(pairs, 1):
        trial_dir = outdir / f"{idx:02d}-{task}-{rep}"
        if (trial_dir / "complete.json").exists():
            continue
        if args.limit and ran >= args.limit:
            break
        if trial_dir.exists():
            shutil.rmtree(trial_dir)
        trial_dir.mkdir()
        raw_dir = trial_dir / "raw"
        raw_dir.mkdir()
        meta_log = trial_dir / "meta.jsonl"
        trial_id = f"{args.seed}:{idx}:{task}:{rep}"
        env = os.environ.copy()
        env.update({
            "LLM_CMD": str(ADAPTER),
            "OPENCODE_MODEL": args.model,
            "B5_TRIAL_ID": trial_id,
            "B5_RAW_DIR": str(raw_dir),
            "B5_META_LOG": str(meta_log),
        })
        trial_log = trial_dir / "trial.log"
        t0 = time.perf_counter()
        with open(trial_log, "w") as log:
            p = subprocess.run([str(RUN_TRIAL), task, rep, str(trial_dir / "artifact")],
                               cwd=BASE / "generation", env=env, stdout=log, stderr=subprocess.STDOUT,
                               timeout=1800)
        trial_wall = time.perf_counter() - t0

        attempts = attempt_files(trial_dir, task, rep)
        verifies = []
        for n, raw_artifact, candidate in attempts:
            v = timed_verify(task, rep, candidate)
            v.update({"attempt": n, "artifact": os.path.basename(raw_artifact),
                      "candidate": os.path.basename(candidate)})
            verifies.append(v)
        with open(trial_dir / "verify.jsonl", "w") as f:
            for v in verifies:
                f.write(json.dumps(v) + "\n")

        metas = []
        if meta_log.exists():
            for line in meta_log.read_text().splitlines():
                try: metas.append(json.loads(line))
                except Exception: pass
        tokens = {"input": 0, "output": 0, "reasoning": 0, "total": 0,
                  "cache_read": 0, "cache_write": 0}
        cost = 0.0
        model_step = 0.0
        sessions = []
        for m in metas:
            for k in tokens: tokens[k] += int(m.get("tokens", {}).get(k, 0) or 0)
            cost += float(m.get("cost", 0) or 0)
            model_step += float(m.get("model_step_seconds", 0) or 0)
            for s in m.get("sessions", []):
                if s not in sessions: sessions.append(s)

        final_raw = trial_dir / "artifact"
        final_bin = trial_dir / "artifact.bin"
        summary.append({
            "task": task,
            "representation": rep,
            "seed": args.seed,
            "model": args.model,
            "run_trial_returncode": p.returncode,
            "first_attempt_success": bool(verifies and verifies[0]["returncode"] == 0),
            "final_success": p.returncode == 0,
            "attempts": len(attempts),
            "repairs": max(0, len(attempts) - 1),
            "tokens": tokens,
            "cost": round(cost, 9),
            "model_step_seconds": round(model_step, 6),
            "process_seconds": round(trial_wall, 6),
            "verify_seconds": round(sum(v["seconds"] for v in verifies), 6),
            "sessions": sessions,
            "artifact_bytes": final_raw.stat().st_size if final_raw.exists() else 0,
            "binary_bytes": final_bin.stat().st_size if final_bin.exists() else 0,
            "binary_words": (final_bin.stat().st_size // 4) if final_bin.exists() else 0,
            "verify": verifies,
            "meta": metas,
        })
        with open(outdir / "summary.jsonl", "a") as f:
            f.write(json.dumps(summary[-1]) + "\n")
        (trial_dir / "complete.json").write_text(json.dumps(summary[-1], indent=2) + "\n")
        ran += 1
        print(f"{idx:02d}/{len(pairs)} {task} {rep}: final={p.returncode==0} "
              f"first={summary[-1]['first_attempt_success']} attempts={len(attempts)} "
              f"cost={cost:.6f} model={model_step:.2f}s proc={trial_wall:.2f}s", flush=True)

    print(f"completed {ran} trial(s) this invocation; {len(list(outdir.glob('*-*-*/complete.json')))} total completed in {outdir}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
