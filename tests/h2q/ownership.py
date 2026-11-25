#!/usr/bin/env python3
"""H2Q async continuation ownership tests (local-only, bounded).

Verifies that Glon closures capture locals across return and async suspension,
and that an uncaptured local is reported rather than resolved globally.
Python only drives the CLI.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def fail(m):
    print("FAIL:", m)
    raise SystemExit(1)


def ok(m):
    print("PASS:", m)


def run(glon, src, timeout=20):
    p = subprocess.run([str(glon), src], cwd=str(ROOT), text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    opts = ap.parse_args()
    glon = opts.glon
    if not os.path.exists(glon):
        fail(f"missing {glon}")
    td = tempfile.mkdtemp(prefix="h2q-")

    def w(name, body):
        p = pathlib.Path(td) / name
        p.write_text(body)
        return p

    # A: returned closure captures a local
    a = w("a.glon", "[\nmake: func [x] [ func [] [ x ] ]\nc: make 42\ns/print s/from-int c\n]\n")
    rc, out, err = run(glon, a)
    if rc != 0 or "42" not in out:
        fail(f"A returned closure: rc={rc} out={out!r} err={err!r}")
    ok("returned closure captures its local (42)")

    # B: closure captures a local and suspends on task-wait, resumed by a peer
    b = w("b.glon", "\n".join([
        "[",
        "make: func [x] [ func [] [ task-wait  s/print s/+ \"B=\" s/from-int x ] ]",
        "c: make 7",
        "t1: spawn [ c ]",
        "t2: spawn [ task-wake t1 ]",
        "run-tasks",
        "]",
    ]))
    rc, out, err = run(glon, b)
    if rc != 0 or "B=7" not in out:
        fail(f"B async closure: rc={rc} out={out!r} err={err!r}")
    ok("closure captures a local across async suspension (B=7)")

    # E: uncaptured local is not resolved globally (strict unbound)
    e = w("e.glon", "\n".join([
        "[",
        "make: func [x] [ func [] [ y ] ]",
        "c: make 1",
        "t1: spawn [ c ]",
        "t2: spawn [ task-wake t1 ]",
        "run-tasks",
        "]",
    ]))
    rc, out, err = run(glon, e)
    if rc == 0:
        fail("E: uncaptured local resolved (expected failure)")
    ok("uncaptured local is rejected (no global fallback)")

    import shutil
    shutil.rmtree(td, ignore_errors=True)
    print("H2Q_OWNERSHIP PASS")


if __name__ == "__main__":
    main()