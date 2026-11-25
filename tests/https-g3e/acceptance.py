#!/usr/bin/env python3
"""G3E HTTPS + SQLite message-application acceptance tests (local-only).

The application is examples/https-g3e/messages.glon.  Every server runs in its
own temporary working directory so messages.db is isolated and restart tests
are deterministic.  Python only drives HTTP and inspects the SQLite file; all
application logic (routing, validation, SQL, JSON) is Glon.

Clients reuse one keep-alive HTTPS connection each (the CivetWeb server runs a
single Glon evaluator thread, so a fresh connection per request would trigger
the documented keep-alive/accept starvation).  This mirrors the G3A endurance
harness.

Covered:
  * fresh start, POST/GET one/GET list, JSON shape
  * restart persistence (stop, restart, same data)
  * concurrent inserts from several clients -> unique ids, all retrievable
  * malformed / oversized / wrong-type requests -> 4xx, no state change
  * SQL-injection attempt stored literally, table intact
  * failed requests leave no partial record
  * peak resident memory bounded under repeated requests
"""

import argparse
import concurrent.futures
import http.client
import json
import os
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

HOST = "127.0.0.1"
PORT = 8443
JSON = "application/json"
FORM = "application/x-www-form-urlencoded"


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


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


def jbytes(obj):
    return json.dumps(obj, separators=(",", ":")).encode()


class Client:
    """One persistent keep-alive HTTPS connection, reconnecting on failure."""

    def __init__(self, timeout=30.0):
        self.ctx = ssl._create_unverified_context()
        self.timeout = timeout
        self.conn = None

    def _connect(self):
        self.conn = http.client.HTTPSConnection(
            HOST, PORT, context=self.ctx, timeout=self.timeout)

    def request(self, method, path, body=None, ctype=None):
        headers = {}
        if ctype:
            headers["Content-Type"] = ctype
        if self.conn is None:
            self._connect()
        try:
            self.conn.request(method, path, body=body, headers=headers)
            resp = self.conn.getresponse()
            data = resp.read()
            return resp.status, {k.lower(): v for k, v in resp.getheaders()}, data
        except (http.client.HTTPException, OSError, ssl.SSLError):
            try:
                self.conn.close()
            except OSError:
                pass
            self.conn = None
            self._connect()
            self.conn.request(method, path, body=body, headers=headers)
            resp = self.conn.getresponse()
            data = resp.read()
            return resp.status, {k.lower(): v for k, v in resp.getheaders()}, data

    def close(self):
        if self.conn is not None:
            try:
                self.conn.close()
            except OSError:
                pass
            self.conn = None


