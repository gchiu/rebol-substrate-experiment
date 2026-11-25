#!/usr/bin/env python3
"""G3F HTTPS concurrency acceptance tests (local-only, bounded).

The application is examples/https-g3e/messages.glon (unchanged).  The server is
started with a bounded CivetWeb worker pool (--http-threads N).  Exactly one
thread is inside the Glon session at a time (the host's evaluator-ownership
mutex), so the single Glon interpreter and the shared SQLite connection stay
serialised while overlapping connections make progress.

Python only drives sockets and inspects the SQLite file; all application logic
is Glon.

Covered:
  * 10 overlapping clients: one idle keep-alive + nine submitters
  * genuine overlap (wall time well below the sum of per-request latencies),
    not merely sequential requests that happen to succeed
  * the idle client does not block the submitters and is still usable after
  * unique ids across overlapping inserts; data correct after restart
  * a slow/trickling client cannot block or corrupt another request
  * the worker-pool bound is enforced (0 and >32 rejected)
  * peak resident memory under repeated overlapping load
"""

import argparse
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


def tls_socket(timeout=30.0):
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=timeout)
    return ctx.wrap_socket(raw, server_hostname="localhost")


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


def read_response(sock):
    """Read one HTTP response (status line + Content-Length body) from a socket."""
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


class Server:
    def __init__(self, glon, args, cwd, label):
        self.label = label
        self.peak_rss_kb = 0
        self._mon = None
        self._stop = threading.Event()
        self.proc = subprocess.Popen(
            [glon] + list(args), cwd=cwd, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, start_new_session=True)
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
        while time.time() < end:
            if self.proc.poll() is not None:
                self.stderr = self.proc.stderr.read()
                fail(f"{self.label}: exited before listening:\n{self.stderr}")
            try:
                s = tls_socket(timeout=1.0)
                s.close()
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


def app_args(opts, root, threads):
    return [
        "--http-bind", HOST,
        "--http-cert", opts.cert,
        "--http-threads", str(threads),
        "--http-timeout-ms", "30000",
        "--http-max-body", str(opts.max_body),
        "--io-root", root,
        "--allow-file-read", "--allow-file-write",
        opts.web, opts.json, opts.app,
    ]


def start_server(opts, root, label, threads):
    if not wait_port_free():
        fail(f"port {PORT} already in use before {label}")
    srv = Server(opts.glon, app_args(opts, root, threads), root, label)
    srv.wait_ready()
    return srv


def post(client, i):
    t = time.time()
    st, _, body = client.request(
        "POST", "/messages", jbytes({"author": f"c{i}", "text": f"msg{i}"}), JSON)
    return (i, st, time.time() - t, body)


def run_post(i):
    """One request on a short-lived connection (closed so the pool is freed)."""
    cli = Client()
    try:
        return post(cli, i)
    finally:
        cli.close()


