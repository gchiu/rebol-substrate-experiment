#!/usr/bin/env python3
"""Same-origin browser read check using the existing Glon Worlds chat assets.

The test harness serves the real chat/index.html and chat/chat.js on 127.0.0.1
and reverse-proxies /api to the Glon demo.  No static hosting is added to Glon.
"""
import http.client
import http.server
import json
import shutil
import ssl
import sys
import tempfile
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import acceptance  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

WORLDS = Path("/mnt/d/repos/glon-worlds")
CHAT = WORLDS / "chat"
BROWSER_PORT = 8080
GLON_HOST = "127.0.0.1"
GLON_PORT = acceptance.PORT


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _proxy(self, method):
        parsed = urllib.parse.urlsplit(self.path)
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length) if length else None
        conn = http.client.HTTPSConnection(GLON_HOST, GLON_PORT,
                                            context=ssl._create_unverified_context(),
                                            timeout=8)
        headers = {"Content-Type": self.headers.get("Content-Type", "application/json")}
        conn.request(method, parsed.path + (("?" + parsed.query) if parsed.query else ""),
                     body=body, headers=headers)
        r = conn.getresponse()
        data = r.read()
        self.send_response(r.status)
        for k, v in r.getheaders():
            if k.lower() in ("content-length", "transfer-encoding", "connection"):
                continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        conn.close()

    def _static(self, name, ctype):
        data = (CHAT / name).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path.startswith("/api/"):
            return self._proxy("GET")
        if path in ("/", "/index.html"):
            return self._static("index.html", "text/html; charset=utf-8")
        if path == "/chat.js":
            return self._static("chat.js", "text/javascript; charset=utf-8")
        if path.startswith("/wasm/"):
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        if urllib.parse.urlsplit(self.path).path.startswith("/api/"):
            return self._proxy("POST")
        self.send_response(404)
        self.end_headers()


def expect(cond, message):
    if not cond:
        raise AssertionError(message)


def main():
    data = Path(tempfile.mkdtemp(prefix="glon-worlds-browser-"))
    proc = log = httpd = None
    try:
        proc, log = acceptance.start(data)
        c = acceptance.Client()
        status, hdrs, msg = c.post({"text": "hello from the browser compatibility check"})
        expect(status == 201, f"seed post {status} {msg!r}")

        httpd = http.server.ThreadingHTTPServer(("127.0.0.1", BROWSER_PORT), Handler)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()

        api_status = {}

        with sync_playwright() as pw:
            browser = pw.chromium.launch(headless=True)
            page = browser.new_page()

            def on_response(resp):
                if "/api/" in resp.url:
                    api_status[resp.request.method + " " + urllib.parse.urlsplit(resp.url).path] = resp.status
            page.on("response", on_response)
            page.goto(f"http://127.0.0.1:{BROWSER_PORT}/?group=general", wait_until="domcontentloaded")
            page.wait_for_selector("#log .msg", timeout=8000)
            text = page.locator("#log").inner_text()
            expect("hello from the browser compatibility check" in text,
                   f"message not rendered: {text!r}")
            expect(api_status.get("GET /api/groups") == 200, f"groups response: {api_status!r}")
            expect(api_status.get("GET /api/messages") == 200, f"messages response: {api_status!r}")
            expect(api_status.get("GET /api/session") == 200, f"session response: {api_status!r}")
            status_text = page.locator("#status").inner_text()
            expect("read-only" in status_text, f"status: {status_text!r}")
            browser.close()

        print("PASS existing chat HTML/JS rendered a Glon message")
        print("PASS same-origin API groups/messages/session all returned 200")
        print("PASS unauthenticated browser is read-only")
        print(f"PASS API calls observed: {api_status}")
    finally:
        if httpd is not None:
            httpd.shutdown()
            httpd.server_close()
        if proc is not None:
            acceptance.stop(proc, log)
        shutil.rmtree(data, ignore_errors=True)


if __name__ == "__main__":
    main()
