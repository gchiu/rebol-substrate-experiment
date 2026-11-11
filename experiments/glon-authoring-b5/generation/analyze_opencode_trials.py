#!/usr/bin/env python3
"""Aggregate B5 OpenCode trial logs without calling a model."""
import argparse
import glob
import json
import os
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent


def classify(verify, convert_error):
    if convert_error:
        return "invalid representation/encoding"
    out = (verify.get("stdout", "") + "\n" + verify.get("stderr", "")).lower()
    if verify.get("returncode") == 0:
        return "pass"
    if "stack" in out and "imbalance" in out:
        return "calling-convention/stack"
    if "result " in out and "expected " in out:
        return "incorrect program semantics"
    if ("tagged" in out or "direct" in out or "bad file size" in out or
        "bad magic" in out or "unknown operation" in out or "label id" in out or
        "undefined entry" in out or "hex" in out or "conversion failed" in out):
        return "invalid representation/encoding"
    if "compile fail" in out:
        return "invalid representation/encoding"
    return "other/compiler limitation"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--json", default=None)
    ap.add_argument("--markdown", default=None)
    args = ap.parse_args()
    outdir = Path(args.outdir)
    if not outdir.is_absolute(): outdir = BASE / outdir
    config = json.loads((outdir / "run-config.json").read_text())
    trials = []
    for idx, o in enumerate(config["order"], 1):
        task, rep = o["task"], o["representation"]
        d = outdir / f"{idx:02d}-{task}-{rep}"
        if not (d / "complete.json").exists():
            continue
        complete = json.loads((d / "complete.json").read_text())
        metas = []
        if (d / "meta.jsonl").exists():
            for line in (d / "meta.jsonl").read_text().splitlines():
                try: metas.append(json.loads(line))
                except Exception: pass
        verifies = []
        if (d / "verify.jsonl").exists():
            for line in (d / "verify.jsonl").read_text().splitlines():
                try: verifies.append(json.loads(line))
                except Exception: pass
        attempts = sorted(glob.glob(str(d / "artifact.attempt*")))
        attempts = [p for p in attempts if not p.endswith((".bin", ".test", ".convert"))]
        failures = []
        for a in attempts:
            n = int(a.rsplit("attempt", 1)[1])
            v = next((x for x in verifies if x.get("attempt") == n), {})
            conv = Path(a + ".convert")
            cerror = conv.exists() and conv.stat().st_size > 0
            failures.append({"attempt": n, "category": classify(v, cerror),
                             "returncode": v.get("returncode"),
                             "stdout": v.get("stdout", "")[-1000:]})
        final_raw = d / "artifact"
        if not final_raw.exists() and attempts:
            final_raw = Path(attempts[-1])
        final_bin = Path(str(final_raw) + ".bin")
        if not final_bin.exists() and attempts:
            candidate_bin = Path(attempts[-1] + ".bin")
            if candidate_bin.exists():
                final_bin = candidate_bin
        tokens = {"input": 0, "output": 0, "reasoning": 0, "total": 0,
                  "cache_read": 0, "cache_write": 0}
        cost = 0.0; model_s = 0.0; sessions = []
        for m in metas:
            for k in tokens: tokens[k] += int(m.get("tokens", {}).get(k, 0) or 0)
            cost += float(m.get("cost", 0) or 0)
            model_s += float(m.get("model_step_seconds", 0) or 0)
            for s in m.get("sessions", []):
                if s not in sessions: sessions.append(s)
        first_success = bool(verifies and verifies[0].get("returncode") == 0)
        final_success = bool(complete.get("run_trial_returncode") == 0)
        timeout_attempts = sum(1 for m in metas
                               if int(m.get("tokens", {}).get("total", 0) or 0) == 0
                               and int(m.get("text_parts", 0) or 0) == 0
                               and float(m.get("process_seconds", 0) or 0) >= 170)
        trials.append({
            "task": task, "representation": rep,
            "first_attempt_success": first_success,
            "final_success": final_success,
            "timeout_attempts": timeout_attempts,
            "timeout_affected": timeout_attempts > 0,
            "evaluable": timeout_attempts == 0,
            "attempts": len(attempts),
            "repairs": max(0, len(attempts) - 1),
            "tokens": tokens, "cost": round(cost, 9),
            "model_step_seconds": round(model_s, 6),
            "process_seconds": complete.get("process_seconds", 0),
            "verify_seconds": complete.get("verify_seconds", 0),
            "artifact_bytes": final_raw.stat().st_size if final_raw.exists() else 0,
            "binary_bytes": final_bin.stat().st_size if final_bin.exists() else 0,
            "binary_words": (final_bin.stat().st_size // 4) if final_bin.exists() else 0,
            "sessions": sessions,
            "attempts_detail": failures,
        })
    by = {}
    for rep in "ABC":
        rs = [t for t in trials if t["representation"] == rep]
        n = len(rs)
        successes = [t for t in rs if t["final_success"]]
        first = sum(t["first_attempt_success"] for t in rs)
        attempts = sum(t["attempts"] for t in rs)
        timeout_trials = sum(1 for t in rs if t["timeout_affected"])
        evaluable = sum(1 for t in rs if t["evaluable"])
        tokens = {k: sum(t["tokens"][k] for t in rs) for k in
                  ("input", "output", "reasoning", "total", "cache_read", "cache_write")}
        cost = sum(t["cost"] for t in rs)
        model_s = sum(t["model_step_seconds"] for t in rs)
        proc_s = sum(t["process_seconds"] for t in rs)
        verify_s = sum(t["verify_seconds"] for t in rs)
        first_fail = {}
        final_fail = {}
        all_fail = {}
        for t in rs:
            if t["attempts_detail"]:
                c = t["attempts_detail"][0]["category"]
                if c != "pass": first_fail[c] = first_fail.get(c, 0) + 1
                if not t["final_success"]:
                    c = t["attempts_detail"][-1]["category"]
                    final_fail[c] = final_fail.get(c, 0) + 1
                for a in t["attempts_detail"]:
                    if a["category"] != "pass":
                        all_fail[a["category"]] = all_fail.get(a["category"], 0) + 1
        by[rep] = {
            "trials": n, "first_success": first, "final_success": len(successes),
            "timeout_affected_trials": timeout_trials,
            "evaluable_trials": evaluable,
            "attempts": attempts, "repairs": attempts - n,
            "tokens": tokens, "cost": round(cost, 9),
            "model_step_seconds": round(model_s, 6),
            "process_seconds": round(proc_s, 6),
            "verify_seconds": round(verify_s, 6),
            "cost_per_attempted_task": round(cost / n, 9) if n else None,
            "cost_per_success": round(cost / len(successes), 9) if successes else None,
            "tokens_per_attempted_task": round(tokens["total"] / n, 2) if n else None,
            "tokens_per_success": round(tokens["total"] / len(successes), 2) if successes else None,
            "first_failure_categories": first_fail,
            "final_failure_categories": final_fail,
            "all_failure_categories": all_fail,
            "tasks": sorted(rs, key=lambda x: x["task"]),
        }
    result = {"config": config, "by_representation": by, "trials": trials}
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n")
    if args.markdown:
        lines = ["# B5 OpenCode trial analysis", ""]
        lines.append(f"Model: `{config.get('model')}`; seed: {config.get('seed')}; "
                     f"tasks: {len(config['order'])//3}; trials: {len(trials)}.")
        lines += ["", "| Rep | First success | Final success | Evaluable | Timeout-affected | Attempts | Repairs | Tokens | Cost | Model s | Process s | Cost/attempt | Cost/success |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for rep in "ABC":
            x = by[rep]
            lines.append(f"| {rep} | {x['first_success']}/{x['trials']} | {x['final_success']}/{x['trials']} | "
                         f"{x['evaluable_trials']}/{x['trials']} | {x['timeout_affected_trials']} | {x['attempts']} | {x['repairs']} | {x['tokens']['total']} | ${x['cost']:.6f} | "
                         f"{x['model_step_seconds']:.2f} | {x['process_seconds']:.2f} | "
                         f"${x['cost_per_attempted_task']:.6f} | {('$%.6f' % x['cost_per_success']) if x['cost_per_success'] is not None else 'no success'} |")
        lines += ["", "## Per task", "", "| Task | Rep | First | Final | Timeouts | Attempts | Tokens | Cost |",
                  "|---|---|---:|---:|---:|---:|---:|---:|"]
        for t in sorted(trials, key=lambda x: (x["representation"], x["task"])):
            lines.append(f"| {t['task']} | {t['representation']} | {t['first_attempt_success']} | "
                         f"{t['final_success']} | {t['timeout_attempts']} | {t['attempts']} | "
                         f"{t['tokens']['total']} | ${t['cost']:.6f} |")
        lines += ["", "## Failure categories", ""]
        for rep in "ABC":
            x = by[rep]
            lines.append(f"**{rep}** first: {x['first_failure_categories']}; "
                         f"final: {x['final_failure_categories']}; all attempts: {x['all_failure_categories']}")
        lines += ["", "## Infrastructure caveat", ""]
        for rep in "ABC":
            x = by[rep]
            if x["timeout_affected_trials"]:
                lines.append(f"{rep}: {x['timeout_affected_trials']}/{x['trials']} trials had at least one "
                             "170-180 s zero-token adapter timeout. These are adapter/infrastructure "
                             "failures, not valid model-generation outcomes.")
        Path(args.markdown).write_text("\n".join(lines) + "\n")
    print(json.dumps({r: {k: by[r][k] for k in
          ("trials", "first_success", "final_success", "timeout_affected_trials", "evaluable_trials", "attempts", "repairs",
           "cost", "tokens", "model_step_seconds", "process_seconds",
           "cost_per_attempted_task", "cost_per_success",
           "first_failure_categories", "final_failure_categories",
           "all_failure_categories")} for r in "ABC"}, indent=2))


if __name__ == "__main__":
    main()
