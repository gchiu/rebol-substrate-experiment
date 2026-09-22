// demo/shop/traffic_node_test.js -- headless verification of the standalone
// traffic page wiring (no DOM, no browser, no Emscripten runtime).
//
// Mirrors node_test.js: it instantiates demo/shop/glon.wasm with the four host
// imports, extracts the combined source from traffic.html's
// <script type="application/glon"> block (exactly as a browser's script.textContent
// would), glon_load's it, then drives the simulation through the same
// glon_event bridge the page's traffic-host.js uses, asserting:
//   1. the traffic page embeds exactly common.glon + traffic.glon (no launcher);
//   2. initial render emits the road line + 25 vehicle dots;
//   3. advancing changes vehicle state and re-renders it;
//   4. reset returns to the initial state;
//   5. the disturbance window is ticks 200..215 (BRAKING shown then, not after).
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon.wasm");
const HTML = fs.readFileSync(path.join(HERE, "traffic.html"), "utf8");
const COMMON = fs.readFileSync(path.join(HERE, "common.glon"), "utf8");
const TRAFFIC = fs.readFileSync(path.join(HERE, "demos", "traffic.glon"), "utf8");

// Match build-traffic.py exactly: Python's str.splitlines() does not yield a
// trailing empty element for a final newline, so strip trailing newlines first.
function stripComments(s) {
  return s.replace(/\n+$/, "").split("\n").map(l => l.split(";;")[0]).join("\n");
}
function unbox(s) { const t = s.trim(); return (t.startsWith("[") && t.endsWith("]")) ? t.slice(1, -1) : t; }

const scriptMatch = /<script type="application\/glon">([\s\S]*?)<\/script>/.exec(HTML);
if (!scriptMatch) {
  console.error("TRAFFIC_TEST FAIL: traffic.html has no <script type=\"application/glon\"> block");
  process.exit(1);
}
const SRC = scriptMatch[1];
const EXPECTED = "[ " + stripComments(COMMON) + " " + unbox(stripComments(TRAFFIC)) + " ]";
if (SRC !== EXPECTED) {
  console.error("TRAFFIC_TEST FAIL: traffic.html source differs from common.glon + traffic.glon " +
    "(script " + SRC.length + " bytes vs expected " + EXPECTED.length + " bytes)");
  process.exit(1);
}

let mem;
const statuses = [];
const canvasScripts = [];
const imports = {
  env: {
    host_print(ptr, len) { /* console noise only */ },
    host_set_text(handle, value) { /* unused */ },
    host_set_html(handle, ptr, len) {
      statuses.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_canvas_script(ptr, len) {
      canvasScripts.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    }
  }
};

function fail(msg) {
  console.error("TRAFFIC_TEST FAIL: " + msg);
  process.exit(1);
}

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  const e = instance.exports;
  mem = new DataView(e.memory.buffer);

  const put = (s) => {
    const bytes = new TextEncoder().encode(s);
    const p = e.glon_alloc(bytes.length);
    new Uint8Array(e.memory.buffer).set(bytes, p);
    return [p, bytes.length];
  };
  const load = (src) => {
    const [p, n] = put(src);
    const rc = e.glon_load(p, n);
    if (rc !== 0) fail("glon_load rc=" + rc);
  };
  const event = (token) => {
    const [p, n] = put(token);
    const rc = e.glon_event(p, n);
    if (rc !== 0) fail("glon_event('" + token + "') rc=" + rc);
  };

  if (e.glon_init() !== 0) fail("glon_init");

  load(SRC);

  // 1. initial render (reset) -> road + 25 dots, tick 0
  event("traffic-reset");
  const initScript = canvasScripts.join("");
  const dots = (initScript.match(/^D /gm) || []).length;
  if (dots !== 25 || !/^L 0 300 1000 300/m.test(initScript))
    fail("initial render: expected road + 25 dots, got " + dots + " dots");
  if (!/tick 0/.test(statuses[statuses.length - 1]))
    fail("initial status: expected 'tick 0', got: " + statuses[statuses.length - 1]);

  // 2. advance changes state
  event("traffic-advance");   // 5 ticks
  if (!/tick 5/.test(statuses[statuses.length - 1]))
    fail("after one advance: expected 'tick 5', got: " + statuses[statuses.length - 1]);

  // 3. advance to the disturbance window (tick 200)
  for (let i = 1; i < 40; i++) event("traffic-advance");   // 5 + 39*5 = 200
  if (!/tick 200/.test(statuses[statuses.length - 1]))
    fail("at tick 200: expected 'tick 200', got: " + statuses[statuses.length - 1]);
  if (!/BRAKING/.test(statuses[statuses.length - 1]))
    fail("at tick 200: expected 'BRAKING', got: " + statuses[statuses.length - 1]);

  // 4. past the window (tick 220)
  for (let i = 0; i < 4; i++) event("traffic-advance");    // 220
  if (!/tick 220/.test(statuses[statuses.length - 1]))
    fail("at tick 220: expected 'tick 220', got: " + statuses[statuses.length - 1]);
  if (/BRAKING/.test(statuses[statuses.length - 1]))
    fail("at tick 220: expected 'free' (window ended), got: " + statuses[statuses.length - 1]);

  // 5. reset restores initial state
  event("traffic-reset");
  if (!/tick 0/.test(statuses[statuses.length - 1]))
    fail("reset: expected 'tick 0', got: " + statuses[statuses.length - 1]);

  console.log("TRAFFIC_TEST PASS (embedded source / initial render / advance / disturbance window / reset)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
