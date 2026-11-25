#!/usr/bin/env python3
"""H2A bounded asynchronous HTTP bridge acceptance tests (local-only).

The server runs with --http-bridge --http-threads N: CivetWeb workers copy each
request into a bounded host table and wait, while one interpreter-owner loop runs
the Glon application.  Python only drives sockets and checks responses.

H2A does NOT claim cooperative Glon-task interleaving (that is H2B); it verifies
the host-side bridge: admission, per-client correctness, isolation, timeouts,
disconnects, queue overflow and shutdown.
"""

import argparse
import http.client
import json
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

HOST = "127.0.0.1"
PORT = 8443
JSON = "application/json"
ROOT = pathlib.Path(__file__).resolve().parents[2]


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


class Server:
    def __init__(self, glon, args, cwd, label, threads):
        self.label = label
        self.peak_rss_kb = 0
        self._stop = threading.Event()
        self._mon = None
        argv = [glon, "--http-bind", HOST] + args
        self.proc = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE, text=True,
                                     start_new_session=True)
        self.threads = threads

    def _monitor(self):
        while not self._stop.is_set():
            try:
                with open(f"/proc/{self.proc.pid}/status") as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            self.peak_rss_kb = max(self.peak_rss_kb, int(line.split()[1]))
                            break
            except OSError:
                pass
            self._stop.wait(0.01)

    def wait_ready(self, timeout=30.0):
        self._mon = threading.Thread(target=self._monitor, daemon=True)
        self._mon.start()
        end = time.time() + timeout
        ctx = ssl._create_unverified_context()
        while time.time() < end:
            if self.proc.poll() is not None:
                fail(f"{self.label}: exited before listening: {self.proc.stderr.read()}")
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
        return self.proc.returncode


def request(method, path, body=None, ctype=None, timeout=30.0):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    headers = {}
    if ctype:
        headers["Content-Type"] = ctype
    conn.request(method, path, body=body, headers=headers)
    resp = conn.getresponse()
    data = resp.read()
    conn.close()
    return resp.status, data


def app_args(opts, root):
    return ["--http-cert", opts.cert, "--http-threads", str(opts.threads),
            "--http-bridge", "--http-timeout-ms", "30000",
            "--http-max-body", str(opts.max_body), "--io-root", root,
            "--allow-file-read", "--allow-file-write",
            opts.web, opts.json, opts.app]


def start(opts, root, label, threads=None):
    if not wait_port_free():
        fail(f"port {PORT} in use before {label}")
    srv = Server(opts.glon, app_args(opts, root) if threads is None else
                 ["--http-cert", opts.cert, "--http-threads", str(threads),
                  "--http-bridge", "--http-timeout-ms", "30000",
                  "--http-max-body", str(opts.max_body), "--io-root", root,
                  "--allow-file-read", "--allow-file-write",
                  opts.web, opts.json, opts.app], root, label, threads or opts.threads)
    srv.wait_ready()
    return srv


