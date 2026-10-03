#!/bin/sh
# apps/glon-fetch/security_test.sh -- app-model authority regression (POSIX).
#
# Proves the BROKERED model: Glon Fetch has NO process/spawn authority, yet the
# trusted host fetch service still downloads and hashes. Manifest requests are
# intersected with trusted grants; editing the manifest cannot escalate.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
app="$here"
bin="$root/glon-desktop"
cd "$root"
tmp="$(mktemp -d)"
fail=0
pids=""

fx="desktop/appdata/glon-fetch/fixture"
mkdir -p "$fx"
[ -f "$fx/small.bin" ] || head -c 262144 /dev/urandom > "$fx/small.bin"
exp_sha=$(sha256sum "$fx/small.bin" | cut -d' ' -f1)
python3 -m http.server 8796 --bind 127.0.0.1 --directory "$fx" >/tmp/glonapp_fix.log 2>&1 &
fix=$!
FIXURL="http://127.0.0.1:8796/small.bin"
i=0
while ! curl -s --max-time 1 -o /dev/null "$FIXURL" 2>/dev/null; do
    i=$((i + 1)); [ "$i" -gt 50 ] && { echo "fixture failed to start"; exit 1; }
    sleep 0.1
done

start() {
    name="$1"; port="$2"; shift 2
    "$bin" --install org.glon.fetch --no-browser --port "$port" "$@" >"$tmp/$name.out" 2>"$tmp/$name.err" &
    pids="$pids $!"
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -s --max-time 1 -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null; then break; fi
        i=$((i + 1)); sleep 0.1
    done
}
stopall() { for p in $pids; do kill "$p" 2>/dev/null || true; done; pids=""; }
cleanup() { stopall; kill "$fix" 2>/dev/null || true; [ -n "$KEEP_TMP" ] || rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }
status() {
    desc="$1"; want="$2"; url="$3"; method="${4:-GET}"; body="$5"
    if [ -n "$body" ]; then
        got=$(curl -s --max-time 30 -o /dev/null -w '%{http_code}' -X "$method" --data-binary "$body" "$url")
    else
        got=$(curl -s --max-time 30 -o /dev/null -w '%{http_code}' -X "$method" "$url")
    fi
    if [ "$got" = "$want" ]; then ok "$desc ($got)"; else bad "$desc (want $want got $got)"; fi
}
fetch_to() { # port name outfile
    curl -s --max-time 60 --data-binary "$(printf '%s\n%s' "$FIXURL" "$2")" "http://127.0.0.1:$1/api/fetch" > "$3" 2>/dev/null || true
}
has_perm() { echo "$1" | grep -q "effective:.*$2" && return 0 || return 1; }

# ---- manifest variants ----------------------------------------------------
mk_manifest() { # file perms...
    f="$1"; shift
    { echo "id: org.glon.fetch"; echo "name: Variant"; echo "version: 0.0"; echo "entry: app.glon"; echo "view: view.glon";
      echo "module: strings"; echo "module: fetch";
      for p in "$@"; do echo "permission: $p"; done; } > "$f"
}
mk_manifest "$tmp/m_no_net"    file/app-write open/folder
mk_manifest "$tmp/m_no_write"  net/connect open/folder
mk_manifest "$tmp/m_no_folder" net/connect file/app-write
mk_manifest "$tmp/m_extra"     net/connect file/app-write open/folder process/spawn bluetooth root/all
mk_manifest "$tmp/m_missing"   net/connect
{ echo "id: org.glon.fetch"; echo "name: Missing"; echo "version: 0.0"; echo "entry: app.glon"; echo "view: view.glon";
  echo "module: strings"; echo "module: pdf"; echo "permission: net/connect"; } > "$tmp/m_missing_module"

