#!/usr/bin/env python3
"""Compare IO-3, MASM-1, MASM-2 and streaming copy on identical streams."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MASM1 = ROOT / "experiments" / "masm-vocabulary"
APP = ROOT / "tests" / "io-file" / "fixtures" / "stream_recipe.glon"
COPY = ROOT / "experiments" / "io4" / "stream_copy.glon"

def run(glon, args, data, expected):
    with tempfile.TemporaryDirectory(prefix="masm2-stream.") as d:
        root = Path(d)
        (root / "doc.txt").write_bytes(data)
        t0 = time.perf_counter()
        r = subprocess.run([str(glon), "--io-root", str(root),
                            "--allow-file-read", "--allow-file-write", *args],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=900)
        dt = time.perf_counter() - t0
        out = (root / "out.txt").read_bytes() if (root / "out.txt").exists() else None
        return r, out == expected and (root / "doc.txt").read_bytes() == data, dt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--four-mib", action="store_true")
    ap.add_argument("--include-io3", action="store_true")
    args = ap.parse_args()
    g = args.glon.resolve()
    sizes = [4096, 65536, 1048576]
    if args.four_mib: sizes.append(4 * 1024 * 1024)
    variants = [
        ("masm2", [str(MASM1 / "find.glon"), str(HERE / "u32.glon"),
                   str(MASM1 / "file-stream-masm.glon"), str(APP)]),
        ("masm1", [str(MASM1 / "find.glon"), str(MASM1 / "file-stream-masm.glon"), str(APP)]),
        ("copy-u32", [str(HERE / "u32.glon"), str(COPY)]),
        ("copy", [str(COPY)]),
    ]
    if args.include_io3:
        variants.insert(0, ("io3", [str(APP)]))
    failed = 0
    for size in sizes:
        for kind in ("match", "none", "boundary"):
            if kind == "match":
                data = b"web" + b"x" * (size - 3)
                exp = b"rebol" + b"x" * (size - 3)
            elif kind == "none":
                data = b"x" * size
                exp = data
            else:
                data = b"a" * 398 + b"web" + b"x" * (size - 401)
                exp = b"a" * 398 + b"rebol" + b"x" * (size - 401)
            for name, vargs in variants:
                if name.startswith("copy") and kind != "none":
                    continue
                r, ok, dt = run(g, vargs, data, exp)
                good = r.returncode == 0 and ok
                print(("PASS" if good else "FAIL"),
                      f"{name} {size} {kind} elapsed={dt:.3f}s")
                if not good:
                    print(r.stderr.decode(errors="replace")[-400:])
                    failed += 1
    print(f"{failed} failure(s)")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