def test_basic(opts):
    td = tempfile.mkdtemp(prefix="h2a-basic-")
    srv = None
    try:
        srv = start(opts, td, "h2a-basic")
        st, b = request("GET", "/messages")
        if st != 200 or json.loads(b) != []:
            fail(f"fresh list: {st} {b[:120]!r}")
        st, b = request("POST", "/messages",
                        jbytes({"author": "Ada", "text": "bridge"}), JSON)
        if st != 201:
            fail(f"POST: {st} {b[:120]!r}")
        mid = json.loads(b)["id"]
        st, b = request("GET", f"/messages/{mid}")
        if st != 200 or json.loads(b)["id"] != mid:
            fail(f"GET one: {st} {b[:120]!r}")
        ok("basic CRUD through the bridge returns correct responses")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_overlap_and_idle(opts):
    td = tempfile.mkdtemp(prefix="h2a-overlap-")
    srv = None
    try:
        srv = start(opts, td, "h2a-overlap")
        idle = tls_socket()
        results = []
        n = opts.clients - 1

        def submit(i):
            t = time.time()
            st, b = request("POST", "/messages",
                            jbytes({"author": f"c{i}", "text": f"m{i}"}), JSON)
            return (i, st, time.time() - t, b)

        ths = [threading.Thread(target=lambda i=i: results.append(submit(i)))
               for i in range(n)]
        t0 = time.time()
        for t in ths:
            t.start()
        for t in ths:
            t.join()
        wall = time.time() - t0
        bad = [r for r in results if r[1] != 201]
        if bad:
            fail(f"overlap POST failures: {bad[:3]}")
        # each response must carry its own author (detects cross-delivery)
        for i, st, dt, b in results:
            obj = json.loads(b)
            if obj.get("author") != f"c{i}":
                fail(f"cross-delivery: client {i} got {obj.get('author')!r}")
        ids = [json.loads(r[3])["id"] for r in results]
        if len(set(ids)) != len(ids):
            fail("duplicate ids under overlap")
        idle.sendall(b"GET /messages HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        st, _ = read_response(idle)
        idle.close()
        if st != 200:
            fail(f"idle client not served after overlap: {st}")
        ok(f"{opts.clients} overlapping clients: wall {wall:.2f}s, per-client "
           f"correctness, idle client served, unique ids")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def tls_socket(timeout=30.0):
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=timeout)
    return ctx.wrap_socket(raw, server_hostname="localhost")


def read_response(sock):
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            return 0, b""
        buf += chunk
    head, rest = buf.split(b"\r\n\r\n", 1)
    status = int(head.split(b" ", 2)[1])
    length = 0
    for line in head.split(b"\r\n")[1:]:
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1].strip())
    while len(rest) < length:
        chunk = sock.recv(4096)
        if not chunk:
            break
        rest += chunk
    return status, rest[:length]


