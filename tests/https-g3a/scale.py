#!/usr/bin/env python3
"""G3A register scale/restore measurement (bounded, local-only).

Records are pre-generated as NDJSON before each run rather than added through
thousands of individual HTTPS requests.  The measurement is therefore the
startup/restore path (file reads + JSON decode + register insertion), which is
what a populated register pays at boot; it deliberately does not claim HTTPS
ingest throughput.  Several detail and list-page requests are then timed.

Usage:
    python3 tests/https-g3a/scale.py --glon ./glon
    python3 tests/https-g3a/scale.py --sizes 10,100,1000
"""

import argparse
import http.client
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

HOST = "127.0.0.1"
PORT = 8443


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def page_of(mid):
    return sum(mid.encode("utf-8")) % 160


def wait_port_free(timeout=10.0):
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


class Server:
    def __init__(self, glon, args, cwd, timeout):
        self.cwd = cwd
        self.timeout = timeout
        self.proc = subprocess.Popen([glon] + args, cwd=cwd,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
        self.stderr = ""

    def wait_ready(self):
        end = time.time() + self.timeout
        ctx = ssl._create_unverified_context()
        while time.time() < end:
            if self.proc.poll() is not None:
                self.stderr = self.proc.stderr.read()
                fail(f"server exited before listening:\n{self.stderr}")
            try:
                raw = socket.create_connection((HOST, PORT), timeout=0.3)
                raw.settimeout(0.5)
                ss = ctx.wrap_socket(raw, server_hostname="localhost")
                ss.close()
                return
            except OSError:
                time.sleep(0.05)
        fail(f"no listener within {self.timeout}s")

    def stop(self, timeout=30.0):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
            fail("server did not stop")
        self.stderr += self.proc.stderr.read()
        return self.proc.returncode


def request(method, path, body=None, ctype=None, timeout=60.0):
    ctx = ssl._create_unverified_context()
    conn = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    headers = {}
    if ctype:
        headers["Content-Type"] = ctype
    conn.request(method, path, body=body, headers=headers)
    resp = conn.getresponse()
    data = resp.read()
    status = resp.status
    conn.close()
    return status, data


def generate_log(path, n):
    with open(path, "w") as f:
        for i in range(n):
            obj = {"id": f"s{i}", "name": f"n{i}", "email": f"n{i}@x.io",
                   "status": "active"}
            f.write(json.dumps(obj, separators=(",", ":")) + "\n")


def measure(opts, n):
    if not wait_port_free():
        fail("port 8443 is already in use")
    td = tempfile.mkdtemp(prefix=f"g3a-scale-{n}-")
    srv = None
    try:
        generate_log(os.path.join(td, "members.ndjson"), n)
        args = [
            "--allow-file-read", "--allow-file-write",
            "--http-cert", opts.cert,
            "--http-max-body", "2048",
            opts.web, opts.json, opts.slots, opts.reg, opts.persist, opts.server,
        ]
        t0 = time.time()
        srv = Server(opts.glon, args, td, opts.timeout)
        srv.wait_ready()
        startup = time.time() - t0

        status, body = request("GET", "/status")
        if status != 200:
            fail(f"n={n}: /status returned {status}: {body[:120]!r}")
        st = json.loads(body)
        if st.get("count") != n:
            fail(f"n={n}: /status count {st.get('count')}")

        probes = sorted(set([0, n // 2, n - 1]))
        detail_ok = 0
        list_ok = 0
        detail_times = []
        list_times = []
        pages = []
        for i in probes:
            mid = f"s{i}"
            t = time.time()
            status, body = request("GET", f"/api/members/{mid}")
            detail_times.append(time.time() - t)
            if status != 200 or json.loads(body).get("id") != mid:
                fail(f"n={n}: detail {mid} failed ({status})")
            detail_ok += 1

            page = page_of(mid)
            t = time.time()
            status, body = request("GET", f"/api/members?page={page}")
            list_times.append(time.time() - t)
            if status != 200:
                fail(f"n={n}: list page {page} returned {status}")
            data = json.loads(body)
            ids = [m.get("id") for m in data.get("members", [])]
            if len(ids) > 64 or mid not in ids or data.get("count") != n:
                fail(f"n={n}: list page {page} wrong")
            list_ok += 1
            pages.append(f"{page}:{len(ids)}")

        print(f"SCALE n={n} startup_restore_s={startup:.3f} "
              f"status_count={st.get('count')} "
              f"detail_ok={detail_ok}/{len(probes)} "
              f"detail_avg_ms={1000 * sum(detail_times) / len(detail_times):.1f} "
              f"list_ok={list_ok}/{len(probes)} "
              f"list_avg_ms={1000 * sum(list_times) / len(list_times):.1f} "
              f"list_pages={','.join(pages)}")
        if srv.stop() != 0:
            fail(f"n={n}: server did not stop cleanly")
        srv = None
        return {
            "n": n, "startup": startup, "count": st.get("count"),
            "detail_ms": 1000 * sum(detail_times) / len(detail_times),
            "list_ms": 1000 * sum(list_times) / len(list_times),
        }
    finally:
        if srv and srv.proc.poll() is None:
            srv.proc.kill()
        shutil.rmtree(td, ignore_errors=True)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", default=os.path.join(root, "glon"))
    ap.add_argument("--web", default=os.path.join(root, "modules/web.glon"))
    ap.add_argument("--json", default=os.path.join(root, "modules/json.glon"))
    ap.add_argument("--slots", default=os.path.join(root, "examples/https-g3a/slots.glon"))
    ap.add_argument("--reg", default=os.path.join(root, "examples/https-g3a/reg.glon"))
    ap.add_argument("--persist", default=os.path.join(root, "examples/https-g3a/persist.glon"))
    ap.add_argument("--server", default=os.path.join(root, "examples/https-g3a/server.glon"))
    ap.add_argument("--cert", default=os.path.join(root, "tests/https-g1/.cert/server.pem"))
    ap.add_argument("--sizes", default="10,100,1000,10000")
    ap.add_argument("--timeout", type=float, default=600.0)
    opts = ap.parse_args()

    sizes = [int(x) for x in opts.sizes.split(",") if x.strip()]
    if not sizes or any(n < 1 for n in sizes):
        fail("--sizes must be a comma-separated list of positive counts")
    for path in (opts.glon, opts.web, opts.json, opts.slots, opts.reg,
                 opts.persist, opts.server, opts.cert):
        if not os.path.exists(path):
            fail(f"missing file {path}")

    results = []
    for n in sizes:
        results.append(measure(opts, n))
    print("SCALE_RESULTS " + json.dumps(results, separators=(",", ":")))
    print("HTTPS_G3A_SCALE PASS")


if __name__ == "__main__":
    main()
