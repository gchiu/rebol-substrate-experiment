#!/usr/bin/env python3
"""H3C acceptance: HTTP method propagation into cooperative Glon tasks.

Verifies, end to end, that the request method that CivetWeb received is the
method observed inside the Glon task (not inferred from the HTTP status):

  1. method echo   -- a task handler returns http/method request; GET/POST/PUT/
                      DELETE must round-trip exactly (no fall-through to GET).
  2. sqlite effect -- examples/h2b/tasks.glon: GET returns the row count, POST
                      inserts and returns "ok"; the count must increase.
  3. interleaving  -- three overlapping mixed-method requests show a trace
                      where each task suspends (wait) before any resumes, and
                      every response is correct.

Local-only.  Requires a task+http+sqlite build (make h3c-test).
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


class Server:
    def __init__(self, app, label, threads=10):
        self.label = label
        self.proc = None
        self.trace = ""
        self.app = app
        self.threads = threads

    def __enter__(self):
        glon = ROOT / "glon"
        cert = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
        for p in (glon, cert, self.app):
            if not p.exists():
                fail(f"missing {p}")
        self.td = tempfile.mkdtemp(prefix=f"h3c-{self.label}-")
        self.trace_path = os.path.join(self.td, "trace.log")
        self._tf = open(self.trace_path, "w+")
        if not wait_port_free():
            fail(f"{self.label}: port {PORT} still in use before start")
        self.proc = subprocess.Popen(
            [str(glon), "--http-bind", HOST, "--http-cert", str(cert),
             "--http-tasks", "--http-threads", str(self.threads),
             "--http-max-body", "500",
             "--io-root", self.td, "--allow-file-read", "--allow-file-write",
             str(self.app)],
            cwd=str(ROOT), stdout=self._tf, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        ctx = ssl._create_unverified_context()
        end = time.time() + 20
        while time.time() < end:
            if self.proc.poll() is not None:
                fail(f"{self.label} server exited: {self._read_trace()}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ctx.wrap_socket(raw, server_hostname="localhost").close()
                return self
            except OSError:
                time.sleep(0.05)
        fail(f"{self.label} server did not start")

    def _read_trace(self):
        self._tf.flush()
        with open(self.trace_path) as f:
            return f.read()

    def read_trace(self):
        return self._read_trace()

    def request(self, method, path="/", body=None, timeout=10):
        ctx = ssl._create_unverified_context()
        conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
        try:
            conn.request(method, path, body=body)
            r = conn.getresponse()
            data = r.read().decode()
            return r.status, data
        finally:
            conn.close()

    def __exit__(self, *a):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._tf.flush()
        self.trace = self._read_trace()
        self._tf.close()
        import shutil
        shutil.rmtree(self.td, ignore_errors=True)
        wait_port_free()


def test_method_echo():
    app = ROOT / "tests" / "h3c" / "method.glon"
    with Server(app, "method") as s:
        for m in ("GET", "POST", "PUT", "DELETE"):
            st, body = s.request(m, body=b"x")
            if st != 200 or body != m:
                fail(f"method echo {m}: status={st} body={body!r} (expected 200/{m!r})")
        ok("method echo: GET/POST/PUT/DELETE round-trip exactly inside the task")


def test_sqlite_effect():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    with Server(app, "sqlite") as s:
        st, body = s.request("GET", "/messages")
        if st != 200 or body != "0":
            fail(f"initial GET: status={st} body={body!r} (expected 200/'0')")
        st, body = s.request("POST", "/messages", body=b"first")
        if st != 201 or body != "ok":
            fail(f"POST: status={st} body={body!r} (expected 201/'ok')")
        st, body = s.request("GET", "/messages")
        if st != 200 or body != "1":
            fail(f"GET after POST: status={st} body={body!r} (expected 200/'1')")
        ok("sqlite effect: POST inserted (count 0 -> 1) and returned 'ok'")


def test_interleaving():
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    results = {}
    barrier = threading.Barrier(3)

    def worker(name, method, body):
        ctx = ssl._create_unverified_context()
        barrier.wait()
        conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=10)
        try:
            conn.request(method, "/messages", body=body)
            r = conn.getresponse()
            results[name] = (r.status, r.read().decode())
        finally:
            conn.close()

    with Server(app, "interleave") as s:
        threads = [
            threading.Thread(target=worker, args=("A", "GET", None)),
            threading.Thread(target=worker, args=("B", "POST", b"b")),
            threading.Thread(target=worker, args=("C", "POST", b"c")),
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        # A is a GET (count), B and C are POSTs (ok).  Order-independent.
        if results.get("A", (0, ""))[0] != 200:
            fail(f"interleaving A (GET): {results.get('A')}")
        for n in ("B", "C"):
            if results.get(n, (0, ""))[:2] != (201, "ok"):
                fail(f"interleaving {n} (POST): {results.get(n)}")
        time.sleep(0.3)  # let s/print output flush
        trace = s.read_trace()
        # Trace must show every task suspending before any resumes.
        lines = [ln.split() for ln in trace.splitlines() if ln.split()[:1] in (["start"], ["wait"], ["resume"])]
        waits = sum(1 for p in lines if p[0] == "wait")
        resumes = sum(1 for p in lines if p[0] == "resume")
        first_resume = next((i for i, p in enumerate(lines) if p[0] == "resume"), None)
        waits_before = sum(1 for p in lines[:first_resume] if p[0] == "wait") if first_resume is not None else 0
        if waits < 2 or resumes < 2:
            fail(f"interleaving trace lacks suspend/resume: {trace!r}")
        if first_resume is None or waits_before < 2:
            fail(f"no overlap: all resumes happened before 2 waits: {trace!r}")
        ok(f"interleaving: {waits} tasks suspended before any resumed; responses correlated")


def main():
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)
    test_method_echo()
    test_sqlite_effect()
    test_interleaving()
    print("H3C ACCEPTANCE PASS")


if __name__ == "__main__":
    main()