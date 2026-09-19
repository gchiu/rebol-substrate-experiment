#!/usr/bin/env python3
"""demo/shop/build.py -- bundle the Glon Shop G1A application.

Reads the authoritative GLON source (shop.glon) and the three readable HTML
fragments (fragments/*.html), converts each fragment into a GLON byte-list
definition, and writes the fully-bundled page demo/shop/app.html with:

  * the combined GLON source inlined as <script type="application/glon">;
  * the <div id="app" data-glon-id="1"> render target;
  * a <script src="host.js"> and <script src="glon.wasm"> are referenced by
    host.js/instantiateStreaming at runtime (host.js fetches glon.wasm).

No fragment is fetched over the network at runtime: fragments are byte-lists
bundled inside the inlined GLON source. This is the "compressed fragment ->
decompress -> scan/evaluate -> render" pipeline with the decompress step still
the identity (fragments are stored uncompressed); a future build step can
compress the bytes before emitting the list without changing the GLON source
or the renderer.
"""

import html
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
FRAGMENTS = ["home", "products", "not-found"]


def fragment_to_glon(name: str) -> str:
    raw = (HERE / "fragments" / f"{name}.html").read_text(encoding="utf-8")
    codes = [str(b) for b in raw.encode("utf-8")]
    return f"{name}-fragment: [ {' '.join(codes)} ]"


def main() -> None:
    shop = (HERE / "shop.glon").read_text(encoding="utf-8")

    frag_lines = "\n".join(fragment_to_glon(n) for n in FRAGMENTS)
    if ";; @fragments" not in shop:
        raise SystemExit("shop.glon is missing the ';; @fragments' marker")
    combined = shop.replace(";; @fragments", frag_lines)

    host_js = (HERE / "host.js").read_text(encoding="utf-8")
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
<script type="application/glon">{html.escape(combined)}</script>
<script src="host.js"></script>
</body>
</html>
"""
    (HERE / "app.html").write_text(page, encoding="utf-8")
    (HERE / "bundle.glon").write_text(combined, encoding="utf-8")
    print(f"wrote {HERE / 'app.html'} and {HERE / 'bundle.glon'} "
          f"({len(combined)} bytes of GLON source)")


if __name__ == "__main__":
    main()
