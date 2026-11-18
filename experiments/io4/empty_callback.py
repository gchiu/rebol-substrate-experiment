#!/usr/bin/env python3
"""Measure ordinary Glon function-call dispatch with no host I/O.

The source calls a trivial closure N times.  A one-call run is subtracted from
an N-call run to reduce loading/parse noise.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def run(glon, calls):
    lines = ["[", "f: func [x] [x]", "t: 1"]
    lines += ["t: f t" for _ in range(calls)]
    lines += ["print t", "]"]
    fd, path = tempfile.mkstemp(prefix="empty-callback.", suffix=".glon")
    import os
    os.close(fd); Path(path).write_text("\n".join(lines) + "\n")
    try:
        t0 = time.perf_counter()
        r = subprocess.run([str(glon), path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60)
        dt = time.perf_counter() - t0
    finally:
        Path(path).unlink(missing_ok=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace"))
    return dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--calls", type=int, default=100)
    args = ap.parse_args()
    g = args.glon.resolve()
    t1 = run(g, 1)
    tn = run(g, args.calls)
    per = (tn - t1) / max(1, args.calls - 1)
    print(f"calls={args.calls} us_per_call={per*1e6:.3f} "
          f"total_ms={tn*1000:.3f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
