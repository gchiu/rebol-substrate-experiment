#!/bin/sh
# apps/d8_authority_test.sh -- D8 per-application identity/authority proof.
#
# Central invariant: SAME REQUESTED PERMISSIONS + DIFFERENT APPLICATION ID
#                   != SAME AUTHORITY.
#
# Identity comes from trusted installation state, never from the manifest.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
bin="$root/glon-desktop"
cd "$root"
tmp="$(mktemp -d)"
data="$tmp/appdata"
fail=0
pids=""

# fixture for the identity-scoped storage proof
fx="$tmp/fixture"; mkdir -p "$fx"
head -c 131072 /dev/urandom > "$fx/small.bin"
exp_sha=$(sha256sum "$fx/small.bin" | cut -d' ' -f1)
python3 -m http.server 8797 --bind 127.0.0.1 --directory "$fx" >/tmp/d8_fix.log 2>&1 &
fix=$!
i=0; while ! curl -s --max-time 1 -o /dev/null http://127.0.0.1:8797/small.bin; do
    i=$((i+1)); [ "$i" -gt 50 ] && { echo "fixture failed"; exit 1; }; sleep 0.1; done

start() { # port [extra...]
    port="$1"; shift
    "$bin" --no-browser --port "$port" "$@" >"$tmp/h$port.out" 2>"$tmp/h$port.err" &
    pids="$pids $!"
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -s --max-time 1 -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null; then break; fi
        i=$((i+1)); sleep 0.1
    done
}
stopall() { for p in $pids; do kill "$p" 2>/dev/null || true; done; pids=""; }
cleanup() { stopall; kill "$fix" 2>/dev/null || true; [ -n "$KEEP_TMP" ] || rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }
perms() { curl -s "http://127.0.0.1:$1/api/permissions"; }
has() { echo "$1" | grep -q "^$2:.*$3" && return 0 || return 1; }
run_expect_fail() { # desc args...
    desc="$1"; shift
    set +e
    "$bin" --no-browser --port 8899 "$@" >"$tmp/fail.out" 2>"$tmp/fail.err"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then ok "$desc (exit $rc)"; else bad "$desc (expected failure)"; fi
}

# ---- A: Fetch receives its own grants -------------------------------------
echo "D8: Fetch receives its own grants"
start 8841 --install org.glon.fetch
p=$(perms 8841)
has "$p" app-id "org.glon.fetch" && ok "app-id is org.glon.fetch" || bad "wrong app-id"
has "$p" requested "net/connect" && has "$p" requested "file/app-write" && has "$p" requested "open/folder" && ok "requests 3" || bad "requested wrong"
has "$p" granted "net/connect" && has "$p" granted "file/app-write" && has "$p" granted "open/folder" && ok "granted 3" || bad "granted wrong"
has "$p" effective "net/connect" && has "$p" effective "file/app-write" && has "$p" effective "open/folder" && ok "effective 3" || bad "effective wrong"
has "$p" effective "process/spawn" && bad "process/spawn effective" || ok "no process/spawn effective"
stopall

# ---- B: second app, same requests, no grants -> no authority --------------
echo "D8: same requests, different id, no authority"
start 8842 --install org.glon.authority-test
p=$(perms 8842)
has "$p" app-id "org.glon.authority-test" && ok "app-id is org.glon.authority-test" || bad "wrong app-id"
has "$p" requested "net/connect" && has "$p" requested "file/app-write" && has "$p" requested "open/folder" && ok "requests the same 3" || bad "requested wrong"
echo "$p" | grep -q "^granted:[[:space:]]*$" && ok "granted none" || bad "granted not empty"
echo "$p" | grep -q "^effective:[[:space:]]*$" && ok "effective none" || bad "effective not empty"
got=$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' -X POST --data-binary "$(printf 'http://127.0.0.1:8797/small.bin\nn.bin')" "http://127.0.0.1:8842/api/fetch")
[ "$got" = 403 ] && ok "authority-test cannot fetch (403)" || bad "authority-test fetch returned $got"
stopall

# ---- C: grant for one id cannot be used by another ------------------------
echo "D8: cross-app grant isolation"
start 8843 --install org.glon.authority-test --grants desktop/grants.conf
p=$(perms 8843)
echo "$p" | grep -q "^granted:[[:space:]]*$" && ok "authority-test granted none despite fetch grants" || bad "inherited grants"
stopall

# ---- D: name has no authority effect --------------------------------------
echo "D8: name is display metadata only"
printf 'id: org.glon.fetch\nname: Authority Test\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\npermission: open/folder\n' > "$tmp/m_fetch_named_at"
start 8844 --install org.glon.fetch --manifest "$tmp/m_fetch_named_at"
has "$(perms 8844)" effective "net/connect" && ok "fetch named 'Authority Test' still effective" || bad "name changed authority"
stopall
printf 'id: org.glon.authority-test\nname: Glon Fetch\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\npermission: open/folder\n' > "$tmp/m_at_named_fetch"
start 8845 --install org.glon.authority-test --manifest "$tmp/m_at_named_fetch"
echo "$(perms 8845)" | grep -q "^granted:[[:space:]]*$" && ok "authority-test named 'Glon Fetch' still granted none" || bad "name changed authority"
stopall

