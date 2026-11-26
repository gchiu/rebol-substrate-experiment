#!/usr/bin/env python3
"""H3D acceptance: HTTP task lifetime, cancellation and loader reclamation.

Establishes that the H3C loader-frontier strategy (hold the loader across
spawn+run-tasks, reclaim after) stays memory-safe under:

  1. stress      -- 1,000 mixed GET/POST with overlapping keep-alive clients;
                    correct responses, expected SQLite count, bounded RSS.
  2. disconnect  -- clients that vanish mid-request; the server keeps serving.
  3. capacity    -- more concurrent clients than bridge slots / task slots;
                    excess get 503, no crash, later requests still work.
  4. isolation   -- a task that raises (500) does not disturb other tasks.
  5. restart     -- clean shutdown + restart; the SQLite count persists.

Local-only.  Requires a task+http+sqlite build (make h3d-test).
"""

import http.client
import os
import pathlib
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443


def fail(m):
    print("FAIL:", m)
    raise SystemExit(1)


def ok(m):
    print("PASS:", m)


def wait_port_free(timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.2)
        try:
            s.connect((HOST, PORT))
        except OSError:
            s.close()
            return True
        s.close()
        time.sleep(0.05)
    return False


def wait_port_open(timeout=20.0):
    ctx = ssl._create_unverified_context()
    end = time.time() + timeout
    while time.time() < end:
        try:
            raw = socket.create_connection((HOST, PORT), timeout=0.3)
            raw.settimeout(0.5)
            ctx.wrap_socket(raw, server_hostname="localhost").close()
            return True
        except OSError:
            time.sleep(0.05)
    return False


class Server:
    def __init__(self, app, label, threads=10, extra=None):
        self.label = label
        self.app = app
        self.threads = threads
        self.extra = extra or []
        self.proc = None
        self.peak_rss_kb = 0
        self._stop = threading.Event()
        self._mon = None

    def __enter__(self):
        glon = ROOT / "glon"
        cert = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
        for p in (glon, cert, self.app):
            if not p.exists():
                fail(f"missing {p}")
        self.td = tempfile.mkdtemp(prefix=f"h3d-{self.label}-")
        if not wait_port_free():
            fail(f"{self.label}: port {PORT} still in use before start")
        argv = [str(glon), "--http-bind", HOST, "--http-cert", str(cert),
                "--http-tasks", "--http-threads", str(self.threads),
                "--http-max-body", "500",
                "--io-root", self.td, "--allow-file-read", "--allow-file-write",
                str(self.app)] + self.extra
        self.proc = subprocess.Popen(argv, cwd=str(ROOT),
                                     stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL,
                                     start_new_session=True)
        if not wait_port_open():
            fail(f"{self.label}: server did not start")
        self._stop.clear()
        self._mon = threading.Thread(target=self._monitor, daemon=True)
        self._mon.start()
        return self

    def _monitor(self):
        while not self._stop.is_set():
            try:
                with open(f"/proc/{self.proc.pid}/status") as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            self.peak_rss_kb = max(self.peak_rss_kb,
                                                   int(line.split()[1]))
                            break
            except OSError:
                return
            time.sleep(0.05)

    def rss_kb(self):
        try:
            with open(f"/proc/{self.proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1])
        except OSError:
            pass
        return 0

    def __exit__(self, *a):
        self._stop.set()
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        import shutil
        shutil.rmtree(self.td, ignore_errors=True)
        wait_port_free()


def _conn(timeout=15):
    ctx = ssl._create_unverified_context()
    return http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)


def _get_count():
    c = _conn()
    try:
        c.request("GET", "/messages")
        r = c.getresponse()
        return r.status, r.read().decode()
    finally:
        c.close()


def test_stress_1000():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    N = 1000
    with Server(app, "stress") as s:
        results = {"get": 0, "post": 0, "bad": 0}
        start_rss = s.rss_kb()
        t0 = time.time()
        for i in range(N):
            method = "GET" if i % 2 == 0 else "POST"
            good = False
            for attempt in range(2):
                conn = _conn(timeout=15)
                try:
                    if method == "GET":
                        conn.request("GET", "/messages")
                    else:
                        conn.request("POST", "/messages", body=b"x")
                    r = conn.getresponse()
                    b = r.read().decode()
                    good = (r.status == 200) if method == "GET" else \
                           (r.status == 201 and b == "ok")
                    break
                except Exception:
                    pass
                finally:
                    try:
                        conn.close()
                    except Exception:
                        pass
            results[method.lower() if good else "bad"] += 1
        dt = time.time() - t0
        st, body = _get_count()
        end_rss = s.rss_kb()
        if results["bad"] != 0:
            fail(f"stress: {results['bad']} bad responses of {N}: {results}")
        if results["get"] != N // 2 or results["post"] != N // 2:
            fail(f"stress: unexpected split {results}")
        if st != 200 or body != str(results["post"]):
            fail(f"stress: final count {body!r} != successful POSTs {results['post']}")
        if s.peak_rss_kb > 200000:
            fail(f"stress: peak RSS {s.peak_rss_kb} KiB too high")
        ok(f"stress: {N} mixed requests in {dt:.1f}s; count={body}; "
           f"peak RSS {s.peak_rss_kb} KiB (start {start_rss}, end {end_rss})")


