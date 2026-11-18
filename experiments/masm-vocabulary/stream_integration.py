#!/usr/bin/env python3
"""Run the IO-3 streaming app with the experimental MASM s/find.

The original modules/file-stream.glon is loaded by MODULES=file; this script
then loads find.glon and the experimental file-stream-masm.glon copy, so the
original implementation remains untouched.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
APP = ROOT / "tests" / "io-file" / "fixtures" / "stream_recipe.glon"


def run(glon, size, kind, gc=False):
    with tempfile.TemporaryDirectory(prefix="masm-stream.") as d:
        root = Path(d)
        data = (b"web" + b"x" * (size - 3)) if kind == "match" else b"x" * size
        expected = (b"rebol" + b"x" * (size - 3)) if kind == "match" else data
        (root / "doc.txt").write_bytes(data)
        cmd = [str(glon), "--io-root", str(root),
               "--allow-file-read", "--allow-file-write"]
        if gc: cmd.append("--gc-report")
        cmd += [str(HERE / "find.glon"), str(HERE / "file-stream-masm.glon"),
                str(APP)]
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=900)
        elapsed = time.perf_counter() - t0
        out = (root / "out.txt").read_bytes() if (root / "out.txt").exists() else None
        return r, out == expected, (root / "doc.txt").read_bytes() == data, elapsed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--four-mib", action="store_true")
    args = ap.parse_args()
    glon = args.glon.resolve()
    sizes = [1024, 65536, 1048576]
    if args.four_mib:
        sizes.append(4 * 1024 * 1024)
    failed = 0
    for size in sizes:
        for kind in ("match", "none"):
            r, out_ok, src_ok, elapsed = run(glon, size, kind)
            ok = r.returncode == 0 and out_ok and src_ok
            print(("PASS" if ok else "FAIL"),
                  f"stream-masm {size} {kind} elapsed={elapsed:.3f}s "
                  f"out={out_ok} source={src_ok}")
            if not ok:
                print(r.stderr.decode(errors="replace")[-500:])
                failed += 1
    print(f"{failed} failure(s)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
