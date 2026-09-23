#!/usr/bin/env python3
"""demo/shop/build-linda.py -- bundle the standalone Linda demo page.

Reads the self-contained Linda demo (demos/linda.glon) into the page's single,
always-loaded <script type="application/glon"> block.  The page is driven
entirely through the existing G1A event bridge (glon_event) by linda-host.js;
this script contains no Linda or scheduling semantics.

demos/linda.glon is self-contained (no common.glon): the standalone page has a
fixed 16 KiB source buffer, and the full scheduling + tuple-space + render
program must fit it.
"""

import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def main() -> None:
    combined = strip_comments((HERE / "demos/linda.glon").read_text(encoding="utf-8"))

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon Linda - cooperative blocking</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 900px; margin: 1.5rem auto; padding: 0 1rem; color: #111; }}
  h1 {{ font-size: 1.4rem; }}
  h2 {{ font-size: 1.05rem; margin: 1rem 0 0.3rem; }}
  .muted {{ color: #666; font-size: 0.9rem; }}
  #status {{ background: #f6f6f6; border: 1px solid #e0e0e0; border-radius: 4px; padding: 0.6rem 0.8rem; }}
  #status ul {{ margin: 0.2rem 0 0.4rem 1.2rem; padding: 0; }}
  button {{ font-size: 1rem; padding: 0.4rem 1rem; margin-right: 0.5rem; }}
  code {{ background: #eee; padding: 0 0.2rem; }}
</style>
</head>
<body>
  <h1>Glon Linda</h1>
  <p class="muted">Real cooperative blocking in Glon. Task A runs, executes
     <code>IN [go a]</code>, and genuinely stops; task B runs while A is blocked,
     <code>OUT</code>s <code>[go a]</code>, and A resumes <em>after</em> its IN.
     Every state, tuple, waiter and trace entry below is produced by frozen Glon;
     JavaScript only paces the steps and paints the returned HTML.</p>

  <p>
    <button id="reset">Reset</button>
    <button id="step">Step</button>
    <button id="run">Run</button>
    <span class="muted">Step = one real task slice.</span>
  </p>

  <div id="status" data-glon-id="1">loading&hellip;</div>

  <script type="application/glon">{combined}</script>
  <script src="linda-host.js"></script>
</body>
</html>
"""
    (HERE / "linda.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'linda.html'} (bundle {len(combined)} bytes)")


if __name__ == "__main__":
    main()
