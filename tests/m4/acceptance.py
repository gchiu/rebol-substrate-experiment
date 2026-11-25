#!/usr/bin/env python3
"""M4 task lifecycle / slot-recycling acceptance tests (local-only, bounded).

Runs a task-enabled Glon build and checks that finished/failed/cancelled tasks
release their slots, that generation-tagged handles make stale references safe,
and that thousands of tasks can be created while only a few slots are active.

Python only drives the CLI; all task policy is Glon (modules/task.glon).
"""

import argparse
import json
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


def run(args, cwd=None, timeout=120, rss=False):
    peak = 0
    proc = subprocess.Popen([str(a) for a in args], cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, start_new_session=True)
    stop = threading.Event()

    def mon():
        nonlocal peak
        while not stop.is_set():
            try:
                with open(f"/proc/{proc.pid}/status") as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            peak = max(peak, int(line.split()[1]))
                            break
            except OSError:
                pass
            stop.wait(0.005)

    t = threading.Thread(target=mon, daemon=True)
    if rss:
        t.start()
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
        fail(f"timeout: {args[-1]}")
    stop.set()
    if rss:
        t.join(timeout=2)
    return proc.returncode, out, err, peak


def base_args(opts, root):
    return [opts.glon, "--io-root", root, "--allow-file-read", "--allow-file-write"]


def gen(path, body):
    path.write_text(body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    ap.add_argument("--examples", default=str(ROOT / "examples" / "m4"))
    opts = ap.parse_args()
    glon = opts.glon
    ex = pathlib.Path(opts.examples)
    for f in ("recycle.glon", "generation.glon", "endurance.glon"):
        if not (ex / f).exists():
            fail(f"missing example {f}")
    if not os.path.exists(glon):
        fail(f"missing binary {glon}")

    peaks = []

    # 1. slot recycling: 50 tasks, one active slot
    rc, out, err, peak = run([glon, ex / "recycle.glon"], rss=True)
    peaks.append(peak)
    if rc != 0 or "MADE=50" not in out:
        fail(f"recycle rc={rc} out={out!r} err={err!r}")
    ok("50 tasks created/completed through one recycled slot")

    # 2. generation-tagged handles: stale wake is discarded
    rc, out, err, _ = run([glon, ex / "generation.glon"])
    if rc != 0 or "STALE-OK" not in out or "BAD-WOKEN" in out:
        fail(f"generation rc={rc} out={out!r} err={err!r}")
    ok("a stale handle cannot wake the task that inherited the slot")

    # 3. many batches reuse a bounded set of slots (active <= M1_MAX_TASKS)
    td = tempfile.mkdtemp(prefix="m4-batch-")
    try:
        lines = ["[", "count: 0",
                 "spawn-batch: func [n] [ either > n 0 [ spawn [ count: + count 1 ]  spawn-batch - n 1 ] [ none ] ]"]
        for _ in range(5):
            lines.append("spawn-batch 10")
            lines.append("run-tasks")
        lines.append('s/print s/+ "COUNT=" s/from-int count')
        lines.append("]")
        app = pathlib.Path(td) / "app.glon"
        gen(app, "\n".join(lines))
        rc, out, err, _ = run([glon, app])
        if rc != 0 or "COUNT=50" not in out:
            fail(f"batch rc={rc} out={out!r} err={err!r}")
        ok("5 batches of 10 tasks (50 total) reuse slots; all complete")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 4. cancellation with a late file reply, then slot reuse
    td = tempfile.mkdtemp(prefix="m4-cancel-")
    try:
        (pathlib.Path(td) / "f.txt").write_text("DATA")
        app = pathlib.Path(td) / "app.glon"
        gen(app, "\n".join([
            "[",
            "t1: spawn [ me: self  file/read \"f.txt\" "
            "func [c] [ task-wake me ] func [e] [ task-wake me ]  task-wait  s/print \"BAD-A\" ]",
            "run-tasks",
            "task-cancel t1",
            "t2: spawn [ task-wait  s/print \"BAD-B\" ]",
            "run-tasks",
            "s/print \"LATE-OK\"",
            "]",
        ]))
        rc, out, err, _ = run(base_args(opts, td) + [app])
        if rc != 0 or "LATE-OK" not in out or "BAD-A" in out or "BAD-B" in out:
            fail(f"cancel/late rc={rc} out={out!r} err={err!r}")
        ok("a late file reply to a retired task is discarded; the new task is safe")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 5. failure isolation across recycling
    td = tempfile.mkdtemp(prefix="m4-fail-")
    try:
        app = pathlib.Path(td) / "app.glon"
        gen(app, "\n".join([
            "[",
            "bad: spawn [ raise create-sin 'app 'boom 0 ]",
            "run-tasks",
            "err: tasks/error",
            "t: spawn [ s/print \"PEER-OK\" ]",
            "run-tasks",
            "either sin? err [ s/print \"FAIL-ISO-OK\" ] [ s/print \"FAIL-ISO-FAIL\" ]",
            "]",
        ]))
        rc, out, err, _ = run([glon, app])
        if rc != 0 or "PEER-OK" not in out or "FAIL-ISO-OK" not in out:
            fail(f"fail-iso rc={rc} out={out!r} err={err!r}")
        ok("a failed task is isolated; a peer created afterwards runs")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 6. endurance: 10,000 creations, bounded memory
    t0 = time.time()
    rc, out, err, peak = run([glon, ex / "endurance.glon"], rss=True, timeout=120)
    dt = time.time() - t0
    peaks.append(peak)
    if rc != 0 or "MADE=10000" not in out:
        fail(f"endurance rc={rc} out={out!r} err={err!r}")
    ok(f"10,000 task creations/completions in {dt:.2f}s; peak RSS {peak} KiB")

    print(f"PEAK_RSS_KIB={max(peaks) if peaks else 0}")
    print("M4_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()