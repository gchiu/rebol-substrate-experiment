#!/usr/bin/env python3
"""Loopback acceptance test for the Glon Worlds G3B compatibility demo."""
import hashlib
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
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GLON = ROOT / "glon"
APP = ROOT / "examples" / "glon-worlds-g3b" / "server.glon"
CERT = ROOT / "tests" / "https-g1" / ".cert" / "server.pem"
WORLDS = Path("/mnt/d/repos/glon-worlds")
DB = WORLDS / "server" / "data" / "messages.db"
CHAT_JS = WORLDS / "chat" / "chat.js"
PORT = 8443
HOST = "127.0.0.1"


def sha(path):
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def wait_listener(timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("listener did not become ready")


def start(data_dir):
    cmd = [
        str(GLON),
        "--http-bind", HOST,
        "--http-cert", str(CERT),
        "--io-root", str(data_dir),
        "--allow-file-read",
        "--allow-file-write",
        str(ROOT / "glon-lib" / "strings-ext.glon"),
        str(ROOT / "modules" / "web.glon"),
        str(ROOT / "modules" / "json.glon"),
        str(APP),
    ]
    log = open(Path(data_dir) / "server.log", "wb")
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)
    try:
        wait_listener()
    except Exception:
        stop(proc)
        log.close()
        raise
    return proc, log


def stop(proc, log=None):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait(timeout=5)
    if log:
        log.close()


class Client:
    def __init__(self):
        self.ctx = ssl._create_unverified_context()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPSConnection(HOST, PORT, context=self.ctx, timeout=8)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            resp = conn.getresponse()
            data = resp.read()
            return resp.status, dict(resp.getheaders()), data
        finally:
            conn.close()

    def json(self, method, path, body=None, headers=None):
        status, hdrs, data = self.request(method, path, body, headers)
        return status, hdrs, json.loads(data.decode("utf-8"))

    def post(self, payload):
        return self.json("POST", "/api/messages",
                         json.dumps(payload).encode("utf-8"),
                         {"Content-Type": "application/json"})


def expect(cond, message):
    if not cond:
        raise AssertionError(message)


