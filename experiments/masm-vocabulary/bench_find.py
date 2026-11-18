#!/usr/bin/env python3
"""Compare s/find-masm, s/find-glon and c_find on identical bytes.

Glon timing uses two batch sizes (1 and 101 calls) and reports the slope, so
loading/parsing is separated from steady-state search.  C uses its own loop.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def rep(data: bytes) -> str:
    return "mk-string [ " + " ".join(str(b) for b in data) + " ]"


def make_cases():
    cases = []
    for n in (128, 400):
        # Early, late, no match, and repeated-prefix/overlapping candidates.
        for name, text, pat in [
            (f"{n}/early", b"abc" + b"x" * (n - 3), b"abc"),
            (f"{n}/late", b"x" * (n - 3) + b"abc", b"abc"),
            (f"{n}/none", b"x" * n, b"abc"),
            (f"{n}/single", b"a" * n, b"a"),
            (f"{n}/repeat", b"a" * n, b"a" * 10),
            (f"{n}/overlap", b"a" * (n - 2) + b"ab", b"aaab"),
            (f"{n}/longpat", b"x" * (n - 20) + b"abcdefghij" + b"x" * 10,
             b"abcdefghij"),
        ]:
            cases.append((name, text, pat))
    return cases


def glon_sources(text, pat, impl, calls, per_file=90):
    """Split calls across files so each parsed block stays under the form cap."""
    paths = []
    remaining = calls
    first = True
    while remaining > 0:
        n = min(per_file, remaining)
        lines = ["["]
        if first:
            lines += [f"t: {rep(text)}", f"p: {rep(pat)}"]
            first = False
        lines += [f"x: {impl} t p" for _ in range(n)]
        if remaining == n:
            lines.append("print x")
        lines.append("]")
        fd, path = tempfile.mkstemp(prefix="find-bench.", suffix=".glon")
        os.close(fd)
        Path(path).write_text("\n".join(lines) + "\n")
        paths.append(path)
        remaining -= n
    return paths


def run_glon(glon, text, pat, impl, calls):
    paths = glon_sources(text, pat, impl, calls)
    try:
        t0 = time.perf_counter()
        r = subprocess.run([str(glon), str(ROOT / "glon-lib" / "strings-ext.glon"),
                            str(HERE / "find.glon"), *paths],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=300)
        elapsed = time.perf_counter() - t0
    finally:
        for p in paths:
            Path(p).unlink(missing_ok=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace")[-500:])
    vals = []
    for line in r.stdout.splitlines():
        try: vals.append(int(line.strip()))
        except ValueError: pass
    return elapsed, vals[-1] if vals else None

def run_c(cbin, text, pat, iterations):
    with tempfile.TemporaryDirectory(prefix="find-cbench.") as d:
        t = Path(d) / "t"; p = Path(d) / "p"
        t.write_bytes(text); p.write_bytes(pat)
        r = subprocess.run([str(cbin), str(t), str(p), str(iterations)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=60)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace"))
    m = re.search(rb"result (-?\d+) time_ms ([0-9.]+)", r.stdout)
    if not m: raise RuntimeError(r.stdout.decode(errors="replace"))
    return int(m.group(1)), float(m.group(2)) / iterations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--c", required=True, type=Path)
    ap.add_argument("--calls", type=int, default=101)
    ap.add_argument("--c-iters", type=int, default=20000)
    args = ap.parse_args()
    glon = args.glon.resolve(); cbin = args.c.resolve()
    print("case,impl,result,us_per_call")
    for name, text, pat in make_cases():
        expected = 0 if not pat else text.find(pat)
        for impl in ("s/find-glon", "s/find-masm"):
            t1, r1 = run_glon(glon, text, pat, impl, 1)
            tn, rn = run_glon(glon, text, pat, impl, args.calls)
            if r1 != expected or rn != expected:
                raise SystemExit(f"result mismatch {name} {impl}: {r1}/{rn} != {expected}")
            per = (tn - t1) / max(1, args.calls - 1)
            print(f"{name},{impl},{rn},{per*1e6:.3f}")
        cr, cper = run_c(cbin, text, pat, args.c_iters)
        if cr != expected:
            raise SystemExit(f"result mismatch {name} c: {cr} != {expected}")
        print(f"{name},c_find,{cr},{cper*1e6:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
