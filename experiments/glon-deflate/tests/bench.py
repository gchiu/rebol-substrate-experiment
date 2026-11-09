"""experiments/glon-deflate/tests/bench.py -- benchmarks for the Glon decoder.

For each workload: compressed and decompressed sizes; the Glon decoder's
throughput (through build/dzhost, native, the unchanged runtime by default);
the live managed heap after the fullest collection (of 7,232 cells) and the
deepest return stack (of 8,192 cells); library loading and decoder creation
time; the time to the first output byte; and the cost of suspension and
resumption, measured as the same stream read 1, 100 and 4,096 bytes at a time.
The native reference is the C zlib library through Python (zlib.decompress),
timed over many repetitions.

    python3 -I tests/bench.py [--host PATH] [--reps N]
"""
import argparse
import os
import re
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fixtures  # noqa: E402

EXP = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(EXP))

WORKLOADS = [
    ("text 20 KB, dynamic", lambda: fixtures.deflate(fixtures.text(20000, 3), 6)),
    ("text 4 KB, fixed", lambda: fixtures.deflate(fixtures.text(4000, 21), 9, strategy=zlib.Z_FIXED)),
    ("noise 3 KB (dynamic, ~1:1)", lambda: fixtures.deflate(fixtures.noise(3000, 4), 9)),
    ("noise 3 KB, stored", lambda: fixtures.deflate(fixtures.noise(3000, 4), 0)),
    ("'a' x 20 KB (overlapping copies)", lambda: fixtures.deflate(b"a" * 20000, 9)),
]


def stats(host, path, opts):
    p = subprocess.run([host, "-L", ROOT, "-s"] + opts + [path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = p.stderr.decode("latin-1")
    m = re.search(r"STATS (.*)", err)
    if p.returncode != 0 or not m:
        return None, err.strip().splitlines()[-1] if err.strip() else "exit %d" % p.returncode
    return dict(kv.split("=") for kv in m.group(1).split()), ""


def native(comp, reps):
    t0 = time.perf_counter()
    for _ in range(reps):
        zlib.decompress(comp, -15)
    return (time.perf_counter() - t0) / reps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.path.join(EXP, "build", "dzhost"))
    ap.add_argument("--reps", type=int, default=2000)
    ap.add_argument("--report", default="bench-report.txt")
    a = ap.parse_args()
    os.makedirs(os.path.join(EXP, "build", "bench"), exist_ok=True)
    lines = ["host: " + a.host, ""]
    hdr = "%-34s %7s %7s | %9s %9s %8s %7s %6s | %9s %8s" % (
        "workload", "in B", "out B", "Glon KB/s", "first ms", "live max", "RS max", "GCs",
        "zlib MB/s", "ratio")
    lines.append(hdr)
    lines.append("-" * len(hdr))
    resume = []
    s0 = None
    for name, make in WORKLOADS:
        comp = make()
        data = zlib.decompress(comp, -15)
        path = os.path.join(EXP, "build", "bench", re.sub(r"[^a-z0-9]+", "-", name.lower()) + ".deflate")
        with open(path, "wb") as f:
            f.write(comp)
        s, err = stats(a.host, path, ["-c", "128", "-r", "4096"])
        nat = native(comp, a.reps)
        if s is None:
            lines.append("%-34s %7d %7d | FAILED: %s" % (name, len(comp), len(data), err))
            continue
        glon = float(s["out_kb_s"])
        natmb = len(data) / nat / 1e6
        lines.append("%-34s %7d %7d | %9.2f %9.0f %8s %7s %6s | %9.1f %7.0fx" % (
            name, len(comp), len(data), glon, float(s["first_out_ms"]), s["max_live"], s["max_rs"], s["gcs"],
            natmb, natmb * 1e6 / (glon * 1024) if glon else 0))
        if name.startswith("text 20"):
            for r in ("1", "100", "4096"):
                if r == "1":
                    # 1-byte reads of 20 KB would take long: the first 2,000 bytes
                    sub = os.path.join(EXP, "build", "bench", "resume-2k.deflate")
                    with open(sub, "wb") as f:
                        f.write(fixtures.deflate(fixtures.text(2000, 3), 6))
                    s2, e2 = stats(a.host, sub, ["-c", "128", "-r", r])
                else:
                    s2, e2 = stats(a.host, path, ["-c", "128", "-r", r])
                resume.append((r, s2, e2))
            s0 = s
    lines.append("")
    lines.append("Glon KB/s: decompressed bytes per second of the whole conversation (feeds, reads, output frames);")
    lines.append("first ms: time from opening the decoder to the first output; live max: managed cells live after the")
    lines.append("fullest collection (heap 7,232); RS max: deepest return stack in cells (8,192); ratio: zlib / Glon.")
    lines.append("")
    if s0:
        lines.append("startup: libraries %.1f ms (parse + load of prelude, strings, strings-ext, inflate, progress, app: "
                     "%s bindings, %s loader cells, code ends at %s); decoder creation %.2f ms" % (
                         float(s0["libs_ms"]), s0["bindings"], s0["lib_loader"], s0["code_end"], float(s0["open_ms"])))
    lines.append("")
    lines.append("suspension / resumption (text, dynamic): each read returns to the host and resumes the decoder")
    for r, s2, e2 in resume:
        if s2 is None:
            lines.append("  read %5s bytes: FAILED %s" % (r, e2))
        else:
            lines.append("  read %5s bytes: %6s reads, %8.1f us per read, %7.2f KB/s, %s bytes out" % (
                r, s2["reads"], float(s2["us_per_read"]), float(s2["out_kb_s"]), s2["out"]))
    report = "\n".join(lines)
    print(report)
    with open(os.path.join(EXP, "build", a.report), "w") as f:
        f.write(report + "\n")


if __name__ == "__main__":
    main()
