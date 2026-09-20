#!/usr/bin/env python3
"""demo/shop/build.py -- bundle the Glon Demos bootstrap.

Reads the shared machinery (common.glon) and the launcher bootstrap (app.glon),
and writes the fully-bundled bootstrap page demo/shop/app.html plus a
headless-test source demo/shop/bootstrap.glon.

The DEMOS are deliberately NOT bundled here: demo/shop/demos/*.glon remain
separate, readable source files that the launcher loads on demand (via the
host's generic [data-glon-load] fetch + glon_load + re-route path). Adding a
demo must not grow the bootstrap image.

Glon parses Glon: `"..."` string literals are a native parser feature and are
passed through verbatim (no Python string rewriting / hoisting). Everything is
ordinary GLON interpreted at runtime.

The browser page inlines the combined bootstrap source in a
<script type="application/glon"> block and references style.css / host.js /
glon.wasm; no fragment is fetched over the network at runtime (only the demos
are, and only on demand).
"""

import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def main() -> None:
    common = strip_comments((HERE / "common.glon").read_text(encoding="utf-8"))
    app = strip_comments((HERE / "app.glon").read_text(encoding="utf-8"))
    combined = "[ " + common + " " + app + " ]"

    (HERE / "bootstrap.glon").write_text(combined, encoding="utf-8")

    # The GLON source is embedded in a <script> element, whose content is raw
    # text: the HTML parser does NOT decode character references there, so
    # html.escape() would corrupt the source ('home -> &#x27;home, < -> &lt;,
    # etc.).  GLON contains no "</script" sequence, so the source can be inlined
    # verbatim and reaches glon_load byte-for-byte as bootstrap.glon.
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
    print(f"wrote {HERE / 'app.html'} and {HERE / 'bootstrap.glon'} "
          f"({len(combined)} bytes of GLON bootstrap source)")


if __name__ == "__main__":
    main()
