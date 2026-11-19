#!/usr/bin/env python3
"""Bounded G3 workload and resource measurements.  Not a performance benchmark."""

import argparse
import http.client
import json
import os
import re
import signal
import socket
import ssl
import subprocess
import time
import urllib.parse

HOST = "127.0.0.1"
PORT = 8443
FORM = "application/x-www-form-urlencoded"


def request(method, path, body=None, ctype=None, timeout=5.0):
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    h = {"Content-Type": ctype} if ctype else {}
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    status = r.status
    c.close()
    return status, data


def form(fields):
    return request("POST", "/", urllib.parse.urlencode(fields).encode(), FORM)


def jpost(path, obj):
    return request("POST", path, json.dumps(obj).encode(), "application/json")


class Server:
    def __init__(self, glon, parse, app, cert):
        self.proc = subprocess.Popen(
            [glon, "--http-cert", cert, "--http-max-requests", "100000",
             "--http-max-body", "2048", "--http-report", parse, app],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.lat = []

    def ready(self):
        ctx = ssl._create_unverified_context()
        for _ in range(200):
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

    def stop(self):
        peak = self.rss_kb()
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        err = self.proc.stderr.read()
        live = None
        line_out = None
        for line in err.splitlines():
            if line.startswith("glon: http: requests="):
                line_out = line
                m = re.search(r"live_cells=(\d+)", line)
                if m:
                    live = int(m.group(1))
        return peak, live, line_out


def timed(method, path, body=None, ctype=None):
    t0 = time.perf_counter()
    status, data = request(method, path, body, ctype)
    return (time.perf_counter() - t0) * 1000.0, status


def median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0.0


def run_phase(label, add_records, total, glon, parse, app, cert):
    srv = Server(glon, parse, app, cert)
    srv.ready()
    if add_records:
        assert form({"id": "m3", "name": "Alice", "email": "alice@example.test",
                     "status": "active"})[0] == 200
        assert jpost("/api/members", {"id": "m4", "name": "Bob",
                                      "email": "bob@example.test",
                                      "status": "active"})[0] == 201
    # Warm-up.
    for _ in range(10):
        request("GET", "/")
    lat = []
    for i in range(total):
        k = i % 5
        if k == 0:
            ms, st = timed("GET", "/")
        elif k == 1:
            ms, st = timed("GET", "/members/m1")
        elif k == 2:
            if add_records:
                ms, st = timed("POST", "/members/m3",
                               urllib.parse.urlencode({"id": "m3", "name": "Alice",
                                                       "email": "alice@example.test",
                                                       "status": "active"}).encode(),
                               FORM)
            else:
                ms, st = timed("GET", "/members/m1")
        elif k == 3:
            ms, st = timed("DELETE", "/")
        else:
            ms, st = timed("POST", "/",
                           urllib.parse.urlencode({"id": "bad", "name": "",
                                                   "email": "b@example.test",
                                                   "status": "active"}).encode(),
                           FORM)
        if st not in (200, 405, 422):
            raise SystemExit(f"{label}: request {i} status {st}")
        if i < 100:
            lat.append(ms)
        if total >= 300 and i + 1 == 300:
            st, body = request("GET", "/")
            assert st == 200, f"{label}: checkpoint 300 list status {st}"
    st, body = request("GET", "/")
    assert st == 200, f"{label}: final list status {st}"
    peak, live, line = srv.stop()
    print(f"{label}: requests={total} peak_rss_kib={peak} live_cells={live} "
          f"median_ms={median(lat):.2f}")
    print(f"{label}: {line}")
    return live, peak


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--parse", default=os.path.join(root, "examples/https-g3/webparse.glon"))
    ap.add_argument("--app", default=os.path.join(root, "examples/https-g3/server.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    ap.add_argument("--extended", action="store_true",
                    help="also run the 3,000-request fixed-state checkpoint")
    args = ap.parse_args()
    if not os.path.exists(args.cert):
        subprocess.run([os.path.join(root, "tests/https-g1/make-cert.sh")], check=True)

    run_phase("A-300", True, 300, args.glon, args.parse, args.app, args.cert)
    run_phase("A-1000", True, 1000, args.glon, args.parse, args.app, args.cert)
    run_phase("B-baseline-300", False, 300, args.glon, args.parse, args.app, args.cert)
    run_phase("B-grown-300", True, 300, args.glon, args.parse, args.app, args.cert)
    run_phase("B-grown-1000", True, 1000, args.glon, args.parse, args.app, args.cert)
    if args.extended:
        run_phase("A-3000", True, 3000, args.glon, args.parse, args.app, args.cert)
    print("G3_WORKLOADS PASS")


if __name__ == "__main__":
    main()
