#!/usr/bin/env python3
"""experiments/glon-links-multi/gen_docs.py -- write doc-a.html and doc-b.html, the
two documents of the two-extractor test. Each has 40 rows (about 400 nodes, many
batches) and distinctive links whose text is nested several elements deep:
  doc-a.html  /a/ links: home, rows 1 and 21, last      (4 links)
  doc-b.html  /b/ links: home, rows 1, 11, 21, 31, last (6 links)
"""
def doc(tag, every, extra):
    T = tag.upper()
    rows = []
    for i in range(40):
        if i % every == 1:
            rows.append(f'<li class="r{i}"><span>Row {i}:</span> <a href="/{tag}/item-{i}?ref={tag}">{T} item <em>{i}</em> '
                        f'<small>(<b>{extra}</b>)</small></a> trailing text</li>')
        else:
            rows.append(f'<li class="r{i}"><span>Row {i}:</span> plain <i>text</i> with <code>no link</code></li>')
    return (f'<!doctype html>\n<!-- experiments/glon-links-multi/doc-{tag}.html (written by gen_docs.py) -->\n'
            f'<html><head><title>Document {T}</title></head><body><header><a href="/{tag}/home">{T} <strong>home</strong></a></header>'
            f'<main><h1>Document {T}</h1><ul>' + ''.join(rows) + '</ul>'
            f'<p>Last: <a href="/{tag}/last"><span><em>the <b>{T}</b></em> end</span></a></p></main></body></html>\n')
open('doc-a.html', 'w').write(doc('a', 20, 'alpha'))
open('doc-b.html', 'w').write(doc('b', 10, 'nested'))
