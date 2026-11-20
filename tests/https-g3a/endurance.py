#!/usr/bin/env python3
"""Bounded G3A endurance harness (keep-alive, mixed requests, resource samples).

This is not a long-term reliability claim.  It reports only the run actually
completed.  Use --requests 100000 for the standard endurance checkpoint.
"""

import argparse
import http.client
import json
import os
import re
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

HOST = "127.0.0.1"
PORT = 8443
FORM = "application/x-www-form-urlencoded"


class Server:
    def __init__(self, glon, files, cert, root):
        self.proc = subprocess.Popen(
            [glon, "--http-cert", cert, "--http-keep-alive",
             "--http-max-requests", "0", "--http-max-body", "2048",
             "--allow-file-read", "--allow-file-write", "--io-root", root,
             "--http-report"] + files,
            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.root = root

    def ready(self):
        ctx = ssl._create_unverified_context()
        for _ in range(400):
            if self.proc.poll() is not None:
                raise SystemExit(self.proc.stderr.read())
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ss = ctx.wrap_socket(raw, server_hostname="localhost")
                ss.close()
                return
            except OSError:
                time.sleep(0.05)
        raise SystemExit("server did not listen")

    def rss_kb(self):
        try:
            with open(f"/proc/{self.proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        return int(line.split()[1])
        except OSError:
            pass
        return None

    def fds(self):
        try:
            return len(os.listdir(f"/proc/{self.proc.pid}/fd"))
        except OSError:
            return None

    def stop(self):
        peak = self.rss_kb()
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        err = self.proc.stderr.read()
        live = None
        gc = None
        line_out = None
        for line in err.splitlines():
            if line.startswith("glon: http: requests="):
                line_out = line
                m = re.search(r"live_cells=(\d+)", line)
                if m:
                    live = int(m.group(1))
                m = re.search(r"gc=(\d+)", line)
                if m:
                    gc = int(m.group(1))
        return peak, live, gc, line_out


def wait_free(timeout=5.0):
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


def jbytes(obj):
    return json.dumps(obj, separators=(",", ":")).encode()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--requests", type=int, default=100000)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--interval", type=int, default=10000)
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    opts = ap.parse_args()
    if not wait_free():
        raise SystemExit("port 8443 is in use")
    if not os.path.exists(opts.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")], check=True)
    files = [
        os.path.join(root, "modules/web.glon"),
        os.path.join(root, "modules/json.glon"),
        os.path.join(root, "examples/https-g3a/slots.glon"),
        os.path.join(root, "examples/https-g3a/reg.glon"),
        os.path.join(root, "examples/https-g3a/persist.glon"),
        os.path.join(root, "examples/https-g3a/server.glon"),
    ]
    tmp = tempfile.mkdtemp(prefix="g3a-endurance-")
    srv = Server(opts.glon, files, opts.cert, tmp)
    srv.ready()
    ctx = ssl._create_unverified_context()
    stop = threading.Event()
    samples = []
    errors = []
    status = None

    def monitor():
        while not stop.is_set():
            time.sleep(2.0)
            samples.append((time.time(), srv.rss_kb(), srv.fds()))
    mon = threading.Thread(target=monitor, daemon=True)
    mon.start()

    def seed():
        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=10)
        for i in range(10):
            body = urllib.parse.urlencode({"id": f"e{i}", "name": f"Endurance {i}",
                                           "email": f"e{i}@example.test",
                                           "status": "active"}).encode()
            c.request("POST", "/", body=body, headers={"Content-Type": FORM})
            r = c.getresponse(); r.read()
            if r.status not in (200, 409):
                errors.append(f"seed {i} status {r.status}")
        c.close()

    seed()
    lat = []
    lock = threading.Lock()
    start = time.time()

    def worker(w):
        c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=20)
        n = opts.requests // opts.workers
        if w == 0:
            n += opts.requests % opts.workers
        for i in range(n):
            k = (i + w) % 10
            try:
                t0 = time.perf_counter()
                if k in (0, 1, 2, 3):
                    path = "/" if k < 2 else ("/members/e1" if k == 2 else "/status")
                    c.request("GET", path)
                elif k == 4:
                    c.request("GET", f"/api/members?page={i % 160}")
                elif k == 5:
                    body = urllib.parse.urlencode({"id": "e1", "name": f"Endurance {i}",
                                                   "email": "e1@example.test",
                                                   "status": "active"}).encode()
                    c.request("POST", "/members/e1", body=body,
                              headers={"Content-Type": FORM})
                elif k == 6:
                    c.request("POST", "/", body=urllib.parse.urlencode({"id": "bad", "name": ""}).encode(),
                              headers={"Content-Type": FORM})
                elif k == 7:
                    c.request("DELETE", "/")
                elif k == 8:
                    c.request("GET", "/add")
                else:
                    c.request("GET", "/members/e2")
                r = c.getresponse(); r.read()
                if r.status not in (200, 201, 405, 422):
                    with lock:
                        errors.append(f"status {r.status} path {r.path if hasattr(r,'path') else ''}")
                with lock:
                    if i % 50 == 0:
                        lat.append((time.perf_counter() - t0) * 1000.0)
            except Exception as e:
                with lock:
                    errors.append(f"worker {w}: {e}")
                try:
                    c.close()
                except Exception:
                    pass
                c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=20)
                time.sleep(0.05)
        c.close()

    threads = [threading.Thread(target=worker, args=(w,)) for w in range(opts.workers)]
    for t in threads:
        t.start()
    while any(t.is_alive() for t in threads):
        time.sleep(1.0)
    elapsed = time.time() - start
    stop.set()
    mon.join(timeout=3)
    peak, live, gc, report = srv.stop()
    if not wait_free():
        errors.append("port not released")
    lat = sorted(lat)
    med = lat[len(lat)//2] if lat else 0.0
    p95 = lat[int(len(lat)*0.95)] if lat else 0.0
    print(f"ENDURANCE requests={opts.requests} workers={opts.workers} elapsed_s={elapsed:.2f} "
          f"req_s={opts.requests/elapsed:.1f} latency_median_ms={med:.2f} latency_p95_ms={p95:.2f}")
    print(f"ENDURANCE peak_rss_kib={peak} live_cells={live} gc={gc} fds_start={samples[0][2] if samples else None} "
          f"fds_end={srv.fds() if srv.proc.poll() is None else samples[-1][2] if samples else None}")
    print(f"ENDURANCE persistence_bytes={os.path.getsize(os.path.join(tmp,'members.ndjson')) if os.path.exists(os.path.join(tmp,'members.ndjson')) else 0}")
    print(f"ENDURANCE report={report}")
    if errors:
        print(f"ENDURANCE ERRORS {len(errors)}: {errors[:5]}")
        raise SystemExit(1)
    print("G3A_ENDURANCE PASS")


if __name__ == "__main__":
    main()
