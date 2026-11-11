#!/usr/bin/env python3
"""Aggregate B7 matched A/E trials across one or more seeds."""
import argparse, json, statistics
from pathlib import Path

BASE = Path(__file__).resolve().parent


def load(seed_dirs):
    rows = []
    for d in seed_dirs:
        p = Path(d)
        if not p.is_absolute(): p = BASE / p
        cfg = json.loads((p / "run-config.json").read_text())
        for line in (p / "summary.jsonl").read_text().splitlines():
            r = json.loads(line); r["seed"] = cfg["seed"]; rows.append(r)
    return rows


def agg(rs):
    n = len(rs); succ = [r for r in rs if r["final_success"]]
    t = {k: sum(r["tokens"][k] for r in rs) for k in ("input", "output", "reasoning", "total")}
    return {
        "trials": n,
        "first_success": sum(r["first_attempt_success"] for r in rs),
        "final_success": len(succ),
        "attempts": sum(r["attempts"] for r in rs),
        "repairs": sum(r["repairs"] for r in rs),
        "tokens": t,
        "cost": round(sum(r["cost"] for r in rs), 9),
        "model_step_seconds": round(sum(r["model_step_seconds"] for r in rs), 6),
        "process_seconds": round(sum(r["process_seconds"] for r in rs), 6),
        "verification_seconds": round(sum(r["verify_seconds"] for r in rs), 6),
        "validation_seconds": round(sum(r.get("validation_seconds", 0) or 0 for r in rs), 9),
        "b5_verify_seconds": round(sum(r.get("b5_verify_seconds", 0) or 0 for r in rs), 6),
        "cost_per_attempt": round(sum(r["cost"] for r in rs) / n, 9) if n else None,
        "cost_per_success": round(sum(r["cost"] for r in rs) / len(succ), 9) if succ else None,
        "tokens_per_success": round(t["total"] / len(succ), 2) if succ else None,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--json", default=None)
    ap.add_argument("--markdown", default=None)
    args = ap.parse_args()
    rows = load(args.dirs)
    by = {}
    for rep in ("A", "E"):
        by[rep] = agg([r for r in rows if r["representation"] == rep])
    out = {"trials": rows, "by_representation": by}
    if args.json: Path(args.json).write_text(json.dumps(out, indent=2) + "\n")
    if args.markdown:
        lines = ["# B7 matched A/E analysis", ""]
        lines += ["| Metric | A | E |", "|---|---:|---:|"]
        for k in ("trials", "first_success", "final_success", "attempts", "repairs",
                  "cost", "model_step_seconds", "process_seconds",
                  "verification_seconds", "validation_seconds",
                  "cost_per_attempt", "cost_per_success", "tokens_per_success"):
            lines.append(f"| {k} | {by['A'][k]} | {by['E'][k]} |")
        lines += ["", "| Tokens | A | E |", "|---|---:|---:|"]
        for k in ("input", "output", "reasoning", "total"):
            lines.append(f"| {k} | {by['A']['tokens'][k]} | {by['E']['tokens'][k]} |")
        lines += ["", "## First-attempt diagnostics", ""]
        for rep in ("A", "E"):
            cats = {}
            for r in rows:
                if r["representation"] != rep: continue
                vm = r.get("verification_meta") or []
                cat = vm[0].get("category") if vm else "missing"
                cats[cat] = cats.get(cat, 0) + 1
            lines.append(f"**{rep}**: {cats}")
        Path(args.markdown).write_text("\n".join(lines) + "\n")
    print(json.dumps(by, indent=2))


if __name__ == "__main__":
    main()