def main():
    if not GLON.exists():
        raise SystemExit(f"missing binary: {GLON}")
    if not CERT.exists():
        raise SystemExit(f"missing certificate: {CERT}")
    if not DB.exists() or not CHAT_JS.exists():
        raise SystemExit("Glon Worlds files are missing")

    before_db = sha(DB)
    before_chat = sha(CHAT_JS)
    data = Path(tempfile.mkdtemp(prefix="glon-worlds-g3b-"))
    proc = log = None
    try:
        proc, log = start(data)

        listener = subprocess.check_output(
            ["ss", "-ltnH", f"sport = :{PORT}"], text=True)
        locals_ = [line.split()[3] for line in listener.splitlines() if line.strip()]
        expect(locals_ == [f"{HOST}:{PORT}"],
               f"expected only {HOST}:{PORT}, got {locals_!r}")

        c = Client()

        status, hdrs, body = c.json("GET", "/api/groups")
        expect(status == 200, f"groups status {status}")
        expect(hdrs.get("Content-Type", "").startswith("application/json"),
               f"groups content-type {hdrs.get('Content-Type')}")
        expect(body.get("can_create") is False, f"can_create {body!r}")
        expect(len(body.get("groups", [])) == 1, f"groups {body!r}")
        g = body["groups"][0]
        expect(g == {
            "id": 1, "slug": "general", "name": "general",
            "description": "", "created_at": "2026-01-01T00:00:00+00:00",
        }, f"group shape {g!r}")

        status, hdrs, body = c.json("GET", "/api/session")
        expect(status == 200 and body == {"user": None}, f"session {status} {body!r}")

        status, hdrs, body = c.json("GET", "/api/messages?group=general&after=0")
        expect(status == 200 and body == {"messages": []}, f"empty messages {status} {body!r}")

        status, hdrs, body = c.request("POST", "/api/messages", b"not json",
                                       {"Content-Type": "application/json"})
        expect(status == 400 and json.loads(body) == {"error": "invalid json"},
               f"invalid json {status} {body!r}")

        status, hdrs, body = c.post({"text": "  \t\n"})
        expect(status == 400 and body == {"error": "text required"},
               f"blank text {status} {body!r}")

        status, hdrs, body = c.post({"text": "hello", "group": "missing"})
        expect(status == 400 and body == {"error": "unknown group"},
               f"unknown post group {status} {body!r}")

        text = 'hello <world> & "quoted"'
        status, hdrs, msg = c.post({"text": text, "group": "general"})
        expect(status == 201, f"post status {status}: {msg!r}")
        expect(msg["id"] == 1 and msg["author"] == "Glon Demo", f"post msg {msg!r}")
        expect(msg["text"] == text and msg["group"] == "general", f"post text {msg!r}")
        expect(msg["user_id"] is None and msg["legacy"] is True, f"post identity {msg!r}")
        expect(msg["preview"] is None and msg["preview_pending"] is False,
               f"post preview {msg!r}")
        expect(msg["youtube"] is None, f"post youtube {msg!r}")
        expect(msg["html"] == '<p>hello &lt;world&gt; &amp; &quot;quoted&quot;</p>',
               f"post html {msg['html']!r}")

        status, hdrs, body = c.json("GET", "/api/messages?group=general&after=0")
        expect(status == 200 and [m["id"] for m in body["messages"]] == [1],
               f"read after post {status} {body!r}")
        status, hdrs, body = c.json("GET", "/api/messages?group=general&after=1")
        expect(status == 200 and body == {"messages": []}, f"after=1 {status} {body!r}")
        status, hdrs, body = c.json("GET", "/api/messages?group=missing")
        expect(status == 200 and body == {"messages": []}, f"unknown get group {status} {body!r}")

        status, hdrs, msg2 = c.post({"text": "second"})
        expect(status == 201 and msg2["id"] == 2, f"second post {status} {msg2!r}")
        status, hdrs, body = c.json("GET", "/api/messages?after=0")
        expect([m["id"] for m in body["messages"]] == [1, 2],
               f"order {body!r}")

        stop(proc, log)
        proc = log = None
        expect((data / "messages.ndjson").exists(), "NDJSON store missing")
        lines = (data / "messages.ndjson").read_text().splitlines()
        expect(len(lines) == 2, f"store lines {len(lines)}")

        proc, log = start(data)
        c = Client()
        status, hdrs, body = c.json("GET", "/api/messages?group=general&after=0")
        expect(status == 200 and [m["id"] for m in body["messages"]] == [1, 2],
               f"restart read {status} {body!r}")
        status, hdrs, body = c.json("GET", "/api/session")
        expect(status == 200 and body == {"user": None}, f"restart session {body!r}")

        status, hdrs, msg3 = c.post({"text": "after restart"})
        expect(status == 201 and msg3["id"] == 3, f"restart post {status} {msg3!r}")
        status, hdrs, body = c.json("GET", "/api/messages?after=2")
        expect([m["id"] for m in body["messages"]] == [3], f"restart after {body!r}")

        expect(sha(DB) == before_db, "Glon Worlds messages.db changed")
        expect(sha(CHAT_JS) == before_chat, "Glon Worlds chat.js changed")

        print("PASS groups/session/read")
        print("PASS validation and escaped HTML")
        print("PASS ordering, pagination, unknown group")
        print("PASS restart persistence and next id")
        print("PASS loopback-only listener")
        print("PASS Glon Worlds database and client unchanged")
    finally:
        if proc is not None:
            stop(proc, log)
        if proc is not None and proc.returncode not in (0, None):
            logpath = data / "server.log"
            if logpath.exists():
                print(logpath.read_text(errors="replace"), file=sys.stderr)
        shutil.rmtree(data, ignore_errors=True)


if __name__ == "__main__":
    main()
