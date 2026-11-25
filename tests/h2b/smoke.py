#!/usr/bin/env python3
"""H2B partial smoke: the task path serves a request at low task counts.

This is NOT the H2B success criterion (ten overlapping requests with one
suspending on SQLite while another progresses).  That is blocked by the
per-task return-stack vs. M1-arena limit documented in H2B-RESULTS.md.  This
smoke only confirms the host task path (spawn + run-tasks + per-id response)
serves a request when the per-task stack is large enough (3 tasks).
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


def fail(m):
    print("FAIL:", m)
    raise SystemExit(1)


def ok(m):
    print("PASS:", m)


def main():
    glon = ROOT / "glon"
    cert = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
    app = ROOT / "examples" / "h2b" / "tasks.glon"
    for p in (glon, cert, app):
        if not p.exists():
            fail(f"missing {p}")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")], check=True)
    td = tempfile.mkdtemp(prefix="h2b-smoke-")
    proc = subprocess.Popen(
        [str(glon), "--http-bind", HOST, "--http-cert", str(cert),
         "--http-tasks", "--http-threads", "4", "--http-max-body", "500",
         "--io-root", td, "--allow-file-read", "--allow-file-write", str(app)],
        cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        text=True, start_new_session=True)
    try:
        end = time.time() + 20
        ctx = ssl._create_unverified_context()
        while time.time() < end:
            if proc.poll() is not None:
                fail(f"server exited: {proc.stderr.read()}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ctx.wrap_socket(raw, server_hostname="localhost").close()
                break
            except OSError:
                time.sleep(0.05)
        conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=10)
        conn.request("GET", "/messages")
        r = conn.getresponse()
        body = r.read().decode()
        conn.close()
        if r.status != 200:
            fail(f"status {r.status} body={body[:120]!r}")
        ok(f"task path served a request (partial H2B smoke): {body!r}")
    finally:
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        import shutil
        shutil.rmtree(td, ignore_errors=True)
    print("H2B_SMOKE PASS (partial; see H2B-RESULTS.md)")


if __name__ == "__main__":
    main()