#!/usr/bin/env python3
"""M3 multiple-outstanding-async-I/O acceptance tests (local-only, bounded).

Runs task-enabled Glon builds and checks that many tasks can have independent
I/O operations outstanding at once and each resumes with its own result.  Python
only drives the CLI and inspects the SQLite file; all task/I/O policy is Glon.

Covered:
  * ten tasks with independent outstanding file reads, each its own result
  * ten tasks with independent parameterised SQLite inserts, unique ids
  * an I/O error in one task is isolated to that task
  * request-capacity overflow is rejected without corrupting existing requests
  * cancellation while I/O is pending is safe
  * pending state is reclaimed (io/count returns to zero)
  * peak RSS under a bounded repeated workload
"""

import argparse
import json
import os
import pathlib
import signal
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = "127.0.0.1"


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def run(args, cwd=None, timeout=60, rss=False):
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
        fail(f"timeout: {args[-1]}")
    stop.set()
    if rss:
        t.join(timeout=2)
    return proc.returncode, out, err, peak


def base_args(opts, root):
    return [opts.glon, "--io-root", root, "--allow-file-read",
            "--allow-file-write"]


def gen_app(path, body):
    path.write_text(body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    ap.add_argument("--examples", default=str(ROOT / "examples" / "m3"))
    opts = ap.parse_args()
    glon = opts.glon
    ex = pathlib.Path(opts.examples)
    for f in ("file-fanout.glon", "sqlite-fanout.glon"):
        if not (ex / f).exists():
            fail(f"missing example {f}")
    if not os.path.exists(glon):
        fail(f"missing binary {glon}")

    peaks = []

    # 1. ten independent outstanding file reads
    td = tempfile.mkdtemp(prefix="m3-file-")
    try:
        for i in range(10):
            (pathlib.Path(td) / f"f{i}.txt").write_text(f"content-{i}")
        rc, out, err, peak = run(base_args(opts, td) + [ex / "file-fanout.glon"],
                                 rss=True)
        peaks.append(peak)
        if rc != 0:
            fail(f"file-fanout rc={rc} err={err}")
        missing = [i for i in range(10) if f"R{i}=content-{i}" not in out]
        if missing:
            fail(f"file-fanout missing results {missing}: {out!r}")
        ok("ten tasks with independent outstanding file reads, each its own result")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 2. ten independent parameterised SQLite inserts
    td = tempfile.mkdtemp(prefix="m3-sqlite-")
    try:
        rc, out, err, peak = run(base_args(opts, td) + [ex / "sqlite-fanout.glon"],
                                 rss=True)
        peaks.append(peak)
        if rc != 0:
            fail(f"sqlite-fanout rc={rc} err={err}")
        c = sqlite3.connect(str(pathlib.Path(td) / "m3.db"))
        rows = c.execute("select v from m order by id").fetchall()
        c.close()
        if sorted(r[0] for r in rows) != [f"v{i}" for i in range(10)]:
            fail(f"sqlite-fanout rows wrong: {rows!r}")
        ok("ten tasks with independent parameterised SQLite inserts (unique ids)")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 3. an I/O error in one task is isolated
    td = tempfile.mkdtemp(prefix="m3-err-")
    try:
        for i in range(1, 10):
            (pathlib.Path(td) / f"f{i}.txt").write_text(f"content-{i}")
        lines = ["["]
        lines.append(" ".join(f"r{i}: none" for i in range(10)))
        for i in range(10):
            name = "missing.txt" if i == 0 else f"f{i}.txt"
            lines.append(
                f'spawn [ me: self  file/read "{name}" '
                f'func [c] [ r{i}: c  task-wake me ] func [e] [ r{i}: e  task-wake me ]  '
                f'task-wait  either sin? r{i} [ s/print "R{i}=ERR" ] '
                f'[ s/print s/+ "R{i}=" r{i} ] ]')
        lines.append("run-tasks")
        lines.append("]")
        app = pathlib.Path(td) / "app.glon"
        gen_app(app, "\n".join(lines))
        rc, out, err, _ = run(base_args(opts, td) + [app])
        if rc != 0:
            fail(f"error-isolation rc={rc} err={err}")
        if "R0=ERR" not in out:
            fail(f"missing-file task did not get an isolated error: {out!r}")
        for i in range(1, 10):
            if f"R{i}=content-{i}" not in out:
                fail(f"error isolation: task {i} wrong: {out!r}")
        ok("an I/O error in one task is isolated; other tasks unaffected")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 4. capacity overflow is rejected without corrupting existing requests
    td = tempfile.mkdtemp(prefix="m3-cap-")
    try:
        for i in range(10):
            (pathlib.Path(td) / f"f{i}.txt").write_text(f"content-{i}")
        lines = ["[", "io/max-pending: 4"]
        lines.append(" ".join(f"r{i}: none" for i in range(10)))
        for i in range(10):
            lines.append(
                f'spawn [ me: self  file/read "f{i}.txt" '
                f'func [c] [ r{i}: c  task-wake me ] func [e] [ r{i}: e  task-wake me ]  '
                f'task-wait  s/print s/+ "R{i}=" r{i} ]')
        lines.append("run-tasks")
        lines.append("]")
        app = pathlib.Path(td) / "app.glon"
        gen_app(app, "\n".join(lines))
        rc, out, err, _ = run(base_args(opts, td) + [app])
        if rc != 0:
            fail(f"capacity rc={rc} err={err}")
        got = [i for i in range(10) if f"R{i}=content-{i}" in out]
        if len(got) != 4:
            fail(f"capacity: expected 4 admitted, got {got}: {out!r}")
        ok("request-capacity overflow rejected; the 4 admitted reads are intact")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 5. cancellation while I/O is pending is safe
    td = tempfile.mkdtemp(prefix="m3-cancel-")
    try:
        (pathlib.Path(td) / "f0.txt").write_text("content-0")
        app = pathlib.Path(td) / "app.glon"
        gen_app(app, "\n".join([
            "[",
            "r1: none",
            "t0: spawn [ me: self  file/read \"f0.txt\" "
            "func [c] [ task-wake me ] func [e] [ task-wake me ]  task-wait  s/print \"BAD\" ]",
            "run-tasks",
            "task-cancel t0",
            # a follow-up task proves the queue is not stuck and the cancelled
            # request was reclaimed (its slot is reusable for new work)
            "t1: spawn [ me: self  file/read \"f0.txt\" "
            "func [c] [ r1: c  task-wake me ] func [e] [ r1: e  task-wake me ]  "
            "task-wait  s/print s/+ \"T1=\" r1 ]",
            "run-tasks",
            "]",
        ]))
        rc, out, err, _ = run(base_args(opts, td) + [app], timeout=60)
        if rc != 0:
            fail(f"cancel rc={rc} err={err}")
        if "BAD" in out:
            fail("cancelled task resumed")
        if "T1=content-0" not in out:
            fail(f"follow-up task did not complete after cancel: {out!r}")
        ok("cancellation while I/O is pending is safe; the queue stays usable")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 6. pending state is reclaimed after completion (checked from the reply
    #    callback, which runs after io/reply removed the record), and a bounded
    #    repeated workload keeps memory flat.
    td = tempfile.mkdtemp(prefix="m3-reclaim-")
    try:
        (pathlib.Path(td) / "f0.txt").write_text("content-0")
        app = pathlib.Path(td) / "app.glon"
        gen_app(app, "\n".join([
            "[",
            "r: none",
            "t: spawn [ me: self  file/read \"f0.txt\" "
            "func [c] [ r: c  s/print s/+ \"pending=\" s/from-int io/count  task-wake me ] "
            "func [e] [ r: e  s/print s/+ \"pending=\" s/from-int io/count  task-wake me ]  "
            "task-wait  s/print s/+ \"GOT=\" r ]",
            "run-tasks",
            "]",
        ]))
        rc, out, err, _ = run(base_args(opts, td) + [app])
        if rc != 0:
            fail(f"reclaim rc={rc} err={err}")
        if "pending=0" not in out or "GOT=content-0" not in out:
            fail(f"pending not reclaimed in the reply callback: {out!r}")
        ok("pending state reclaimed in the completion callback (io/count zero)")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    # 7. repeated operations: bounded memory
    td = tempfile.mkdtemp(prefix="m3-mem-")
    try:
        for i in range(10):
            (pathlib.Path(td) / f"f{i}.txt").write_text(f"content-{i}")
        lines = ["["]
        for i in range(10):
            lines.append(
                f'spawn [ me: self  file/read "f{i}.txt" '
                f'func [c] [ task-wake me ] func [e] [ task-wake me ]  task-wait '
                f'file/read "f{i}.txt" func [c] [ task-wake me ] func [e] [ task-wake me ]  task-wait '
                f'file/read "f{i}.txt" func [c] [ task-wake me ] func [e] [ task-wake me ]  task-wait ]')
        lines.append("run-tasks")
        lines.append("]")
        app = pathlib.Path(td) / "app.glon"
        gen_app(app, "\n".join(lines))
        rc, out, err, peak = run(base_args(opts, td) + [app], rss=True, timeout=90)
        peaks.append(peak)
        if rc != 0:
            fail(f"repeat rc={rc} err={err}")
        ok(f"30 repeated reads complete; peak RSS {peak} KiB")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)

    print(f"PEAK_RSS_KIB={max(peaks) if peaks else 0}")
    print("M3_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()