def test_overlap_and_idle(opts):
    td = tempfile.mkdtemp(prefix="g3f-overlap-")
    srv = None
    try:
        srv = start_server(opts, td, "g3f-overlap", opts.threads)
        idle = tls_socket()
        results = []
        threads = [threading.Thread(target=lambda i=i: results.append(run_post(i)))
                   for i in range(opts.clients - 1)]
        t0 = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        wall = time.time() - t0
        bad = [r for r in results if r[1] != 201]
        if bad:
            fail(f"overlapping POST failures: {bad[:3]}")
        ids = [json.loads(r[3])["id"] for r in results]
        if len(set(ids)) != len(ids):
            fail(f"duplicate ids under overlap: {ids}")
        lat = [r[2] for r in results]
        total = sum(lat)
        if wall >= 0.65 * total:
            fail(f"requests look sequential: wall {wall:.2f}s vs sum {total:.2f}s")
        if max(lat) >= total:
            fail("no overlap: max latency >= sum")
        # The idle client must still be usable after the others ran.
        idle.sendall(b"GET /messages HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        st, _ = read_response(idle)
        idle.close()
        if st != 200:
            fail(f"idle client did not get a response after overlap: {st}")
        ok(f"{opts.clients} overlapping clients: wall {wall:.2f}s < sum {total:.2f}s; "
           f"idle client still served; unique ids")
        return srv.peak_rss_kb, ids
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_restart(opts):
    td = tempfile.mkdtemp(prefix="g3f-restart-")
    srv = None
    try:
        srv = start_server(opts, td, "g3f-restart-1", opts.threads)
        ids = []
        for i in range(6):
            _, st, _, body = run_post(i)
            if st != 201:
                fail(f"seed POST {i}: {st}")
            ids.append(json.loads(body)["id"])
        if srv.stop() != 0:
            fail("first server did not stop cleanly")
        srv = None
        if not wait_port_free():
            fail("listener not released before restart")
        srv = start_server(opts, td, "g3f-restart-2", opts.threads)
        cli = Client()
        st, _, body = cli.request("GET", "/messages")
        cli.close()
        if st != 200:
            fail(f"restart list: {st}")
        listed = json.loads(body)
        got = sorted(m["id"] for m in listed)
        if got != sorted(ids):
            fail(f"restart ids changed: {got} vs {sorted(ids)}")
        ok(f"all {len(ids)} messages correct after restart")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_slow_client(opts):
    td = tempfile.mkdtemp(prefix="g3f-slow-")
    srv = None
    try:
        srv = start_server(opts, td, "g3f-slow", opts.threads)
        body = jbytes({"author": "slow", "text": "trickled"})
        slow = tls_socket()
        head = (b"POST /messages HTTP/1.1\r\nHost: x\r\n"
                b"Content-Type: application/json\r\nContent-Length: "
                + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n")
        slow.sendall(head)
        slow.sendall(body[:2])
        time.sleep(0.2)

        # While the slow client is mid-body, a normal client must complete.
        cli = Client()
        t = time.time()
        st, _, rb = cli.request(
            "POST", "/messages", jbytes({"author": "fast", "text": "prompt"}), JSON)
        fast_lat = time.time() - t
        cli.close()
        if st != 201:
            fail(f"fast client during slow upload: {st}")
        if fast_lat > 2.0:
            fail(f"fast client blocked by slow client: {fast_lat:.2f}s")

        # Finish the slow upload; it must also succeed.
        for b in body[2:]:
            slow.sendall(bytes([b]))
            time.sleep(0.05)
        st, _ = read_response(slow)
        slow.close()
        if st != 201:
            fail(f"slow client did not complete: {st}")

        cli = Client()
        st, _, body = cli.request("GET", "/messages")
        cli.close()
        listed = json.loads(body)
        authors = sorted(m["author"] for m in listed)
        if authors != ["fast", "slow"]:
            fail(f"slow/fast messages wrong: {authors}")
        if len({m["id"] for m in listed}) != 2:
            fail("slow/fast ids not unique")
        ok(f"slow/trickling client isolated: fast client {fast_lat:.2f}s, both stored")
        return srv.peak_rss_kb
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_thread_cap(opts):
    td = tempfile.mkdtemp(prefix="g3f-cap-")
    try:
        for bad in ("0", "33"):
            p = subprocess.run(
                [opts.glon, "--http-bind", HOST, "--http-cert", opts.cert,
                 "--http-threads", bad, "--io-root", td,
                 "--allow-file-read", "--allow-file-write", opts.app],
                cwd=td, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=15)
            if p.returncode == 0:
                fail(f"--http-threads {bad} was accepted (rc=0)")
        ok("--http-threads 0 and 33 rejected (bounded worker pool)")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_memory(opts):
    td = tempfile.mkdtemp(prefix="g3f-mem-")
    srv = None
    try:
        srv = start_server(opts, td, "g3f-mem", opts.threads)
        for i in range(opts.requests):
            results = []
            ths = [threading.Thread(target=lambda j=j: results.append(run_post(j)))
                   for j in range(min(4, opts.clients - 1))]
            for t in ths:
                t.start()
            for t in ths:
                t.join()
            for r in results:
                if r[1] != 201:
                    fail(f"repeat {i}: status {r[1]}")
        peak = srv.peak_rss_kb
        if opts.max_rss_kb and peak > opts.max_rss_kb:
            fail(f"peak RSS {peak} KiB exceeded bound {opts.max_rss_kb} KiB")
        ok(f"{opts.requests} overlapping rounds; peak RSS {peak} KiB")
        return peak
    finally:
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
    ap.add_argument("--threads", type=int, default=10)
    ap.add_argument("--clients", type=int, default=10)
    ap.add_argument("--requests", type=int, default=20)
    ap.add_argument("--max-rss-kb", type=int, default=0)
    opts = ap.parse_args()

    for path in (opts.glon, opts.app, opts.web, opts.json):
        if not os.path.exists(path):
            fail(f"missing file {path}")
    if not os.path.exists(opts.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")], check=True)
    if opts.threads < 2 or opts.threads > 32:
        fail("--threads must be 2..32")
    if opts.clients < 2:
        fail("--clients must be >= 2")

    peaks = []
    try:
        peak, _ = test_overlap_and_idle(opts)
        peaks.append(peak)
        peaks.append(test_restart(opts))
        peaks.append(test_slow_client(opts))
        test_thread_cap(opts)
        peaks.append(test_memory(opts))
    finally:
        if not wait_port_free():
            print("WARN: port 8443 still in use", file=sys.stderr)
    print(f"PEAK_RSS_KIB={max(peaks) if peaks else 0}")
    print("HTTPS_G3F_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()