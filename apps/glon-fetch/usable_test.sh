#!/bin/sh
# apps/glon-fetch/usable_test.sh -- D10 usable-app proof (POSIX).
# Success, real cancellation, recovery, unsafe destinations, authority.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bin="$root/glon-desktop"
cd "$root"
tmp="$(mktemp -d)"
fail=0
pids=""

fx="$tmp/fx"; mkdir -p "$fx"
head -c 1048576 /dev/urandom > "$fx/ok.bin"
exp_sha=$(sha256sum "$fx/ok.bin" | cut -d' ' -f1)

python3 -m http.server 8791 --bind 127.0.0.1 --directory "$fx" >/tmp/d10_fx.log 2>&1 & fx_pid=$!
python3 "$here/slow_server.py" 8798 >/tmp/d10_slow.log 2>&1 & slow_pid=$!
i=0; while ! curl -s --max-time 1 -o /dev/null http://127.0.0.1:8791/ok.bin; do i=$((i+1)); [ "$i" -gt 50 ] && { echo "fixture failed"; exit 1; }; sleep 0.1; done

start_host() { port="$1"; shift
    "$bin" --no-browser --port "$port" "$@" >"$tmp/h$port.out" 2>"$tmp/h$port.err" &
    pids="$pids $!"
    i=0; while [ "$i" -lt 50 ]; do
        curl -s --max-time 1 -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null && break
        i=$((i+1)); sleep 0.1
    done
}
stopall() { for p in $pids; do kill "$p" 2>/dev/null || true; done; pids=""; }
cleanup() { stopall; kill "$fx_pid" "$slow_pid" 2>/dev/null || true; [ -n "$KEEP_TMP" ] || rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }

data="$tmp/appdata"
start_host 8853 --install org.glon.fetch --data "$data"

# 1+2+3: successful download, bytes, SHA-256
echo "D10: successful download"
out=$(curl -s --max-time 60 --data-binary "$(printf 'http://127.0.0.1:8791/ok.bin\nok.bin')" "http://127.0.0.1:8853/api/fetch")
echo "$out" | grep -q "^done$" && ok "download reported done" || bad "no done line"
echo "$out" | grep -qE "^sha256 [0-9a-f]{64}$" && ok "SHA-256 returned" || bad "no SHA-256 line"
dl="$data/org.glon.fetch/downloads/ok.bin"
[ "$(sha256sum "$dl" 2>/dev/null | cut -d' ' -f1)" = "$exp_sha" ] && ok "downloaded bytes match" || bad "bytes mismatch"

# 4: unsafe destinations rejected
echo "D10: unsafe destinations"
code=$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' --data-binary "$(printf 'http://127.0.0.1:8791/ok.bin\n../../etc/passwd')" "http://127.0.0.1:8853/api/fetch")
[ "$code" = 400 ] && ok "traversal destination rejected (400)" || bad "traversal returned $code"
code=$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' --data-binary "$(printf 'http://127.0.0.1:8791/ok.bin\na/b')" "http://127.0.0.1:8853/api/fetch")
[ "$code" = 400 ] && ok "separator destination rejected (400)" || bad "separator returned $code"

# 5: real cancellation
echo "D10: real cancellation"
rm -f "$data/org.glon.fetch/downloads/cancel.bin"
curl -s --max-time 1 --data-binary "$(printf 'http://127.0.0.1:8798/slow\ncancel.bin')" "http://127.0.0.1:8853/api/fetch" >/dev/null 2>&1 || true
sleep 3
[ ! -f "$data/org.glon.fetch/downloads/cancel.bin" ] && ok "partial file removed after cancel" || bad "partial file remains"
grep -q "download cancelled" "$tmp/h8853.err" && ok "host terminated the download" || bad "no cancel in host log"

# 6: a second download can run after cancellation
echo "D10: recovery after cancel"
out2=$(curl -s --max-time 60 --data-binary "$(printf 'http://127.0.0.1:8791/ok.bin\nafter.bin')" "http://127.0.0.1:8853/api/fetch")
echo "$out2" | grep -q "^done$" && ok "second download completed" || bad "second download failed"
[ "$(sha256sum "$data/org.glon.fetch/downloads/after.bin" 2>/dev/null | cut -d' ' -f1)" = "$exp_sha" ] && ok "second download bytes match" || bad "second bytes mismatch"

# 7: process/spawn absent
echo "D10: authority"
perms=$(curl -s "http://127.0.0.1:8853/api/permissions")
echo "$perms" | grep -q "^effective:.*process/spawn" && bad "process/spawn effective" || ok "process/spawn remains absent"
stopall

# 8: open/folder is required to open the folder
cat > "$tmp/m_no_folder" <<'EOF'
id: org.glon.fetch
name: Glon Fetch
version: 0.0
entry: app.glon
view: view.glon
module: strings
module: fetch
permission: net/connect
permission: file/app-write
EOF
start_host 8854 --install org.glon.fetch --manifest "$tmp/m_no_folder" --data "$data"
curl -s --max-time 60 --data-binary "$(printf 'http://127.0.0.1:8791/ok.bin\nnf.bin')" "http://127.0.0.1:8854/api/fetch" >/dev/null
code=$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:8854/api/open")
[ "$code" = 403 ] && ok "Open Folder denied without open/folder (403)" || bad "open returned $code"
stopall

# 9: listener localhost only
if command -v ss >/dev/null 2>&1; then
    start_host 8855 --install org.glon.fetch --data "$data"
    ss -ltn 2>/dev/null | grep -q "127.0.0.1:8855" && ok "listener bound to 127.0.0.1 only" || bad "not localhost only"
    stopall
fi

if [ "$fail" = 0 ]; then echo "D10 USABLE TEST PASS"; else echo "D10 USABLE TEST FAIL"; fi
exit "$fail"
