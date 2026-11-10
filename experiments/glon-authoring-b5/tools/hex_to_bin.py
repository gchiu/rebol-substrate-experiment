#!/usr/bin/env python3
"""Convert an LLM-authored whitespace-separated 32-bit hex word list to the
little-endian binary word file expected by the B4 B/C front-ends.

The first word must be the representation magic (42344231 for B, 43344231 for
C). Each hex token is masked to 32 bits. Comments start with ';' or '#'.
"""
import re
import struct
import sys

if len(sys.argv) != 3:
    print("usage: hex_to_bin.py INPUT_HEX OUTPUT_BIN", file=sys.stderr)
    sys.exit(2)

words = []
with open(sys.argv[1], "r", errors="replace") as f:
    for line in f:
        line = re.split(r"[;#]", line, 1)[0]
        for tok in line.split():
            tok = tok.lower()
            if tok.startswith("0x"):
                tok = tok[2:]
            if not re.fullmatch(r"[0-9a-f]+", tok):
                print(f"invalid hex token: {tok}", file=sys.stderr)
                sys.exit(1)
            words.append(int(tok, 16) & 0xFFFFFFFF)

if not words:
    print("empty hex artifact", file=sys.stderr)
    sys.exit(1)
if words[0] not in (0x42344231, 0x43344231):
    print(f"bad magic word: {words[0]:08x}", file=sys.stderr)
    sys.exit(1)

with open(sys.argv[2], "wb") as f:
    f.write(b"".join(struct.pack("<I", w) for w in words))
