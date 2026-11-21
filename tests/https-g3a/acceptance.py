#!/usr/bin/env python3
"""G3A HTTPS membership-register acceptance tests (bounded, local-only).

Every server runs in its own temporary working directory, so members.ndjson
is isolated and restart/persistence tests are deterministic.  The app is a
hash-grouped register (160 groups x 64 slots plus an overflow list), so list
pages are computed from the same id hash the Glon application uses.
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
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from win_shutdown import create_stop_event, set_stop_event, close_stop_event
import urllib.parse

HOST = "127.0.0.1"
PORT = 8443
FORM = "application/x-www-form-urlencoded"
JSON = "application/json"


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def jbytes(obj):
    return json.dumps(obj, separators=(",", ":")).encode()


def page_of(mid):
    return sum(mid.encode("utf-8")) % 160


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
    def __init__(self, glon, args, cwd, label, ready_timeout=240.0):
        self.label = label
        self.cwd = cwd
        self.stop_name, self.stop_handle = create_stop_event()
        argv = [glon]
        if self.stop_name:
            argv += ["--http-stop-event", self.stop_name]
        argv += list(args)
        self.proc = subprocess.Popen(
            argv,
            cwd=cwd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.stderr = ""
        self.ready_timeout = ready_timeout

    def wait_ready(self):
        end = time.time() + self.ready_timeout
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
        fail(f"{self.label}: no listener within {self.ready_timeout}s")

    def stop(self, timeout=15.0):
        if self.proc.poll() is None:
            if self.stop_handle:
                set_stop_event(self.stop_handle)
            else:
                self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
            fail(f"{self.label}: did not stop")
        close_stop_event(self.stop_handle)
        self.stop_handle = None
        self.stderr += self.proc.stderr.read()
        return self.proc.returncode


def request(method, path, body=None, ctype=None, timeout=10.0):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    headers = {}
    if ctype:
        headers["Content-Type"] = ctype
    conn.request(method, path, body=body, headers=headers)
    resp = conn.getresponse()
    data = resp.read()
    hs = {k.lower(): v for k, v in resp.getheaders()}
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
                   jbytes({"id": mid, "name": name, "email": email,
                               "status": status}), JSON)


def expect(status, hs, body, want, label):
    if status != want:
        fail(f"{label}: status {status}, expected {want}; body={body[:160]!r}")
    return body


def app_args(opts):
    return [
        "--allow-file-read", "--allow-file-write",
        "--http-cert", opts.cert,
        "--http-max-requests", "100000",
        "--http-max-body", str(opts.max_body),
        opts.web, opts.json, opts.slots, opts.reg, opts.persist, opts.server,
    ]


def check_status_json():
    status, hs, body = request("GET", "/status")
    expect(status, hs, body, 200, "GET /status")
    try:
        data = json.loads(body)
    except ValueError:
        fail(f"/status is not JSON: {body[:160]!r}")
    for key in ("count", "capacity", "persistence", "error"):
        if key not in data:
            fail(f"/status missing {key}: {data!r}")
    if data["capacity"] != 10240:
        fail(f"/status capacity {data['capacity']}, expected 10240")
    if data["persistence"] not in ("ready", "error", "loading"):
        fail(f"/status persistence {data['persistence']!r}")
    return data


def test_register_and_persistence(opts):
    td = tempfile.mkdtemp(prefix="g3a-accept-")
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 already in use")
        srv = Server(opts.glon, app_args(opts), td, "g3a")
        srv.wait_ready()

        status, hs, body = request("GET", "/members/m1")
        expect(status, hs, body, 200, "fresh seed detail")
        if b"Ada Lovelace" not in body:
            fail("fresh start did not seed m1/Ada Lovelace")
        status, hs, body = request("GET", "/api/members/m1")
        expect(status, hs, body, 200, "fresh seed JSON")
        if json.loads(body).get("id") != "m1":
            fail(f"seed JSON object wrong: {body!r}")

        p = page_of("m1")
        status, hs, body = request("GET", f"/api/members?page={p}")
        expect(status, hs, body, 200, "seed list page")
        seed_page = json.loads(body)
        if seed_page.get("count") != 1 or seed_page.get("page") != p:
            fail(f"seed list count/page wrong: {seed_page!r}")
        if [m.get("id") for m in seed_page.get("members", [])] != ["m1"]:
            fail(f"seed not on its hash page: {seed_page!r}")
        if p != 0:
            status, hs, body = request("GET", "/api/members?page=0")
            expect(status, hs, body, 200, "empty page 0")
            if json.loads(body).get("members") != []:
                fail("page 0 should be empty before adds")
        st = check_status_json()
        if st["count"] != 1 or st["persistence"] != "ready":
            fail(f"fresh /status wrong: {st!r}")
        ok("fresh start seeds m1, hash page and /status agree")

        # Persist the seed via an ordinary update; from now on restarts can load it.
        status, hs, body = request(
            "POST", "/api/members/m1",
            jbytes({"id": "m1", "name": "Ada Lovelace",
                        "email": "ada@example.test", "status": "active"}),
            JSON)
        expect(status, hs, body, 200, "persist seed")
        if json.loads(body).get("id") != "m1":
            fail("persist-seed response did not return m1")

        status, hs, body = add_form("a1", "Alice", "alice@example.test")
        expect(status, hs, body, 200, "HTML add")
        status, hs, body = request("GET", "/members/a1")
        expect(status, hs, body, 200, "HTML add visible")
        if b"Alice" not in body:
            fail("form-added member not visible")
        status, hs, body = request(
            "POST", "/members/a1",
            form_body({"id": "a1", "name": "Alice Smith",
                       "email": "alice@example.test", "status": "inactive"}),
            FORM)
        expect(status, hs, body, 200, "HTML edit")
        status, hs, body = request("GET", "/members/a1")
        expect(status, hs, body, 200, "HTML edit visible")
        if b"Alice Smith" not in body:
            fail("form edit not visible")
        ok("HTML add and edit")

        status, hs, body = add_json("j1", "Bob", "bob@example.test")
        expect(status, hs, body, 201, "JSON add")
        if json.loads(body).get("id") != "j1":
            fail(f"JSON add response wrong: {body!r}")
        status, hs, body = request(
            "PUT", "/api/members/j1",
            jbytes({"id": "j1", "name": "Bobby",
                        "email": "bob@example.test", "status": "active"}),
            JSON)
        expect(status, hs, body, 200, "JSON PUT edit")
        status, hs, body = request("GET", "/api/members/j1")
        expect(status, hs, body, 200, "JSON edit visible")
        if json.loads(body).get("name") != "Bobby":
            fail("JSON edit not visible")
        ok("JSON add (201) and edit (200)")

        # Validation, duplicates, missing records and method/content handling.
        status, hs, body = add_form("bad", "", "bad@example.test")
        expect(status, hs, body, 422, "required field validation")
        status, hs, body = request("GET", "/api/members/bad")
        expect(status, hs, body, 404, "invalid add must not mutate state")

        status, hs, body = add_json("m1", "Duplicate", "dup@example.test")
        expect(status, hs, body, 409, "duplicate id")

        status, hs, body = request("GET", "/members/nope")
        expect(status, hs, body, 404, "missing detail")
        status, hs, body = request(
            "POST", "/members/nope",
            form_body({"id": "nope", "name": "N", "email": "n@example.test",
                       "status": "active"}), FORM)
        expect(status, hs, body, 404, "missing update")

        status, hs, body = request("DELETE", "/")
        expect(status, hs, body, 405, "unsupported method")
        if "GET" not in (hs.get("allow") or ""):
            fail(f"405 lacked Allow: {hs!r}")

        status, hs, body = request("POST", "/", b"{}", JSON)
        expect(status, hs, body, 415, "HTML route rejects JSON")
        status, hs, body = request("POST", "/api/members",
                                   form_body({"id": "x"}), FORM)
        expect(status, hs, body, 415, "API route rejects form")

        status, hs, body = request("POST", "/", b"id=%ZZ&name=A", FORM)
        expect(status, hs, body, 400, "malformed form")
        status, hs, body = request("POST", "/api/members", b'{"id":', JSON)
        expect(status, hs, body, 400, "malformed JSON")
        status, hs, body = request("POST", "/api/members", b"", JSON)
        expect(status, hs, body, 400, "empty body")
        status, hs, body = request(
            "POST", "/api/members",
            jbytes({"id": "num", "name": 1, "email": "n@example.test",
                        "status": "active"}), JSON)
        expect(status, hs, body, 422, "non-string JSON field")
        status, hs, body = request(
            "POST", "/api/members",
            jbytes({"id": "x" * 65, "name": "Too Long",
                        "email": "long@example.test",
                        "status": "active"}), JSON)
        expect(status, hs, body, 422, "overlong id")
        status, hs, body = request(
            "POST", "/api/members",
            b'{"id":"' + b"x" * 3000 + b'"}', JSON)
        expect(status, hs, body, 413, "oversized body")
        ok("validation, duplicates, 404/405/415/400/413 and no partial state")

        # Bounded JSON adds give several non-empty hash pages.
        bulk = opts.bulk_adds
        for i in range(bulk):
            status, hs, body = add_json(f"u{i}", f"User {i}",
                                        f"u{i}@example.test")
            if status != 201:
                fail(f"bulk add u{i}: status {status} body={body[:120]!r}")
        want_count = 3 + bulk
        st = check_status_json()
        if st["count"] != want_count:
            fail(f"count after {bulk} adds: {st['count']}, expected {want_count}")

        probe_i = min(42, bulk - 1)
        probe = f"u{probe_i}"
        p = page_of(probe)
        status, hs, body = request("GET", f"/api/members?page={p}")
        expect(status, hs, body, 200, "JSON list page")
        page = json.loads(body)
        if page.get("page") != p or page.get("pages") != 161 or page.get("count") != want_count:
            fail(f"JSON list metadata wrong: {page!r}")
        members = page.get("members")
        if not isinstance(members, list) or len(members) > 64:
            fail(f"JSON list page shape wrong: {members!r}")
        if probe not in [m.get("id") for m in members]:
            fail(f"{probe} missing from its hash page: {page!r}")

        status, hs, body = request("GET", f"/?page={p}")
        expect(status, hs, body, 200, "HTML list page")
        if probe.encode() not in body or f"User {probe_i}".encode() not in body:
            fail(f"HTML hash page did not list {probe}")

        status, hs, body = request("GET", "/api/members?page=160")
        expect(status, hs, body, 200, "overflow page")
        overflow = json.loads(body)
        if overflow.get("page") != 160 or not isinstance(overflow.get("members"), list):
            fail(f"overflow page shape wrong: {overflow!r}")

        status, hs, body = request("GET", "/api/members?page=x")
        expect(status, hs, body, 400, "bad page token")
        status, hs, body = request("GET", "/api/members?page=161")
        expect(status, hs, body, 400, "page out of range")
        ok("paginated JSON/HTML lists, overflow page and page validation")

        html_name = "<script>alert(1)</script>"
        status, hs, body = request(
            "POST", "/api/members/j1",
            jbytes({"id": "j1", "name": html_name,
                        "email": "bob@example.test", "status": "active"}),
            JSON)
        expect(status, hs, body, 200, "HTML escaping update")
        status, hs, body = request("GET", "/members/j1")
        expect(status, hs, body, 200, "HTML escaped detail")
        if html_name.encode() in body or b"&lt;script&gt;" not in body:
            fail(f"HTML escaping failed: {body[:200]!r}")

        special = 'Bob "quote" \\ slash'
        status, hs, body = request(
            "POST", "/api/members/j1",
            jbytes({"id": "j1", "name": special,
                        "email": "bob@example.test", "status": "inactive"}),
            JSON)
        expect(status, hs, body, 200, "JSON escaping update")
        if json.loads(body).get("name") != special:
            fail(f"JSON response did not round-trip: {body!r}")
        if b'\\"' not in body or b"\\\\" not in body:
            fail(f"JSON response did not escape quote/backslash: {body!r}")
        status, hs, body = request("GET", "/api/members/j1")
        expect(status, hs, body, 200, "JSON escaped detail")
        if json.loads(body).get("name") != special:
            fail("JSON detail did not round-trip")
        ok("HTML escaping and JSON escaping/round-trip")

        # Leave a known edited value for the restart check.
        status, hs, body = request(
            "PUT", "/api/members/j1",
            jbytes({"id": "j1", "name": "Persisted Bob",
                        "email": "bob@example.test", "status": "inactive"}),
            JSON)
        expect(status, hs, body, 200, "persist edited value")
        status, hs, body = request("GET", "/status")
        expect(status, hs, body, 200, "status before stop")

        log_path = os.path.join(td, "members.ndjson")
        if not os.path.exists(log_path):
            fail("members.ndjson was not created")
        with open(log_path, "rb") as f:
            log_before = f.read()
        lines = [ln for ln in log_before.splitlines() if ln]
        want_lines = 8 + bulk
        if len(lines) < want_lines:
            fail(f"persistence log has {len(lines)} lines, expected >= {want_lines}")
        for i, ln in enumerate(lines):
            try:
                obj = json.loads(ln)
            except ValueError:
                fail(f"persistence line {i} is not JSON: {ln[:120]!r}")
            if not isinstance(obj, dict) or "id" not in obj:
                fail(f"persistence line {i} is not a member object")
        ok("append log contains one valid JSON object per successful mutation")

        if srv.stop() != 0:
            fail("first server did not stop cleanly")
        srv = None
        if not wait_port_free():
            fail("listener was not released")

        # Restart in the same directory: last line wins and the seed is loaded.
        srv = Server(opts.glon, app_args(opts), td, "g3a-restart")
        srv.wait_ready()
        status, hs, body = request("GET", "/api/members/j1")
        expect(status, hs, body, 200, "restart edited record")
        if json.loads(body).get("name") != "Persisted Bob":
            fail(f"edit did not survive restart: {body!r}")
        status, hs, body = request("GET", "/api/members/a1")
        expect(status, hs, body, 200, "restart form record")
        if json.loads(body).get("name") != "Alice Smith":
            fail("form edit did not survive restart")
        status, hs, body = request("GET", "/api/members/m1")
        expect(status, hs, body, 200, "restart persisted seed")
        status, hs, body = request("GET", f"/api/members/u{bulk - 1}")
        expect(status, hs, body, 200, "restart bulk record")
        st = check_status_json()
        if st["count"] != want_count:
            fail(f"seed was duplicated or records lost: count={st['count']}")
        with open(log_path, "rb") as f:
            if f.read() != log_before:
                fail("loading rewrote members.ndjson")
        ok(f"restart restores add/edit/{bulk} records without duplicating the seed")
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_malformed_log(opts):
    td = tempfile.mkdtemp(prefix="g3a-malformed-")
    try:
        if not wait_port_free():
            fail("port 8443 already in use before malformed-log test")
        log = os.path.join(td, "members.ndjson")
        good = jbytes({"id": "ok1", "name": "Good", "email": "g@example.test",
                           "status": "active"}) + b"\n"
        bad = b"this is not a JSON member line\n"
        with open(log, "wb") as f:
            f.write(good + bad)
        before = open(log, "rb").read()
        try:
            p = subprocess.run([opts.glon] + app_args(opts), cwd=td,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, timeout=15)
        except subprocess.TimeoutExpired:
            fail("malformed persistence log did not abort startup")
        if p.returncode == 0:
            fail("malformed persistence log started the server")
        if not wait_port_free():
            fail("malformed log left a listener running")
        if open(log, "rb").read() != before:
            fail("malformed persistence log was overwritten")
        ok("malformed complete NDJSON line aborts startup and is not overwritten")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)


def test_truncated_tail(opts):
    td = tempfile.mkdtemp(prefix="g3a-truncated-")
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 already in use before truncated-tail test")
        log = os.path.join(td, "members.ndjson")
        valid = [
            {"id": "t1", "name": "Tail One", "email": "t1@example.test",
             "status": "active"},
            {"id": "t2", "name": "Tail Two", "email": "t2@example.test",
             "status": "inactive"},
        ]
        data = b"".join(jbytes(x) + b"\n" for x in valid)
        data += b'{"id":"truncated","name":"No Newline"'
        with open(log, "wb") as f:
            f.write(data)
        srv = Server(opts.glon, app_args(opts), td, "g3a-truncated")
        srv.wait_ready()
        status, hs, body = request("GET", "/api/members/t1")
        expect(status, hs, body, 200, "truncated tail: t1")
        status, hs, body = request("GET", "/api/members/t2")
        expect(status, hs, body, 200, "truncated tail: t2")
        status, hs, body = request("GET", "/api/members/truncated")
        expect(status, hs, body, 404, "truncated tail must be ignored")
        st = check_status_json()
        if st["count"] != 2:
            fail(f"truncated tail changed count: {st!r}")
        if not st.get("warning"):
            fail(f"truncated tail warning missing from /status: {st!r}")
        if srv.stop() != 0:
            fail("truncated-tail server did not stop cleanly")
        srv = None
        if open(log, "rb").read() != data:
            fail("reading a truncated log modified it")
        ok("truncated trailing line is ignored, earlier records load, warning reported")
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
    ap.add_argument("--web", default=os.path.join(root, "modules/web.glon"))
    ap.add_argument("--json", default=os.path.join(root, "modules/json.glon"))
    ap.add_argument("--slots", default=os.path.join(root, "examples/https-g3a/slots.glon"))
    ap.add_argument("--reg", default=os.path.join(root, "examples/https-g3a/reg.glon"))
    ap.add_argument("--persist", default=os.path.join(root, "examples/https-g3a/persist.glon"))
    ap.add_argument("--server", default=os.path.join(root, "examples/https-g3a/server.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    ap.add_argument("--max-body", type=int, default=2048)
    ap.add_argument("--bulk-adds", type=int, default=200)
    opts = ap.parse_args()

    for path in (opts.glon, opts.web, opts.json, opts.slots, opts.reg,
                 opts.persist, opts.server):
        if not os.path.exists(path):
            fail(f"missing file {path}")
    if not os.path.exists(opts.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")],
                       check=True)

    try:
        test_register_and_persistence(opts)
        test_malformed_log(opts)
        test_truncated_tail(opts)
    finally:
        if not wait_port_free():
            print("WARN: port 8443 is still in use", file=sys.stderr)
    print("HTTPS_G3A_ACCEPTANCE PASS")


if __name__ == "__main__":
    main()
