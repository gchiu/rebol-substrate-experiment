#!/usr/bin/env python3
"""Standalone io/u32 encoder benchmark: ordinary vs MASM."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

def run(glon, impl, calls, gc=False):
    files = []
    left = calls
    first = True
    while left:
        n = min(90, left)
        lines = ["["]
        if first:
            lines += ["v: 16909060"]
            first = False
        lines += [f"x: {impl} v" for _ in range(n)]
        if left == n:
            lines += ["print s/length x"]
        lines += ["]"]
        fd, path = tempfile.mkstemp(prefix="u32-bench.", suffix=".glon")
        import os; os.close(fd)
        Path(path).write_text("\n".join(lines) + "\n")
        files.append(path)
        left -= n
    cmd = [str(glon)]
    if gc: cmd.append("--gc-report")
    cmd += [str(HERE / "u32.glon"), *files]
    try:
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=120)
        dt = time.perf_counter() - t0
    finally:
        for p in files: Path(p).unlink(missing_ok=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace")[-400:])
    return dt, r.stderr

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--calls", type=int, default=101)
    ap.add_argument("--gc", action="store_true")
    args = ap.parse_args()
    glon = args.glon.resolve()
    for impl in ("io/u32-glon", "io/u32-fused", "io/u32-masm"):
        t1, _ = run(glon, impl, 1, args.gc)
        tn, err = run(glon, impl, args.calls, args.gc)
        per = (tn - t1) / max(1, args.calls - 1)
        print(f"{impl} calls={args.calls} us_per_call={per*1e6:.3f}")
        if args.gc:
            for line in err.splitlines():
                if b"glon: gc:" in line:
                    print(line.decode(errors="replace"))
    return 0

if __name__ == "__main__":
    sys.exit(main())
