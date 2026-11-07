#!/usr/bin/env python3
"""experiments/glon-links-native/gen_pages.py (a copy of ../glon-dom-host/gen_pages.py) -- write the scaling test pages.

gen-N.html has about N DOM nodes (elements and text, excluding whitespace-only
text): N/8 blocks of <div><p>Lorem ipsum <em>dolor</em> sit</p><span>x</span></div>,
ten of which hold <a href="/link/i">Link <b>i</b></a> instead of the span, and
one chain of 20 nested <div>s. Written without whitespace between tags, so the
node count is exact. Usage: gen_pages.py OUTDIR N...
"""
import sys, os
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
for n in map(int, sys.argv[2:]):
    blocks, links = max(1, n // 8), 10
    parts, k = [], 0
    for i in range(blocks):
        tail = '<span>x</span>'
        if k < links and i >= k * blocks // links:
            tail = f'<a href="/link/{k}">Link <b>{k}</b></a>'; k += 1
        parts.append(f'<div><p>Lorem ipsum <em>dolor</em> sit</p>{tail}</div>')
    deep = '<div>' * 20 + 'deep' + '</div>' * 20
    html = ('<!doctype html><html><head><title>gen-%d</title></head><body><h1>Synthetic page</h1>%s%s</body></html>'
            % (n, ''.join(parts), deep))
    with open(os.path.join(out, f'gen-{n}.html'), 'w') as f:
        f.write(html)
