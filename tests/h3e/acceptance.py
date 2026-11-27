#!/usr/bin/env python3
"""H3E acceptance: task-mode HTTP response lifecycle (GLON-HTTP-H3E).

Ownership rule under test: once a handler produces a response it stays
available until the host collects it or the request ends; a response is
delivered at most once and only to its own request.

  A. immediate      -- a handler that responds directly gets its response
  B. asynchronous   -- the SQLite callback responds (task-waiting, and the G3H
                       style that returns without waiting)
  C. sequential     -- 24 mixed requests in a row, each correct
  D. overlap        -- pairs of simultaneous requests, responses correlated
  E. failures       -- raise -> 500; raise after respond keeps the response;
                       a second response is dropped; no response -> prompt 500
  F. capacity       -- with every task slot held, a new request waits for a
                       slot instead of failing
  G. disconnect     -- clients that vanish mid-request (and timeouts) release
                       their task slots; nothing is left behind
  H. bookkeeping    -- no uncollected response or open id remains; no host error
  I. after-response -- a task that already responded still runs its remaining
                       code (it is not cancelled when its request closes)

Local-only.  Requires a task+http+sqlite build (make h3e-test).
"""

import http.client
import os
import pathlib
import signal
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
APP = ROOT / "tests" / "h3e" / "lifecycle.glon"
TIMEOUT_MS = 2000          # server request deadline
M1_TASKS = 3               # make h3e-test builds with M1_TASKS=3


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


