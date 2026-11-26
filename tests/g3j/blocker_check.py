#!/usr/bin/env python3
"""G3J blocker check (deterministic).

The G3J password-auth implementation (tests/g3j/server-auth-attempt.glon) cannot
load: the Glon interpreter's fixed symbol table (SYM_CAP=512, r0_s1_runtime.c)
is exhausted by the G3H server + the loaded libraries (http, json, sqlite, sec,
web) before the auth vocabulary can be interned.  This check documents the
blocker reproducibly:

  1. the baseline G3H server + sec loads and serves (regression intact);
  2. the auth-attempt server fails with `symbol_table_full`.

It does not weaken any requirement; it records the platform limit.
"""

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
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]


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


def run_server(app, io_root, wait=3.0):
    argv = [str(ROOT / "glon"), "--http-bind", HOST, "--http-cert", str(CERT),
            "--io-root", io_root, "--allow-file-read", "--allow-file-write"] + LIBS + [str(app)]
    return subprocess.Popen(argv, cwd=str(ROOT), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, start_new_session=True)


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


def main():
    glon = ROOT / "glon"
    if not glon.exists() or not CERT.exists():
        fail("missing glon binary or cert (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)

    # 1. baseline G3H server + sec must load and serve.
    td = tempfile.mkdtemp(prefix="g3j-base-")
    p = run_server(ROOT / "examples" / "glon-worlds-g3b" / "server.glon", td)
    try:
        if not wait_port():
            stop(p)
            fail("baseline G3H server did not start")
        import http.client
        ctx = ssl._create_unverified_context()
        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=10)
        c.request("GET", "/api/groups")
        r = c.getresponse(); r.read(); c.close()
        if r.status != 200:
            fail(f"baseline groups status {r.status}")
        ok("baseline G3H server + sec loads and serves (regression intact)")
    finally:
        stop(p)
        import shutil; shutil.rmtree(td, ignore_errors=True)
    time.sleep(0.3)

    # 2. auth-attempt server must fail with symbol_table_full.
    td2 = tempfile.mkdtemp(prefix="g3j-auth-")
    p = run_server(ROOT / "tests" / "g3j" / "server-auth-attempt.glon", td2, wait=1.0)
    time.sleep(1.5)
    stop(p)
    try:
        out = p.stdout.read()
    except Exception:
        out = ""
    if "symbol_table_full" not in out:
        fail(f"expected symbol_table_full blocker, got: {out[:200]!r}")
    ok("auth-attempt blocked by symbol_table_full (SYM_CAP=512) as documented")
    import shutil; shutil.rmtree(td2, ignore_errors=True)
    print("G3J BLOCKER CHECK PASS (milestone blocked; see G3J-RESULTS.md)")


if __name__ == "__main__":
    main()