def test_slow_client(opts):
    td = tempfile.mkdtemp(prefix="h2a-slow-")
    srv = None
    try:
        srv = start(opts, td, "h2a-slow")
        body = jbytes({"author": "slow", "text": "trickle"})
        slow = tls_socket()
        head = (b"POST /messages HTTP/1.1\r\nHost: x\r\n"
                b"Content-Type: application/json\r\nContent-Length: "
                + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n")
        slow.sendall(head)
        slow.sendall(body[:2])
        time.sleep(0.2)
        t = time.time()
        st, rb = request("POST", "/messages",
                         jbytes({"author": "fast", "text": "prompt"}), JSON)
        fast = time.time() - t
        if st != 201 or fast > 3.0:
            fail(f"fast client blocked by slow: {st} {fast:.2f}s")
        for b in body[2:]:
            slow.sendall(bytes([b]))
            time.sleep(0.02)
        st, _ = read_response(slow)
        slow.close()
        if st != 201:
            fail(f"slow client failed: {st}")
        ok(f"slow/trickling client isolated: fast client {fast:.2f}s")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_disconnect(opts):
    td = tempfile.mkdtemp(prefix="h2a-disc-")
    srv = None
    try:
        srv = start(opts, td, "h2a-disc")
        # a client that sends a partial body then vanishes
        s = tls_socket()
        s.sendall(b"POST /messages HTTP/1.1\r\nHost: x\r\n"
                  b"Content-Type: application/json\r\nContent-Length: 100\r\n\r\n{")
        time.sleep(0.1)
        s.close()
        time.sleep(0.2)
        # a normal client must still get a correct response
        st, b = request("GET", "/messages")
        if st != 200:
            fail(f"after disconnect: {st}")
        st, b = request("POST", "/messages",
                        jbytes({"author": "after", "text": "disc"}), JSON)
        if st != 201 or json.loads(b)["author"] != "after":
            fail(f"post-disconnect POST wrong: {st} {b[:120]!r}")
        ok("a disconnected client does not corrupt a later request")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_overflow(opts):
    td = tempfile.mkdtemp(prefix="h2a-over-")
    srv = None
    try:
        # 20 workers, 20 simultaneous -> more than the 16-slot table
        srv = start(opts, td, "h2a-over", threads=20)
        results = []

        def submit(i):
            try:
                st, b = request("POST", "/messages",
                                jbytes({"author": f"o{i}", "text": "x"}), JSON)
                return st
            except Exception as e:
                return f"exc:{e}"

        ths = [threading.Thread(target=lambda i=i: results.append(submit(i)))
               for i in range(20)]
        for t in ths:
            t.start()
        for t in ths:
            t.join()
        oks = [r for r in results if r == 201]
        rej = [r for r in results if r == 503]
        other = [r for r in results if r not in (201, 503)]
        if other:
            fail(f"unexpected statuses: {other[:3]}")
        if not rej:
            fail("expected some 503 (queue overflow) with 20 workers / 16 slots")
        # the admitted requests must be correct and the table intact
        st, b = request("GET", "/messages")
        if st != 200:
            fail("list after overflow failed")
        ok(f"queue overflow controlled: {len(oks)} admitted, {len(rej)} rejected (503)")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_shutdown(opts):
    td = tempfile.mkdtemp(prefix="h2a-shut-")
    srv = None
    try:
        srv = start(opts, td, "h2a-shut")
        # open an idle keep-alive connection and a slow client, then stop
        idle = tls_socket()
        slow = tls_socket()
        slow.sendall(b"POST /messages HTTP/1.1\r\nHost: x\r\n"
                     b"Content-Type: application/json\r\nContent-Length: 50\r\n\r\n{")
        rc = srv.stop()
        idle.close()
        slow.close()
        if rc != 0:
            fail(f"shutdown rc={rc}")
        ok("shutdown releases waiting workers and request resources cleanly")
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_memory(opts):
    td = tempfile.mkdtemp(prefix="h2a-mem-")
    srv = None
    try:
        srv = start(opts, td, "h2a-mem")
        for i in range(opts.requests):
            st, _ = request("GET", "/messages")
            if st != 200:
                fail(f"repeat {i}: {st}")
        peak = srv.peak_rss_kb
        ok(f"{opts.requests} repeated requests; peak RSS {peak} KiB")
        return peak
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=str(ROOT / "glon"))
    ap.add_argument("--app", default=str(ROOT / "examples" / "https-g3e" / "messages.glon"))
    ap.add_argument("--web", default=str(ROOT / "modules" / "web.glon"))
    ap.add_argument("--json", default=str(ROOT / "modules" / "json.glon"))
    ap.add_argument("--cert", default=str(ROOT / "tests" / "https-g1" / ".cert" / "server.pem"))
    ap.add_argument("--max-body", type=int, default=500)
    ap.add_argument("--threads", type=int, default=10)
    ap.add_argument("--clients", type=int, default=10)
    ap.add_argument("--requests", type=int, default=50)
    opts = ap.parse_args()

    for p in (opts.glon, opts.app, opts.web, opts.json):
        if not os.path.exists(p):
            fail(f"missing {p}")
    if not os.path.exists(opts.cert):
        subprocess.run([os.path.join(ROOT, "tests/https-g1/make-cert.sh")], check=True)

    peaks = []
    try:
        peaks.append(test_basic(opts))
        peaks.append(test_overlap_and_idle(opts))
        peaks.append(test_slow_client(opts))
        peaks.append(test_disconnect(opts))
        test_overflow(opts)
        test_shutdown(opts)
        peaks.append(test_memory(opts))
    finally:
        if not wait_port_free():
            print("WARN: port 8443 still in use", file=sys.stderr)
    print(f"PEAK_RSS_KIB={max(peaks) if peaks else 0}")
    print("H2A_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()