#!/usr/bin/env python3
"""GLON-WORLDS-W1: reliable ordinary messaging over HTTPS in task mode.

Starts the G3H Glon Worlds app with --http-tasks (3 x 3,400-cell task stacks),
sends and retrieves ordinary messages (plain, HTML-metacharacter, Unicode and
the 1,024-byte maximum), checks malformed/oversized controlled errors,
overlapping request isolation, and persistence across restart.

Local-only. Requires make w1-test.
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
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST, PORT = "127.0.0.1", 8443
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
MAX_TEXT = 1024
COOKIE = None


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def esc(s):
    body = (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;")
             .replace("'", "&#39;"))
    return "<p>" + body + "</p>"


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
            fail("server did not start: " + self.trace())

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
                   json.dumps({"username": "w1user", "password": "password123"}))
    if st != 201:
        fail(f"register w1user: {st} {body[:120]!r}")
    st, body = req("POST", "/api/auth/login",
                   json.dumps({"username": "w1user", "password": "password123"}))
    if st != 200 or not COOKIE:
        fail(f"login w1user: {st} {body[:120]!r}")


def post_message(text):
    return req("POST", "/api/messages",
               json.dumps({"text": text, "group": "general"}))


def get_messages():
    st, body = req("GET", "/api/messages?group=general&after=0")
    if st != 200:
        fail(f"GET /api/messages: {st} {body[:120]!r}")
    return json.loads(body)["messages"]


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists():
        fail("missing glon binary, certificate or G3H app (build first)")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w1-msg-")
    io_root = os.path.join(td, "io")
    log_path = os.path.join(td, "server.log")
    os.makedirs(io_root)
    expected = []
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)

        st, body = req("GET", "/api/groups")
        if st != 200:
            fail(f"GET /api/groups: {st} {body[:120]!r}")
        ok("server is serving the G3H routes")
        auth_setup()
        ok("registered and authenticated a messaging user")

        plain = "ordinary hello"
        st, body = post_message(plain)
        if st != 201:
            fail(f"plain POST: {st} {body[:120]!r}")
        expected.append((plain, esc(plain)))
        ok("plain message POST -> 201")

        html_text = "<script>alert('x&y')</script> \"quoted\""
        st, body = post_message(html_text)
        if st != 201:
            fail(f"HTML POST: {st} {body[:120]!r}")
        expected.append((html_text, esc(html_text)))
        ok("HTML-metacharacter message POST -> 201")

        unicode_text = "café π 😀"
        st, body = post_message(unicode_text)
        if st != 201:
            fail(f"Unicode POST: {st} {body[:120]!r}")
        expected.append((unicode_text, esc(unicode_text)))
        ok("Unicode message POST -> 201")

        max_text = "A" * MAX_TEXT
        st, body = post_message(max_text)
        if st != 201:
            fail(f"1024-byte POST: {st} {body[:160]!r}")
        expected.append((max_text, esc(max_text)))
        ok(f"maximum {MAX_TEXT}-byte message POST -> 201")

        special_text = "<" * MAX_TEXT
        st, body = post_message(special_text)
        if st != 201:
            fail(f"1024-escape POST: {st} {body[:160]!r}")
        expected.append((special_text, esc(special_text)))
        ok("1,024 escaped-byte message POST -> 201 (bounded escaping)")

        st, body = post_message("concurrent-A")
        if st != 201:
            fail(f"concurrent A: {st} {body[:120]!r}")
        st, body = post_message("concurrent-B")
        if st != 201:
            fail(f"concurrent B: {st} {body[:120]!r}")

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:
            futs = [ex.submit(post_message, t) for t in ("overlap-1", "overlap-2")]
            results = [f.result() for f in futs]
        if any(st != 201 for st, _ in results):
            fail(f"overlapping POSTs: {[st for st, _ in results]}")
        expected += [("concurrent-A", esc("concurrent-A")),
                     ("concurrent-B", esc("concurrent-B")),
                     ("overlap-1", esc("overlap-1")),
                     ("overlap-2", esc("overlap-2"))]
        ok("overlapping requests kept their own messages")

        msgs = get_messages()
        by_text = {m["text"]: m for m in msgs}
        for text, html in expected:
            if text not in by_text:
                fail(f"message missing from GET: {text[:40]!r}")
            if by_text[text]["html"] != html:
                fail(f"wrong escaped html for {text[:40]!r}: got {by_text[text]['html'][:80]!r} want {html[:80]!r}")
        ok(f"all {len(expected)} messages retrieved with exact text and HTML escaping")

        # Controlled malformed/oversized requests.
        st, body = req("POST", "/api/messages", '{"text":')
        if st != 400:
            fail(f"malformed JSON: {st} {body[:120]!r}")
        too_long = "B" * (MAX_TEXT + 1)
        st, body = post_message(too_long)
        if st != 400:
            fail(f"text too long: {st} {body[:120]!r}")
        # The old bridge failed near 500 bytes because it embedded one large
        # mk-string block.  Chunking must keep this controlled by the app.
        bridge_body = json.dumps({"text": "C" * 1400})
        st, body = req("POST", "/api/messages", bridge_body)
        if st != 400:
            fail(f"1400-byte bridge body: {st} {body[:120]!r}")
        huge = json.dumps({"text": "D" * 3000})
        st, body = req("POST", "/api/messages", huge)
        if st not in (400, 413):
            fail(f"oversized body: {st} {body[:120]!r}")
        if srv.proc.poll() is not None:
            fail("server died during malformed/oversized requests")
        st, _ = req("GET", "/api/groups")
        if st != 200:
            fail(f"server unhealthy after malformed requests: {st}")
        ok("malformed, over-limit and oversized bodies return controlled errors")

        store_path = os.path.join(io_root, "messages.ndjson")
        if not os.path.exists(store_path):
            fail("messages.ndjson was not written")
        # The app responds before its asynchronous file write callback runs.
        # Wait for the write to settle before restarting, as a client would.
        deadline = time.time() + 15
        stored_texts = set()
        stored = ""
        while time.time() < deadline:
            stored = pathlib.Path(store_path).read_text(errors="replace")
            stored_texts = set()
            for line in stored.splitlines():
                if not line:
                    continue
                try:
                    stored_texts.add(json.loads(line)["text"])
                except (ValueError, KeyError, TypeError):
                    pass
            if all(text in stored_texts for text, _ in expected):
                break
            time.sleep(0.1)
        missing = [text[:30] for text, _ in expected if text not in stored_texts]
        if missing:
            fail(f"messages missing from store after write wait: {missing!r}")
        ok(f"message store settled ({len(stored)} bytes, {len(stored_texts)} records) before restart")
        srv.stop()
        time.sleep(0.5)

        srv = Server(io_root, log_path)
        msgs = get_messages()
        by_text = {m["text"]: m for m in msgs}
        for text, html in expected:
            if text not in by_text or by_text[text]["html"] != html:
                fail(f"message did not persist correctly: {text[:40]!r}")
        ok("all messages persisted across restart")
        trace = srv.trace()
        srv.stop()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-500:])
        ok("no machine halt in the task-mode server log")
        print("W1_MESSAGING PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
