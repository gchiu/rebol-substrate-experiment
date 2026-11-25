#!/usr/bin/env python3
"""H1 scheduler-level interleaving acceptance (local-only, bounded).

This proves the cooperative scheduler can suspend an I/O-bound task while a CPU
task runs to completion.  It does NOT claim HTTP requests are tasks: the
CivetWeb begin_request boundary is synchronous (see H1-RESULTS.md).  Python only
drives the CLI and checks the observable event order.
"""

import argparse
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def run(args, cwd=None, timeout=30):
    proc = subprocess.Popen([str(a) for a in args], cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, start_new_session=True)
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
        fail(f"timeout: {args[-1]}")
    return proc.returncode, out, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    ap.add_argument("--example", default=str(ROOT / "examples" / "h1" / "interleave.glon"))
    ap.add_argument("--delay-ms", type=int, default=200)
    opts = ap.parse_args()

    if not os.path.exists(opts.glon):
        fail(f"missing binary {opts.glon}")
    if not os.path.exists(opts.example):
        fail(f"missing example {opts.example}")

    td = tempfile.mkdtemp(prefix="h1-")
    try:
        (pathlib.Path(td) / "f.txt").write_text("X")
        rc, out, err = run([opts.glon, "--io-delay-ms", str(opts.delay_ms),
                            "--io-root", td, "--allow-file-read",
                            "--allow-file-write", opts.example])
        if rc != 0:
            fail(f"interleave rc={rc} err={err!r}")
        lines = [ln for ln in out.splitlines() if ln]
        if "B-start" not in lines or "B-done" not in lines or "A-done" not in lines:
            fail(f"missing events: {lines!r}")
        if lines.index("B-done") > lines.index("A-done"):
            fail(f"B did not finish before A: {lines!r}")
        ok(f"task B (CPU) completed while task A was suspended on I/O: {lines}")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    print("H1_SCHEDULER_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()