class Server:
    def __enter__(self):
        glon = ROOT / "glon"
        cert = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
        for p in (glon, cert, APP):
            if not p.exists():
                fail(f"missing {p}")
        self.td = tempfile.mkdtemp(prefix="h3e-")
        self.trace_path = os.path.join(self.td, "trace.log")
        self._tf = open(self.trace_path, "w+")
        if not wait_port_free():
            fail(f"port {PORT} still in use before start")
        self.proc = subprocess.Popen(
            [str(glon), "--http-bind", HOST, "--http-cert", str(cert),
             "--http-tasks", "--http-threads", "10",
             "--http-timeout-ms", str(TIMEOUT_MS),
             "--io-root", self.td, "--allow-file-read", "--allow-file-write",
             str(APP)],
            cwd=str(ROOT), stdout=self._tf, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        ctx = ssl._create_unverified_context()
        end = time.time() + 20
        while time.time() < end:
            if self.proc.poll() is not None:
                fail(f"server exited: {self.trace()}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ctx.wrap_socket(raw, server_hostname="localhost").close()
                return self
            except OSError:
                time.sleep(0.05)
        fail("server did not start")

    def trace(self):
        self._tf.flush()
        with open(self.trace_path) as f:
            return f.read()

    def get(self, path, timeout=8):
        ctx = ssl._create_unverified_context()
        conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
        try:
            conn.request("GET", path)
            r = conn.getresponse()
            return r.status, r.read().decode()
        finally:
            conn.close()

    def alive(self):
        return self.proc.poll() is None

    def __exit__(self, *a):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._tf.close()
        shutil.rmtree(self.td, ignore_errors=True)
        wait_port_free()


def expect(srv, path, status, body, what, max_s=None):
    t0 = time.time()
    st, data = srv.get(path)
    dt = time.time() - t0
    if (st, data) != (status, body):
        fail(f"{what}: {path} -> {st} {data!r}, expected {status} {body!r}")
    if max_s is not None and dt > max_s:
        fail(f"{what}: {path} took {dt:.2f}s (> {max_s}s)")
    return dt


def parallel(srv, paths, stagger=0.0):
    """GET every path at (nearly) the same moment; results in input order."""
    res = [None] * len(paths)
    barrier = threading.Barrier(len(paths))

    def one(i, p):
        barrier.wait()
        time.sleep(stagger * i)
        try:
            res[i] = srv.get(p)
        except Exception as e:   # noqa: BLE001 -- reported below
            res[i] = ("exc", repr(e))

    ts = [threading.Thread(target=one, args=(i, p)) for i, p in enumerate(paths)]
    for t in ts:
        t.start()
    for t in ts:
        t.join(30)
    return res


def disconnect_mid_request(path):
    """Send a request over TLS and close the connection without reading."""
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=3)
    s = ctx.wrap_socket(raw, server_hostname="localhost")
    s.sendall(f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
    time.sleep(0.1)
    s.close()


def main():
    with Server() as srv:
        # A. immediate
        dt = expect(srv, "/now", 200, "now", "immediate", max_s=1.0)
        ok(f"immediate response delivered (200 'now', {dt*1000:.0f} ms)")

        # B. asynchronous (SQLite callback responds)
        expect(srv, "/wait", 200, "count 0", "async task-wait", max_s=1.0)
        expect(srv, "/nowait", 200, "count 0", "async no-wait", max_s=1.0)
        ok("asynchronous responses delivered (task-wait and G3H-style no-wait)")

        # C. sequential mixed
        for k in range(24):
            kind = k % 4
            if kind == 0:
                expect(srv, "/now", 200, "now", f"seq {k}")
            elif kind == 1:
                expect(srv, "/wait", 200, "count 0", f"seq {k}")
            elif kind == 2:
                expect(srv, "/nowait", 200, "count 0", f"seq {k}")
            else:
                expect(srv, f"/echo?seq={k}", 200, f"seq={k}", f"seq {k}")
        ok("24 sequential mixed requests all correct")

        # D. overlapping pairs: responses must not cross
        pairs = [("/echo?p=a1", "/echo?p=b1"), ("/wait", "/echo?p=b2"),
                 ("/nowait", "/now"), ("/echo?p=a4", "/wait"),
                 ("/wait", "/nowait")]
        want = {"/now": (200, "now"), "/wait": (200, "count 0"),
                "/nowait": (200, "count 0")}
        for a, b in pairs:
            got = parallel(srv, [a, b])
            for p, g in zip((a, b), got):
                exp = want[p] if p in want else (200, p.split("?", 1)[1])
                if tuple(g) != exp:
                    fail(f"overlap {a} | {b}: {p} -> {g}, expected {exp}")
        got = parallel(srv, [f"/echo?q={i}" for i in range(M1_TASKS)])
        for i, g in enumerate(got):
            if tuple(g) != (200, f"q={i}"):
                fail(f"overlap x{M1_TASKS}: /echo?q={i} -> {g}")
        ok(f"overlapping requests correlated (5 pairs + {M1_TASKS} at once)")

        # E. failures
        expect(srv, "/fail", 500, "internal error", "handler raises")
        expect(srv, "/fail-after", 200, "kept", "raise after respond")
        expect(srv, "/twice", 200, "first", "double respond")
        dt = expect(srv, "/none", 500, "no response", "no response", max_s=1.0)
        ok(f"failures handled (raise 500, kept 200, first wins, no-response 500 in {dt*1000:.0f} ms)")

        # F. capacity: hold every task slot with /hang, then ask for /now
        holders = []
        hres = []

        def hold():
            try:
                hres.append(srv.get("/hang"))
            except Exception as e:   # noqa: BLE001
                hres.append(("exc", repr(e)))

        for _ in range(M1_TASKS):
            t = threading.Thread(target=hold)
            t.start()
            holders.append(t)
        time.sleep(1.0)              # slots taken; /hang deadlines at ~2.0 s
        t0 = time.time()
        st, data = srv.get("/now")   # its deadline is ~3.0 s
        dt = time.time() - t0
        for t in holders:
            t.join(10)
        if (st, data) != (200, "now"):
            fail(f"capacity: /now with all slots held -> {st} {data!r}")
        if [h[0] for h in hres] != [503] * M1_TASKS:
            fail(f"capacity: /hang holders -> {hres}")
        ok(f"with all {M1_TASKS} task slots held, /now waited {dt:.2f}s for a slot "
           f"and got 200; holders timed out with 503")

        # G. disconnects: abandoned requests must release their task slots
        for _ in range(M1_TASKS + 2):
            disconnect_mid_request("/hang")
        disconnect_mid_request("/now")
        disconnect_mid_request("/wait")
        time.sleep(TIMEOUT_MS / 1000.0 + 1.0)
        for k in range(M1_TASKS + 1):
            expect(srv, "/now", 200, "now", f"after disconnects {k}", max_s=1.0)
        expect(srv, "/wait", 200, "count 0", "async after disconnects", max_s=1.0)
        ok(f"{M1_TASKS + 4} disconnected requests released their slots; service continues")

        # I. work after the response: every /wait task resumes after task-wait
        def resumed():
            st, data = srv.get("/state")
            return int(data.rsplit("resumed=", 1)[1])
        r0 = resumed()
        for _ in range(5):
            expect(srv, "/wait", 200, "count 0", "after-response")
        time.sleep(0.2)
        r1 = resumed()
        if r1 - r0 != 5:
            fail(f"after-response: {r1 - r0} of 5 answered tasks resumed")
        ok("tasks that already responded run to completion (5 of 5 resumed)")

        # H. bookkeeping and health
        st, data = srv.get("/state")
        if not data.startswith("responses=0 open=0 "):
            fail(f"bookkeeping: /state -> {st} {data!r}")
        if not srv.alive():
            fail("server died")
        tr = srv.trace()
        bad = [l for l in tr.splitlines() if "error" in l.lower() or "halt" in l.lower()]
        if bad:
            fail(f"server reported errors: {bad[:5]}")
        ok("no uncollected response or open id remains; no host error")
    print("H3E ACCEPTANCE PASS")


if __name__ == "__main__":
    main()
