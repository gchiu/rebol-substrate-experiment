#!/bin/sh
# Verify the local demonstration. Native comparison requires a built
# experiments/glon-deflate/build/dzhost; the browser-runtime test requires Node
# 22+ (global fetch/WebSocket). The real-browser test is separate because it
# needs a browser with a remote-debugging port; see README.md.
set -eu
cd "$(dirname "$0")"
NODE=${NODE:-node}
DZHOST=${DZHOST:-../glon-deflate/build/dzhost}

if [ -x "$DZHOST" ]; then
  echo "Native Glon comparison:"
  python3 - "$DZHOST" <<'PY'
import base64, json, os, subprocess, sys, tempfile
host = sys.argv[1]
for s in json.load(open('samples.json'))['samples']:
    blob = base64.b64decode(s['data'])
    if s['format'] == 'zlib': raw = blob[2:-4]
    elif s['format'] == 'gzip':
        flags, p = blob[3], 10
        if flags & 4: p += 2 + (blob[p] | (blob[p + 1] << 8))
        if flags & 8:
            while blob[p]: p += 1
            p += 1
        if flags & 16:
            while blob[p]: p += 1
            p += 1
        if flags & 2: p += 2
        raw = blob[p:-8]
    else: raw = blob
    fd, path = tempfile.mkstemp(suffix='.deflate'); os.write(fd, raw); os.close(fd)
    out = path + '.out'
    subprocess.run([host, '-L', '../..', '-c', ','.join(map(str, s['chunks'])), '-r', '256',
                    '-o', out, path], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    got = open(out, 'rb').read(); want = base64.b64decode(s['expected_base64'])
    os.unlink(path); os.unlink(out)
    if got != want: raise SystemExit('native mismatch: ' + s['id'])
    print('ok  native %s: %d bytes' % (s['id'], len(got)))
PY
else
  echo "Native Glon comparison skipped: $DZHOST is not built"
fi

if command -v "$NODE" >/dev/null 2>&1 || [ -x "$NODE" ]; then
  "$NODE" test-node.mjs
else
  echo "Browser-runtime Node test skipped: set NODE to a Node 22+ executable"
fi

echo "Real-browser test: see README.md (Chrome/Edge + CDP; test-browser.mjs)."
