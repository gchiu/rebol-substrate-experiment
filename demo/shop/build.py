#!/usr/bin/env python3
"""demo/shop/build.py -- bundle the Glon Demos launcher application.

Reads the STRING! primitives (g1s.glon), the view-dialect library (g1b.glon)
and the application source (shop.glon), and writes the fully-bundled page
demo/shop/app.html plus a headless-test source demo/shop/bundle.glon.

Glon parses Glon: `"..."` string literals are a native parser feature and are
passed through verbatim (no Python string rewriting / hoisting). Everything is
ordinary GLON interpreted at runtime.

The browser page inlines the combined source in a
<script type="application/glon"> block and references style.css / host.js /
glon.wasm; no fragment is fetched over the network at runtime.
"""

import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def main() -> None:
    string_lib = strip_comments((HERE / "g1s.glon").read_text(encoding="utf-8"))
    library = strip_comments((HERE / "g1b.glon").read_text(encoding="utf-8"))
    app = strip_comments((HERE / "shop.glon").read_text(encoding="utf-8"))
    combined = "[ " + string_lib + " " + library + " " + app + " ]"

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
<title>Glon Demos</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
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
