#!/usr/bin/env python3
"""G2 HTTPS routing/inspection acceptance tests for the optional Glon module.

Bounded, local-only: 127.0.0.1:8443 with the local self-signed certificate.
Retains the G1 suite separately; this file covers the G2 application and the
new request-inspection vocabulary.
"""

import argparse
import concurrent.futures
import http.client
import json
import os
import re
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

HOST = "127.0.0.1"
PORT = 8443
HOME_MARKERS = (b"href='/about'", b"href='/status'")


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


def request(path="/", method="GET", timeout=5.0):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    conn.request(method, path)
    resp = conn.getresponse()
    body = resp.read()
    headers = dict(resp.getheaders())
    status = resp.status
    conn.close()
    return status, headers, body


def get_json(path="/status"):
    status, headers, body = request(path)
    if status != 200:
        fail(f"GET {path}: status {status}")
    if headers.get("Content-Type") != "application/json":
        fail(f"GET {path}: content type {headers.get('Content-Type')!r}")
    try:
        return json.loads(body), body
    except json.JSONDecodeError as exc:
        fail(f"status response is not valid JSON: {exc}: {body!r}")


def assert_home():
    status, headers, body = request("/")
    if status != 200:
        fail(f"GET /: status {status}")
    if headers.get("Content-Type") != "text/html; charset=utf-8":
        fail(f"GET /: content type {headers.get('Content-Type')!r}")
    for marker in HOME_MARKERS:
        if marker not in body:
            fail(f"GET /: missing link marker {marker!r}")
    return body


def assert_about(path="/about"):
    status, headers, body = request(path)
    if status != 200:
        fail(f"GET {path}: status {status}")
    if headers.get("Content-Type") != "text/html; charset=utf-8":
        fail(f"GET {path}: content type {headers.get('Content-Type')!r}")
    if b"About Glon" not in body:
        fail(f"GET {path}: missing About content")
    return body


def test_stale_and_inspection(glon, cert):
    if not wait_port_free():
        fail("port 8443 is in use before inspection test")
    app = os.path.join(os.path.dirname(os.path.abspath(__file__)), "inspect.glon")
    srv = Server(glon, ["--http-cert", cert,
                        "--http-max-requests", "2", app], "inspect")
    try:
        srv.wait_ready()
        status, headers, body = request("/a%20b?x=1&y=two")
        if status != 200 or body != b"GET /a b ?x=1&y=two":
            fail(f"inspection response: status={status} body={body!r}")
        status, headers, body = request("/stale")
        if status != 200 or body != b"stale rejected":
            fail(f"stale accessor was not rejected: status={status} body={body!r}")
        if srv.stop(timeout=10) != 0:
            fail("inspect server did not stop cleanly")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()
    ok("method/path/query inspection and stale-handle rejection")


