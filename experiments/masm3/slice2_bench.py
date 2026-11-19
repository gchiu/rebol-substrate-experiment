#!/usr/bin/env python3
"""Standalone s/slice2 benchmark: ordinary reference vs MASM worker."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent


def data_expr(n, seed):
    b = bytes((i * seed + 7) & 255 for i in range(n))
    return "mk-string [ " + " ".join(str(x) for x in b) + " ]"


def run(glon, impl, calls, gc=False):
    files = []
    left = calls
    first = True
    while left:
        n = min(50, left)
        lines = ["["]
        if first:
            lines += [f"a: {data_expr(400, 3)}", f"b: {data_expr(400, 11)}"]
            first = False
        lines += [f"x: {impl} a b 100 400" for _ in range(n)]
        if left == n:
            lines += ["print s/length x"]
        lines += ["]"]
        fd, path = tempfile.mkstemp(prefix="slice2-bench.", suffix=".glon")
        import os; os.close(fd)
        Path(path).write_text("\n".join(lines) + "\n")
        files.append(path)
        left -= n
    cmd = [str(glon)]
    if gc:
        cmd.append("--gc-report")
    cmd += [str(HERE / "slice2.glon"), *files]
    try:
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=300)
        dt = time.perf_counter() - t0
    finally:
        for p in files:
            Path(p).unlink(missing_ok=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace")[-400:])
    return dt, r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--calls", type=int, default=201)
    ap.add_argument("--gc", action="store_true")
    args = ap.parse_args()
    glon = args.glon.resolve()
    for impl in ("s/slice2-glon", "s/slice2-masm", "s/raw-slice2"):
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
