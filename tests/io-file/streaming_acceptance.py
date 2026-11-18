#!/usr/bin/env python3
"""Streaming Recipe 0004 acceptance and scale checks.

The reference is Python bytes.replace(old, new, 1); the Glon run is the
independent implementation.  Output is always a distinct out.txt file.
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
FIX = HERE / "fixtures"
STREAM = FIX / "stream_recipe.glon"
SHORT = FIX / "stream_short.glon"
LONG = FIX / "stream_long.glon"


def run(glon, data, source=STREAM, flags=(), allow=None, timeout=600,
        measure=False):
    td = tempfile.TemporaryDirectory(prefix="glon-stream-test.")
    root = Path(td.name)
    if data is not None:
        (root / "doc.txt").write_bytes(data)
    grants = []
    if allow is None:
        grants = ["--allow-file-read", "--allow-file-write"]
    else:
        if "read" in allow:
            grants.append("--allow-file-read")
        if "write" in allow:
            grants.append("--allow-file-write")
    cmd = [str(glon), "--io-root", root, *grants, *flags, str(source)]
    if measure:
        cmd = ["/usr/bin/time", "-f", "TIME %e RSS %M", *cmd]
    t0 = time.time()
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout)
    elapsed = time.time() - t0
    out = (root / "out.txt").read_bytes() if (root / "out.txt").exists() else None
    result = (r, out, r.stderr, elapsed, td)
    return result


def check(cond, name, detail=""):
    if cond:
        print(f"PASS {name}")
        return 0
    print(f"FAIL {name}" + (f": {detail}" if detail else ""))
    return 1


def positive_case(glon, name, data, old=b"web", new=b"rebol", source=STREAM,
                  flags=(), **kw):
    r, out, err, elapsed, td = run(glon, data, source, flags, **kw)
    expected = data.replace(old, new, 1) if old else data
    ok = (r.returncode == 0 and out == expected and
          (td.name and (Path(td.name) / "doc.txt").read_bytes() == data))
    rc = check(ok, name, f"rc={r.returncode} out_ok={out == expected} "
                         f"err={err[-300:]!r} elapsed={elapsed:.2f}s")
    td.cleanup()
    return rc


def fail_case(glon, name, data, needle, source=STREAM, flags=(), allow=None,
              **kw):
    r, out, err, elapsed, td = run(glon, data, source, flags, allow, **kw)
    ok = r.returncode != 0 and needle.encode() in err
    if data is not None:
        ok = ok and (Path(td.name) / "doc.txt").read_bytes() == data
    rc = check(ok, name, f"rc={r.returncode} err={err[-400:]!r}")
    td.cleanup()
    return rc


def scale_case(glon, name, data, old=b"web", new=b"rebol"):
    r, out, err, elapsed, td = run(glon, data, STREAM, flags=("--gc-report",), measure=True)
    expected = data.replace(old, new, 1)
    m = re.search(rb"TIME ([0-9.]+) RSS ([0-9]+)", err)
    times = m.groups() if m else (b"?", b"?")
    gc = [tuple(map(int, x)) for x in
          re.findall(rb"collect=(\d+) live_cells=(\d+) live_objs=(\d+).*?"
                     rb"heap_high=(\d+)", err)]
    max_live = max((x[1] for x in gc), default=0)
    max_objs = max((x[2] for x in gc), default=0)
    collects = max((x[0] for x in gc), default=0)
    heap_high = max((x[3] for x in gc), default=0)
    ok = (r.returncode == 0 and out == expected and
          (Path(td.name) / "doc.txt").read_bytes() == data)
    rc = check(ok, name,
               f"rc={r.returncode} elapsed={times[0].decode()}s "
               f"rss={times[1].decode()}KB max_live={max_live} "
               f"max_objs={max_objs} collects={collects} heap_high={heap_high} "
               f"err={err[-200:]!r}")
    print(f"MEASURE {name} size={len(data)} elapsed={times[0].decode()}s "
          f"rss={times[1].decode()}KB max_live={max_live} "
          f"max_objs={max_objs} collects={collects} heap_high={heap_high}")
    td.cleanup()
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--scale", action="store_true",
                    help="include 1 MiB sustained streaming")
    ap.add_argument("--four-mib", action="store_true",
                    help="include a 4 MiB match-at-start run (slow)")
    args = ap.parse_args()
    glon = args.glon.resolve()
    failures = 0

    # Small semantic cases.
    failures += positive_case(glon, "stream empty input", b"")
    failures += positive_case(glon, "stream no match", b"the internet")
    failures += positive_case(glon, "stream beginning", b"web site")
    failures += positive_case(glon, "stream middle", b"the web is wide")
    failures += positive_case(glon, "stream end", b"the web")
    failures += positive_case(glon, "stream multiple first only", b"web web web")
    failures += positive_case(glon, "stream binary bytes",
                              b"\x00web\x80\xff", b"web", b"rebol")
    failures += positive_case(glon, "stream crossing 398",
                              b"a" * 398 + b"web" + b"z" * 10)
    failures += positive_case(glon, "stream crossing 399",
                              b"a" * 399 + b"web" + b"z" * 10)
    failures += positive_case(glon, "stream short replacement", b"a web b",
                              new=b"X", source=SHORT)
    failures += positive_case(glon, "stream long replacement", b"a web b",
                              new=b"a much longer replacement", source=LONG)
    failures += positive_case(glon, "stream short reads/writes",
                              b"web " + b"x" * 1000, flags=("--io-short", "3"))
    failures += positive_case(glon, "stream 1 KiB", b"web" + b"x" * 1021)
    failures += positive_case(glon, "stream 4 KiB", b"web" + b"x" * 4093)
    failures += positive_case(glon, "stream 64 KiB", b"web" + b"x" * 65533)
    failures += positive_case(glon, "stream 64 KiB no match", b"x" * 65536)

    # Host-level progress during a pending streaming request.
    data = b"web" + b"x" * 4093
    r, out, err, elapsed, td = run(
        glon, data, STREAM,
        flags=("--io-delay-ms", "50",
               "--io-progress", FIX / "progress.glon", "--io-report"))
    p_at = r.stdout.find(b"PROGRESS_DONE")
    s_at = r.stdout.find(b"STREAM_DONE")
    pending_at = err.find(b"progress job completed while request 1 was pending")
    reply_at = err.find(b"reply 1 status")
    ok = (r.returncode == 0 and out == data.replace(b"web", b"rebol", 1) and
          0 <= p_at < s_at and 0 <= pending_at < reply_at)
    failures += check(ok, "stream progress during pending I/O",
                      f"rc={r.returncode} out={r.stdout!r} err={err[-300:]!r}")
    td.cleanup()

    # Error, permission and cleanup cases.
    failures += fail_case(glon, "stream empty pattern", b"web site",
                          "empty-pattern", source=FIX / "stream_empty_pattern.glon")
    failures += fail_case(glon, "stream same source/target", b"web site",
                          "same-file", source=FIX / "stream_same_file.glon")
    failures += fail_case(glon, "stream missing input", None, "not-found")
    failures += fail_case(glon, "stream read permission", b"web site",
                          "permission", allow=("write",))
    failures += fail_case(glon, "stream write permission", b"web site",
                          "permission", allow=("read",))
    data = b"x" * 4096
    r, out, err, elapsed, td = run(glon, data, STREAM,
                                    flags=("--io-max-file", "1000"))
    ok = (r.returncode != 0 and b"too-large" in err and out is not None and
          0 < len(out) < len(data) and
          (Path(td.name) / "doc.txt").read_bytes() == data)
    failures += check(ok, "stream partial output on host cap",
                      f"rc={r.returncode} out_len={len(out) if out else None} "
                      f"err={err[-300:]!r}")
    td.cleanup()

    # Host cancellation must leave no handles and must not overwrite source.
    r, out, err, elapsed, td = run(glon, b"web" + b"x" * 4093, STREAM,
                                    flags=("--io-delay-ms", "100",
                                           "--io-cancel-before-reply",
                                           "--io-report"))
    ok = (r.returncode != 0 and b"cancelling request" in err and
          b"handles_open=0" in err and
          (Path(td.name) / "doc.txt").read_bytes() == b"web" + b"x" * 4093)
    failures += check(ok, "stream cancellation cleanup",
                      f"rc={r.returncode} err={err[-400:]!r}")
    td.cleanup()

    if args.scale:
        failures += scale_case(glon, "stream 1 MiB match start",
                               b"web" + b"x" * (1048576 - 3))
        failures += scale_case(glon, "stream 1 MiB no match", b"x" * 1048576)
    if args.four_mib:
        failures += scale_case(glon, "stream 4 MiB match start",
                               b"web" + b"x" * (4 * 1024 * 1024 - 3))

    print(f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
