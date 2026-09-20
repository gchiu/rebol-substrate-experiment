#!/usr/bin/env python3
"""demo/shop/build.py -- bundle the Glon Shop G1B application.

Reads the view-dialect library (g1b.glon) and the application source
(shop.glon), and writes the fully-bundled page demo/shop/app.html plus a
headless-test source demo/shop/bundle.glon.

The build-time translation is string literals -> STRING! construction (GLON
has no `"..."` literal syntax, so `"Products"` is hoisted to
`str-N: mk-string [ 80 114 111 ... ]` at the top of the bundle and referenced
as `str-N`). This keeps the source readable while using the existing managed
STRING! datatype. Everything else (views, routes, state) is ordinary GLON
interpreted at runtime.

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


def hoist_strings(text: str) -> tuple[str, str]:
    """Extract each unique `"..."` literal, return (definitions, converted text).

    Returns a GLON source fragment `str-1: mk-string [...] str-2: ...` defining
    the strings (created once at load) and the input text with every literal
    replaced by its `str-N` reference."""
    names: dict[str, str] = {}
    defs: list[tuple[str, str]] = []

    def repl(m: re.Match) -> str:
        s = m.group(1)
        if s not in names:
            name = "str-" + str(len(names) + 1)
            names[s] = name
            defs.append((name, s))
        return names[s]

    converted = re.sub(r'"([^"]*)"', repl, text)
    def_text = " ".join(f"{name}: mk-string {to_byte_list(s)}" for name, s in defs)
    return def_text, converted


def main() -> None:
    string_lib = strip_comments((HERE / "g1s.glon").read_text(encoding="utf-8"))
    library = strip_comments((HERE / "g1b.glon").read_text(encoding="utf-8"))
    app = strip_comments((HERE / "shop.glon").read_text(encoding="utf-8"))
    defs, lib_app = hoist_strings(library + "\n" + app)
    combined = "[ " + string_lib + " " + defs + " " + lib_app + " ]"

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
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0;
    font-family: system-ui, -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    color: #1f2937;
    background: #f7f8fa;
    line-height: 1.5;
  }}
  #app {{
    max-width: 42rem;
    margin: 0 auto;
    padding: 2rem 1.25rem 3rem;
  }}
  #app h1 {{
    font-size: 1.75rem;
    letter-spacing: -0.02em;
    margin: 0 0 0.5rem;
  }}
  #app h2 {{
    font-size: 1.05rem;
    margin: 0 0 0.75rem;
  }}
  .tagline {{ color: #6b7280; margin: 0 0 1.5rem; }}

  /* product catalogue */
  .catalog {{
    list-style: none;
    margin: 0 0 1.25rem;
    padding: 0;
    border: 1px solid #e5e7eb;
    border-radius: 0.5rem;
    overflow: hidden;
    background: #ffffff;
  }}
  .catalog li {{
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    padding: 0.75rem 1rem;
  }}
  .catalog li + li {{ border-top: 1px solid #e5e7eb; }}
  .item {{ font-weight: 500; }}

  /* buttons */
  .btn {{
    font: inherit;
    font-weight: 500;
    color: #1f2937;
    background: #ffffff;
    border: 1px solid #d1d5db;
    border-radius: 0.5rem;
    padding: 0.4rem 0.9rem;
    cursor: pointer;
    transition: background-color 0.12s ease, border-color 0.12s ease;
  }}
  .btn:hover {{ background: #f3f4f6; border-color: #9ca3af; }}
  .btn:active {{ background: #e5e7eb; }}
  .btn:focus-visible {{ outline: 2px solid #2563eb; outline-offset: 2px; }}

  /* status panel (search + metrics) */
  .panel {{
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 0.5rem;
    padding: 1rem;
    margin: 0 0 1.25rem;
  }}
  .search-row {{
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.75rem;
  }}
  .search-input {{
    font: inherit;
    flex: 1;
    min-width: 0;
    padding: 0.4rem 0.75rem;
    border: 1px solid #d1d5db;
    border-radius: 0.5rem;
  }}
  .search-input:focus-visible {{ outline: 2px solid #2563eb; outline-offset: 2px; }}
  .meta {{ color: #6b7280; font-size: 0.9rem; margin: 0.25rem 0; }}

  /* basket */
  .basket {{
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 0.5rem;
    padding: 1rem;
    margin: 0 0 1.25rem;
  }}
  .basket-list {{
    list-style: none;
    margin: 0;
    padding: 0;
  }}
  .basket-list li {{
    display: flex;
    justify-content: space-between;
    padding: 0.4rem 0;
  }}
  .basket-list li + li {{ border-top: 1px solid #f3f4f6; }}
  .qty {{ color: #6b7280; font-variant-numeric: tabular-nums; }}
  .total {{
    font-weight: 600;
    border-top: 1px solid #e5e7eb;
    padding-top: 0.6rem;
    margin: 0.5rem 0 0;
  }}

  .nav {{ margin: 0; }}
</style>
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
