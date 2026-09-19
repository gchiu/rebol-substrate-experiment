#!/usr/bin/env python3
"""demo/shop/build.py -- bundle the Glon Shop G1B application.

Reads the view-dialect library (g1b.glon) and the application source
(shop.glon), and writes the fully-bundled page demo/shop/app.html plus a
headless-test source demo/shop/bundle.glon.

The one build-time translation is string literals -> GLON byte-lists (GLON has
no string literal syntax yet): `"Products"` becomes `[ 80 114 111 ... ]`. This
keeps the source readable while staying within the existing language. Everything
else (views, routes, state) is ordinary GLON interpreted at runtime.

The browser page inlines the combined source in a
<script type="application/glon"> block and references host.js / glon.wasm; no
fragment is fetched over the network at runtime.
"""

import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent


def to_byte_list(s: str) -> str:
    return "[" + " ".join(str(b) for b in s.encode("utf-8")) + "]"


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def convert_strings(text: str) -> str:
    return re.sub(r'"([^"]*)"', lambda m: to_byte_list(m.group(1)), text)


def main() -> None:
    library = strip_comments((HERE / "g1b.glon").read_text(encoding="utf-8"))
    app = strip_comments((HERE / "shop.glon").read_text(encoding="utf-8"))
    combined = "[ " + library + " " + app + " ]"
    combined = convert_strings(combined)

    (HERE / "bundle.glon").write_text(combined, encoding="utf-8")

    # The GLON source is embedded in a <script> element, whose content is raw
    # text: the HTML parser does NOT decode character references there, so
    # html.escape() would corrupt the source ('home -> &#x27;home, < -> &lt;,
    # etc.).  GLON contains no "</script" sequence, so the source can be inlined
    # verbatim and reaches glon_load byte-for-byte as bundle.glon.
    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon Shop</title>
<style>
  body {{ font-family: system-ui, sans-serif; margin: 2rem; color: #1a1a1a; }}
  button {{ font-size: 1rem; padding: 0.35rem 0.8rem; cursor: pointer; }}
  #app {{ max-width: 40rem; }}
</style>
</head>
<body>
<h1>Glon Shop</h1>
<div id="app" data-glon-id="1"></div>
<script type="application/glon">{combined}</script>
<script src="host.js"></script>
</body>
</html>
"""
    (HERE / "app.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'app.html'} and {HERE / 'bundle.glon'} "
          f"({len(combined)} bytes of GLON source)")


if __name__ == "__main__":
    main()
