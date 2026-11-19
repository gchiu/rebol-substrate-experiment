#!/usr/bin/env python3
"""G1 HTTPS acceptance tests for the optional Glon HTTP module.

Bounded, local-only: binds 127.0.0.1:8443, uses a self-signed certificate and
standard-library TLS/HTTP clients.  No timing or throughput claims are made.
"""

import argparse
import concurrent.futures
import http.client
import os
import signal
import socket
import ssl
import subprocess
import sys
import time

EXPECTED = b"<h1>Served by Glon</h1>"
CONTENT_TYPE = "text/html; charset=utf-8"
HOST = "127.0.0.1"
PORT = 8443


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


class Server:
    def __init__(self, glon, args, label):
        self.label = label
        self.proc = subprocess.Popen(
            [glon] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.stderr = ""

    def wait_ready(self, timeout=10.0):
        end = time.time() + timeout
        ctx = ssl._create_unverified_context()
        while time.time() < end:
            if self.proc.poll() is not None:
                self.stderr = self.proc.stderr.read()
                fail(f"{self.label}: server exited before listening:\n{self.stderr}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ss = ctx.wrap_socket(raw, server_hostname="localhost")
                ss.close()
                return
            except OSError:
                time.sleep(0.05)
        fail(f"{self.label}: server did not accept TLS within {timeout}s")

    def peak_rss_kb(self):
        try:
            with open(f"/proc/{self.proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        return int(line.split()[1])
        except OSError:
            pass
        return None

    def stop(self, sig=signal.SIGTERM, timeout=10.0):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
            fail(f"{self.label}: server did not stop after signal")
        self.stderr += self.proc.stderr.read()
        return self.proc.returncode


def request(path="/", timeout=5.0):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    conn.request("GET", path)
    resp = conn.getresponse()
    body = resp.read()
    status = resp.status
    ctype = resp.getheader("Content-Type")
    conn.close()
    return status, ctype, body


def assert_page():
    status, ctype, body = request("/")
    if status != 200:
        fail(f"status {status}, expected 200")
    if ctype != CONTENT_TYPE:
        fail(f"content type {ctype!r}, expected {CONTENT_TYPE!r}")
    if body != EXPECTED:
        fail(f"body {body!r}, expected {EXPECTED!r}")


def test_bad_cert(glon, app):
    if not wait_port_free():
        fail("port 8443 is already in use; cannot test TLS startup failure")
    before = time.time()
    p = subprocess.run(
        [glon, "--http-cert", "/nonexistent/g1-server.pem", app],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=10,
    )
    elapsed = time.time() - before
    if p.returncode == 0:
        fail("server started with a missing TLS certificate")
    if "listening on" in p.stderr:
        fail("server announced a listener despite TLS failure")
    if time.time() < before:
        fail("clock went backwards")
    if not wait_port_free():
        fail("a listener survived the failed TLS configuration")
    ok(f"missing certificate prevents startup (rc={p.returncode}, {elapsed:.2f}s)")


def test_client_disconnect():
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=3)
    raw.settimeout(3)
    ss = ctx.wrap_socket(raw, server_hostname="localhost")
    ss.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")  # no blank line
    ss.close()
    time.sleep(0.1)
    assert_page()
    ok("client disconnect during a request is handled")


def test_oversized(max_size):
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=3)
    raw.settimeout(3)
    ss = ctx.wrap_socket(raw, server_hostname="localhost")
    try:
        ss.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nX-Big: "
                   + b"A" * (max_size + 4096) + b"\r\n\r\n")
        try:
            data = ss.recv(8192)
        except OSError:
            data = b""
    except OSError:
        data = b""
    ss.close()
    time.sleep(0.15)
    assert_page()
    if not data:
        ok("oversized request was rejected (connection closed)")
    elif any(code in data for code in (b"400", b"413", b"431")):
        ok("oversized request was rejected with a 4xx status")
    else:
        fail(f"oversized request returned an unexpected response: {data[:120]!r}")


def test_concurrency(workers=8, each=5):
    def one(_):
        assert_page()
        return 1

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        total = sum(ex.map(one, range(workers * each)))
    if total != workers * each:
        fail("not every concurrent request succeeded")
    ok(f"{total} overlapping client requests ({workers} workers) succeeded")


def start_and_stop(glon, app, cert, max_requests, label):
    if not wait_port_free():
        fail(f"port 8443 is still in use before {label}")
    srv = Server(glon, ["--http-cert", cert,
                        "--http-max-requests", str(max_requests),
                        app], label)
    return srv


