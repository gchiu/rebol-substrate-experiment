#!/usr/bin/env python3
"""Differential tests for the MASM u32 encoder, including allocation/GC."""
import argparse
from pathlib import Path
import random
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

def run(glon, values, extra=()):
    with tempfile.TemporaryDirectory(prefix="masm2-u32.") as d:
        src = ["["]
        src.append("chk: func [n] [ either s/= io/u32-glon n io/u32-masm n [ either s/= io/u32-fused n io/u32-masm n [ 1 ] [ raise create-sin 'test 'u32 n ] ] [ raise create-sin 'test 'u32 n ] ]")
        for n in values:
            src.append(f"chk {n}")
        src += ["s/print \"U32_OK\"", "]"]
        path = Path(d) / "t.glon"; path.write_text("\n".join(src) + "\n")
        r = subprocess.run([str(glon), str(HERE / "u32.glon"), *extra, str(path)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    return r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    args = ap.parse_args()
    glon = args.glon.resolve()
    vals = [0, 1, 2, 127, 128, 129, 255, 256, 257, 65535, 65536,
            16777215, 16777216, 16777217, 2147483647, 2147483648,
            0xFFFFFFFF]
    rng = random.Random(0x4D41534D32)
    vals += [rng.randrange(0, 0x100000000) for _ in range(200)]
    got = []
    for start in range(0, len(vals), 30):
        r = run(glon, vals[start:start + 30])
        if r.returncode != 0 or b"U32_OK" not in r.stdout:
            print(r.stdout.decode(errors="replace"))
            print(r.stderr.decode(errors="replace"))
            return 1
        got.append(True)
    print(f"PASS u32 differential: {len(vals)} values")

    # Keep a result across many allocations, with GC reporting enabled.
    with tempfile.TemporaryDirectory(prefix="masm2-u32-gc.") as d:
        src = ["[", "a: io/u32-masm 16909060"]
        src += ["x: mk-string [ 65 66 67 ]" for _ in range(120)]
        src += ["s/print either s/= a io/u32-glon 16909060 [ \"U32_LIVE_OK\" ] [ \"U32_LIVE_BAD\" ]", "]"]
        path = Path(d) / "t.glon"; path.write_text("\n".join(src) + "\n")
        r = subprocess.run([str(glon), "--gc-report", str(HERE / "u32.glon"), str(path)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    ok = r.returncode == 0 and b"U32_LIVE_OK" in r.stdout
    print(("PASS" if ok else "FAIL") + " u32 survives allocation pressure")
    if not ok:
        print(r.stdout.decode(errors="replace")); print(r.stderr.decode(errors="replace"))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
