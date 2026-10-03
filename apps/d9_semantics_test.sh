#!/bin/sh
# apps/d9_semantics_test.sh -- D9 semantic permission contracts proof.
#
# Invariant: THE APP MAY EXPLAIN WHY IT WANTS AUTHORITY.
#            IT MAY NOT DEFINE WHAT THAT AUTHORITY MEANS.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
bin="$root/glon-desktop"
cd "$root"
tmp="$(mktemp -d)"
fail=0
pids=""

start() { port="$1"; shift
    "$bin" --no-browser --port "$port" "$@" >"$tmp/h$port.out" 2>"$tmp/h$port.err" &
    pids="$pids $!"
    i=0; while [ "$i" -lt 50 ]; do
        curl -s --max-time 1 -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null && break
        i=$((i+1)); sleep 0.1
    done
}
stopall() { for p in $pids; do kill "$p" 2>/dev/null || true; done; pids=""; }
cleanup() { stopall; [ -n "$KEEP_TMP" ] || rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }

perms() { curl -s "http://127.0.0.1:$1/api/permissions"; }

# ---- A: catalogue metadata + Fetch purpose --------------------------------
echo "D9: trusted catalogue + Fetch purpose"
start 8871 --install org.glon.fetch
p=$(perms 8871)
echo "$p" | grep -q "  label: Connect to network services" && ok "net/connect label is trusted" || bad "net/connect label"
echo "$p" | grep -q "  class: general" && ok "net/connect class general" || bad "net/connect class"
echo "$p" | grep -q "  risk: medium" && ok "net/connect risk medium" || bad "net/connect risk"
echo "$p" | grep -q "  allows: outbound-network" && ok "net/connect allows outbound-network" || bad "net/connect allows"
echo "$p" | grep -q "  purpose: Download URLs selected by the user" && ok "Fetch explains net/connect purpose" || bad "net/connect purpose"
echo "$p" | grep -q "  class: scoped" && ok "file/app-write class scoped" || bad "file/app-write class"
echo "$p" | grep -q "  class: brokered-service" && ok "open/folder class brokered-service" || bad "open/folder class"
echo "$p" | grep -q "  effective: yes" && ok "permissions are effective" || bad "not effective"
stopall

# ---- B: adversarial app cannot redefine canonical semantics ---------------
echo "D9: app cannot redefine canonical semantics"
# manifest tries to claim process/spawn is harmless; the host must ignore those fields
cat > "$tmp/m_redef" <<'EOF'
id: org.glon.authority-test
name: AT
version: 0.0
entry: app.glon
view: view.glon
module: strings
module: fetch
permission: process/spawn
permission: teleport/moon
purpose: process/spawn Display the current time
class: brokered-service
risk: low
label: Harmless clock feature
allows: harmless-clock
EOF
start 8872 --install org.glon.authority-test --manifest "$tmp/m_redef"
p=$(perms 8872)
echo "$p" | grep -q "  id: process/spawn" && ok "process/spawn requested" || bad "process/spawn missing"
echo "$p" | grep -q "  label: Run arbitrary child programs" && ok "canonical label preserved" || bad "label redefined"
echo "$p" | grep -q "  class: privileged" && ok "canonical class preserved" || bad "class redefined"
echo "$p" | grep -q "  risk: high" && ok "canonical risk preserved" || bad "risk redefined"
echo "$p" | grep -q "  allows: arbitrary-child-process" && ok "canonical allows preserved" || bad "allows redefined"
echo "$p" | grep -q "  purpose: Display the current time" && ok "purpose shown as an untrusted claim" || bad "purpose missing"
echo "$p" | grep -q "  effective: no" && ok "process/spawn is not effective" || bad "process/spawn effective"
echo "$p" | grep -q "  label: (unknown capability)" && ok "unknown capability marked unknown" || bad "unknown not marked"
grep -q "unknown capability: teleport/moon" "$tmp/h8872.err" && ok "startup diagnostic names the unknown capability" || bad "no diagnostic"
# misleading purpose does not alter enforcement
code=$(curl -s --max-time 20 -o /dev/null -w '%{http_code}' -X POST --data-binary "$(printf 'http://127.0.0.1:1/x\nz.bin')" "http://127.0.0.1:8872/api/fetch")
[ "$code" = 403 ] && ok "misleading purpose does not grant fetch (403)" || bad "fetch returned $code"
stopall

# ---- C: unknown capability fails closed even with a crafted grant ---------
echo "D9: unknown capability fails closed even if granted"
cat > "$tmp/grants_unknown" <<'EOF'
app org.glon.authority-test
  grant teleport/moon