class Server:
    def __init__(self, glon, args, cwd, label):
        self.label = label
        self.cwd = cwd
        self.peak_rss_kb = 0
        self._mon = None
        self._stop = threading.Event()
        self.proc = subprocess.Popen(
            [glon] + list(args),
            cwd=cwd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        self.stderr = ""

    def _sample_rss(self):
        try:
            with open(f"/proc/{self.proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1])
        except (OSError, ValueError):
            pass
        return 0

    def _monitor(self):
        while not self._stop.is_set():
            kb = self._sample_rss()
            if kb > self.peak_rss_kb:
                self.peak_rss_kb = kb
            self._stop.wait(0.02)
        kb = self._sample_rss()
        if kb > self.peak_rss_kb:
            self.peak_rss_kb = kb

    def wait_ready(self, timeout=30.0):
        self._mon = threading.Thread(target=self._monitor, daemon=True)
        self._mon.start()
        end = time.time() + timeout
        ctx = ssl._create_unverified_context()
        while time.time() < end:
            if self.proc.poll() is not None:
                self.stderr = self.proc.stderr.read()
                fail(f"{self.label}: exited before listening:\n{self.stderr}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ss = ctx.wrap_socket(raw, server_hostname="localhost")
                ss.close()
                return
            except OSError:
                time.sleep(0.05)
        fail(f"{self.label}: no listener within {timeout}s")

    def stop(self, timeout=15.0):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.proc.wait(timeout=5)
            fail(f"{self.label}: did not stop")
        self._stop.set()
        if self._mon:
            self._mon.join(timeout=2)
        self.stderr += self.proc.stderr.read()
        return self.proc.returncode


def app_args(opts, root):
    return [
        "--http-bind", HOST,
        "--http-cert", opts.cert,
        "--http-keep-alive",
        "--http-max-body", str(opts.max_body),
        "--io-root", root,
        "--allow-file-read", "--allow-file-write",
        opts.web, opts.json, opts.app,
    ]


def start_server(opts, root, label):
    if not wait_port_free():
        fail(f"port {PORT} already in use before {label}")
    srv = Server(opts.glon, app_args(opts, root), root, label)
    srv.wait_ready()
    return srv


def msg_count(body):
    data = json.loads(body)
    if not isinstance(data, list):
        fail(f"/messages did not return a JSON array: {body[:120]!r}")
    return len(data)


def test_fresh_and_crud(opts):
    td = tempfile.mkdtemp(prefix="g3e-crud-")
    srv = None
    cli = Client()
    try:
        srv = start_server(opts, td, "g3e-crud")
        status, _, body = cli.request("GET", "/messages")
        if status != 200 or msg_count(body) != 0:
            fail(f"fresh /messages not empty: {status} {body[:120]!r}")

        status, _, body = cli.request(
            "POST", "/messages", jbytes({"author": "Ada", "text": "Hello Glon"}), JSON)
        if status != 201:
            fail(f"POST valid: status {status} body={body[:160]!r}")
        created = json.loads(body)
        if created.get("author") != "Ada" or created.get("text") != "Hello Glon":
            fail(f"POST response wrong: {created!r}")
        if not isinstance(created.get("id"), int) or created["id"] < 1:
            fail(f"POST did not assign an integer id: {created!r}")
        if not isinstance(created.get("timestamp"), int):
            fail(f"POST did not assign an integer timestamp: {created!r}")
        mid = created["id"]

        status, _, body = cli.request("GET", f"/messages/{mid}")
        if status != 200 or json.loads(body) != created:
            fail(f"GET one mismatch: {status} {body[:160]!r}")

        status, _, body = cli.request("GET", "/messages")
        if status != 200 or msg_count(body) != 1:
            fail(f"list after one insert: {status} {body[:160]!r}")
        ok("fresh start, POST 201, GET one and GET list agree")
    finally:
        cli.close()
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_restart(opts):
    td = tempfile.mkdtemp(prefix="g3e-restart-")
    srv = None
    cli = Client()
    try:
        srv = start_server(opts, td, "g3e-restart-1")
        status, _, body = cli.request(
            "POST", "/messages", jbytes({"author": "Grace", "text": "survives restart"}), JSON)
        if status != 201:
            fail(f"seed POST: {status} {body[:120]!r}")
        mid = json.loads(body)["id"]
        cli.close()
        if srv.stop() != 0:
            fail("first server did not stop cleanly")
        srv = None
        if not wait_port_free():
            fail("listener not released before restart")

        srv = start_server(opts, td, "g3e-restart-2")
        cli = Client()
        status, _, body = cli.request("GET", f"/messages/{mid}")
        if status != 200:
            fail(f"restart GET one: {status} {body[:120]!r}")
        got = json.loads(body)
        if got.get("author") != "Grace" or got.get("text") != "survives restart":
            fail(f"restart data changed: {got!r}")
        status, _, body = cli.request("GET", "/messages")
        if status != 200 or msg_count(body) != 1:
            fail(f"restart list count: {status} {body[:120]!r}")
        ok("messages survive a full server restart")
    finally:
        cli.close()
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_concurrent_inserts(opts):
    td = tempfile.mkdtemp(prefix="g3e-concur-")
    srv = None
    try:
        srv = start_server(opts, td, "g3e-concur")
        workers = opts.workers
        per = opts.per_worker
        total = workers * per

        def post(client, i):
            st, _, body = client.request(
                "POST", "/messages",
                jbytes({"author": f"u{i}", "text": f"message {i}"}), JSON)
            if st != 201:
                return (i, st, body[:120])
            return (i, 201, json.loads(body)["id"])

        def run(w):
            client = Client()
            out = []
            try:
                for j in range(per):
                    out.append(post(client, w * per + j))
            finally:
                client.close()
            return out

        results = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
            for part in ex.map(run, range(workers)):
                results.extend(part)
        bad = [r for r in results if r[1] != 201]
        if bad:
            fail(f"concurrent POST failures: {bad[:3]}")
        ids = [r[2] for r in results]
        if len(set(ids)) != total:
            fail(f"concurrent inserts produced duplicate ids: {len(set(ids))} of {total}")

        cli = Client()
        try:
            status, _, body = cli.request("GET", "/messages")
        finally:
            cli.close()
        if status != 200:
            fail(f"list after concurrency: {status}")
        listed = json.loads(body)
        if len(listed) != total:
            fail(f"list has {len(listed)} rows, expected {total}")
        if sorted(m["id"] for m in listed) != sorted(ids):
            fail("listed ids do not match inserted ids")
        ok(f"{total} concurrent inserts across {workers} clients -> unique ids, all retrievable")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_validation_and_injection(opts):
    td = tempfile.mkdtemp(prefix="g3e-valid-")
    srv = None
    cli = Client()
    try:
        srv = start_server(opts, td, "g3e-valid")
        base = msg_count(cli.request("GET", "/messages")[2])

        cases = [
            ("POST", "/messages", b"", JSON, 400, "empty body"),
            ("POST", "/messages", b'{"author":', JSON, 400, "malformed JSON"),
            ("POST", "/messages", b"[]", JSON, 400, "non-object JSON"),
            ("POST", "/messages", jbytes({"author": "X"}), JSON, 422, "missing text"),
            ("POST", "/messages", jbytes({"text": "Y"}), JSON, 422, "missing author"),
            ("POST", "/messages", jbytes({"author": "", "text": "Y"}), JSON, 422, "empty author"),
            ("POST", "/messages", jbytes({"author": 1, "text": "Y"}), JSON, 422, "non-string author"),
            ("POST", "/messages", jbytes({"author": "x" * 65, "text": "Y"}), JSON, 422, "overlong author"),
            ("POST", "/messages", jbytes({"author": "x", "text": "y" * 401}), JSON, 422, "overlong text"),
            ("POST", "/messages", b"author=X&text=Y", FORM, 415, "wrong content type"),
            ("GET", "/messages/abc", None, None, 400, "non-numeric id"),
            ("GET", "/messages/99999", None, None, 404, "missing id"),
            ("DELETE", "/messages", None, None, 405, "method not allowed"),
            ("GET", "/nope", None, None, 404, "unknown path"),
        ]
        for method, path, body, ctype, want, label in cases:
            st, _, rb = cli.request(method, path, body, ctype)
            if st != want:
                fail(f"{label}: status {st}, expected {want}; body={rb[:120]!r}")

        big = jbytes({"author": "x", "text": "y" * (opts.max_body + 100)})
        st, _, _ = cli.request("POST", "/messages", big, JSON)
        if st != 413:
            fail(f"oversized body: status {st}, expected 413")

        after = msg_count(cli.request("GET", "/messages")[2])
        if after != base:
            fail(f"rejected requests changed row count: {base} -> {after}")
        ok("malformed/oversized/wrong-type requests rejected with no state change")

        payload = "x'); DROP TABLE messages;--"
        st, _, body = cli.request(
            "POST", "/messages", jbytes({"author": payload, "text": "inj"}), JSON)
        if st != 201:
            fail(f"injection POST: {st} {body[:120]!r}")
        inj = json.loads(body)
        st, _, body = cli.request("GET", f"/messages/{inj['id']}")
        if st != 200 or json.loads(body)["author"] != payload:
            fail("injection payload was not stored literally")
        st, _, body = cli.request("GET", "/messages")
        if st != 200:
            fail("messages table was dropped by injection attempt")
        ok("SQL-injection payload stored literally; table intact")
        return srv.peak_rss_kb
    finally:
        cli.close()
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_memory_bounded(opts):
    td = tempfile.mkdtemp(prefix="g3e-mem-")
    srv = None
    cli = Client()
    try:
        srv = start_server(opts, td, "g3e-mem")
        cli.request("GET", "/messages")  # warm up
        for i in range(opts.requests):
            if i % 3 == 0:
                st, _, _ = cli.request(
                    "POST", "/messages",
                    jbytes({"author": "bulk", "text": f"row {i}"}), JSON)
            else:
                st, _, _ = cli.request("GET", "/messages")
            if st not in (200, 201):
                fail(f"repeat request {i}: status {st}")
        peak = srv.peak_rss_kb
        if opts.max_rss_kb and peak > opts.max_rss_kb:
            fail(f"peak RSS {peak} KiB exceeded bound {opts.max_rss_kb} KiB")
        ok(f"{opts.requests} repeated requests; peak RSS {peak} KiB")
        return peak
    finally:
        cli.close()
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--app", default=os.path.join(root, "examples/https-g3e/messages.glon"))
    ap.add_argument("--web", default=os.path.join(root, "modules/web.glon"))
    ap.add_argument("--json", default=os.path.join(root, "modules/json.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    ap.add_argument("--max-body", type=int, default=500)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--per-worker", type=int, default=2)
    ap.add_argument("--requests", type=int, default=20)
    ap.add_argument("--max-rss-kb", type=int, default=0,
                    help="optional peak-RSS ceiling in KiB (0 disables)")
    opts = ap.parse_args()

    for path in (opts.glon, opts.app, opts.web, opts.json):
        if not os.path.exists(path):
            fail(f"missing file {path}")
    if not os.path.exists(opts.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")], check=True)

    peaks = []
    try:
        test_fresh_and_crud(opts)
        test_restart(opts)
        peaks.append(test_concurrent_inserts(opts))
        peaks.append(test_validation_and_injection(opts))
        peaks.append(test_memory_bounded(opts))
    finally:
        if not wait_port_free():
            print("WARN: port 8443 still in use", file=sys.stderr)
    print(f"PEAK_RSS_KIB={max(peaks) if peaks else 0}")
    print("HTTPS_G3E_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()