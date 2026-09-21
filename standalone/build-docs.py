#!/usr/bin/env python3
"""Generate the public standalone demo page (docs/standalone.html) from the
single authoritative R0/GLON source standalone/app.glon.

Mirrors web/build-docs.py (W1.1): the two logical sections of app.glon are
marked with line comments

    ;; @section application
    ;; @section masm

and are HTML-escaped into static <pre><code> listings.  The FULL app.glon
source is also inlined into a <script type="application/glon"> block so that
standalone/glon.js can pass it to glon_load at runtime -- there is no virtual
filesystem and no fetch of the source.  glon.js / glon.wasm are copied into
docs/ alongside the page.
"""
import html
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
APP_GLON = ROOT / "standalone" / "app.glon"
GLON_JS = ROOT / "standalone" / "glon.js"
DOCS = ROOT / "docs"


def read_sections(path):
    lines = path.read_text().splitlines()
    markers = {}
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith(";; @section "):
            markers[s[len(";; @section "):].strip()] = i
    if "application" not in markers or "masm" not in markers:
        sys.exit("build-docs: missing @section markers in app.glon")
    app = lines[markers["application"] + 1: markers["masm"]]
    raw = lines[markers["masm"] + 1:]
    # drop the single trailing outer-block "]" line (the file closes the RAW
    # fragment's own block, then the outer block)
    if raw and raw[-1].strip() == "]":
        raw = raw[:-1]
    return dedent_lines(app), dedent_lines(raw)


def dedent_lines(lines):
    while lines and not lines[0].strip():
        lines = lines[1:]
    while lines and not lines[-1].strip():
        lines = lines[:-1]
    return lines


def code_block(lines):
    text = "\n".join(lines).rstrip()
    return "<pre><code>" + html.escape(text) + "</code></pre>"


def main():
    DOCS.mkdir(exist_ok=True)

    full_src = APP_GLON.read_text().rstrip("\n")
    app_lines, raw_lines = read_sections(APP_GLON)
    js_lines = GLON_JS.read_text().splitlines()

    app_block = code_block(app_lines)
    raw_block = code_block(raw_lines)
    js_block = code_block(js_lines)

    page = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>R0 / S1 Standalone WebAssembly Demo</title>
  <style>
    body {{ font-family: system-ui, sans-serif; max-width: 46rem; margin: 2rem auto; padding: 0 1rem; line-height: 1.5; color: #111; }}
    h1 {{ font-size: 1.5rem; }}
    pre {{ background: #f6f6f6; border: 1px solid #e0e0e0; border-radius: 4px; padding: 0.75rem 1rem; overflow-x: auto; }}
    code {{ font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }}
    summary {{ cursor: pointer; font-weight: 600; }}
    details {{ margin: 0.75rem 0; }}
    .muted {{ color: #666; font-size: 0.9rem; }}
    #counter {{ font-weight: 700; }}
    button {{ font-size: 1rem; padding: 0.4rem 1rem; }}
  </style>
</head>
<body>
  <h1>R0 / S1 Standalone WebAssembly Demo</h1>

  <p>Counter: <span id="counter" data-glon-id="1">0</span></p>
  <button data-glon-click="increment">Increment</button>

  <p class="muted">Application logic: R0 &rarr; WebAssembly<br>
     Browser plumbing: JavaScript (handwritten, no Emscripten JS runtime)</p>

  <p class="muted">This is the same R0 application as the W1.1 demo, but the
     WebAssembly module is built <code>-nostdlib</code> with a 22&nbsp;KB
     <code>glon.wasm</code>, a handwritten <code>glon.js</code>, and no
     virtual filesystem: the source below is inlined in the page and passed to
     <code>glon_load</code> at runtime.</p>

  <h2>R0 / GLON application</h2>
  {app}

  <details>
    <summary>Show low-level MASM/S1 host adapter</summary>
    <p class="muted">MASM is R0&rsquo;s macroassembler over the tiny Forth-like S1
       substrate. Application code normally stays above this layer.</p>
    {raw}
  </details>

  <details>
    <summary>Show handwritten JavaScript bootloader</summary>
    <p class="muted">This bootloader contains no counter state or increment
       logic; it only instantiates <code>glon.wasm</code>, supplies the two
       host imports, and forwards clicks / DOM updates.</p>
    {js}
  </details>

  <!-- the authoritative source, inlined for glon.js to pass to glon_load -->
  <script type="application/glon">{src}</script>

  <script src="glon.js"></script>
</body>
</html>
""".format(app=app_block, raw=raw_block, js=js_block, src=full_src)

    (DOCS / "standalone.html").write_text(page)

    for name in ("glon.js", "glon.wasm"):
        src = ROOT / "standalone" / name
        if src.exists():
            shutil.copyfile(src, DOCS / name)
        else:
            sys.exit(f"build-docs: missing {name} (run `make wasm-standalone` first)")

    print("build-docs: generated docs/standalone.html (+ glon.js, glon.wasm)")


if __name__ == "__main__":
    main()
