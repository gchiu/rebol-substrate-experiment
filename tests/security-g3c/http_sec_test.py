#!/usr/bin/env python3
"""HTTP pre-response sec pump and peer-address check.

Requires ./glon built with both sec and http (for example
MODULES=file,http,sec).  The test uses only loopback and the local test cert.
"""
import http.client
import pathlib
import signal
import socket
import ssl
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
GLON = ROOT / "glon"
APP = ROOT / "examples" / "security-g3c" / "http_sec.glon"
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
PORT = 8443
HOST = "127.0.0.1"

def wait(timeout=8):
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
    p = subprocess.Popen([str(GLON), "--http-bind", HOST, "--http-cert",
                          str(CERT), str(APP)], cwd=ROOT,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True, start_new_session=True)
    try:
        wait()
        ctx = ssl._create_unverified_context()
        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=8)
        c.request("GET", "/")
        r = c.getresponse()
        body = r.read().decode()
        c.close()
        assert r.status == 200, (r.status, body)
        assert "sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" in body, body
        assert "peer=127.0.0.1" in body, body
        t = int(body.split("time=")[1].split(" ")[0])
        assert t > 1700000000, t

        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=8)
        c.request("GET", "/bad-header")
        r = c.getresponse()
        bad = r.read()
        c.close()
        assert r.status >= 400, (r.status, bad)
        print("PASS HTTP sec pre-pump SHA-256/time")
        print("PASS http/peer reports the transport loopback address")
        print("PASS response header control bytes are rejected")
    finally:
        if p.poll() is None:
            try:
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
            except Exception:
                pass
            try:
                import os
                os.killpg(p.pid, signal.SIGTERM)
            except Exception:
                pass
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    import os
                    os.killpg(p.pid, signal.SIGKILL)
                except Exception:
                    pass
                p.wait()
        out, err = p.communicate()
        # A forbidden response header makes the host fail closed and stop the
        # listener; that is the expected rc=2 for the last check.
        if p.returncode not in (0, 2):
            print(out, end="")
            print(err, end="", file=sys.stderr)
            sys.exit(f"HTTP sec app failed (rc={p.returncode})")

if __name__ == "__main__":
    main()
