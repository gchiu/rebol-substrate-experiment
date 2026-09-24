#!/usr/bin/env python3
"""site/check-site.py -- verify an assembled Pages tree before it is published.

  python3 site/check-site.py <docs-dir>

<docs-dir> is the directory GitHub Pages serves (docs/ on main, as assembled
by .github/workflows/deploy-pages.yml). The check fails (exit 1) unless:

  - docs/index.html is exactly site/index.html (the authoritative source);
  - every relative href/src in docs/index.html resolves to a file in the tree
    (a link ending in "/" needs that directory's index.html);
  - the published demos the landing page depends on are all present.

Absolute (https://) links are listed but not fetched: this check runs offline.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "site" / "index.html"

# pages that must exist in every published tree (the landing page's promises)
REQUIRED = [
    "index.html",
    "r0-counter.html", "demo.js", "demo.wasm",
    "shop/index.html", "shop/glon.wasm",
    "shop/primer.html", "shop/primer-host.js",
    "shop/traffic.html", "shop/traffic-host.js",
    "shop/linda.html", "shop/linda-host.js",
]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check-site.py <docs-dir>")
    docs = pathlib.Path(sys.argv[1])
    problems = []

    index = docs / "index.html"
    if not index.is_file():
        sys.exit("check-site: FAIL: %s is missing" % index)
    if index.read_bytes() != SOURCE.read_bytes():
        problems.append("docs/index.html is not a verbatim copy of site/index.html")

    for rel in REQUIRED:
        if not (docs / rel).is_file():
            problems.append("missing published file: " + rel)

    html = index.read_text(encoding="utf-8")
    links = re.findall(r'(?:href|src)="([^"#]+)', html)
    external = []
    for link in links:
        if re.match(r"[a-z]+:", link):
            external.append(link)
            continue
        target = docs / link
        if link.endswith("/"):
            target = target / "index.html"
        if not target.is_file():
            problems.append("broken link in index.html: " + link)

    if problems:
        for p in problems:
            print("check-site: FAIL: " + p)
        sys.exit(1)
    print("check-site: OK: %d relative links resolve, %d required files present, "
          "%d external links (not fetched)" % (len(links) - len(external), len(REQUIRED), len(external)))


if __name__ == "__main__":
    main()
