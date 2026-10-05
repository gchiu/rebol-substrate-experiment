// demos/kaka/kaka_wasm_test.js -- headless WASM verification + soak for
// Attack of the Mutant Kaka (no DOM, no browser, no Emscripten runtime).
//
// Instantiates the real demo/shop/glon.wasm with the four host imports, loads
// prelude + strings + kaka-lib + kaka + kaka-draw + kaka-selftest exactly as
// kaka-host.js does, dispatches kaka-start / kaka-selftest, then sustains many
// kaka-tick events while holding fire and periodically launching a Glon Berry.
// It asserts the deterministic self-test PASS, that drawing keeps happening,
// that the host ABI allocator never fails, and that no exception escapes.
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const ROOT = path.resolve(HERE, "..", "..");
const WASM = path.join(ROOT, "demo", "shop", "glon.wasm");

const rendered = [];
const canvasScripts = [];
const logs = [];
let mem;

const imports = {
  env: {
    host_print(ptr, len) {
      logs.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_set_text() {},
    host_set_html(handle, ptr, len) {
      rendered.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_canvas_script(ptr, len) {
      canvasScripts.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    }
  }
};

function fail(msg) {
  console.error("KAKA_WASM_TEST FAIL: " + msg);
  process.exit(1);
}

const instr = { alloc: 0, allocFail: 0, tick: 0 };

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  const e = instance.exports;
  mem = new DataView(e.memory.buffer);

  const put = (s) => {
    const bytes = new TextEncoder().encode(s);
    const p = e.glon_alloc(bytes.length);
    instr.alloc++;
    if (p === 0) instr.allocFail++;
    new Uint8Array(e.memory.buffer).set(bytes, p);
    return [p, bytes.length];
  };

  const load = (src) => {
    const [p, n] = put(src);
    const rc = e.glon_load(p, n);
    if (rc !== 0) fail("glon_load rc=" + rc + " for source of " + n + " bytes");
  };

  const route = (token) => {
    rendered.length = 0;
    const [p, n] = put(token);
    if (e.glon_route(p, n) !== 0) fail("glon_route('" + token + "') failed");
    return rendered.join("");
  };

  const event = (token) => {
    rendered.length = 0;
    const [p, n] = put(token);
    if (e.glon_event(p, n) !== 0) fail("glon_event('" + token + "') failed");
    return rendered.join("");
  };

  const eventValue = (token, value) => {
    rendered.length = 0;
    const [tp, tn] = put(token);
    const [vp, vn] = put(value);
    if (e.glon_event_value(tp, tn, vp, vn) !== 0) fail("glon_event_value('" + token + "', '" + value + "') failed");
    return rendered.join("");
  };

  const state = () => {
    const m = /<span id='kaka-state'[^>]*>([^<]*)<\/span>/.exec(rendered.join(""));
    return m ? m[1].trim().split(/\s+/).map(Number) : [];
  };

  if (e.glon_init() !== 0) fail("glon_init");

  load(fs.readFileSync(path.join(ROOT, "glon-lib", "prelude.glon"), "utf8"));
  load(fs.readFileSync(path.join(ROOT, "glon-lib", "strings.glon"), "utf8"));
  load(fs.readFileSync(path.join(HERE, "kaka-lib.glon"), "utf8"));
  load(fs.readFileSync(path.join(HERE, "kaka.glon"), "utf8"));
  load(fs.readFileSync(path.join(HERE, "kaka-draw.glon"), "utf8"));
  load(fs.readFileSync(path.join(HERE, "kaka-selftest.glon"), "utf8"));

  const start = route("home");
  if (!/Attack of the Mutant Kaka/.test(start) || !/Eco-Warriors of Karori/.test(start))
    fail("initial render: " + start.slice(0, 160));

  const st = event("kaka-start");
  if (!/Trees: 3\/3/.test(st)) fail("kaka-start: expected 3 trees, got " + st.slice(0, 200));

  const self = event("kaka-selftest");
  if (!/KAKA-SELFTEST PASS/.test(self)) {
    const m = /KAKA-SELFTEST FAIL (\d+)/.exec(self);
    fail("selftest: " + (m ? m[1] + " assertions failed" : self));
  }

  // soak: hold fire + glon berry and run many real ticks, cycling restarts.
  eventValue("kaka-key-down", "fire");
  const SOAK = Number(process.env.KAKA_SOAK_TICKS || 1200);
  let maxBerries = 0, mutants = 0, restarts = 0, drawTicks = 0;
  const t0 = process.hrtime.bigint();
  for (let i = 0; i < SOAK; i++) {
    if (i % 300 === 150) eventValue("kaka-key-down", "glon");
    if (i % 300 === 160) eventValue("kaka-key-up", "glon");
    canvasScripts.length = 0;
    event("kaka-tick");
    instr.tick++;
    if (canvasScripts.join("").indexOf("\n") >= 0) drawTicks++;
    const s = state();
    if (s.length >= 13) {
      if (s[3] > maxBerries) maxBerries = s[3];
      if (s[11] === 1) mutants++;
    }
    if (i % 500 === 499) { event("kaka-restart"); restarts++; }
  }
  eventValue("kaka-key-up", "fire");
  const t1 = process.hrtime.bigint();
  const msPerTick = Number(t1 - t0) / 1e6 / SOAK;

  if (instr.allocFail !== 0) fail("allocFail=" + instr.allocFail);
  if (drawTicks < SOAK) fail("drawing stopped: " + drawTicks + "/" + SOAK + " ticks drew");
  if (canvasScripts.length === 0 && SOAK > 0) fail("no canvas script captured");

  // restart must return a clean, still-live game with controls still wired
  const after = event("kaka-restart");
  eventValue("kaka-key-down", "right");
  const moved = event("kaka-tick");
  if (!/Trees: 3\/3/.test(after)) fail("restart did not reset trees");
  const s2 = state();
  if (s2.length < 13 || s2[4] !== 326) fail("controls dead after restart (px=" + (s2[4]) + ")");

  console.log("KAKA_WASM_TEST PASS (selftest + " + SOAK + " tick soak, allocFail=0, " +
    msPerTick.toFixed(3) + " ms/tick, " + restarts + " restarts, mutants-seen=" + mutants +
    ", ticks-drew=" + drawTicks + ", maxBerries=" + maxBerries + ")");
  process.exit(0);
}).catch((e) => fail(e && e.stack ? e.stack : e));