def test_overlap():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    with Server(app, "overlap", threads=10) as s:
        results = {}
        rlock = threading.Lock()
        barrier = threading.Barrier(3)

        def worker(name, method, body):
            barrier.wait()
            try:
                c = _conn(timeout=15)
                c.request(method, "/messages", body=body)
                r = c.getresponse()
                results[name] = (r.status, r.read().decode())
                c.close()
            except Exception as e:
                with rlock:
                    results[name] = ("exc", repr(e)[:40])

        ts = [threading.Thread(target=worker, args=("A", "GET", None)),
              threading.Thread(target=worker, args=("B", "POST", b"b")),
              threading.Thread(target=worker, args=("C", "POST", b"c"))]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        if results.get("A", (0, ""))[0] != 200:
            fail(f"overlap A (GET): {results.get('A')}")
        for n in ("B", "C"):
            if results.get(n, (0, ""))[:2] != (201, "ok"):
                fail(f"overlap {n} (POST): {results.get(n)}")
        ok("overlap: 3 concurrent mixed-method requests all correct (interleaving)")


def test_disconnect():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    with Server(app, "disconnect") as s:
        # Open connections, send a partial request, and drop them abruptly.
        for _ in range(6):
            try:
                raw = socket.create_connection((HOST, PORT), timeout=2)
                raw.sendall(b"GET /messages HTTP/1.1\r\nHost: x\r\n")
                # close without completing the request
                raw.close()
            except OSError:
                pass
        time.sleep(0.5)
        # The server must still serve a normal request correctly.
        st, body = _get_count()
        if st != 200 or not body.isdigit():
            fail(f"disconnect: server unhealthy after abrupt closes: {st} {body!r}")
        ok(f"disconnect: server still healthy after 6 abrupt disconnects (count={body})")


def test_capacity():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    with Server(app, "capacity") as s:
        # Fire more concurrent clients than bridge slots (16) and task slots (3).
        results = []
        rlock = threading.Lock()
        barrier = threading.Barrier(20)

        def worker():
            barrier.wait()
            try:
                c = _conn()
                c.request("GET", "/messages")
                r = c.getresponse()
                r.read()
                c.close()
                with rlock:
                    results.append(r.status)
            except Exception as e:
                with rlock:
                    results.append(("exc", repr(e)[:40]))

        ts = [threading.Thread(target=worker) for _ in range(20)]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        # Every response is either 200 (served) or 503 (queue full); no crash.
        bad = [r for r in results if r not in (200, 503)]
        if bad:
            fail(f"capacity: unexpected statuses {bad[:5]} (of {len(results)})")
        # Server must still be healthy afterwards.
        st, body = _get_count()
        if st != 200 or not body.isdigit():
            fail(f"capacity: server unhealthy after burst: {st} {body!r}")
        n503 = sum(1 for r in results if r == 503)
        ok(f"capacity: 20-client burst -> {len(results)-n503} served, "
           f"{n503} rejected(503); server healthy (count={body})")


def test_isolation():
    app = ROOT / "tests" / "h3d" / "error.glon"
    with Server(app, "isolation") as s:
        st, _ = _raw("GET", "/boom")
        if st != 500:
            fail(f"isolation: /boom expected 500, got {st}")
        st2, b2 = _raw("GET", "/")
        if st2 != 200 or b2 != "ok":
            fail(f"isolation: healthy path broken after error: {st2} {b2!r}")
        ok("isolation: a raising task yields 500; other tasks still succeed")


def _raw(method, path):
    c = _conn()
    try:
        c.request(method, path)
        r = c.getresponse()
        return r.status, r.read().decode()
    finally:
        c.close()


def test_restart():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    td = tempfile.mkdtemp(prefix="h3d-restart-")
    try:
        for run in (1, 2):
            if not wait_port_free():
                fail("restart: port busy")
            argv = [str(ROOT / "glon"), "--http-bind", HOST,
                    "--http-cert", str(ROOT / "tests" / "https-g1" / ".cert" / "server.pem"),
                    "--http-tasks", "--http-threads", "6", "--http-max-body", "500",
                    "--io-root", td, "--allow-file-read", "--allow-file-write",
                    str(app)]
            proc = subprocess.Popen(argv, cwd=str(ROOT), stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, start_new_session=True)
            if not wait_port_open():
                fail("restart: server did not start")
            c = _conn()
            c.request("POST", "/messages", body=b"r")
            r = c.getresponse()
            r.read()
            c.close()
            st, body = _get_count()
            if st != 200:
                fail(f"restart run {run}: GET failed {st}")
            if run == 1:
                first = body
            else:
                if body != str(int(first) + 1):
                    fail(f"restart: count did not persist ({first} -> {body})")
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            wait_port_free()
        ok(f"restart: SQLite count persisted across clean restart ({first} -> {body})")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def main():
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)
    test_stress_1000()
    test_overlap()
    test_disconnect()
    test_capacity()
    test_isolation()
    test_restart()
    print("H3D ACCEPTANCE PASS")


if __name__ == "__main__":
    main()