def test_edit_without_recompile(glon, cert):
    body = "<h1>Edited G2</h1>"
    src = (
        "[\n"
        "home: func [request] [\n"
        "    http/respond request 200 \"text/html; charset=utf-8\"\n"
        f"        \"{body}\"\n"
        "]\n"
        "http/listen 8443 :home\n"
        "]\n"
    )
    if not wait_port_free():
        fail("port 8443 is in use before edit test")
    fd, path = tempfile.mkstemp(suffix=".glon")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(src)
        srv = Server(glon, ["--http-cert", cert,
                            "--http-max-requests", "1", path], "edited-g2")
        try:
            srv.wait_ready()
            status, headers, got = request("/")
            if status != 200 or got != body.encode():
                fail(f"edited app returned status={status} body={got!r}")
            if srv.stop(timeout=10) != 0:
                fail("edited G2 app did not stop cleanly")
        finally:
            if srv.proc.poll() is None:
                srv.proc.kill()
    finally:
        os.unlink(path)
    ok("editing Glon source changes routing/content without recompiling")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--app", default=os.path.join(root, "examples/https-g2/server.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    ap.add_argument("--max-request-size", type=int, default=8192)
    args = ap.parse_args()

    if not os.path.exists(args.glon):
        fail(f"missing executable {args.glon}")
    if not os.path.exists(args.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")], check=True)
    if not wait_port_free():
        fail("port 8443 is already in use")

    srv = Server(args.glon, [
        "--http-cert", args.cert,
        "--http-max-requests", "100000",
        "--http-report",
        "--http-max-request-size", str(args.max_request_size),
        args.app,
    ], "g2")
    try:
        srv.wait_ready()

        assert_home()
        ok("GET / returns the home page with links")

        assert_about()
        ok("GET /about returns the about page")

        status, headers, body = request("/status")
        if status != 200 or headers.get("Content-Type") != "application/json":
            fail(f"GET /status: status={status} type={headers.get('Content-Type')!r}")
        try:
            data = json.loads(body)
        except json.JSONDecodeError as exc:
            fail(f"/status is not valid JSON: {exc}: {body!r}")
        if data.get("server") != "Glon" or data.get("status") != "running":
            fail(f"/status fields: {data!r}")
        first = data.get("requests")
        if not isinstance(first, int):
            fail(f"/status requests is not an integer: {data!r}")
        second, _ = get_json("/status")
        if second.get("requests") != first + 1:
            fail(f"request counter did not persist: {first} -> {second.get('requests')}")
        ok("GET /status returns valid JSON and the counter persists")

        status, headers, body = request("/missing")
        if status != 404:
            fail(f"unknown path: status {status}, expected 404")
        ok("unknown path returns 404")

        for path in ("/about", "/status"):
            status, headers, body = request(path, method="POST")
            if status != 405:
                fail(f"POST {path}: status {status}, expected 405")
            allow = headers.get("Allow", "")
            if "GET" not in [x.strip() for x in allow.split(",")]:
                fail(f"POST {path}: Allow header {allow!r}")
            if headers.get("Content-Type") != "text/plain; charset=utf-8":
                fail(f"POST {path}: content type {headers.get('Content-Type')!r}")
        ok("unsupported methods return 405 with Allow: GET")

        assert_about("/about?x=1&y=2")
        get_json("/status?foo=bar")
        ok("query strings do not interfere with path routing")

        def one(i):
            kind = i % 4
            if kind == 0:
                assert_home()
            elif kind == 1:
                assert_about()
            elif kind == 2:
                get_json("/status")
            else:
                status, _, _ = request("/missing")
                if status != 404:
                    fail(f"concurrent 404 got {status}")
            return 1

        before, _ = get_json("/status")
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            total = sum(ex.map(one, range(40)))
        if total != 40:
            fail("not every overlapping request succeeded")
        after, _ = get_json("/status")
        if after.get("requests", -1) < before.get("requests", 0) + 40:
            fail(f"counter did not account for overlapping requests: {before} -> {after}")
        ok("40 overlapping client requests were processed safely")

        # Bounded repeated-request check: the server must still work, and the
        # shutdown report must show a bounded post-collection live set.
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            total = sum(ex.map(one, range(300)))
        if total != 300:
            fail("not every repeated request succeeded")
        get_json("/status")
        ok("300 bounded repeated requests succeeded")

        rc = srv.stop(timeout=10)
        if rc != 0:
            fail(f"server shutdown returned {rc}:\n{srv.stderr}")
        if not wait_port_free():
            fail("listener was not released after shutdown")
        live = None
        for line in srv.stderr.splitlines():
            if line.startswith("glon: http: requests="):
                print("MEASURE:", line)
                m = re.search(r"live_cells=(\d+)", line)
                if m:
                    live = int(m.group(1))
        if live is None:
            fail("shutdown report did not include live_cells")
        if live > 4096:
            fail(f"post-collection live managed heap grew unexpectedly: {live} cells")
        ok(f"repeated requests leave a bounded live heap ({live} cells)")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()

    test_stale_and_inspection(args.glon, args.cert)
    test_edit_without_recompile(args.glon, args.cert)

    print("HTTPS_G2_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()
