#!/usr/bin/env python3
"""experiments/glon-links-multi/check.py -- check multi.glon's output.

  check.py MULTI_OUT EXPECTED_A EXPECTED_B MIN_SWITCHES

EXPECTED_* are the --raw outputs of the single extractor (../glon-links-native
links.glon) on each document alone. Checks that job A's links and job B's links
(kept, or streamed as ~A / ~B) are exactly the expected ones in document order,
that each job reports the right count, and that the batches alternated at least
MIN_SWITCHES times. Netstrings are read as BYTES.
"""
import sys, re

def netstrings(b, i, n):
    out = []
    for _ in range(n):
        c = b.index(b":", i); ln = int(b[i:c]); out.append(b[c + 1:c + 1 + ln]); i = c + 1 + ln + 1
    return out, i

def single(path):                      # the single extractor's --raw output
    b = open(path, "rb").read(); i = 0; links = []
    while i < len(b) and b[i:i + 1].isdigit():
        (h, t), i = netstrings(b, i, 2); links.append((h, t))
    return links

out = open(sys.argv[1], "rb").read()
exp = {b"A": single(sys.argv[2]), b"B": single(sys.argv[3])}
got = {b"A": [], b"B": []}; counts = {}
i = 0
while i < len(out):
    j = out.index(b"\n", i); line = out[i:j]
    if line.startswith(b"~"):          # a streamed link
        (h, t), i = netstrings(out, j + 1, 2); got[line[1:]].append((h, t)); continue
    m = re.match(rb"== ([AB]) (\d+)$", line)
    if m:
        counts[m.group(1)] = int(m.group(2)); i = j + 1
        while i < len(out) and out[i:i + 1].isdigit():
            (h, t), i = netstrings(out, i, 2); got[m.group(1)].append((h, t))
        continue
    i = j + 1
order = re.search(rb"# order ([AB]+)", out)
switches = sum(1 for a, b in zip(order.group(1), order.group(1)[1:]) if a != b) if order else -1
ok = True
for k in (b"A", b"B"):
    same = got[k] == exp[k] and counts.get(k) == len(exp[k])
    ok &= same
    print(f"  job {k.decode()}: {len(got[k])} links, count {counts.get(k)}, expected {len(exp[k])}: "
          f"{'exact, in document order' if same else 'DIFFERENT'}")
print(f"  batches alternated {switches} times (at least {sys.argv[4]} required)")
ok &= switches >= int(sys.argv[4])
sys.exit(0 if ok else 1)
