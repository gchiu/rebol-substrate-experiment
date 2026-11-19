#!/usr/bin/env python3
"""Measure in-place replacement: time, GC, live memory and peak RSS."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MASM = (
    ROOT / "experiments/masm-vocabulary/find.glon",
    ROOT / "experiments/masm2/u32.glon",
    ROOT / "experiments/masm-vocabulary/file-stream-masm.glon",
)
ATOMIC = ROOT / "modules/file-atomic.glon"
REPLACE = ROOT / "modules/file-replace.glon"
PROG = """[ file/replace-first "doc.txt" "web" "rebol"
    func [x] [ s/print "BENCH_DONE" ]
    func [e] [ raise e ] ]
"""


def run(glon, size, kind):
    data = (b"web" + b"x" * (size - 3)) if kind == "match" else (b"x" * size)
    expected = data.replace(b"web", b"rebol", 1)
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-bench.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(data)
    prog = root / "run.glon"
    prog.write_text(PROG)
    cmd = ["/usr/bin/time", "-v", str(glon), "--io-root", str(root),
           "--allow-file-read", "--allow-file-write", "--gc-report",
           *map(str, MASM), str(ATOMIC), str(REPLACE), str(prog)]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=1800)
    got = (root / "doc.txt").read_bytes() if (root / "doc.txt").exists() else None
    temps = [p.name for p in root.iterdir() if p.name.startswith(".glon-tmp-")]
    err = r.stderr.decode(errors="replace")
    elapsed = re.search(r"Elapsed \(wall clock\).*?: ([0-9:.]+)", err)
    rss = re.search(r"Maximum resident set size \(kbytes\): (\d+)", err)
    gc = None
    for line in err.splitlines():
        if "gc: job-end" in line and "collect=" in line:
            gc = line
    obj = {
        "size": size,
        "case": kind,
        "ok": r.returncode == 0 and got == expected and not temps,
        "seconds": None,
        "peak_rss_kb": int(rss.group(1)) if rss else None,
        "gc": gc,
        "stdout": r.stdout.decode(errors="replace").strip().splitlines()[-1:] if r.stdout else [],
    }
    if elapsed:
        parts = elapsed.group(1).split(":")
        if len(parts) == 2:
            obj["seconds"] = round(int(parts[0]) * 60 + float(parts[1]), 3)
        else:
            obj["seconds"] = round(int(parts[0]) * 3600 + int(parts[1]) * 60 + float(parts[2]), 3)
    td.cleanup()
    return obj


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--sizes", default="1024,65536,1048576,4194304")
    ap.add_argument("--kinds", default="match,none")
    args = ap.parse_args()
    glon = args.glon.resolve()
    for size in [int(x) for x in args.sizes.split(",")]:
        for kind in args.kinds.split(","):
            obj = run(glon, size, kind)
            print(json.dumps(obj, sort_keys=True), flush=True)
            if not obj["ok"]:
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
