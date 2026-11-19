#!/usr/bin/env python3
"""G3 HTTPS membership-register acceptance tests (bounded, local-only)."""

import argparse
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
import urllib.parse

HOST = "127.0.0.1"
PORT = 8443
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


class Server:
    def __init__(self, glon, args, label):
        self.label = label
        self.proc = subprocess.Popen([glon] + args, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
        self.stderr = ""

    def wait_ready(self, timeout=10.0):
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

    def stop(self, timeout=10.0):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
            fail(f"{self.label}: did not stop")
        self.stderr += self.proc.stderr.read()
        return self.proc.returncode


def request(method, path, body=None, ctype=None, timeout=5.0, headers=None):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    h = dict(headers or {})
    if ctype:
        h["Content-Type"] = ctype
    conn.request(method, path, body=body, headers=h)
    resp = conn.getresponse()
    data = resp.read()
    hs = dict(resp.getheaders())
    status = resp.status
    conn.close()
    return status, hs, data


def form_body(fields):
    return urllib.parse.urlencode(fields).encode()


def add_form(mid, name, email, status="active"):
    return request("POST", "/", form_body({"id": mid, "name": name,
                                           "email": email, "status": status}),
                   FORM)


def add_json(mid, name, email, status="active"):
    return request("POST", "/api/members",
                   json.dumps({"id": mid, "name": name,
                               "email": email, "status": status}).encode(),
                   "application/json")


def test_inspect(glon, cert):
    if not wait_port_free():
        fail("port in use before inspection test")
    app = os.path.join(os.path.dirname(os.path.abspath(__file__)), "inspect.glon")
    srv = Server(glon, ["--http-cert", cert, "--http-max-requests", "2",
                        "--http-max-body", "2048", app], "inspect")
    try:
        srv.wait_ready()
        status, hs, body = request("POST", "/", b"hello", "text/plain",
                                   headers={"X-Test": "yes"})
        if status != 200 or body != b"body=hello header=yes ct=text/plain":
            fail(f"inspection response: {status} {body!r}")
        status, hs, body = request("POST", "/stale", b"ignored", "text/plain")
        if status != 200 or body != b"stale rejected":
            fail(f"stale body accessor was not rejected: {status} {body!r}")
        if srv.stop(timeout=10) != 0:
            fail("inspect server did not stop cleanly")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()
    ok("request body/header/content-type accessors and stale rejection")


def test_edit_without_recompile(glon, cert):
    body = "<h1>Edited G3</h1>"
    src = (
        "[\nhome: func [request] [\n"
        "    http/respond request 200 \"text/html; charset=utf-8\"\n"
        f"        \"{body}\"\n]\nhttp/listen 8443 :home\n]\n"
    )
    if not wait_port_free():
        fail("port in use before edit test")
    fd, path = tempfile.mkstemp(suffix=".glon")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(src)
        srv = Server(glon, ["--http-cert", cert, "--http-max-requests", "1",
                            path], "edited-g3")
        try:
            srv.wait_ready()
            status, hs, got = request("GET", "/")
            if status != 200 or got != body.encode():
                fail(f"edited app returned {status} {got!r}")
            if srv.stop(timeout=10) != 0:
                fail("edited G3 app did not stop cleanly")
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
    ap.add_argument("--parse", default=os.path.join(root, "examples/https-g3/webparse.glon"))
    ap.add_argument("--app", default=os.path.join(root, "examples/https-g3/server.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
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
        "--http-max-body", "2048",
        "--http-report",
        args.parse, args.app,
    ], "g3")
    try:
        srv.wait_ready()

        status, hs, body = request("GET", "/")
        if status != 200 or b"<h1>Members</h1>" not in body or b"Ada Lovelace" not in body:
            fail("home list failed")
        ok("GET / lists seeded members and the add form")

        status, hs, body = request("GET", "/members/m1")
        if status != 200 or b"Ada Lovelace" not in body:
            fail("member view failed")
        ok("GET /members/<id> views a member")

        status, hs, body = add_form("m3", "Alice", "alice@example.test")
        if status != 200:
            fail(f"form add status {status}")
        status, hs, body = request("GET", "/members/m3")
        if status != 200 or b"Alice" not in body:
            fail("form-added member not visible")
        ok("HTML form adds a member")

        status, hs, body = add_json("m4", "Bob", "bob@example.test")
        if status != 201:
            fail(f"JSON add status {status}")
        if json.loads(body).get("id") != "m4":
            fail(f"JSON add response {body!r}")
        ok("JSON add returns 201 and a JSON id")

        status, hs, body = request("POST", "/members/m3",
                                   form_body({"id": "m3", "name": "Alice Smith",
                                              "email": "alice@example.test",
                                              "status": "inactive"}), FORM)
        if status != 200:
            fail(f"form edit status {status}")
        status, hs, body = request("GET", "/members/m3")
        if status != 200 or b"Alice Smith" not in body:
            fail("form edit not visible")
        ok("HTML form edits an existing member")

        special_name = 'Bob "quote" \\ slash'
        status, hs, body = request("POST", "/api/members/m4",
                                   json.dumps({"id": "m4", "name": special_name,
                                               "email": "bob@example.test",
                                               "status": "inactive"}).encode(),
                                   "application/json")
        if status != 200:
            fail(f"JSON edit status {status}")
        parsed = json.loads(body)
        if parsed.get("id") != "m4" or parsed.get("name") != special_name:
            fail(f"JSON escaping round trip failed: {body!r}")
        if b'\\"' not in body or b"\\\\" not in body:
            fail(f"JSON response did not escape quote/backslash: {body!r}")
        status, hs, body = request("GET", "/members/m4")
        if status != 200:
            fail("JSON edit not visible")
        # Reset to ordinary content so later list rendering stays bounded.
        status, hs, body = request("POST", "/api/members/m4",
                                   json.dumps({"id": "m4", "name": "Bob B",
                                               "email": "bob@example.test",
                                               "status": "inactive"}).encode(),
                                   "application/json")
        if status != 200:
            fail(f"JSON reset edit status {status}")
        ok("JSON edit updates a member and escapes JSON output")

        status, hs, body = add_form("bad", "", "bad@example.test")
        if status != 422:
            fail(f"invalid field status {status}, expected 422")
        status, hs, body = request("GET", "/members/bad")
        if status != 404:
            fail("invalid add changed state")
        ok("invalid fields return 422 without changing state")

        status, hs, body = add_json("m1", "Duplicate", "dup@example.test")
        if status != 409:
            fail(f"duplicate id status {status}, expected 409")
        ok("duplicate identifiers return 409")

        status, hs, body = request("GET", "/members/nope")
        if status != 404:
            fail(f"missing record GET {status}")
        status, hs, body = request("POST", "/members/nope",
                                   form_body({"id": "nope", "name": "N",
                                              "email": "n@example.test",
                                              "status": "active"}), FORM)
        if status != 404:
            fail(f"missing record update {status}")
        ok("missing records return 404")

        status, hs, body = request("DELETE", "/")
        if status != 405 or "GET" not in (hs.get("Allow") or ""):
            fail(f"unsupported method: {status} Allow={hs.get('Allow')!r}")
        ok("unsupported methods return 405 with Allow")

        status, hs, body = request("POST", "/", b"id=x", "application/json")
        if status != 415:
            fail(f"HTML route JSON content type: {status}")
        status, hs, body = request("POST", "/api/members",
                                   form_body({"id": "x"}), FORM)
        if status != 415:
            fail(f"API route form content type: {status}")
        ok("unsupported content types return 415")

        status, hs, body = request("POST", "/", b"id=%ZZ&name=A", FORM)
        if status != 400:
            fail(f"malformed form: {status}")
        status, hs, body = request("POST", "/api/members", b'{"id":', "application/json")
        if status != 400:
            fail(f"malformed JSON: {status}")
        status, hs, body = request("POST", "/api/members", b"", "application/json")
        if status != 400:
            fail(f"empty body: {status}")
        ok("malformed form/JSON and empty bodies return 400")

        status, hs, body = request("POST", "/api/members",
                                   b'{"id":"' + b"x" * 3000 + b'"}',
                                   "application/json")
        if status != 413:
            fail(f"oversized body: {status}, expected 413")
        ok("oversized request bodies return 413")

        html_name = "<script>alert(1)</script>"
        status, hs, body = request("POST", "/members/m3",
                                   form_body({"id": "m3", "name": html_name,
                                              "email": "alice@example.test",
                                              "status": "active"}), FORM)
        if status != 200:
            fail(f"escaping edit status {status}")
        status, hs, body = request("GET", "/")
        if b"<script>alert(1)</script>" in body or b"&lt;script&gt;alert(1)&lt;/script&gt;" not in body:
            fail(f"HTML escaping failed: status={status} len={len(body)} body={body[:200]!r}")
        status, hs, body = request("GET", "/members/m3")
        if b"<script>alert(1)</script>" in body or b"&lt;script&gt;" not in body:
            fail("HTML detail escaping failed")
        ok("user-supplied HTML is escaped")

        status, hs, body = add_form("m5", "Over Capacity", "o@example.test")
        if status != 422:
            fail(f"register capacity status {status}, expected 422")
        ok("the bounded register rejects additions beyond its capacity")

        # Client disconnect during an incomplete POST.
        ctx = ssl._create_unverified_context()
        raw = socket.create_connection((HOST, PORT), timeout=3)
        raw.settimeout(3)
        ss = ctx.wrap_socket(raw, server_hostname="localhost")
        ss.sendall(b"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 50\r\n\r\nid=x")
        ss.close()
        time.sleep(0.1)
        status, hs, body = request("GET", "/")
        if status != 200:
            fail("server did not survive a client disconnect")
        ok("client disconnection is handled")

        # Bounded fixed-state workload.
        for i in range(300):
            if i % 3 == 0:
                status, hs, body = request("GET", "/")
            elif i % 3 == 1:
                status, hs, body = request("GET", "/members/m1")
            else:
                status, hs, body = request("DELETE", "/")
            if status not in (200, 405):
                fail(f"workload request {i} returned {status}")
        status, hs, body = request("GET", "/")
        if status != 200:
            fail("server failed after bounded workload")
        ok("300 bounded mixed reads/updates/rejections succeeded")

        rc = srv.stop(timeout=15)
        if rc != 0:
            fail(f"server shutdown returned {rc}:\n{srv.stderr}")
        if not wait_port_free():
            fail("listener was not released")
        live = None
        for line in srv.stderr.splitlines():
            if line.startswith("glon: http: requests="):
                print("MEASURE:", line)
                m = re.search(r"live_cells=(\d+)", line)
                if m:
                    live = int(m.group(1))
        if live is None:
            fail("no shutdown report")
        if live > 6500:
            fail(f"unexpected managed heap growth: {live} cells")
        ok(f"repeated requests leave bounded state ({live} live cells)")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()

    test_inspect(args.glon, args.cert)
    test_edit_without_recompile(args.glon, args.cert)
    print("HTTPS_G3_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()