EOF
cat > "$tmp/m_unknown" <<'EOF'
id: org.glon.authority-test
name: AT
version: 0.0
entry: app.glon
view: view.glon
module: strings
module: fetch
permission: teleport/moon
EOF
start 8873 --install org.glon.authority-test --manifest "$tmp/m_unknown" --grants "$tmp/grants_unknown"
p=$(perms 8873)
echo "$p" | grep -q "  id: teleport/moon" && ok "unknown capability requested" || bad "unknown missing"
echo "$p" | grep -q "  known: no" && ok "unknown capability marked not known" || bad "unknown marked known"
echo "$p" | grep -q "^effective:[[:space:]]*$" && ok "unknown capability is not effective despite a grant" || bad "unknown became effective"
stopall

# ---- C2: catalogue-only capability is not implemented ---------------------
echo "D9: catalogue entry without host implementation is not effective"
cat > "$tmp/grants_bt" <<'EOF'
app org.glon.authority-test
  grant bluetooth/scan
EOF
cat > "$tmp/m_bt" <<'EOF'
id: org.glon.authority-test
name: AT
version: 0.0
entry: app.glon
view: view.glon
module: strings
permission: bluetooth/scan
EOF
start 8877 --install org.glon.authority-test --manifest "$tmp/m_bt" --grants "$tmp/grants_bt"
p=$(perms 8877)
echo "$p" | grep -q "  label: Scan for Bluetooth devices" && ok "catalogue semantics are exposed" || bad "catalogue label missing"
echo "$p" | grep -q "  class: general" && ok "catalogue class exposed" || bad "catalogue class missing"
echo "$p" | grep -q "  known: yes" && ok "capability is catalogue-known" || bad "not catalogue-known"
echo "$p" | grep -q "  implemented: no" && ok "host does not implement it" || bad "marked implemented"
echo "$p" | grep -q "  effective: no" && ok "catalogue-only capability is not effective" || bad "catalogue-only became effective"
echo "$p" | grep -q "^effective:[[:space:]]*$" && ok "effective set is empty" || bad "effective not empty"
grep -q "catalogue capability not implemented by host: bluetooth/scan" "$tmp/h8877.err" \
    && ok "diagnostic names the unimplemented capability" || bad "no unimplemented diagnostic"
stopall

# ---- C3: host implementation without catalogue metadata fails closed ------
echo "D9: host implementation without catalogue metadata fails closed"
awk 'BEGIN{skip=0} /^capability /{skip=($2=="net/connect")} !skip' \
    desktop/capabilities.conf > "$tmp/cat_no_net"
start 8878 --install org.glon.fetch --capabilities "$tmp/cat_no_net"
p=$(perms 8878)
eff=$(echo "$p" | awk -F': ' '/^effective:/{print $2}')
echo "$eff" | grep -q "net/connect" && bad "net/connect effective without catalogue metadata" \
    || ok "net/connect ineffective without catalogue metadata"
echo "$eff" | grep -q "file/app-write" && ok "other permissions remain effective" || bad "file/app-write lost"
grep -q "host implementation without trusted catalogue metadata: net/connect" "$tmp/h8878.err" \
    && ok "diagnostic names the metadata-less implementation" || bad "no metadata-less diagnostic"
stopall

# ---- D: Fetch keeps exactly its three effective permissions ---------------
echo "D9: Fetch effective permissions unchanged"
start 8874 --install org.glon.fetch
p=$(perms 8874)
eff=$(echo "$p" | awk -F': ' '/^effective:/{print $2}')
[ "$eff" = "net/connect file/app-write open/folder" ] && ok "effective = the three intended permissions" || bad "effective = $eff"
echo "$p" | grep -q "process/spawn" && { echo "$p" | awk '/^effective:/{print}' | grep -q process/spawn && bad "process/spawn effective" || ok "no process/spawn in effective"; } || ok "no process/spawn requested/effective"
stopall

# ---- E: per-app grant isolation (D8) remains intact -----------------------
echo "D9: per-app isolation remains intact"
start 8875 --install org.glon.authority-test
p=$(perms 8875)
echo "$p" | grep -q "^granted:[[:space:]]*$" && ok "authority-test granted none" || bad "authority-test has grants"
stopall

# ---- F: listener localhost only -------------------------------------------
echo "D9: listener localhost only"
start 8876 --install org.glon.fetch
if command -v ss >/dev/null 2>&1; then
    ss -ltn 2>/dev/null | grep -q "127.0.0.1:8876" && ok "bound to 127.0.0.1 only" || bad "not localhost only"
fi
stopall

if [ "$fail" = 0 ]; then echo "D9 SEMANTICS TEST PASS"; else echo "D9 SEMANTICS TEST FAIL"; fi
exit "$fail"
