#!/usr/bin/env python3
"""demo/shop/build-traffic.py -- bundle the standalone traffic page.

Reads the shared view-dialect machinery (common.glon), the IDM traffic
simulator (demos/traffic.glon) and the shared Model dispatcher
(demos/models-dispatch.glon) into the page's main, always-loaded
<script type="application/glon"> block -- exactly the original common+traffic
bundle, plus the small dispatcher, so the initial load stays small.

The other three model families (demos/newell.glon, demos/ovm.glon,
demos/nasch.glon) are each written into their OWN separate
<script type="application/glon" data-model="..."> block, hidden and NOT
parsed at page load. traffic-host.js loads a model's block with its own
glon_load call the first time it is selected (the same lazy-load pattern the
main shop launcher already uses for its own demos -- see
demo/shop/build.py/host.js). This keeps the initial and per-model loader-heap
footprint the same as loading one demo at a time; combining common.glon plus
all four model families PLUS the dispatcher into one glon_load call was tried
first and exceeds the parser's per-block form limit (github: verified with a
throwaway probe, not guessed) -- reported in GLON-TRAFFIC-MODELS.md as
application-level friction, not a reason to touch the parser or the heap
limit (both forbidden for this task).

Each demo/dispatcher file is wrapped in its own [ ... ] block; the ones going
into the main bundle are unboxed here so they combine into ONE outer block
and load with a single glon_load call. The three lazy model blocks are left
boxed (traffic-host.js glon_loads each one exactly as its own file stands).
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


def load(rel: str) -> str:
    return strip_comments((HERE / rel).read_text(encoding="utf-8"))


def main() -> None:
    common = load("common.glon")
    traffic = unbox(load("demos/traffic.glon"))
    dispatch = unbox(load("demos/models-dispatch.glon"))
    combined = "[ " + common + " " + traffic + " " + dispatch + " ]"

    lazy_blocks = ""
    for model_id, fname in (("newell", "newell.glon"), ("ovm", "ovm.glon"), ("nasch", "nasch.glon")):
        src = load(f"demos/{fname}")  # kept boxed -- glon_load'd as its own file
        lazy_blocks += (
            f'\n  <script type="application/glon" data-model="{model_id}" '
            f'style="display:none">{src}</script>'
        )

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon traffic model laboratory</title>
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
  <h1>Glon traffic model laboratory</h1>
  <p class="muted">25 vehicles on a one-lane ring road (1 km circumference, drawn unrolled),
     under four deliberately different published car-following models. Every model and every
     number shown runs in frozen Glon Alpha; this page only schedules ticks, loads a model's
     source the first time it is selected, paints the canvas, and shows the status Glon emits.
     See GLON-TRAFFIC-MODELS.md for the model comparison.</p>
  <p class="muted">Brake disturbance: vehicle 0 is speed-capped for a window near the start of
     the run, then normal dynamics resume (NaSch instead forces a full stop -- see the model
     comparison doc for why). Vehicle 0 is drawn in red.</p>

  <div id="status" data-glon-id="1">loading&hellip;</div>
  <canvas id="glon-canvas" width="1000" height="320"></canvas>

  <p style="margin-top: 0.75rem;">
    <label for="model">Model</label>
    <select id="model">
      <option value="idm" selected>IDM</option>
      <option value="newell">Newell</option>
      <option value="ovm">FVDM (OVM)</option>
      <option value="nasch">NaSch</option>
    </select>
    <label for="mode">Scenario</label>
    <select id="mode">
      <option value="baseline" selected>Baseline</option>
      <option value="pacing">Pacing (IDM only)</option>
    </select>
    <button id="start">Start</button>
    <button id="reset">Reset</button>
    <label for="speed">Speed</label>
    <select id="speed">
      <option value="1">1x</option>
      <option value="2" selected>2x</option>
      <option value="5">5x</option>
    </select>
  </p>
  <p class="muted">Pacing (IDM only): vehicle 24 (the disturbed vehicle's immediate follower) is
     speed-capped at 18 m/s from the moment the brake begins -- see GLON-TRAFFIC-MODELS.md for why
     this does not (and is not claimed to) generalize to the other three models. Switching model
     or scenario resets the experiment.</p>

  <script type="application/glon">{combined}</script>{lazy_blocks}
  <script src="traffic-host.js"></script>
</body>
</html>
"""
    (HERE / "traffic.html").write_text(page, encoding="utf-8")
    print(f"wrote {HERE / 'traffic.html'} (main bundle {len(combined)} bytes + 3 lazy model blocks)")


if __name__ == "__main__":
    main()
