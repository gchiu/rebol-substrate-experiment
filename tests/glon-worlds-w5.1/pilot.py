#!/usr/bin/env python3
"""GLON-WORLDS-W5.1: invitation atomicity and trusted proxy-header acceptance."""

import hashlib
import http.client
import json
import os
import pathlib
import shutil
import signal
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
HTML = ROOT / "examples" / "glon-worlds-g3b" / "web" / "index.html"
INVITE = ROOT / "deploy" / "glon-worlds" / "glon-worlds-invite.py"
CADDY = ROOT / "deploy" / "glon-worlds" / "Caddyfile.worlds"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]


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


def wait_port(timeout=25.0):
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
    def __init__(self, io_root, log_path):
        self.out = open(log_path, "a+")
        self.proc = subprocess.Popen(
            [str(ROOT / "glon"), "--http-tasks", "--http-bind", HOST,
             "--http-cert", str(CERT), "--io-root", io_root,
             "--allow-file-read", "--allow-file-write",
             "--http-timeout-ms", "30000"] + LIBS + [str(APP)],
            cwd=str(ROOT), stdout=self.out, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        if not wait_port():
            self.stop()
            fail("server did not start: " + self.trace())

    def trace(self):
        self.out.flush()
        try:
            return pathlib.Path(self.out.name).read_text()
        except OSError:
            return ""

    def stop(self):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.out.close()


def raw_req(method, path, body=None, headers=None, timeout=60):
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    try:
        h = dict(headers or {})
        if body is not None and "Content-Type" not in h:
            h["Content-Type"] = "application/json"
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        return r.status, r.getheader("Set-Cookie"), r.read()
    finally:
        c.close()


def make_invite(db_path, code, label="test", days=1):
    subprocess.run(
        ["python3", str(INVITE), "--db", str(db_path), "--code", code,
         "--label", label, "--days", str(days)],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    if not all(p.exists() for p in (ROOT / "glon", CERT, APP, HTML, INVITE, CADDY)):
        fail("missing binary, certificate, app, asset or invite tool")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w51-pilot-")
    io_root = os.path.join(td, "io")
    web_root = os.path.join(io_root, "web")
    log_path = os.path.join(td, "server.log")
    db_path = os.path.join(io_root, "users.db")
    os.makedirs(web_root)
    shutil.copyfile(HTML, os.path.join(web_root, "index.html"))
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)

        invite_a = "w51-invite-alice-0001"
        invite_atomic = "w51-invite-atomic-0002"
        make_invite(db_path, invite_a, "alice")
        make_invite(db_path, invite_atomic, "atomic")

        st, _, body = raw_req(
            "POST", "/api/auth/register",
            json.dumps({"username": "alice", "password": "password123",
                        "invite": invite_a, "display_name": "Alice"}),
            {"X-Glon-Client-IP": "203.0.113.41"})
        if st != 201:
            fail(f"initial registration: {st} {body[:120]!r}")

        # A failed duplicate-username registration must roll back the transaction
        # and leave the invitation usable.
        st, _, body = raw_req(
            "POST", "/api/auth/register",
            json.dumps({"username": "alice", "password": "password123",
                        "invite": invite_atomic, "display_name": "Duplicate"}),
            {"X-Glon-Client-IP": "203.0.113.41"})
        if st != 409:
            fail(f"duplicate registration: {st} {body[:120]!r}")
        con = sqlite3.connect(db_path)
        used = con.execute(
            "SELECT used_at FROM auth_invites WHERE code_hash=?",
            (hashlib.sha256(invite_atomic.encode()).hexdigest(),)).fetchone()[0]
        con.close()
        if used is not None:
            fail("failed registration consumed the invitation")
        st, _, body = raw_req(
            "POST", "/api/auth/register",
            json.dumps({"username": "alice2", "password": "password123",
                        "invite": invite_atomic, "display_name": "Alice Two"}),
            {"X-Glon-Client-IP": "203.0.113.41"})
        if st != 201:
            fail(f"reuse after rollback: {st} {body[:120]!r}")
        con = sqlite3.connect(db_path)
        used = con.execute(
            "SELECT used_at FROM auth_invites WHERE code_hash=?",
            (hashlib.sha256(invite_atomic.encode()).hexdigest(),)).fetchone()[0]
        con.close()
        if used is None:
            fail("successful registration did not consume the invitation")
        ok("failed registration rolls back and leaves the invitation usable")

        # Forged X-Forwarded-For must not create independent rate-limit keys.
        for i in range(5):
            st, _, _ = raw_req(
                "POST", "/api/auth/login",
                json.dumps({"username": "ghost", "password": "wrong"}),
                {"X-Forwarded-For": f"198.51.100.{100 + i}"})
            if st != 401:
                fail(f"forged-XFF failure {i + 1} returned {st}")
        st, _, _ = raw_req(
            "POST", "/api/auth/login",
            json.dumps({"username": "ghost", "password": "wrong"}),
            {"X-Forwarded-For": "198.51.100.250"})
        if st != 429:
            fail(f"forged X-Forwarded-For evaded rate limiting: {st}")
        ok("forged X-Forwarded-For does not evade rate limiting")

        # A distinct Caddy-trusted client header is a distinct rate-limit key.
        st, _, _ = raw_req(
            "POST", "/api/auth/login",
            json.dumps({"username": "phantom", "password": "wrong"}),
            {"X-Glon-Client-IP": "203.0.113.99"})
        if st != 401:
            fail(f"trusted client key unexpectedly blocked: {st}")
        ok("trusted proxy client address is used instead of client X-Forwarded-For")

        caddy = CADDY.read_text()
        for marker in ("header_up X-Glon-Client-IP {remote_host}",
                       "header_up X-Forwarded-For {remote_host}"):
            if marker not in caddy:
                fail(f"Caddy config missing {marker}")
        ok("Caddy config overwrites forwarding headers with the observed peer")

        trace = srv.trace()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-400:])
        ok("no machine halt in the task-mode server log")
        print("W5.1_PILOT PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
