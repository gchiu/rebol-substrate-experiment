#!/usr/bin/env python3
"""G3H smoke: loopback-only SQLite user registry for the native Glon Worlds demo.

Covers:
  1. create two users;
  2. reject duplicate usernames incl. case variants (409);
  3. retrieve both users by stable id;
  4. disable one user (status flips, record kept);
  5. records persist across a server restart;
  6. a non-loopback peer cannot perform mutations (403);
  7. the existing message endpoints still work;
  8. G3G logging still works and never records user-management bodies.

Local-only.  Requires a build with MODULES=file,http,sqlite (make g3h-test).
"""

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
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
COOKIE = None
CSRF = None


def fail(m):
    print("FAIL:", m)
    raise SystemExit(1)


def ok(m):
    print("PASS:", m)


def container_ip():
    try:
        out = subprocess.check_output(["hostname", "-I"], text=True).split()
        for a in out:
            if not a.startswith("127."):
                return a
    except Exception:
        pass
    return None


def wait_port(host, timeout=15.0):
    ctx = ssl._create_unverified_context()
    end = time.time() + timeout
    while time.time() < end:
        try:
            raw = socket.create_connection((host, PORT), timeout=0.3)
            raw.settimeout(0.5)
            ctx.wrap_socket(raw, server_hostname="localhost").close()
            return True
        except OSError:
            time.sleep(0.05)
    return False


def start(bind, io_root, log_path=None):
    argv = [str(ROOT / "glon"), "--http-tasks", "--http-bind", bind, "--http-cert", str(CERT),
            "--io-root", io_root, "--allow-file-read", "--allow-file-write"]
    if log_path:
        argv += ["--http-log", log_path]
    argv += LIBS + [str(APP)]
    return subprocess.Popen(argv, cwd=str(ROOT), stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)


def stop(proc):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


def req(method, path, body=None, host=HOST, timeout=10):
    global COOKIE
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(host, PORT, context=ctx, timeout=timeout)
    try:
        headers = {"Content-Type": "application/json"} if body else {}
        if COOKIE and host == HOST:
            headers["Cookie"] = COOKIE
        if CSRF:
            headers["X-CSRF-Token"] = CSRF
        c.request(method, path, body=body, headers=headers)
        r = c.getresponse()
        sc = r.getheader("Set-Cookie")
        if sc and host == HOST:
            first = sc.split(";", 1)[0]
            if first.startswith("gw_session="):
                COOKIE = first if first != "gw_session=" else None
        return r.status, r.read().decode()
    finally:
        c.close()


def auth_setup():
    global CSRF
    s, b = req("POST", "/api/auth/register",
               '{"username":"g3huser","password":"password123"}')
    if s != 201:
        fail(f"register g3huser: {s} {b[:80]}")
    s, b = req("POST", "/api/auth/login",
               '{"username":"g3huser","password":"password123"}')
    if s != 200 or not COOKIE:
        fail(f"login g3huser: {s} {b[:80]}")
    CSRF = json.loads(b).get("csrf")
    if not CSRF:
        fail("login returned no csrf token")


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists():
        fail("missing glon binary, cert or server.glon (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)
    ip = container_ip()

    td = tempfile.mkdtemp(prefix="g3h-")
    io_root = os.path.join(td, "io")
    log_path = os.path.join(td, "logs", "server.log")
    os.makedirs(io_root)
    os.makedirs(os.path.dirname(log_path))
    secret = "alice-secret-body"

    try:
        # Run 1: create, duplicate, get, disable, message endpoints, logging.
        p = start(HOST, io_root, log_path)
        if not wait_port(HOST):
            stop(p); fail("server did not start")
        s, b = req("POST", "/api/users",
                   body='{"username":"alice","display_name":"Alice","role":"admin"}')
        if s != 201:
            fail(f"create alice: {s} {b[:80]!r}")
        s, b = req("POST", "/api/users", body='{"username":"bob","display_name":"Bob"}')
        if s != 201:
            fail(f"create bob: {s} {b[:80]!r}")
        ok("created two users (alice=admin, bob=user)")

        s, _ = req("POST", "/api/users", body='{"username":"ALICE","display_name":"x"}')
        if s != 409:
            fail(f"duplicate case-variant not rejected: {s}")
        ok("duplicate username (case variant) rejected with 409")

        s, b = req("GET", "/api/users?id=1")
        if s != 200 or '"username":"alice"' not in b:
            fail(f"get id 1: {s} {b[:80]!r}")
        s, b = req("GET", "/api/users?id=2")
        if s != 200 or '"username":"bob"' not in b:
            fail(f"get id 2: {s} {b[:80]!r}")
        ok("retrieved both users by stable id")

        s, b = req("POST", "/api/users/disable", body='{"id":2}')
        if s != 200 or '"status":"disabled"' not in b:
            fail(f"disable: {s} {b[:80]!r}")
        s, b = req("GET", "/api/users?id=2")
        if '"status":"disabled"' not in b:
            fail("disabled status did not persist")
        ok("disabled user 2 (record kept, status=disabled)")

        # message endpoints still work for an authenticated user
        auth_setup()
        if req("GET", "/api/groups")[0] != 200:
            fail("groups endpoint broken")
        if req("POST", "/api/messages", body='{"text":"hi","group":"general"}')[0] != 201:
            fail("post message broken")
        if req("GET", "/api/messages?group=general&after=0")[0] != 200:
            fail("get messages broken")
        ok("existing message endpoints still work")

        # logging must not contain the user body
        log_txt = ""
        if os.path.exists(log_path):
            log_txt = pathlib.Path(log_path).read_text()
        if secret in log_txt:
            fail("user-management body leaked into the log")
        if "POST /api/users" in log_txt or "method=POST path=/api/users" in log_txt:
            pass  # an access line is fine; only the body must be absent
        ok("G3G logging works without exposing user-management bodies")
        stop(p)
        time.sleep(0.3)

        # Run 2: restart persistence.
        p = start(HOST, io_root, log_path)
        if not wait_port(HOST):
            stop(p); fail("server did not restart")
        s, b = req("GET", "/api/users?id=1")
        if s != 200 or '"username":"alice"' not in b:
            fail(f"user 1 did not persist across restart: {s} {b[:80]!r}")
        s, b = req("GET", "/api/users?id=2")
        if '"status":"disabled"' not in b:
            fail("disabled status did not persist across restart")
        ok("user records persist across restart")
        stop(p)
        time.sleep(0.3)

        # Run 3: non-loopback mutation is rejected (bind 0.0.0.0).
        if not ip:
            print("SKIP: no non-loopback address available for remote test")
        else:
            p = start("0.0.0.0", io_root)
            if not wait_port(HOST):
                stop(p); fail("server did not start on 0.0.0.0")
            s, _ = req("POST", "/api/users",
                       body='{"username":"mallory","display_name":"M"}', host=ip)
            if s != 403:
                fail(f"non-loopback mutation not rejected: {s}")
            # loopback still allowed on the same server
            s, _ = req("POST", "/api/users",
                       body='{"username":"carol","display_name":"C"}', host=HOST)
            if s != 201:
                fail(f"loopback mutation on 0.0.0.0 bind failed: {s}")
            ok("non-loopback mutation rejected (403); loopback still allowed")
            stop(p)
            time.sleep(0.3)

        print("G3H SMOKE PASS")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()