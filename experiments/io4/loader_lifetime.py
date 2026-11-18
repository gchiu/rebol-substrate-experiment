#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="io4-loader.") as d:
        root = Path(d)
        (root / "doc.txt").write_bytes(b"payload")
        r = subprocess.run(
            [str(args.glon.resolve()), "--io-root", str(root),
             "--allow-file-read", "--allow-file-write",
             str(HERE / "loader_lifetime.glon")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    ok = r.returncode == 0 and b"LIFETIME_OK" in r.stdout and b"test" not in r.stderr
    print(("PASS" if ok else "FAIL") + " loader lifetime")
    if not ok:
        print(r.stdout.decode(errors="replace"))
        print(r.stderr.decode(errors="replace"))
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
