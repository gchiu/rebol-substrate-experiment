#!/usr/bin/env python3
"""demo/shop/build-traffic.py -- bundle the standalone traffic page.

Reads the shared view-dialect machinery (common.glon) and the IDM traffic
simulator (demos/traffic.glon), and writes demo/shop/traffic.html: a single
self-contained page that loads ONLY these two sources (no launcher app.glon, so
the loader-heap budget of the multi-demo launcher is untouched) and drives the
simulation through the same G1A event bridge as the launcher.

The page inlines the combined source in a <script type="application/glon">
block and references traffic-host.js (the minimal browser host). glon.wasm is
the existing demo/shop/glon.wasm, reused unchanged.

traffic.glon is wrapped in its own [ ... ] block; it is unboxed here so the two
sources can be combined into ONE outer block and loaded with a single glon_load
call (each loaded file is one parsed block).
"""

import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def strip_comments(text: str) -> str:
    return "\n".join(line.split(";;", 1)[0] for line in text.splitlines())


def unbox(text: str) -> str:
    t = text.strip()
    if t.startswith("[") and t.endswith("]"):
        t = t[1:-1]
    return t


def main() -> None:
    common = strip_comments((HERE / "common.glon").read_text(encoding="utf-8"))
    traffic = unbox(strip_comments((HERE / "demos" / "traffic.glon").read_text(encoding="utf-8")))
    combined = "[ " + common + " " + traffic + " ]"

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon IDM traffic simulator</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 1040px; margin: 1.5rem auto; padding: 0 1rem; color: #111; }}
  h1 {{ font-size: 1.4rem; }}
  .muted {{ color: #666; font-size: 0.9rem; }}
  #status {{ font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 0.95rem;
            background: #f6f6f6; border: 1px solid #e0e0e0; border-radius: 4px; padding: 0.4rem 0.6rem; margin: 0.6rem 0; }}
  #glon-canvas {{ border: 1px solid #ccc; border-radius: 4px; display: block; background: #fff; }}
  button {{ font-size: 1rem; padding: 0.4rem 1rem; margin-right: 0.5rem; }}
  select {{ font-size: 1rem; padding: 0.3rem; }}
</style>
</head>
<body>
  <h1>Glon IDM traffic simulator</h1>
  <p class="muted">25 vehicles follow the Intelligent Driver Model on a one-lane ring road
     (1 km circumference, drawn unrolled). The simulation runs in frozen Glon Alpha; this page
     only schedules ticks, paints the canvas, and shows the status Glon emits.</p>
  <p class="muted">Brake disturbance: vehicle 0 is speed-capped at 5 m/s for 20.0&ndash;21.5 s,
     then normal IDM dynamics resume. Vehicle 0 is drawn in red.</p>

  <div id="status" data-glon-id="1">loading&hellip;</div>
  <canvas id="glon-canvas" width="1000" height="320"></canvas>

  <p style="margin-top: 0.75rem;">
    <button id="start">Start</button>
    <button id="reset">Reset</button>
    <label for="speed">Speed</label>
    <select id="speed">
      <option value="1">1x</option>
      <option value="2" selected>2x</option>
      <option value="5">5x</option>
    </select>
    <span class="muted" style="margin-left: 0.75rem;">1 tick = 0.1 simulated seconds</span>
  </p>

  <script type="application/glon">{combined}</script>
  <script src="traffic-host.js"></script>
</body>
</html>
"""
    (HERE / "traffic.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'traffic.html'} ({len(combined)} bytes of GLON source)")


if __name__ == "__main__":
    main()