# ---- A: default -- no process/spawn, but download + SHA still work --------
echo "app-model: brokered default (no process/spawn)"
start default 8811
perms=$(curl -s "http://127.0.0.1:8811/api/permissions")
has_perm "$perms" "net/connect"    && ok "effective includes net/connect"     || bad "effective lacks net/connect"
has_perm "$perms" "file/app-write" && ok "effective includes file/app-write" || bad "effective lacks file/app-write"
has_perm "$perms" "open/folder"    && ok "effective includes open/folder"    || bad "effective lacks open/folder"
has_perm "$perms" "process/spawn"  && bad "effective includes process/spawn (should not)" || ok "effective excludes process/spawn"
rm -f desktop/appdata/org.glon.fetch/downloads/a.bin
fetch_to 8811 a.bin "$tmp/a.out"
grep -q "DONE exit=0" "$tmp/a.out" && ok "download succeeded without process/spawn" || bad "download failed"
act=$(sha256sum desktop/appdata/org.glon.fetch/downloads/a.bin 2>/dev/null | cut -d' ' -f1)
[ "$act" = "$exp_sha" ] && ok "SHA-256 succeeded via host service" || bad "SHA-256/file mismatch ($act)"
status "traversal destination -> 400" 400 "http://127.0.0.1:8811/api/fetch" POST "$(printf 'http://127.0.0.1:1/x\n../../etc/passwd')"
status "absolute destination -> 400" 400 "http://127.0.0.1:8811/api/fetch" POST "$(printf 'http://127.0.0.1:1/x\n/etc/passwd')"
status "separator destination -> 400" 400 "http://127.0.0.1:8811/api/fetch" POST "$(printf 'http://127.0.0.1:1/x\na/b')"
status "browser cannot name an executable -> 400" 400 "http://127.0.0.1:8811/api/fetch" POST "$(printf 'http://127.0.0.1:1/x\nn.bin\nexe=/bin/sh')"
if command -v ss >/dev/null 2>&1; then
    if ss -ltn 2>/dev/null | grep -q "127.0.0.1:8811"; then ok "listener bound to 127.0.0.1 only"; else bad "listener not on 127.0.0.1"; fi
fi
stopall

# ---- B/C: missing net/connect or file/app-write prevents fetch ------------
echo "app-model: missing fetch authority"
start nonet 8812 --manifest "$tmp/m_no_net"
status "fetch denied without net/connect" 403 "http://127.0.0.1:8812/api/fetch" POST "$(printf '%s\nb.bin' "$FIXURL")"
stopall
start nowrite 8813 --manifest "$tmp/m_no_write"
status "fetch denied without file/app-write" 403 "http://127.0.0.1:8813/api/fetch" POST "$(printf '%s\nb.bin' "$FIXURL")"
stopall

# ---- D: missing open/folder blocks open but not download ------------------
echo "app-model: open/folder is separate from download"
start nofolder 8814 --manifest "$tmp/m_no_folder"
perms=$(curl -s "http://127.0.0.1:8814/api/permissions")
has_perm "$perms" "open/folder" && bad "open/folder became effective" || ok "open/folder is not effective"
rm -f desktop/appdata/org.glon.fetch/downloads/d.bin
fetch_to 8814 d.bin "$tmp/d.out"
grep -q "DONE exit=0" "$tmp/d.out" && ok "download still succeeds without open/folder" || bad "download blocked by missing open/folder"
status "open denied without open/folder" 403 "http://127.0.0.1:8814/api/open" POST "x"
stopall

# ---- E: manifest cannot escalate ------------------------------------------
echo "app-model: manifest cannot escalate"
start extra 8815 --manifest "$tmp/m_extra"
perms=$(curl -s "http://127.0.0.1:8815/api/permissions")
has_perm "$perms" "process/spawn" && bad "requested process/spawn became effective" || ok "requested process/spawn is not effective"
has_perm "$perms" "bluetooth"     && bad "unimplemented bluetooth became effective" || ok "unimplemented bluetooth is not effective"
has_perm "$perms" "root/all"      && bad "unimplemented root/all became effective" || ok "unimplemented root/all is not effective"
stopall

# ---- F: no shell interpretation -------------------------------------------
echo "app-model: no shell interpretation"
rm -f /tmp/glon_pwned
start shell 8816
curl -s --max-time 30 -o /dev/null -X POST --data-binary "$(printf 'http://127.0.0.1:1/x; touch /tmp/glon_pwned\npwn.bin')" "http://127.0.0.1:8816/api/fetch" || true
sleep 0.3
[ -e /tmp/glon_pwned ] && bad "shell metacharacters executed" || ok "URL metacharacters were not shell-interpreted"
stopall

# ---- G: missing required module -------------------------------------------
echo "app-model: missing required module"
set +e
"$bin" --install org.glon.fetch --no-browser --port 8817 --manifest "$tmp/m_missing_module" >"$tmp/missing.out" 2>"$tmp/missing.err"
rc=$?
set -e
[ "$rc" -ne 0 ] && ok "missing required module exits nonzero ($rc)" || bad "missing module did not fail"
grep -q "not found" "$tmp/missing.err" && ok "missing module reports a clear error" || bad "missing module error unclear"

if [ "$fail" = 0 ]; then echo "APP SECURITY TEST PASS"; else echo "APP SECURITY TEST FAIL"; fi
exit "$fail"
