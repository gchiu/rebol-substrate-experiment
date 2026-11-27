#!/usr/bin/env python3
"""GLON-JSON-J2: JSON decoding inside G3H's HTTP tasks (3 tasks x 3,400 cells).

Escape-heavy and malformed request bodies (within the bridge's ~500-byte
limit) must be decoded or rejected with 400 without exhausting a task's return
stack; before J2 one such body halted the whole HTTPS server.  Requires the
expanded task build (make json-j2-g3h-test).
"""
import http.client, json, os, pathlib, shutil, signal, ssl, subprocess, sys, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[2]
os.chdir(ROOT)
S = tempfile.mkdtemp(prefix="json-j2-g3h-")
root = os.path.join(S, "root")
os.makedirs(root)
log = open(os.path.join(S, "srv.out"), "w")
p = subprocess.Popen(["./glon", "--http-tasks", "--http-bind", "127.0.0.1", "--http-cert",
     "tests/https-g1/.cert/server.pem", "--io-root", root, "--allow-file-read", "--allow-file-write",
     "modules/web.glon", "modules/json.glon", "examples/glon-worlds-g3b/server.glon"],
     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
ctx = ssl._create_unverified_context()
def req(method, path, body=None):
    c = http.client.HTTPSConnection("127.0.0.1", 8443, context=ctx, timeout=8)
    try:
        c.request(method, path, body=body, headers={"Content-Type": "application/json"} if body else {})
        r = c.getresponse(); return r.status, r.read().decode()
    finally: c.close()
for _ in range(80):
    try: req("GET", "/api/session"); break
    except OSError: time.sleep(0.25)
ok = True
def check(name, cond, info=""):
    global ok
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "  " + info)); ok = ok and cond
# 1. many escapes in a valid body (< 500 bytes), on POST /api/users: the body
# is decoded, display_name is stored through SQLite and returned through
# json/encode (no HTML escaping on this route)
esc = '\\"' * 30 + '\\\\' * 30 + '\\n' * 20 + '\\t' * 10 + '\\/' * 10 + '\\u00e9' * 15 + '\\ud83d\\ude00' * 5
body = '{"username":"esc","display_name":"' + esc + '"}'
check(f"escape-heavy body is {len(body)} bytes (< 500), 125 escapes", len(body) < 500)
want = json.loads(body)["display_name"]
st, data = req("POST", "/api/users", body)
check("POST /api/users with 125 escapes -> 201", st == 201, f"{st} {data[:80]}")
if st == 201:
    check("stored display_name decoded exactly", json.loads(data)["display_name"] == want, data[:120])
st, data = req("GET", "/api/users")
check("GET /api/users -> 200, display_name round-trips",
      st == 200 and json.loads(data)[-1]["display_name"] == want, f"{st} {data[:80]}")
st, data = req("POST", "/api/messages", '{"text":"say \\"hi\\" \\"x\\" \\"y\\""}')
check("POST with 6 escaped quotes -> 201, decoded", st == 201 and json.loads(data)["text"] == 'say "hi" "x" "y"', f"{st} {data[:80]}")
# 2. malformed bodies -> 400, server alive
bad = {"bad escape": '{"text":"a\\qb"}', "lone high surrogate": '{"text":"\\ud83d"}',
       "lone low surrogate": '{"text":"\\ude00"}', "short \\u": '{"text":"\\u12"}',
       "unterminated": '{"text":"abc', "raw control byte": '{"text":"a\nb"}',
       "17 levels": '[' * 17 + '1' + ']' * 17, "33 items": '[' + ','.join(['1'] * 33) + ']',
       "duplicate key": '{"text":"a","text":"b"}', "trailing data": '{"text":"a"} x'}
for name, b in bad.items():
    st, data = req("POST", "/api/messages", b)
    check(f"malformed ({name}) -> 400", st == 400, f"{st} {data[:60]}")
# 3. still serving
st, data = req("GET", "/api/groups")
check("server still serves after malformed input", st == 200, str(st))
check("server process alive", p.poll() is None)
os.killpg(p.pid, signal.SIGTERM); p.wait(timeout=5); log.close()
errs = [l for l in open(os.path.join(S, "srv.out")) if "halt" in l or "run-tasks" in l]
check("no machine halt in the server log", not errs, str(errs[:2]))
shutil.rmtree(S, ignore_errors=True)
print("JSON_J2_G3H " + ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
