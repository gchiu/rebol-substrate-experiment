"""experiments/glon-deflate/tests/run_tests.py -- the conformance runner.

For every fixture (tests/fixtures.py) the REFERENCE result is computed with
Python's zlib (the C library), fed one byte at a time so that the output
produced before an error is known exactly:
    ok         the stream ends; the output
    truncated  the input ends first; the output so far
    malformed  zlib raises; the output before the error
Then build/dzhost runs the Glon decoder under several schedules of input
chunk sizes and read sizes, and each run must give the same class and the SAME
BYTES. A run is never compared with another Glon run alone.

The one documented difference: a valid stream that needs a distance beyond the
decoder's window must stop with ERROR window, its output a prefix of zlib's.

    python3 -I tests/run_tests.py [-j JOBS] [--quick]
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time
import zlib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fixtures  # noqa: E402

EXP = os.path.dirname(HERE)
HOST = os.path.join(EXP, "build", "dzhost")
HOST_CMD = [HOST]
WORK = os.path.join(EXP, "build", "fixtures")

# (name, dzhost options, which fixtures)
SCHEDULES = [
    ("c128/r100", ["-c", "128", "-r", "100"], lambda n, i, o, t: True),
    ("c1/r1", ["-c", "1", "-r", "1"], lambda n, i, o, t: o <= 3000 and "corrupt" not in t),
    ("awkward", ["-c", "3,7,1,64,2,13,128,5", "-r", "7,128,1,1000,100,129,4096,255"], lambda n, i, o, t: "corrupt" not in t),
    ("c1/r4096", ["-c", "1", "-r", "4096"], lambda n, i, o, t: "corrupt" not in t),
    ("c128/r65536", ["-c", "128", "-r", "65536"], lambda n, i, o, t: "corrupt" not in t),
    ("eager", ["-E", "-c", "128", "-r", "300"], lambda n, i, o, t: i <= 400 and "corrupt" not in t),
    ("c2/r1", ["-c", "2", "-r", "1"], lambda n, i, o, t: "corrupt" in t),
]


def reference(data):
    d = zlib.decompressobj(-15)
    out = bytearray()
    for k in range(len(data)):
        try:
            out += d.decompress(data[k:k + 1])
        except zlib.error as e:
            return "malformed", bytes(out), str(e)
        if d.eof:
            return "ok", bytes(out), ""
    return "truncated", bytes(out), ""


def run(fx_path, opts, out_path):
    t0 = time.time()
    p = subprocess.run(HOST_CMD + opts + ["-o", out_path, fx_path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=900)
    dt = time.time() - t0
    err = p.stderr.decode("latin-1")
    got = open(out_path, "rb").read() if os.path.exists(out_path) else b""
    if p.returncode == 0:
        cls = "ok"
    elif p.returncode == 3:
        eid = err.split("ERROR ", 1)[1].split()[0] if "ERROR " in err else "?"
        cls = {"truncated": "truncated", "window": "window"}.get(eid, "malformed")
        cls = cls if eid != "?" else "host-failure"
    else:
        cls, eid = "host-failure", err.strip()[-200:]
    return cls, got, (eid if p.returncode == 3 else ""), dt, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--quick", action="store_true", help="first schedule only")
    ap.add_argument("--only", default="", help="fixture name substring")
    ap.add_argument("--host", default=HOST, help="a dzhost binary (default build/dzhost)")
    ap.add_argument("--report", default="test-report.txt", help="report file name in build/")
    a = ap.parse_args()
    if not os.path.exists(a.host):
        sys.exit("%s is missing: run make" % a.host)
    global HOST_CMD
    HOST_CMD = [a.host, "-L", os.path.dirname(os.path.dirname(EXP))]
    os.makedirs(WORK, exist_ok=True)
    fx = [f for f in fixtures.fixtures() if a.only in f[0]]
    refs = {}
    jobs = []
    for name, data, tags in fx:
        path = os.path.join(WORK, name + ".deflate")
        with open(path, "wb") as f:
            f.write(data)
        refs[name] = reference(data)
        for sname, opts, want in (SCHEDULES[:1] if a.quick else SCHEDULES):
            if want(name, len(data), len(refs[name][1]), tags):
                jobs.append((name, tags, path, sname, opts))

    def one(job):
        name, tags, path, sname, opts = job
        fd, out = tempfile.mkstemp(prefix="dz-", dir=WORK); os.close(fd)
        try:
            cls, got, eid, dt, err = run(path, opts, out)
        finally:
            os.unlink(out)
        rcls, rout, rmsg = refs[name]
        if "window" in tags:
            ok = cls == "window" and rout.startswith(got)
        else:
            ok = cls == rcls and got == rout
        return name, tags, sname, ok, cls, eid, rcls, rmsg, len(got), len(rout), dt, err

    t0 = time.time()
    with ThreadPoolExecutor(a.j) as ex:
        results = list(ex.map(one, jobs))
    wall = time.time() - t0

    fails = [r for r in results if not r[3]]
    by_class = {}
    for r in results:
        k = r[6] if "window" not in r[1] else "window-limit"
        by_class.setdefault(k, [0, 0]); by_class[k][0] += 1; by_class[k][1] += r[3]
    by_sched = {}
    for r in results:
        by_sched.setdefault(r[2], [0, 0]); by_sched[r[2]][0] += 1; by_sched[r[2]][1] += r[3]
    lines = []
    lines.append("host: %s" % a.host)
    lines.append("DEFLATE conformance: %d fixtures, %d runs, %.0f s wall (%d jobs)" % (len(fx), len(results), wall, a.j))
    lines.append("  by reference class: " + ", ".join("%s %d/%d" % (k, v[1], v[0]) for k, v in sorted(by_class.items())))
    lines.append("  by schedule:        " + ", ".join("%s %d/%d" % (k, v[1], v[0]) for k, v in by_sched.items()))
    ids = {}
    for r in results:
        if r[5]:
            ids[r[5]] = ids.get(r[5], 0) + 1
    lines.append("  Glon error ids seen: " + ", ".join("%s %d" % kv for kv in sorted(ids.items())))
    for r in fails[:40]:
        name, tags, sname, ok, cls, eid, rcls, rmsg, gl, rl, dt, err = r
        lines.append("  FAIL %-34s %-12s glon=%s(%s) %d bytes; zlib=%s %d bytes %s | %s" %
                     (name, sname, cls, eid, gl, rcls, rl, rmsg, err.strip()[-160:].replace("\n", " ")))
    lines.append("RESULT %s (%d/%d runs pass)" % ("PASS" if not fails else "FAIL", len(results) - len(fails), len(results)))
    report = "\n".join(lines)
    print(report)
    with open(os.path.join(EXP, "build", a.report), "w") as f:
        f.write(report + "\n")
        for r in results:
            f.write("%s %s %s %s %s(%s) %d %.2fs\n" % ("pass" if r[3] else "FAIL", r[0], r[2], r[6], r[4], r[5], r[8], r[10]))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
