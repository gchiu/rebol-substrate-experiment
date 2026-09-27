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

    style = """
* { box-sizing: border-box; }
html, body { height: 100%; }
body {
    margin: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    background: #f4f4f5;
    color: #18181b;
    font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    -webkit-font-smoothing: antialiased;
}
.console {
    width: min(92vw, 380px);
    padding: 32px 28px 22px;
    background: #ffffff;
    border: 1px solid #e4e4e7;
    border-radius: 18px;
    box-shadow: 0 12px 40px rgba(24, 24, 27, 0.08);
    text-align: center;
}
.brand {
    font-size: 13px;
    font-weight: 700;
    letter-spacing: 0.22em;
    color: #52525b;
}
.status {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 9px;
    margin: 22px 0 24px;
    font-size: 17px;
    color: #3f3f46;
}
.dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #a1a1aa;
    transition: background 0.2s ease;
}
.dot.live {
    background: #dc2626;
    box-shadow: 0 0 0 0 rgba(220, 38, 38, 0.45);
    animation: pulse 1.6s infinite;
}
@keyframes pulse {
    0% { box-shadow: 0 0 0 0 rgba(220, 38, 38, 0.45); }
    70% { box-shadow: 0 0 0 9px rgba(220, 38, 38, 0); }
    100% { box-shadow: 0 0 0 0 rgba(220, 38, 38, 0); }
}
.btn {
    appearance: none;
    width: 100%;
    padding: 16px 20px;
    font-size: 17px;
    font-weight: 700;
    letter-spacing: 0.1em;
    color: #ffffff;
    background: #27272a;
    border: 0;
    border-radius: 12px;
    cursor: pointer;
    transition: background 0.2s ease, transform 0.05s ease;
}
.btn:hover { background: #3f3f46; }
.btn:active { transform: translateY(1px); }
#app[data-state="listening"] .btn { background: #dc2626; }
#app[data-state="listening"] .btn:hover { background: #b91c1c; }
.hint {
    margin-top: 14px;
    min-height: 18px;
    font-size: 13px;
    color: #71717a;
}
.meter {
    height: 4px;
    margin: 16px 0 4px;
    background: #e4e4e7;
    border-radius: 999px;
    overflow: hidden;
}
.meter > span {
    display: block;
    width: 0%;
    height: 100%;
    background: #dc2626;
    border-radius: 999px;
    transition: width 0.08s linear;
}
.error {
    margin-top: 14px;
    font-size: 13px;
    color: #b91c1c;
    overflow-wrap: anywhere;
}
.foot {
    margin-top: 20px;
    font-size: 11px;
    letter-spacing: 0.18em;
    text-transform: uppercase;
    color: #a1a1aa;
}
"""

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#dc2626">
<title>Live Translate</title>
<style>{style}</style>
</head>
<body>
<div id="app" data-glon-id="1"></div>
<script type="application/glon" data-env="common">{common_block}</script>
<script type="application/glon" data-env="strings">{strings}</script>
<script type="application/glon" data-env="live">{live}</script>
<script src="qwen-client.js"></script>
<script src="live-host.js"></script>
</body>
</html>
"""
    (HERE / "live.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'live.html'} "
          f"({len(common_block)} + {len(strings)} + {len(live)} bytes of GLON source)")


if __name__ == "__main__":
    main()
