#!/bin/sh
# desktop/security_test.sh -- HTTP-level authority regression for the desktop
# proof. Proves the C host never reads a browser-supplied path: only logical
# names that native Glon explicitly authorises resolve to a file.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
port="${1:-8899}"
bin="$root/glon-desktop"

"$bin" --no-browser --port "$port" >/tmp/glon-sec.out 2>/tmp/glon-sec.err &
srv=$!
trap 'kill "$srv" 2>/dev/null || true' EXIT INT TERM

i=0
while [ "$i" -lt 50 ]; do
    if curl -s --max-time 2 -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null; then break; fi
    i=$((i + 1)); sleep 0.1
done

fail=0
status() {
    desc="$1"; want="$2"; url="$3"
    got="$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' "$url")"
    if [ "$got" = "$want" ]; then echo "  ok: $desc ($got)"
    else echo "  FAIL: $desc (want $want, got $got)"; fail=1; fi
}
body_is() {
    desc="$1"; url="$2"; file="$3"
    curl -s --max-time 20 -o /tmp/glon-sec.body "$url"
    if cmp -s /tmp/glon-sec.body "$file"; then echo "  ok: $desc"
    else echo "  FAIL: $desc ($(wc -c </tmp/glon-sec.body) vs $(wc -c <"$file") bytes)"; fail=1; fi
}

echo "desktop authority regression (port $port)"
status "/native/read?path=greeting -> 200"            200 "http://127.0.0.1:$port/native/read?path=greeting"
body_is "greeting returns exactly desktop/demo.txt"   "http://127.0.0.1:$port/native/read?path=greeting" "$here/demo.txt"
status "/native/read?path=desktop/big.txt -> 404"     404 "http://127.0.0.1:$port/native/read?path=desktop/big.txt"
status "/native/read?path=../../etc/passwd -> 404"    404 "http://127.0.0.1:$port/native/read?path=../../etc/passwd"
status "/resource?name=large -> 200"                  200 "http://127.0.0.1:$port/resource?name=large"
body_is "large returns exactly desktop/big.txt"       "http://127.0.0.1:$port/resource?name=large" "$here/big.txt"
status "/resource?name=nope -> 404"                   404 "http://127.0.0.1:$port/resource?name=nope"

if [ "$fail" = 0 ]; then echo "SECURITY TEST PASS"; else echo "SECURITY TEST FAIL"; fi
exit "$fail"