def test_edit_without_recompile(glon, cert):
    import tempfile
    body = "<h1>Edited by Glon</h1>"
    src = (
        "[\n"
        "home: func [request] [\n"
        "    http/respond request 200 \"text/html; charset=utf-8\"\n"
        f"        \"{body}\"\n"
        "]\n"
        "http/listen 8443 :home\n"
        "]\n"
    )
    fd, path = tempfile.mkstemp(suffix=".glon")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(src)
        srv = start_and_stop(glon, path, cert, 1, "edited-app")
        try:
            srv.wait_ready()
            status, ctype, got = request("/")
            if status != 200 or got != body.encode():
                fail(f"edited application returned status={status} body={got!r}")
            if srv.stop(timeout=10) != 0:
                fail("edited-app server did not stop cleanly")
        finally:
            if srv.proc.poll() is None:
                srv.proc.kill()
    finally:
        os.unlink(path)
    ok("editing the Glon source changes the response without recompiling")


def test_application_stop(glon, cert):
    here = os.path.dirname(os.path.abspath(__file__))
    app = os.path.join(here, "stop-after-one.glon")
    srv = start_and_stop(glon, app, cert, 0, "app-stop")
    try:
        srv.wait_ready()
        status, ctype, body = request("/")
        if status != 200 or body != EXPECTED:
            fail(f"application-stop request failed: {status} {body!r}")
        try:
            rc = srv.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            fail("http/stop did not stop the server")
        if rc != 0:
            fail(f"http/stop server exited {rc}:\n{srv.stderr}")
        if not wait_port_free():
            fail("http/stop did not release the listener")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()
    ok("application http/stop shuts the server down cleanly")


def test_handler_error(glon, cert):
    here = os.path.dirname(os.path.abspath(__file__))
    app = os.path.join(here, "error-app.glon")
    srv = start_and_stop(glon, app, cert, 3, "handler-error")
    try:
        srv.wait_ready()
        status, ctype, body = request("/boom")
        if status != 500 or body != b"internal error":
            fail(f"handler SIN! did not become a 500: {status} {body!r}")
        assert_page()
        assert_page()
        rc = srv.stop(timeout=10)
        if rc != 0:
            fail(f"handler-error server exited {rc}:\n{srv.stderr}")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()
    ok("a handler SIN! becomes a 500 and the server continues")


def test_stale_handle(glon, stale_app, cert, max_requests=3):
    if not wait_port_free():
        fail("port 8443 is still in use before stale-handle test")
    srv = Server(glon, ["--http-cert", cert,
                        "--http-max-requests", str(max_requests),
                        stale_app], "stale-handle")
    try:
        srv.wait_ready()
        for i in range(max_requests):
            status, ctype, body = request("/")
            if status != 200 or body != EXPECTED:
                fail(f"stale-handle request {i+1}: status={status} body={body!r}")
        rc = srv.stop(timeout=10)
        if rc != 0:
            fail(f"stale-handle server exited {rc}:\n{srv.stderr}")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()
    ok("stale request handles are rejected without affecting the current response")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--app", default=os.path.join(root, "examples/https-g1/server.glon"))
    ap.add_argument("--stale-app", default=os.path.join(here, "stale-handle.glon"))
    ap.add_argument("--cert", default=os.path.join(here, ".cert/server.pem"))
    ap.add_argument("--max-request-size", type=int, default=8192)
    args = ap.parse_args()

    if not os.path.exists(args.glon):
        fail(f"missing executable {args.glon}")
    if not os.path.exists(args.cert):
        subprocess.run([os.path.join(here, "make-cert.sh")], check=True)
    if not wait_port_free():
        fail("port 8443 is already in use")

    test_bad_cert(args.glon, args.app)

    srv = Server(args.glon, [
        "--http-cert", args.cert,
        "--http-max-requests", "100000",
        "--http-report",
        "--http-max-request-size", str(args.max_request_size),
        args.app,
    ], "main")
    try:
        srv.wait_ready()

        assert_page()
        ok("HTTPS request status, content type and exact body")

        for _ in range(30):
            assert_page()
        ok("30 repeated sequential requests succeeded")

        test_concurrency()
        test_client_disconnect()
        test_oversized(args.max_request_size)

        peak = srv.peak_rss_kb()
        rc = srv.stop(timeout=10)
        if rc != 0:
            fail(f"server shutdown returned {rc}:\n{srv.stderr}")
        if not wait_port_free():
            fail("listener was not released after shutdown")
        ok("SIGTERM shutdown released the listener and exited cleanly")
        if peak is not None:
            print(f"MEASURE: server peak RSS (VmHWM) = {peak} KiB")
        for line in srv.stderr.splitlines():
            if line.startswith("glon: http: requests="):
                print("MEASURE:", line)
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()

    test_edit_without_recompile(args.glon, args.cert)
    test_application_stop(args.glon, args.cert)
    test_handler_error(args.glon, args.cert)
    test_stale_handle(args.glon, args.stale_app, args.cert)

    print("HTTPS_G1_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()
