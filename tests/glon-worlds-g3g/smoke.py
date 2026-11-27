#!/usr/bin/env python3
"""G3G smoke: persistent, rotating activity log for the native Glon Worlds demo.

Verifies, with a small test-only rotation limit:
  1. the server starts and creates logs/server.log;
  2. a GET generates an access entry with the method/path/status;
  3. a POST generates an entry without exposing its body;
  4. an HTTP error (404) is recorded;
  5. the log survives a server restart (startup logged again, messages persist);
  6. rotation produces a backup file.

Local-only.  Requires a build with MODULES=file,http (make glon MODULES=file,http).
"""

import http.client
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
COOKIE = None
LIBS = [str(ROOT / "glon-lib" / "strings-ext.glon"),
        str(ROOT / "modules" / "web.glon"),
        str(ROOT / "modules" / "json.glon")]


def fail(m):
    print("FAIL:", m)
    raise SystemExit(1)


def ok(m):
    print("PASS:", m)


def wait_port(timeout=15.0):
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


def start(log_path, io_root):
    argv = [str(ROOT / "glon"), "--http-tasks", "--http-bind", HOST, "--http-cert", str(CERT),
            "--io-root", io_root, "--allow-file-read", "--allow-file-write",
            "--http-log", log_path, "--http-log-max-bytes", "250",
            "--http-log-backups", "3"] + LIBS + [str(APP)]
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
    s, b = req("POST", "/api/auth/register",
               '{"username":"g3guser","password":"password123"}')
    if s != 201:
        fail(f"register g3guser: {s} {b[:80]}")
    s, b = req("POST", "/api/auth/login",
               '{"username":"g3guser","password":"password123"}')
    if s != 200 or not COOKIE:
        fail(f"login g3guser: {s} {b[:80]}")


def all_log_text(log_dir):
    text = []
    for p in sorted(pathlib.Path(log_dir).glob("server.log*")):
        text.append(p.read_text())
    return "\n".join(text)


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists():
        fail("missing glon binary, cert or server.glon (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)

    td = tempfile.mkdtemp(prefix="g3g-")
    io_root = os.path.join(td, "io")
    log_dir = os.path.join(td, "logs")
    os.makedirs(io_root)
    os.makedirs(log_dir)
    log_path = os.path.join(log_dir, "server.log")
    secret = "top-secret-body-42"

    try:
        # Run 1: startup, GET, POST, error.
        p = start(log_path, io_root)
        if not wait_port():
            stop(p)
            fail("server did not start")
        auth_setup()
        s1, _ = req("GET", "/api/groups")
        s2, _ = req("POST", "/api/messages",
                    body='{"text":"%s","group":"general"}' % secret)
        s3, _ = req("GET", "/api/messages?group=general&after=0")
        s4, _ = req("GET", "/nope")
        stop(p)
        time.sleep(0.3)

        if (s1, s2, s3, s4) != (200, 201, 200, 404):
            fail(f"unexpected statuses: {(s1, s2, s3, s4)}")
        if not os.path.exists(log_path):
            fail("server.log was not created")
        text = all_log_text(log_dir)
        if "startup" not in text:
            fail("startup not logged")
        if "method=GET path=/api/groups status=200" not in text:
            fail("GET access entry missing/incorrect")
        if "method=POST path=/api/messages status=201" not in text:
            fail("POST access entry missing/incorrect")
        if "path=/nope status=404" not in text:
            fail("404 error not logged")
        if secret in text:
            fail("request body leaked into the log")
        if "group=" in text or "after=" in text:
            fail("query string leaked into the log")
        ok("startup, GET, POST (body not logged), 404 and sanitisation verified")

        # Rotation: a backup must exist with the small test limit.
        backups = list(pathlib.Path(log_dir).glob("server.log.*"))
        if not backups:
            fail("rotation did not produce a backup file")
        ok(f"rotation produced {len(backups)} backup file(s)")

        # Run 2: restart persistence (log appends, message survives).
        p = start(log_path, io_root)
        if not wait_port():
            stop(p)
            fail("server did not restart")
        s5, body5 = req("GET", "/api/messages?group=general&after=0")
        stop(p)
        time.sleep(0.3)
        if s5 != 200 or secret not in body5:
            fail(f"message did not persist across restart: {s5} {body5[:80]!r}")
        text2 = all_log_text(log_dir)
        if text2.count("startup") < 2:
            fail("log did not persist across restart (startup count < 2)")
        ok("logs and messages persist across restart")
        print("G3G SMOKE PASS")
    finally:
        import shutil
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()