#!/usr/bin/env python3
"""GLON-WORLDS-W3: browser client asset + two-client HTTPS acceptance.

Checks the native server serves the single-page browser client and that two
independent HTTPS clients can exchange messages, refresh, page history and
recover after restart.  This is a browser-equivalent automated test: no real
browser engine is available in this environment.

Local-only. Requires make w3-test.
"""

import http.client
import json
import os
import pathlib
import re
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
HTML = ROOT / "examples" / "glon-worlds-g3b" / "web" / "index.html"
LIBS = [str(ROOT / "modules" / "web.glon"), str(ROOT / "modules" / "json.glon")]
PAGE = 9


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


def req(method, path, body=None, headers=None, timeout=60):
    ctx = ssl._create_unverified_context()
    c = http.client.HTTPSConnection(HOST, PORT, context=ctx, timeout=timeout)
    try:
        h = dict(headers or {})
        if body is not None and "Content-Type" not in h:
            h["Content-Type"] = "application/json"
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        return r.status, dict(r.getheaders()), r.read()
    finally:
        c.close()


class Client:
    """An independent browser-equivalent HTTPS client."""

    def __init__(self, name):
        self.name = name

    def page(self, after, limit=PAGE):
        path = f"/api/messages?group=general&after={after}&limit={limit}"
        st, _, body = req("GET", path)
        if st == 503:
            return {"busy": True}
        if st != 200:
            fail(f"{self.name} page {after}: {st} {body[:100]!r}")
        return json.loads(body)

    def all_messages(self):
        after = 0
        out = []
        for _ in range(1000):
            data = self.page(after)
            if data.get("busy"):
                time.sleep(0.1)
                continue
            out.extend(data["messages"])
            if not data["has_more"]:
                return out
            nxt = data["next_after"]
            if nxt <= after:
                fail(f"{self.name}: pagination did not advance")
            after = nxt
        fail(f"{self.name}: too many pages")

    def send(self, text):
        st, _, body = req("POST", "/api/messages",
                          json.dumps({"text": text, "group": "general"}))
        if st != 201:
            fail(f"{self.name} send: {st} {body[:120]!r}")
        return json.loads(body)


def main():
    if not (ROOT / "glon").exists() or not CERT.exists() or not APP.exists() or not HTML.exists():
        fail("missing glon binary, certificate, app or browser asset")
    subprocess.run([str(ROOT / "tests" / "https-g1" / "make-cert.sh")],
                   check=True, stdout=subprocess.DEVNULL)
    td = tempfile.mkdtemp(prefix="w3-browser-")
    io_root = os.path.join(td, "io")
    web_root = os.path.join(io_root, "web")
    log_path = os.path.join(td, "server.log")
    os.makedirs(web_root)
    shutil.copyfile(HTML, os.path.join(web_root, "index.html"))
    srv = None
    try:
        if not wait_port_free():
            fail("port 8443 is already in use before start")
        srv = Server(io_root, log_path)

        # Browser asset served by the native server.
        for path in ("/", "/index.html"):
            st, hs, body = req("GET", path)
            if st != 200 or not hs.get("Content-Type", "").startswith("text/html"):
                fail(f"GET {path}: {st} {hs.get('Content-Type')!r}")
            text = body.decode("utf-8")
            for marker in ("Glon Worlds", "Load older", "Refresh", "Message"):
                if marker not in text:
                    fail(f"browser asset missing {marker!r}")
        source = HTML.read_text()
        if "textContent" not in source or "innerHTML" in source:
            fail("browser rendering is not textContent-only")
        if "fetch(" not in source or "/api/messages" not in source:
            fail("browser asset does not use the same-origin API")
        ok("native server serves the browser client and safe textContent rendering")

        st, _, body = req("GET", "/api/groups")
        if st != 200 or b"groups" not in body:
            fail("groups route unavailable to browser client")
        ok("browser client's group API is available")

        a = Client("A")
        b = Client("B")
        if [m["id"] for m in a.all_messages()] != []:
            fail("fresh history is not empty")

        html_text = '<script>alert("x&y")</script> "quoted"'
        unicode_text = "café π 😀"
        ma = a.send(html_text)
        mu = a.send(unicode_text)
        ok("browser-equivalent Client A sent HTML-metacharacter and Unicode messages")

        b_msgs = b.all_messages()
        if not any(m["id"] == ma["id"] and m["text"] == html_text for m in b_msgs):
            fail("Client B did not retrieve A's HTML-metacharacter message")
        if not any(m["id"] == mu["id"] and m["text"] == unicode_text for m in b_msgs):
            fail("Client B did not retrieve A's Unicode message")
        ok("Client B retrieved both messages with exact text")

        reply = b.send("reply from B")
        a_msgs = a.all_messages()
        if not any(m["id"] == reply["id"] and m["text"] == "reply from B" for m in a_msgs):
            fail("Client A did not retrieve B's reply")
        b_again = b.all_messages()
        if [m["id"] for m in a_msgs] != [m["id"] for m in b_again]:
            fail("clients disagree on message ordering")
        if [m["text"] for m in a_msgs] != [m["text"] for m in b_again]:
            fail("clients disagree on message content")
        if [m["id"] for m in a_msgs] != sorted(m["id"] for m in a_msgs):
            fail("message IDs are not chronological")
        ok("two clients share the same stable message IDs and ordering")

        # Page-walk exactly as the browser initial load does: every page once,
        # no duplicate IDs across page boundaries.
        seen = []
        after = 0
        for _ in range(100):
            data = a.page(after)
            if data.get("busy"):
                time.sleep(0.1)
                continue
            for m in data["messages"]:
                if m["id"] in seen:
                    fail(f"duplicate message across pages: {m['id']}")
                seen.append(m["id"])
            if not data["has_more"]:
                break
            after = data["next_after"]
        if seen != [m["id"] for m in a_msgs]:
            fail("browser page walk omitted or duplicated messages")
        ok("browser pagination loads earlier history without duplicates")

        # A second refresh after no new posts is stable.
        before = a.all_messages()
        after_refresh = a.all_messages()
        if [m["id"] for m in before] != [m["id"] for m in after_refresh]:
            fail("refresh changed message history")
        ok("browser refresh is stable with no new messages")

        srv.stop()
        time.sleep(0.5)
        srv = Server(io_root, log_path)
        a_restart = a.all_messages()
        b_restart = b.all_messages()
        if [m["id"] for m in a_restart] != [m["id"] for m in a_msgs]:
            fail("history IDs changed after restart")
        if [m["text"] for m in a_restart] != [m["text"] for m in a_msgs]:
            fail("history content changed after restart")
        if [m["id"] for m in b_restart] != [m["id"] for m in a_restart]:
            fail("clients disagree after restart")
        ok("conversation survives server restart for both clients")

        trace = srv.trace()
        if "halt" in trace or "run-tasks" in trace:
            fail("machine halt in server log: " + trace[-400:])
        ok("no machine halt in the task-mode server log")
        print("W3_BROWSER PASS")
    finally:
        if srv is not None:
            srv.stop()
        shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    main()