# ---- E: version has no authority effect -----------------------------------
echo "D8: version is not authority"
printf 'id: org.glon.fetch\nname: Glon Fetch\nversion: 9.9.9\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\npermission: open/folder\n' > "$tmp/m_fetch_v9"
start 8846 --install org.glon.fetch --manifest "$tmp/m_fetch_v9"
has "$(perms 8846)" effective "file/app-write" && ok "version 9.9.9 keeps Fetch grants" || bad "version changed authority"
stopall

# ---- F: changing the manifest id is rejected (manifest cannot authenticate)
echo "D8: manifest id must match installed identity"
printf 'id: org.glon.fetch.evil\nname: X\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\npermission: net/connect\n' > "$tmp/m_evil"
run_expect_fail "manifest id org.glon.fetch.evil under install org.glon.fetch is rejected" --install org.glon.fetch --manifest "$tmp/m_evil"

# ---- G: prefix/similar id does not inherit --------------------------------
echo "D8: prefix/similar id does not match"
printf 'install org.glon.fetch.evil apps/glon-fetch\n' > "$tmp/install_evil"
printf 'id: org.glon.fetch.evil\nname: Evil\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\npermission: open/folder\n' > "$tmp/m_prefix"
start 8847 --install org.glon.fetch.evil --install-conf "$tmp/install_evil" --manifest "$tmp/m_prefix"
p=$(perms 8847)
has "$p" app-id "org.glon.fetch.evil" && ok "app-id is org.glon.fetch.evil" || bad "wrong app-id"
echo "$p" | grep -q "^granted:[[:space:]]*$" && ok "prefix id inherits no Fetch grant" || bad "prefix inherited a grant"
stopall

# ---- H: unknown id gets nothing (not installed) ---------------------------
echo "D8: unknown id is not installed"
run_expect_fail "unknown id is rejected" --install org.glon.unknown

# ---- I/J: manifest cannot manufacture permissions -------------------------
echo "D8: manifest cannot manufacture authority"
printf 'id: org.glon.fetch\nname: Glon Fetch\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\npermission: open/folder\npermission: process/spawn\npermission: bluetooth\npermission: root/all\n' > "$tmp/m_extras"
start 8848 --install org.glon.fetch --manifest "$tmp/m_extras"
p=$(perms 8848)
has "$p" effective "process/spawn" && bad "process/spawn manufactured" || ok "process/spawn not effective"
has "$p" effective "bluetooth" && bad "bluetooth manufactured" || ok "unimplemented bluetooth not effective"
has "$p" effective "root/all" && bad "root/all manufactured" || ok "unimplemented root/all not effective"
stopall

# ---- K: identity-scoped storage -------------------------------------------
echo "D8: app data is identity-scoped"
printf 'install org.glon.authority-test apps/glon-fetch\n' > "$tmp/install_at_fetchcode"
printf 'id: org.glon.authority-test\nname: AT\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\nmodule: fetch\npermission: net/connect\npermission: file/app-write\n' > "$tmp/m_at_fetchcode"
printf 'app org.glon.authority-test\n  grant net/connect\n  grant file/app-write\n' > "$tmp/grants_at"
start 8849 --install org.glon.authority-test --install-conf "$tmp/install_at_fetchcode" --manifest "$tmp/m_at_fetchcode" --grants "$tmp/grants_at" --data "$data"
curl -s --max-time 30 --data-binary "$(printf 'http://127.0.0.1:8797/small.bin\niso.bin')" "http://127.0.0.1:8849/api/fetch" >/dev/null || true
at_file="$data/org.glon.authority-test/downloads/iso.bin"
fetch_file="$data/org.glon.fetch/downloads/iso.bin"
if [ -f "$at_file" ]; then ok "write landed under its own identity dir"; else bad "identity-scoped file missing"; fi
if [ -f "$fetch_file" ]; then bad "authority-test wrote into Fetch's appdata"; else ok "did not write into Fetch's appdata"; fi
[ "$(sha256sum "$at_file" 2>/dev/null | cut -d' ' -f1)" = "$exp_sha" ] && ok "identity-scoped download SHA matches" || bad "identity-scoped SHA mismatch"
stopall

# ---- L: malformed / traversal ids rejected --------------------------------
echo "D8: malformed ids rejected"
for badid in "org/glon" "../evil" "Org.glon" "org..glon" "org." ".org" "org glon" "org.glon." ; do
    printf 'id: %s\nname: Bad\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\n' "$badid" > "$tmp/m_bad"
    run_expect_fail "malformed id '$badid' rejected" --install org.glon.fetch --manifest "$tmp/m_bad"
done
printf 'name: NoId\nversion: 0.0\nentry: app.glon\nview: view.glon\nmodule: strings\n' > "$tmp/m_noid"
run_expect_fail "missing id rejected" --install org.glon.fetch --manifest "$tmp/m_noid"

# ---- M: listener localhost only -------------------------------------------
echo "D8: listener is localhost only"
start 8850 --install org.glon.fetch
if command -v ss >/dev/null 2>&1; then
    ss -ltn 2>/dev/null | grep -q "127.0.0.1:8850" && ok "bound to 127.0.0.1 only" || bad "not bound to 127.0.0.1"
fi
stopall

if [ "$fail" = 0 ]; then echo "D8 AUTHORITY TEST PASS"; else echo "D8 AUTHORITY TEST FAIL"; fi
exit "$fail"
