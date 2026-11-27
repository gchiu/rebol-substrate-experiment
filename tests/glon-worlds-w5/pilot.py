#!/usr/bin/env python3
"""GLON-WORLDS-W5: invited-user pilot security and deployment acceptance.

Automated browser-equivalent HTTPS test. No real browser engine is used.
Requires make w5-test.
"""

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
UNIT = ROOT / "deploy" / "glon-worlds" / "glon-worlds.service"
CADDY = ROOT / "deploy" / "glon-worlds" / "Caddyfile.worlds"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
PASS_A = "correct horse battery"
PASS_B = "another strong pass"


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


class Client:
    def __init__(self, name, xff=None):
        self.name = name
        self.xff = xff
        self.cookie = None
        self.csrf = None

    def req(self, method, path, body=None, headers=None, timeout=60):
        h = dict(headers or {})
        if self.cookie:
            h["Cookie"] = self.cookie
        if self.csrf:
            h["X-CSRF-Token"] = self.csrf
        if self.xff:
            h["X-Glon-Client-IP"] = self.xff
        st, sc, data = raw_req(method, path, body, h, timeout)
        if sc:
            first = sc.split(";", 1)[0]
            if first.startswith("gw_session="):
                self.cookie = first if first != "gw_session=" else None
        return st, data

    def register(self, username, password, invite, display_name=None):
        body = {"username": username, "password": password, "invite": invite}
        if display_name is not None:
            body["display_name"] = display_name
        return self.req("POST", "/api/auth/register", json.dumps(body))

    def login(self, username, password):
        st, data = self.req("POST", "/api/auth/login",
                            json.dumps({"username": username, "password": password}))
        parsed = json.loads(data) if data else None
        if st == 200 and parsed:
            self.csrf = parsed.get("csrf")
        return st, parsed

    def logout(self):
        st, data = self.req("POST", "/api/auth/logout", "")
        self.csrf = None
        return st, data

    def session(self):
        st, data = self.req("GET", "/api/auth/session")
        return st, json.loads(data) if data else None

    def send(self, text, **extra):
        body = {"text": text, "group": "general"}
        body.update(extra)
        st, data = self.req("POST", "/api/messages", json.dumps(body))
        return st, json.loads(data) if data else None

    def page(self, after=0, limit=9):
        st, data = self.req(
            "GET", f"/api/messages?group=general&after={after}&limit={limit}")
        return st, json.loads(data) if data else None


