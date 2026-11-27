#!/usr/bin/env python3
"""GLON-WORLDS-W4: registration, login, sessions and authenticated messaging.

Local-only HTTPS acceptance over a temporary SQLite database and NDJSON store.
Requires make w4-test.
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
    def __init__(self, name):
        self.name = name
        self.cookie = None
        self.csrf = None

    def req(self, method, path, body=None, headers=None, timeout=60):
        h = dict(headers or {})
        if self.cookie:
            h["Cookie"] = self.cookie
        if self.csrf:
            h["X-CSRF-Token"] = self.csrf
        st, sc, data = raw_req(method, path, body, h, timeout)
        if sc:
            first = sc.split(";", 1)[0]
            if first.startswith("gw_session="):
                self.cookie = first if first != "gw_session=" else None
        return st, data

    def register(self, username, password, display_name=None):
        body = {"username": username, "password": password}
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


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists() or not HTML.exists():
        fail("missing glon binary, certificate, app or browser asset")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w4-auth-")
    io_root = os.path.join(td, "io")
    log_path = os.path.join(td, "server.log")
    db_path = os.path.join(io_root, "users.db")
    web_root = os.path.join(io_root, "web")
    os.makedirs(web_root)
    shutil.copyfile(HTML, os.path.join(web_root, "index.html"))
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)

        for path in ("/", "/index.html"):
            st, _, page = raw_req("GET", path)
            text = page.decode("utf-8")
            if st != 200 or "Sign in" not in text or "Register" not in text:
                fail(f"GET {path}: {st}")
            if "Log out" not in text:
                fail(f"browser auth controls missing from {path}")
        source = HTML.read_text()
        for marker in ("/api/auth/login", "/api/auth/register",
                       "/api/auth/session", "/api/auth/logout"):
            if marker not in source:
                fail(f"browser asset missing {marker}")
        if "textContent" not in source or "innerHTML" in source:
            fail("browser auth asset is not textContent-only")
        ok("server serves registration/login/logout browser controls safely")

        st, _, _ = raw_req("GET", "/api/groups")
        if st != 200:
            fail(f"GET /api/groups: {st}")
        ok("server is serving public group metadata")

        st, sc, _ = raw_req("GET", "/api/messages?group=general&after=0&limit=9")
        if st != 401 or sc:
            fail(f"unauthenticated GET messages returned {st}")
        st, sc, _ = raw_req("POST", "/api/messages",
                            json.dumps({"text": "no session"}))
        if st != 401 or sc:
            fail(f"unauthenticated POST messages returned {st}")
        ok("unauthenticated message reads and writes are rejected")

        a = Client("A")
        b = Client("B")
        st, body = a.register("alice", PASS_A, "Alice")
        if st != 201:
            fail(f"register alice: {st} {body[:120]!r}")
        st, _ = a.register("ALICE", PASS_A, "Fake")
        if st != 409:
            fail(f"duplicate case-variant username: {st}")
        st, body = b.register("bob", PASS_B, "Bob")
        if st != 201:
            fail(f"register bob: {st} {body[:120]!r}")
        ok("registration creates unique users; duplicate usernames rejected")

        con = sqlite3.connect(db_path)
        rows = con.execute(
            "SELECT u.username,p.salt,p.hash,p.n,p.r,p.p FROM users u "
            "JOIN user_passwords p ON p.user_id=u.id ORDER BY u.id").fetchall()
        con.close()
        if [r[0] for r in rows] != ["alice", "bob"]:
            fail(f"unexpected stored users: {[r[0] for r in rows]}")
        for _, salt, digest, n, r, p in rows:
            if salt == PASS_A or digest == PASS_A or salt == PASS_B or digest == PASS_B:
                fail("plaintext password stored")
            if len(salt) < 32 or len(digest) < 64 or (n, r, p) != (16384, 8, 1):
                fail(f"unexpected password verifier: {salt!r} {digest!r} {n} {r} {p}")
        raw_db = pathlib.Path(db_path).read_bytes()
        if PASS_A.encode() in raw_db or PASS_B.encode() in raw_db:
            fail("plaintext password appears in the database file")
        ok("passwords are stored as salted scrypt verifiers, never plaintext")

        st, _ = a.login("alice", "wrong password")
        if st != 401:
            fail(f"wrong password returned {st}")
        st, _ = a.login("nobody", PASS_A)
        if st != 401:
            fail(f"unknown user returned {st}")
        ok("wrong passwords and unknown users get generic 401")

        st, user_a = a.login("alice", PASS_A)
        if st != 200 or not a.cookie:
            fail(f"login alice: {st} {user_a}")
        st, user_b = b.login("bob", PASS_B)
        if st != 200 or not b.cookie:
            fail(f"login bob: {st} {user_b}")
        if user_a["user"]["username"] != "alice" or user_b["user"]["username"] != "bob":
            fail("login returned the wrong user")
        st, session_a = a.session()
        if st != 200 or session_a["user"]["username"] != "alice":
            fail(f"session alice: {st} {session_a}")
        ok("correct passwords create distinct authenticated sessions")

        # The login response must carry a hardened cookie.
        st, sc, _ = raw_req("POST", "/api/auth/login",
                            json.dumps({"username": "alice", "password": PASS_A}))
        low = (sc or "").lower()
        for attr in ("httponly", "secure", "samesite=lax", "path=/", "max-age="):
            if attr not in low:
                fail(f"session cookie missing {attr}: {sc!r}")
        ok("session cookies are HttpOnly, Secure, SameSite=Lax with expiry")

        st, _, _ = raw_req("GET", "/api/messages?group=general&after=0&limit=9")
        if st != 401:
            fail("unauthenticated GET still succeeds")
        st, _, _ = raw_req("POST", "/api/messages", json.dumps({"text": "x"}))
        if st != 401:
            fail("unauthenticated POST still succeeds")

        st, msg_a = a.send("hello from A", author="bob", user_id=999)
        if st != 201:
            fail(f"A send: {st} {msg_a}")
        if msg_a["author"] != "alice" or msg_a["user_id"] != user_a["user"]["id"]:
            fail(f"message identity spoofing accepted: {msg_a}")
        st, page_b = b.page(0)
        if st != 200 or not any(m["id"] == msg_a["id"] and m["author"] == "alice"
                                for m in page_b["messages"]):
            fail(f"B did not read A's message: {st}")
        st, msg_b = b.send("reply from B", author="alice", user_id=1)
        if st != 201 or msg_b["author"] != "bob":
            fail(f"B reply identity: {st} {msg_b}")
        st, page_a = a.page(msg_a["id"])
        if st != 200 or [m["text"] for m in page_a["messages"]] != ["reply from B"]:
            fail(f"A incremental read after auth: {st} {page_a}")
        ok("two authenticated clients exchange messages; author comes from session")

        # Automatic polling equivalent: B asks only for IDs after the latest it
        # has displayed and receives a new A message without a full reload.
        latest = max(m["id"] for m in b.page(0)[1]["messages"])
        st, auto = a.send("polled from A")
        if st != 201:
            fail("poll send failed")
        st, polled = b.page(latest)
        if st != 200 or [m["id"] for m in polled["messages"]] != [auto["id"]]:
            fail(f"authenticated polling page: {st} {polled}")
        ok("authenticated incremental polling catches a new message")

        # Logout invalidates the session; replaying the old cookie fails.
        old_cookie = a.cookie
        st, _ = a.logout()
        if st != 200 or a.cookie is not None:
            fail(f"logout: {st} cookie={a.cookie!r}")
        a.cookie = old_cookie
        st, _ = a.page(0)
        if st != 401:
            fail(f"replayed logged-out session returned {st}")
        a.cookie = None
        ok("logout revokes the session and replayed cookies are rejected")

        # Expire B's server-side session in the temporary database.
        token_b = b.cookie.split("=", 1)[1]
        con = sqlite3.connect(db_path)
        con.execute("UPDATE auth_sessions_v2 SET expires_at=1 WHERE token_hash=?", (hashlib.sha256(token_b.encode()).hexdigest(),))
        con.commit()
        con.close()
        st, expired = b.session()
        if st != 200 or expired["user"] is not None:
            fail(f"expired session still reports a user: {st} {expired}")
        st, _ = b.page(0)
        if st != 401:
            fail(f"expired session read returned {st}")
        ok("expired sessions are rejected for reads and session lookup")

        # Malformed inputs and body limits stay controlled.
        bad_cases = [
            ("POST", "/api/auth/register", '{"username":'),
            ("POST", "/api/auth/register", json.dumps({"username": "x", "password": PASS_A})),
            ("POST", "/api/auth/register", json.dumps({"username": "a" * 65, "password": PASS_A})),
            ("POST", "/api/auth/register", json.dumps({"username": "carol", "password": "short"})),
            ("POST", "/api/auth/login", json.dumps({"username": "alice"})),
        ]
        for method, path, body in bad_cases:
            st, _, _ = raw_req(method, path, body)
            if st != 400:
                fail(f"malformed auth input {body[:40]!r} returned {st}")
        st, _, _ = raw_req("POST", "/api/auth/login",
                           json.dumps({"username": "alice", "password": "x" * 3000}))
        if st not in (400, 413):
            fail(f"oversized auth body returned {st}")
        st, _, _ = raw_req("GET", "/api/groups")
        if st != 200:
            fail("server unhealthy after malformed inputs")
        ok("malformed inputs and request-size limits are controlled")

        # Records and history survive a restart.
        srv.stop()
        time.sleep(0.5)
        srv = Server(io_root, log_path)
        a.cookie = None
        st, _ = a.login("alice", PASS_A)
        if st != 200:
            fail(f"login after restart: {st}")
        st, page = a.page(0)
        if st != 200:
            fail(f"history after restart: {st}")
        texts = [m["text"] for m in page["messages"]]
        for wanted in ("hello from A", "reply from B", "polled from A"):
            if wanted not in texts:
                fail(f"message lost across restart: {wanted!r}")
        st, _, users = raw_req("GET", "/api/users")
        if st != 200 or b"alice" not in users or b"bob" not in users:
            fail("user records lost across restart")
        ok("users, sessions and message history survive restart")

        trace = srv.trace()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-400:])
        ok("no machine halt in the task-mode server log")
        print("W4_AUTH PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
