#!/bin/sh
# apps/glon-fetch/download_test.sh -- POSIX real-download proof.
# Starts a local HTTP fixture and the Glon Fetch host, asks it to fetch a large
# file, and verifies the downloaded bytes without the file entering Glon.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bin="$root/glon-desktop"
app="$here"
cd "$root"
fx="desktop/appdata/glon-fetch/fixture"
mkdir -p "$fx"
if [ ! -f "$fx/big.bin" ]; then
    head -c 8388608 /dev/urandom > "$fx/big.bin"
fi
python3 -m http.server 8795 --bind 127.0.0.1 --directory "$fx" >/tmp/glonfetch_fix.log 2>&1 &
fix=$!
"$bin" --install org.glon.fetch --no-browser --port 8828 >/tmp/glonfetch_host.out 2>/tmp/glonfetch_host.err &
h=$!
trap 'kill "$fix" "$h" 2>/dev/null || true' EXIT INT TERM

i=0
while ! curl -s --max-time 1 -o /dev/null "http://127.0.0.1:8828/" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -gt 50 ]; then echo "host failed to start"; cat /tmp/glonfetch_host.err; exit 1; fi
    sleep 0.1
done

perms=$(curl -s "http://127.0.0.1:8828/api/permissions")
if echo "$perms" | grep -q "effective:.*process/spawn"; then
    echo "  FAIL: effective permissions include process/spawn"; exit 1
else
    echo "  ok: effective permissions EXCLUDE process/spawn (brokered service)"
fi

rm -f desktop/appdata/org.glon.fetch/downloads/posix.bin
out=$(curl -s --max-time 120 --data-binary "$(printf 'http://127.0.0.1:8795/big.bin\nposix.bin')" "http://127.0.0.1:8828/api/fetch")
echo "$out" | tail -1
fail=0
echo "$out" | grep -q "DONE exit=0" && echo "  ok: streamed status reached DONE exit=0" || { echo "  FAIL: no DONE exit=0"; fail=1; }
dl="desktop/appdata/org.glon.fetch/downloads/posix.bin"
exp=$(sha256sum "$fx/big.bin" | cut -d' ' -f1)
act=$(sha256sum "$dl" 2>/dev/null | cut -d' ' -f1)
if [ "$exp" = "$act" ]; then
    echo "  ok: downloaded SHA-256 matches ($act), $(wc -c < "$dl") bytes"
else
    echo "  FAIL: SHA-256 mismatch exp=$exp act=$act"; fail=1
fi
if [ "$fail" = 0 ]; then echo "POSIX FETCH TEST PASS"; else echo "POSIX FETCH TEST FAIL"; fi
exit "$fail"
