#!/usr/bin/env python3
"""demo/shop/build-live.py -- bundle the experimental Live Translate page.

Reads the shared machinery (common.glon), the STRING! library (strings.glon)
and the live probe (live.glon), and writes demo/shop/live.html.  The three Glon
sources are kept in three separate <script type="application/glon"> blocks
(common is wrapped in one outer [ ... ] so its masm/raw forms parse as
block-context keywords); live-host.js loads them in order.

Nothing here touches the existing shop/app.html build or its host imports.
"""

import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def main() -> None:
    common = strip_comments((HERE / "common.glon").read_text(encoding="utf-8"))
    strings = strip_comments((HERE / "strings.glon").read_text(encoding="utf-8"))
    live = strip_comments((HERE / "live.glon").read_text(encoding="utf-8"))

    # Common is a top-level sequence; wrap it so `masm`/`raw` parse as
    # block-context keywords.  strings.glon and live.glon already carry their
    # own outer [ ... ].
    common_block = "[ " + common + " ]"

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon Live Translate boundary probe</title>
</head>
<body>
<div id="app" data-glon-id="1"></div>
<script type="application/glon" data-env="common">{common_block}</script>
<script type="application/glon" data-env="strings">{strings}</script>
<script type="application/glon" data-env="live">{live}</script>
<script src="live-host.js"></script>
</body>
</html>
"""
    (HERE / "live.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'live.html'} "
          f"({len(common_block)} + {len(strings)} + {len(live)} bytes of GLON source)")


if __name__ == "__main__":
    main()
