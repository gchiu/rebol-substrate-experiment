#!/usr/bin/env python3
"""GLON-WORLDS-CORE-W0: all G3H routes in task mode (3 x 3,400 stacks).

Starts the restored G3H server with --http-tasks, exercises every supported
route, checks SQLite persistence across requests and a restart, and verifies
that no machine fail-stop occurred.  Local-only.

Requires a build with MODULES=file,http,sqlite,task on a profile with the
3 x 3,400-cell task stacks (make w0-routes-test).
"""

import http.client
import json
import os
import pathlib
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
COOKIE = None


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def wait_port(timeout=20.0):
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
        self.out = open(log_path, "w+")
        self.proc = subprocess.Popen(
            [str(ROOT / "glon"), "--http-tasks", "--http-bind", HOST,
             "--http-cert", str(CERT), "--io-root", io_root,
             "--allow-file-read", "--allow-file-write",
             "--http-timeout-ms", "3000"] + LIBS + [str(APP)],
            cwd=str(ROOT), stdout=self.out, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        if not wait_port():
            self.stop()
            fail("server did not start: " + self.trace())
        self.io_root = io_root

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
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.out.close()


def req(method, path, body=None):
    global COOKIE
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=10)
    try:
        headers = {"Content-Type": "application/json"} if body else {}
        if COOKIE:
            headers["Cookie"] = COOKIE
        c.request(method, path, body=body, headers=headers)
        r = c.getresponse()
        sc = r.getheader("Set-Cookie")
        if sc:
            first = sc.split(";", 1)[0]
            if first.startswith("gw_session="):
                COOKIE = first if first != "gw_session=" else None
        return r.status, r.read().decode()
    finally:
        c.close()


def auth_setup():
    st, body = req("POST", "/api/auth/register",
                   json.dumps({"username": "w0user", "password": "password123"}))
    if st != 201:
        fail(f"register w0user: {st} {body[:120]}")
    st, body = req("POST", "/api/auth/login",
                   json.dumps({"username": "w0user", "password": "password123"}))
    if st != 200 or not COOKIE:
        fail(f"login w0user: {st} {body[:120]}")


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists():
        fail("missing glon binary, certificate or server.glon (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w0-routes-")
    io_root = os.path.join(td, "io")
    log_path = os.path.join(td, "server.log")
    os.makedirs(io_root)
    try:
        srv = Server(io_root, log_path)

        st, body = req("GET", "/api/groups")
        if st != 200 or "groups" not in body:
            fail(f"GET /api/groups: {st} {body[:100]}")
        ok("GET /api/groups -> 200")

        st, body = req("GET", "/api/session")
        if st != 200 or "user" not in body:
            fail(f"GET /api/session: {st} {body[:100]}")
        ok("GET /api/session -> 200")

        st, body = req("GET", "/api/users")
        if st != 200 or not body.startswith("["):
            fail(f"GET /api/users (empty): {st} {body[:100]}")
        ok("GET /api/users -> 200 (empty list)")

        st, body = req("POST", "/api/users",
                       '{"username":"alice","display_name":"Alice","role":"admin"}')
        if st != 201:
            fail(f"POST /api/users: {st} {body[:120]}")
        uid = json.loads(body)["id"]
        ok(f"POST /api/users -> 201 (id={uid})")

        st, body = req("GET", f"/api/users?id={uid}")
        if st != 200 or json.loads(body)["username"] != "alice":
            fail(f"GET /api/users?id={uid}: {st} {body[:120]}")
        ok("GET /api/users?id=N -> 200 (persisted across requests)")

        st, body = req("POST", "/api/users",
                       '{"username":"bob","display_name":"Bob"}')
        if st != 201:
            fail(f"POST /api/users (bob): {st} {body[:120]}")
        bid = json.loads(body)["id"]

        st, body = req("POST", "/api/users/disable", json.dumps({"id": bid}))
        if st != 200 or json.loads(body)["status"] != "disabled":
            fail(f"POST /api/users/disable: {st} {body[:120]}")
        ok("POST /api/users/disable -> 200")

        st, body = req("POST", "/api/users",
                       '{"username":"ALICE","display_name":"x"}')
        if st != 409:
            fail(f"duplicate username: {st} {body[:100]}")
        ok("duplicate username -> 409")

        st, _ = req("GET", "/api/messages?group=general&after=0")
        if st != 401:
            fail(f"unauthenticated messages: {st}")
        ok("unauthenticated messages -> 401")
        auth_setup()
        ok("registered and authenticated a messaging user")

        st, body = req("GET", "/api/messages?group=general&after=0")
        if st != 200 or "messages" not in body:
            fail(f"GET /api/messages (empty): {st} {body[:100]}")
        ok("GET /api/messages (empty) -> 200")

        st, body = req("POST", "/api/messages",
                       '{"text":"hello","group":"general"}')
        if st != 201:
            fail(f"POST /api/messages: {st} {body[:120]}")
        ok("POST /api/messages -> 201")

        st, body = req("GET", "/api/messages?group=general&after=0")
        if st != 200 or "hello" not in body:
            fail(f"GET /api/messages (stored): {st} {body[:120]}")
        ok("GET /api/messages with stored message -> 200")

        st, body = req("GET", "/api/nope")
        if st != 404:
            fail(f"unknown route: {st} {body[:100]}")
        ok("unknown route -> 404")

        if srv.proc.poll() is not None:
            fail("server died during route checks: " + srv.trace())
        srv.stop()
        time.sleep(0.3)

        srv = Server(io_root, log_path)
        st, body = req("GET", f"/api/users?id={uid}")
        if st != 200 or json.loads(body)["username"] != "alice":
            fail(f"user did not persist across restart: {st} {body[:120]}")
        st, body = req("GET", f"/api/users?id={bid}")
        if st != 200 or json.loads(body)["status"] != "disabled":
            fail("disabled status did not persist across restart")
        st, body = req("GET", "/api/messages?group=general&after=0")
        if st != 200 or "hello" not in body:
            fail("message did not persist across restart")
        ok("SQLite users and NDJSON messages persist across restart")
        trace = srv.trace()
        srv.stop()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[:400])
        ok("no machine halt in task-mode server log")
        print("W0_ROUTES PASS")
    finally:
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
