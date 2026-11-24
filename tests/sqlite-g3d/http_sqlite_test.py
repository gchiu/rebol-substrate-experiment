#!/usr/bin/env python3
"""SQLite callback -> HTTP response through the pre-response pump."""
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
GLON = ROOT / "glon"
APP = ROOT / "tests" / "sqlite-g3d" / "http_sqlite.glon"
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
HOST = "127.0.0.1"
PORT = 8443


def wait(timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("listener did not become ready")


def main():
    if not GLON.exists() or not CERT.exists():
        sys.exit("missing glon binary or test certificate")
    data = pathlib.Path(tempfile.mkdtemp(prefix="glon-sqlite-http-"))
    log = open(data / "server.log", "wb")
    p = subprocess.Popen([str(GLON), "--http-bind", HOST, "--http-cert", str(CERT),
                          "--io-root", str(data), "--allow-file-read",
                          "--allow-file-write", str(APP)],
                         cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                         start_new_session=True)
    try:
        wait()
        ctx = ssl._create_unverified_context()
        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=8)
        c.request("GET", "/")
        r = c.getresponse()
        body = r.read().decode()
        c.close()
        assert r.status == 200, (r.status, body)
        assert body == "count=1", body
        print("PASS SQLite step/finalize during HTTP pre-response pump")
        print("PASS SQLite-backed response returned to CivetWeb")
    finally:
        if p.poll() is None:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                p.wait()
        log.close()
        if p.returncode != 0:
            print((data / "server.log").read_text(errors="replace"), file=sys.stderr)
            sys.exit(f"HTTP SQLite app failed (rc={p.returncode})")
        import shutil
        shutil.rmtree(data, ignore_errors=True)


if __name__ == "__main__":
    main()