def make_invite(db_path, code, label="test", days=1):
    subprocess.run(
        ["python3", str(INVITE), "--db", str(db_path),
         "--code", code, "--label", label, "--days", str(days)],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return code


def main():
    if not all(p.exists() for p in (ROOT / "glon", CERT, APP, HTML, INVITE, UNIT, CADDY)):
        fail("missing binary, certificate, app, browser asset or deployment asset")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w5-pilot-")
    io_root = os.path.join(td, "io")
    web_root = os.path.join(io_root, "web")
    log_path = os.path.join(td, "server.log")
    db_path = os.path.join(io_root, "users.db")
    os.makedirs(web_root)
    shutil.copyfile(HTML, os.path.join(web_root, "index.html"))
    # Simulate a pre-W5 database containing the old session table. W5 must not
    # delete or rewrite it while creating its own tables.
    con = sqlite3.connect(db_path)
    con.execute("CREATE TABLE auth_sessions(id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, token TEXT NOT NULL UNIQUE, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, revoked_at INTEGER)")
    con.execute("INSERT INTO auth_sessions(user_id,token,created_at,expires_at,revoked_at) VALUES (1,'legacy-session',1,1,NULL)")
    con.commit()
    con.close()
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)

        for path in ("/", "/index.html"):
            st, _, page = raw_req("GET", path)
            text = page.decode("utf-8")
            if st != 200 or "Invitation code" not in text:
                fail(f"browser invitation UI missing at {path}")
        source = HTML.read_text()
        for marker in ("/api/auth/login", "/api/auth/register", "X-CSRF-Token"):
            if marker not in source:
                fail(f"browser asset missing {marker}")
        if "textContent" not in source or "innerHTML" in source:
            fail("browser asset is not textContent-only")
        ok("browser asset exposes invitation registration and CSRF flow")

        st, _, _ = raw_req("GET", "/api/messages?group=general&after=0&limit=9")
        if st != 401:
            fail(f"unauthenticated messages returned {st}")
        ok("unauthenticated message reads are rejected")

        st, _, _ = raw_req("GET", "/api/users",
                           headers={"X-Glon-Client-IP": "203.0.113.20"})
        if st != 403:
            fail(f"proxied admin registry access returned {st}")
        ok("proxied admin registry access is rejected in depth")

        xff_a = "203.0.113.21"
        xff_b = "203.0.113.22"
        invite_a = "w5-invite-alice-0001"
        invite_b = "w5-invite-bob-0002"
        invite_dup = "w5-invite-dup-0003"
        invite_bad = "w5-invite-expired-0004"
        make_invite(db_path, invite_a, "alice")
        make_invite(db_path, invite_b, "bob")
        make_invite(db_path, invite_dup, "dup")

        a = Client("A", xff_a)
        b = Client("B", xff_b)
        st, _ = a.register("alice", PASS_A, "", "Alice")
        if st != 403:
            fail(f"remote registration without invite returned {st}")
        ok("remote registration without an invitation is rejected")

        st, _ = a.register("alice", PASS_A, invite_bad, "Alice")
        if st != 403:
            fail(f"unknown/expired invitation returned {st}")
        con = sqlite3.connect(db_path)
        now = int(time.time())
        con.execute("INSERT INTO auth_invites(code_hash,label,created_at,expires_at,used_at,used_by) VALUES (?,?,?,?,NULL,NULL)",
                    (hashlib.sha256(invite_bad.encode()).hexdigest(), "expired", now - 100, now - 1))
        con.commit()
        con.close()
        st, _ = a.register("alice", PASS_A, invite_bad, "Alice")
        if st != 403:
            fail(f"expired invitation returned {st}")
        ok("unknown and expired invitation codes are rejected")

        st, body = a.register("alice", PASS_A, invite_a, "Alice")
        if st != 201:
            fail(f"register alice: {st} {body[:120]!r}")
        st, _ = Client("A2", "203.0.113.23").register("carol", "password123", invite_a)
        if st != 403:
            fail(f"reused invitation returned {st}")
        ok("valid invitation creates one account and cannot be reused")

        st, body = b.register("bob", PASS_B, invite_b, "Bob")
        if st != 201:
            fail(f"register bob: {st} {body[:120]!r}")
        st, _ = Client("D", "203.0.113.24").register("alice", PASS_A, invite_dup, "Duplicate")
        if st != 409:
            fail(f"duplicate username with fresh invite returned {st}")
        ok("duplicate usernames remain rejected")

        st, user_a = a.login("alice", PASS_A)
        if st != 200 or not a.cookie or not a.csrf:
            fail(f"login alice: {st} {user_a}")
        st, user_b = b.login("bob", PASS_B)
        if st != 200 or not b.cookie or not b.csrf:
            fail(f"login bob: {st} {user_b}")
        st, sess_a = a.session()
        if st != 200 or sess_a["user"]["username"] != "alice" or sess_a["csrf"] != a.csrf:
            fail(f"session alice: {st} {sess_a}")
        ok("login and session return authenticated user plus CSRF token")

        token_a = a.cookie.split("=", 1)[1]
        expected_hash = hashlib.sha256(token_a.encode()).hexdigest()
        con = sqlite3.connect(db_path)
        stored = con.execute("SELECT token_hash FROM auth_sessions_v2 WHERE user_id=? ORDER BY id DESC LIMIT 1",
                             (user_a["user"]["id"],)).fetchone()
        legacy_count = con.execute("SELECT COUNT(*) FROM auth_sessions").fetchone()[0]
        con.close()
        if not stored or stored[0] != expected_hash:
            fail("session token was not stored as its SHA-256 hash")
        raw_db = pathlib.Path(db_path).read_bytes()
        if token_a.encode() in raw_db:
            fail("raw session token appears in the database")
        if legacy_count != 1:
            fail("legacy auth_sessions record was not preserved")
        ok("session tokens are hashed at rest; legacy session records are preserved")

        saved = a.csrf
        a.csrf = None
        st, _ = a.send("no csrf")
        if st != 403:
            fail(f"POST without CSRF returned {st}")
        a.csrf = "0" * 32
        st, _ = a.send("wrong csrf")
        a.csrf = saved
        if st != 403:
            fail(f"POST with wrong CSRF returned {st}")
        st, msg_a = a.send("hello from A", author="bob", user_id=999)
        if st != 201 or msg_a["author"] != "alice":
            fail(f"A send: {st} {msg_a}")
        ok("mutations require a valid CSRF token and identity is server-derived")

        st, page_b = b.page(0)
        if st != 200 or not any(m["id"] == msg_a["id"] for m in page_b["messages"]):
            fail("B could not read A's message")
        st, msg_b = b.send("reply from B", author="alice", user_id=1)
        if st != 201 or msg_b["author"] != "bob":
            fail(f"B reply: {st} {msg_b}")
        st, page_a = a.page(msg_a["id"])
        if st != 200 or [m["text"] for m in page_a["messages"]] != ["reply from B"]:
            fail(f"A incremental page: {st} {page_a}")
        ok("two authenticated clients exchange messages; incremental polling works")

        st, _ = a.logout()
        if st != 200:
            fail(f"logout: {st}")
        old_cookie = a.cookie
        a.cookie = "gw_session=" + token_a
        st, _ = a.page(0)
        if st != 401:
            fail(f"replayed logged-out session returned {st}")
        a.cookie = old_cookie
        ok("logout revokes the session and replay is rejected")

        # Per-client/per-user login rate limit; uses a separate username and IP.
        xff_r = "198.51.100.77"
        for i in range(5):
            st, _, _ = raw_req("POST", "/api/auth/login",
                               json.dumps({"username": "ghost", "password": "wrong"}),
                               {"X-Glon-Client-IP": xff_r})
            if st != 401:
                fail(f"rate-limit failure {i + 1} returned {st}")
        st, _, _ = raw_req("POST", "/api/auth/login",
                           json.dumps({"username": "ghost", "password": "wrong"}),
                           {"X-Glon-Client-IP": xff_r})
        if st != 429:
            fail(f"rate limit did not block after five failures: {st}")
        ok("login rate limiting blocks repeated failures per client/user")

        # A valid user from another client key is unaffected.
        st, _ = Client("A3", "198.51.100.200").login("alice", PASS_A)
        if st != 200:
            fail(f"unrelated login blocked by rate limit: {st}")
        ok("rate limiting does not block unrelated clients")

        srv.stop()
        time.sleep(0.5)
        srv = Server(io_root, log_path)
        a2 = Client("A-restart", xff_a)
        st, _ = a2.login("alice", PASS_A)
        if st != 200:
            fail(f"login after restart: {st}")
        st, page = a2.page(0)
        if st != 200:
            fail(f"history after restart: {st}")
        texts = [m["text"] for m in page["messages"]]
        if "hello from A" not in texts or "reply from B" not in texts:
            fail("message history did not survive restart")
        con = sqlite3.connect(db_path)
        users = con.execute("SELECT username FROM users ORDER BY id").fetchall()
        invites = con.execute("SELECT used_at FROM auth_invites WHERE used_at IS NOT NULL").fetchall()
        legacy_count = con.execute("SELECT COUNT(*) FROM auth_sessions").fetchone()[0]
        con.close()
        if [u[0] for u in users] != ["alice", "bob"] or len(invites) < 2 or legacy_count != 1:
            fail("users, invite usage or legacy records did not persist")
        ok("users, invitations, sessions and message history survive restart")

        unit = UNIT.read_text()
        for marker in ("User=glon-worlds", "NoNewPrivileges=true",
                       "ProtectSystem=strict", "--http-bind 127.0.0.1",
                       "StateDirectory=glon-worlds"):
            if marker not in unit:
                fail(f"systemd unit missing {marker}")
        caddy = CADDY.read_text()
        for marker in ("reverse_proxy https://127.0.0.1:8443",
                       "tls_insecure_skip_verify", "@admin path /api/users*",
                       "respond @admin 404"):
            if marker not in caddy:
                fail(f"Caddy integration missing {marker}")
        ok("unprivileged service and Caddy reverse-proxy assets are staged safely")

        trace = srv.trace()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-400:])
        ok("no machine halt in the task-mode server log")
        print("W5_PILOT PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
