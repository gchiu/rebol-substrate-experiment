#!/usr/bin/env python3
"""GLON-WORLDS-W2: paginated history and multi-client HTTPS messaging.

Starts the native G3H app in HTTPS task mode, writes enough messages to span
several pages, verifies stable chronological pagination (no omissions or
duplicates), two independent HTTPS clients exchanging post/reply, concurrent
posts, controlled page-parameter errors, and restart persistence.

Local-only. Requires make w2-test.
"""

import concurrent.futures
import http.client
import json
import os
import pathlib
import shutil
import signal
import socket
import ssl
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
MAX_PAGE = 9
COOKIE = None


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def wait_port_free(timeout=5.0):
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


def wait_port(timeout=25.0):
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
        self.out = open(log_path, "a+")
        self.proc = subprocess.Popen(
            [str(ROOT / "glon"), "--http-tasks", "--http-bind", HOST,
             "--http-cert", str(CERT), "--io-root", io_root,
             "--allow-file-read", "--allow-file-write",
             "--http-timeout-ms", "30000"] + LIBS + [str(APP)],
            cwd=str(ROOT), stdout=self.out, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        if not wait_port():
            self.stop()
            fail("server did not start")

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
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.out.close()


def req(method, path, body=None, timeout=60):
    global COOKIE
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
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
        return r.status, r.read()
    finally:
        c.close()


def auth_setup():
    st, body = req("POST", "/api/auth/register",
                   json.dumps({"username": "w2user", "password": "password123"}))
    if st != 201:
        fail(f"register w2user: {st} {body[:120]!r}")
    st, body = req("POST", "/api/auth/login",
                   json.dumps({"username": "w2user", "password": "password123"}))
    if st != 200 or not COOKIE:
        fail(f"login w2user: {st} {body[:120]!r}")


class Client:
    """A distinct HTTPS client identity; each request uses its own connection."""

    def __init__(self, name):
        self.name = name

    def get(self, path):
        return req("GET", path)

    def post(self, text, group="general"):
        return req("POST", "/api/messages",
                   json.dumps({"text": text, "group": group}))


def get_page(after, limit=10, group="general"):
    st, body = req(
        "GET",
        f"/api/messages?group={group}&after={after}&limit={limit}")
    if st != 200:
        fail(f"GET page after={after} limit={limit}: {st} {body[:120]!r}")
    return json.loads(body)


def get_all():
    after = 0
    out = []
    pages = 0
    while True:
        data = get_page(after, MAX_PAGE)
        page = data["messages"]
        pages += 1
        if not page:
            break
        out.extend(page)
        if not data["has_more"]:
            break
        nxt = data["next_after"]
        if nxt <= after:
            fail(f"pagination did not advance: after={after} next_after={nxt}")
        after = nxt
    return out, pages


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists():
        fail("missing glon binary, certificate or app (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w2-history-")
    io_root = os.path.join(td, "io")
    log_path = os.path.join(td, "server.log")
    os.makedirs(io_root)
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)
        auth_setup()
        ok("registered and authenticated a history user")

        base = [f"m{i:02d}" for i in range(1, 19)]
        for text in base:
            st, body = req("POST", "/api/messages",
                           json.dumps({"text": text, "group": "general"}))
            if st != 201:
                fail(f"seed POST {text}: {st} {body[:120]!r}")
        ok(f"posted {len(base)} seed messages")

        all_msgs, pages = get_all()
        if [m["text"] for m in all_msgs] != base:
            fail("full history order/content mismatch")
        if len({m["id"] for m in all_msgs}) != len(base):
            fail("duplicate or missing message ids")
        if pages < 2:
            fail("history did not span multiple pages")
        ok(f"paginated history: {len(all_msgs)} messages over {pages} pages, stable ids")

        # Successive pages must not omit or duplicate a message.
        seen = []
        after = 0
        while True:
            data = get_page(after, MAX_PAGE)
            ids = [m["id"] for m in data["messages"]]
            if ids != sorted(ids) or any(i <= after for i in ids):
                fail(f"page after={after} is not ascending/stable: {ids}")
            seen.extend([m["text"] for m in data["messages"]])
            if not data["has_more"]:
                break
            after = data["next_after"]
        if seen != base:
            fail("page walk has omissions/duplicates or wrong order")
        ok("successive pages cover the history exactly once")

        # Explicit page-size limits and controlled errors.
        d = get_page(0, MAX_PAGE)
        if len(d["messages"]) != MAX_PAGE or not d["has_more"]:
            fail(f"max page size {MAX_PAGE} not enforced")
        st, body = req("GET", "/api/messages?group=general&after=0")
        if st != 200 or len(json.loads(body)["messages"]) != MAX_PAGE:
            fail(f"default page size wrong: {st}")
        for bad in (
            "/api/messages?group=general&after=0&limit=0",
            "/api/messages?group=general&after=0&limit=10",
            "/api/messages?group=general&after=x&limit=10",
        ):
            st, _ = req("GET", bad)
            if st != 400:
                fail(f"invalid page parameter accepted: {bad} -> {st}")
        ok("page size 1..9 enforced; invalid parameters return 400")

        client_a = Client("A")
        client_b = Client("B")
        st, body = client_a.post("A-message")
        if st != 201:
            fail(f"client A post: {st} {body[:100]!r}")
        a_msg = json.loads(body)
        st, body = client_b.get(f"/api/messages?group=general&after={a_msg['id'] - 1}&limit=5")
        if st != 200 or not any(m["text"] == "A-message" for m in json.loads(body)["messages"]):
            fail("client B did not retrieve client A's message")
        st, body = client_b.post("B-reply")
        if st != 201:
            fail(f"client B reply: {st} {body[:100]!r}")
        b_msg = json.loads(body)
        st, body = client_a.get(f"/api/messages?group=general&after={a_msg['id'] - 1}&limit=5")
        if st != 200 or not any(m["text"] == "B-reply" for m in json.loads(body)["messages"]):
            fail("client A did not retrieve client B's reply")
        a_view, _ = get_all()
        b_view, _ = get_all()
        if [m["id"] for m in a_view] != [m["id"] for m in b_view]:
            fail("clients disagree on message ordering")
        if [m["text"] for m in a_view[-2:]] != ["A-message", "B-reply"]:
            fail("post/reply ordering is not stable")
        ok("two independent HTTPS clients exchanged post and reply in one order")

        concurrent_texts = [f"c{i}" for i in range(1, 5)]

        def post_one(text):
            return text, req("POST", "/api/messages",
                             json.dumps({"text": text, "group": "general"}))

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
            results = list(ex.map(post_one, concurrent_texts))
        for text, (st, body) in results:
            if st != 201:
                fail(f"concurrent POST {text}: {st} {body[:100]!r}")
        all_msgs, _ = get_all()
        cids = [m["id"] for m in all_msgs if m["text"].startswith("c")]
        if len(cids) != len(concurrent_texts) or len(set(cids)) != len(cids):
            fail(f"concurrent message ids are not unique: {cids}")
        if not set(concurrent_texts).issubset({m["text"] for m in all_msgs}):
            fail("concurrent messages missing from history")
        for m in all_msgs:
            if m["text"] not in base + ["A-message", "B-reply"] + concurrent_texts:
                fail(f"unexpected message in history: {m['text']!r}")
        ok("concurrent posts kept unique ids and complete contents")

        # Overlapping history scans are serialized by the application.  A
        # second scan may receive a controlled 503, never a mixed page.
        def get_once(_):
            return req("GET", "/api/messages?group=general&after=0&limit=9")

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:
            get_results = list(ex.map(get_once, range(2)))
        for st, body in get_results:
            if st not in (200, 503):
                fail(f"overlapping GET returned {st}: {body[:100]!r}")
            if st == 200:
                parsed = json.loads(body)
                ids = [m["id"] for m in parsed["messages"]]
                if ids != sorted(ids) or len(set(ids)) != len(ids):
                    fail(f"overlapping GET returned a mixed page: {ids}")
        ok("overlapping history scans returned complete pages or controlled 503s")

        store_path = os.path.join(io_root, "messages.ndjson")
        lines = [json.loads(x) for x in pathlib.Path(store_path).read_text().splitlines() if x]
        if len(lines) != len(all_msgs):
            fail(f"store has {len(lines)} records, API has {len(all_msgs)}")
        if [x["id"] for x in lines] != sorted(x["id"] for x in lines):
            fail("store ids are not append-ordered")
        if len({x["id"] for x in lines}) != len(lines):
            fail("store contains duplicate ids")
        ok(f"store remains valid NDJSON ({len(lines)} records)")

        if srv.proc.poll() is not None:
            fail("server died during pagination/concurrency checks")
        srv.stop()
        time.sleep(0.5)
        srv = Server(io_root, log_path)
        after_restart, _ = get_all()
        if [m["id"] for m in after_restart] != [m["id"] for m in all_msgs]:
            fail("history ids changed across restart")
        if [m["text"] for m in after_restart] != [m["text"] for m in all_msgs]:
            fail("history text/order changed across restart")
        ok("paginated history survives restart")
        trace = srv.trace()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-400:])
        ok("no machine halt in the task-mode server log")
        print("W2_HISTORY PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
