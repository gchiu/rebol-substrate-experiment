#!/usr/bin/env python3
"""B9 Phase 1: preserve seven-primitive DEFLATE baseline measurements.

The timing binary is the normal current dzhost. The counting binary links an
instrumented copy of the frozen S1 machine (s1_prof.c) and reports executed
opcodes; it is never used for timing.
"""
import hashlib
import json
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
NORMAL = os.path.join(HERE, "..", "glon-deflate", "build", "dzhost")
COUNT = os.path.join(HERE, "build", "dzhost-b9")
fixtures = json.load(open(os.path.join(HERE, "fixtures.json")))["fixtures"]
reps = int(os.environ.get("B9_REPS", "5"))

def run(binary, fx, out):
    cmd = [binary, "-L", ROOT, "-c", ",".join(map(str, fx["chunks"])),
           "-r", ",".join(map(str, fx["read"])), "-s", "-o", out,
           os.path.join(HERE, fx["file"])]
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, timeout=300)
    text = p.stderr.decode()
    stats = None; prof = None
    for line in text.splitlines():
        if line.startswith("STATS "):
            stats = {kv.split("=", 1)[0]: kv.split("=", 1)[1] for kv in line.split()[1:] if "=" in kv}
        elif line.startswith("B9_PROFILE "):
            prof = {kv.split("=", 1)[0]: kv.split("=", 1)[1] for kv in line.split()[1:] if "=" in kv}
    return p.returncode, stats, prof, text

results = {"source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip(),
           "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "environment": {"host": "native x86_64 WSL2", "compiler": "cc -O2",
                           "normal_binary": "experiments/glon-deflate/build/dzhost",
                           "counting_binary": "experiments/glon-deflate-b9/build/dzhost-b9",
                           "repetitions": reps},
           "fixtures": fixtures, "workloads": []}

for fx in fixtures:
    out = os.path.join("/tmp", "b9-" + fx["name"] + ".out")
    times = []; all_stats = []; all_prof = []; logs = []
    for i in range(reps):
        rc, st, pf, log = run(NORMAL, fx, out)
        if rc != 0 or not st:
            raise SystemExit("native run failed for %s: %s" % (fx["name"], log[-500:]))
        times.append(float(st["run_ms"]))
        all_stats.append(st); logs.append(log)
    # deterministic count run (once; count is independent of timing)
    rc, cst, cpf, clog = run(COUNT, fx, out + ".count")
    if rc != 0 or not cpf:
        raise SystemExit("counting run failed for %s: %s" % (fx["name"], clog[-500:]))
    want = open(out, "rb").read()
    if hashlib.sha256(want).hexdigest() != fx["sha256"]:
        raise SystemExit("output mismatch for " + fx["name"])
    med = statistics.median(times)
    item = {
        "name": fx["name"], "compressed_bytes": fx["compressed_bytes"],
        "decompressed_bytes": fx["decompressed_bytes"], "sha256": fx["sha256"],
        "chunks": fx["chunks"], "read": fx["read"],
        "normal_runs": [{"run_ms": float(s["run_ms"]), "read_ms": float(s["read_ms"]),
                         "feed_ms": float(s["feed_ms"]), "open_ms": float(s["open_ms"]),
                         "libs_ms": float(s["libs_ms"]), "gcs": int(s["gcs"]),
                         "max_live": int(s["max_live"]), "max_rs": int(s["max_rs"]),
                         "dispatches": int(s["dispatches"])} for s in all_stats],
        "run_ms_median": med,
        "run_ms_min": min(times), "run_ms_max": max(times),
        "run_ms_spread_pct": 100.0 * (max(times) - min(times)) / med if med else 0,
        "decode_ms_median": statistics.median([float(s["read_ms"]) + float(s["feed_ms"]) for s in all_stats]),
        "output_kb_per_s_median": (fx["decompressed_bytes"] / 1024.0) / (med / 1000.0) if med else 0,
        "instructions": int(cpf["ops"]),
        "host_calls": int(cpf["hostcalls"]),
        "instructions_per_output_byte": int(cpf["ops"]) / fx["decompressed_bytes"] if fx["decompressed_bytes"] else 0,
        "count_run": cpf,
        "count_stats": cst,
        "log_tail": logs[-1][-1000:],
    }
    results["workloads"].append(item)
    print("%-10s in=%d out=%d run=%.1fms ops=%s hostcalls=%s" %
          (fx["name"], fx["compressed_bytes"], fx["decompressed_bytes"], med,
           cpf["ops"], cpf["hostcalls"]))

os.makedirs(os.path.join(HERE, "baseline"), exist_ok=True)
with open(os.path.join(HERE, "baseline", "results.json"), "w") as f:
    json.dump(results, f, indent=2)
    f.write("\n")
with open(os.path.join(HERE, "baseline", "run.log"), "w") as f:
    f.write("source_commit=%s\ndate=%s\n\n" % (results["source_commit"], results["date_utc"]))
    for w in results["workloads"]:
        f.write("%s run_ms_median=%.3f ops=%d hostcalls=%d\n" %
                (w["name"], w["run_ms_median"], w["instructions"], w["host_calls"]))
        f.write(w["log_tail"] + "\n")
print("wrote baseline/results.json and baseline/run.log")
