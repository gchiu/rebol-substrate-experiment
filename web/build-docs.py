#!/usr/bin/env python3
"""Generate the public GitHub Pages page (docs/index.html) from the single
authoritative R0 source web/demo.r0.

The two logical sections of demo.r0 are marked with line comments:

    ;; @section application
    ;; @section raw

This script reads those sections, HTML-escapes them, and emits STATIC
<pre><code> listings into docs/index.html.  No JavaScript fetches or renders
the source.  It also copies the built demo.js / demo.wasm into docs/.
"""
import html
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEMO_R0 = ROOT / "web" / "demo.r0"
GLUE_JS = ROOT / "web" / "glue.js"
DOCS = ROOT / "docs"


def read_sections(path):
    lines = path.read_text().splitlines()
    markers = {}
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith(";; @section "):
            name = s[len(";; @section "):].strip()
            markers[name] = i
    if "application" not in markers or "raw" not in markers:
        sys.exit("build-docs: missing @section markers in demo.r0")
    app = lines[markers["application"] + 1: markers["raw"]]
    raw = lines[markers["raw"] + 1:]
    # drop the single trailing outer-block "]" (the last line of the file),
    # keeping the RAW fragment's own closing "]" above it
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

    app_lines, raw_lines = read_sections(DEMO_R0)
    glue_lines = GLUE_JS.read_text().splitlines()

    app_block = code_block(app_lines)
    raw_block = code_block(raw_lines)
    glue_block = code_block(glue_lines)

    page = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>R0 / S1 WebAssembly Demo</title>
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
  <h1>R0 / S1 WebAssembly Demo</h1>

  <p class="muted">A standalone build (no Emscripten JS runtime, no libc/WASI)
     lives at <a href="standalone.html">docs/standalone.html</a>.</p>

  <p>Counter: <span id="counter">0</span></p>
  <button id="increment">Increment</button>

  <p class="muted">Application logic: R0 &rarr; WebAssembly<br>
     Browser plumbing: JavaScript</p>

  <p class="muted">This application logic is R0 running inside WebAssembly.
     JavaScript only forwards browser events and performs the requested DOM
     update.</p>

  <h2>R0 application</h2>
  {app}

  <details>
    <summary>Show low-level RAW/S1 host adapter</summary>
    <p class="muted">RAW is R0&rsquo;s trapdoor to the tiny Forth-like S1
       substrate. Application code normally stays above this layer.</p>
    {raw}
  </details>

  <details>
    <summary>Show browser JavaScript glue</summary>
    <p class="muted">This glue contains no counter state or increment logic; it
       only forwards clicks and performs the DOM update R0 requests.</p>
    {glue}
  </details>

  <script src="demo.js"></script>
</body>
</html>
""".format(app=app_block, raw=raw_block, glue=glue_block)

    (DOCS / "index.html").write_text(page)

    # publish the built artifacts alongside the page
    for name in ("demo.js", "demo.wasm"):
        src = ROOT / "web" / name
        if src.exists():
            shutil.copyfile(src, DOCS / name)
        else:
            sys.exit(f"build-docs: missing {name} (run `make wasm` first)")

    print("build-docs: generated docs/index.html (+ demo.js, demo.wasm)")


if __name__ == "__main__":
    main()
