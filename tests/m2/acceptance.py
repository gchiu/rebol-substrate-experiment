#!/usr/bin/env python3
"""M2 cooperative-multitasking acceptance tests (local-only, bounded).

Runs the examples/m2 applications with a task-enabled Glon build and checks
their output.  Python only drives the CLI and measures resources; all task
policy (spawn/yield/schedule/wait) is Glon (modules/task.glon).

Covered:
  * ten cooperative tasks make progress and complete
  * round-robin fairness when tasks yield
  * one task suspends on asynchronous file I/O and is resumed
  * completion, failure isolation and cancellation
  * bounded task capacity (M1_MAX_TASKS)
  * peak RSS and context-switch throughput
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


def run(args, cwd=None, timeout=30, rss=False):
    peak = 0
    proc = subprocess.Popen([str(args[0])] + [str(a) for a in args[1:]],
                            cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True,
                            start_new_session=True)
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
            stop.wait(0.01)

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
        fail(f"timeout: {args}")
    stop.set()
    if rss:
        t.join(timeout=2)
    return proc.returncode, out, err, peak


def expect(args, present, absent=(), cwd=None, timeout=30):
    rc, out, err, _ = run(args, cwd=cwd, timeout=timeout)
    if rc != 0:
        fail(f"{args[-1]}: rc={rc}\nstdout={out}\nstderr={err}")
    for s in present:
        if s not in out:
            fail(f"{args[-1]}: missing {s!r} in output {out!r}")
    for s in absent:
        if s in out:
            fail(f"{args[-1]}: unexpected {s!r} in output {out!r}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    ap.add_argument("--examples", default=str(ROOT / "examples" / "m2"))
    opts = ap.parse_args()
    glon = opts.glon
    ex = pathlib.Path(opts.examples)
    for f in ("tasks.glon", "fairness.glon", "failure.glon", "cancel.glon",
              "io-wait.glon", "stress.glon"):
        if not (ex / f).exists():
            fail(f"missing example {f}")
    if not os.path.exists(glon):
        fail(f"missing binary {glon}")

    # 1. ten tasks complete
    rc, out, err, peak = run([glon, ex / "tasks.glon"], rss=True)
    if rc != 0 or "M2_TASKS_OK count=30" not in out:
        fail(f"tasks.glon: rc={rc} out={out!r} err={err!r}")
    ok("ten cooperative tasks make progress and complete (count=30)")

    # 2. fairness
    expect([glon, ex / "fairness.glon"], ["M2_FAIR_OK 123123123"])
    ok("round-robin fairness across three yielding tasks (123123123)")

    # 3. failure isolation
    expect([glon, ex / "failure.glon"], ["M2_FAIL_ISO_T2", "M2_FAIL_ISO_OK"])
    ok("a task raising a SIN! is isolated; the scheduler and peer survive")

    # 4. cancellation
    expect([glon, ex / "cancel.glon"], ["M2_CANCEL_T2", "M2_CANCEL_OK"],
           absent=["M2_CANCEL_BAD"])
    ok("a cancelled task never runs; peers are unaffected")

    # 5. async I/O wait + resume
    td = tempfile.mkdtemp(prefix="m2-io-")
    try:
        (pathlib.Path(td) / "m2io.txt").write_text("hello-m2")
        expect([glon, "--io-root", td, "--allow-file-read",
                "--allow-file-write", ex / "io-wait.glon"],
               ["M2_IO_WAIT_OK hello-m2"], cwd=td)
        ok("a task suspends on asynchronous file I/O and is resumed")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 6. bounded capacity: M1_MAX_TASKS is 10 in the task build
    cap = tempfile.mkdtemp(prefix="m2-cap-")
    try:
        app = pathlib.Path(cap) / "cap.glon"
        app.write_text(
            "[\n"
            "spawn-n: func [k] [ either > k 0 [ spawn [ yield ]  spawn-n - k 1 ] [ none ] ]\n"
            "spawn-n 10\n"
            "extra: spawn [ yield ]\n"
            "either < extra 0 [ s/print \"M2_CAP_OK\" ] [ s/print \"M2_CAP_FAIL\" ]\n"
            "]\n")
        expect([glon, app], ["M2_CAP_OK"])
        ok("bounded task capacity enforced (11th spawn rejected)")
    finally:
        import shutil
        shutil.rmtree(cap, ignore_errors=True)

    # 7. stress: context-switch throughput and memory
    t0 = time.time()
    rc, out, err, peak = run([glon, ex / "stress.glon"], rss=True, timeout=60)
    dt = time.time() - t0
    if rc != 0 or "M2_STRESS_OK switches=50" not in out:
        fail(f"stress.glon: rc={rc} out={out!r} err={err!r}")
    ok(f"50 cooperative context switches in {dt:.2f}s; peak RSS {peak} KiB")

    print(f"PEAK_RSS_KIB={peak}")
    print("M2_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()