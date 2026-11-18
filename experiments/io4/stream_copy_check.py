#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--size", type=int, action="append")
    args = ap.parse_args()
    sizes = args.size or [4096, 65536, 1048576]
    for size in sizes:
        with tempfile.TemporaryDirectory(prefix="io4-copy.") as d:
            root = Path(d)
            data = b"x" * size
            (root / "doc.txt").write_bytes(data)
            t0 = time.perf_counter()
            r = subprocess.run([str(args.glon.resolve()), "--io-root", str(root),
                                "--allow-file-read", "--allow-file-write",
                                str(HERE / "stream_copy.glon")],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=300)
            dt = time.perf_counter() - t0
            out = (root / "out.txt").read_bytes() if (root / "out.txt").exists() else None
            ok = r.returncode == 0 and out == data and (root / "doc.txt").read_bytes() == data
            print(("PASS" if ok else "FAIL"),
                  f"stream-copy {size} elapsed={dt:.3f}s out={out == data}")
            if not ok:
                print(r.stderr.decode(errors="replace")[-300:])
                return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
