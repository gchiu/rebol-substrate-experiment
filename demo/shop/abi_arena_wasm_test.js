// demo/shop/abi_arena_wasm_test.js -- generic WASM regression for the host
// ABI argument-buffer arena (D11.6).
//
// Loads the real G1A WASM module (built from standalone/glon.c) and hammers the
// host->WASM bridge: every iteration allocates a token buffer and a value
// buffer with glon_alloc and delivers them with glon_event_value.  This is the
// generic detector for the class of defect where the allocator backing
// glon_alloc is not reclaimed between execution entries: allocation usage must
// stay bounded, token/value buffers must not alias, and events must keep
// dispatching correctly.  A long value-less glon_event run (and a large single
// allocation) is included as well.
//
// The app loads the notebook prelude (mk-string / str-eq) and defines a tiny
// byte-list emitter so do-event returns a real render fragment: 'M' for the
// valued event, 'T' for the value-less event.
//
// Usage: node abi_arena_wasm_test.js [path/to/glon.wasm]
// (default: demo/shop/glon.wasm)
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = process.argv[2] || path.join(HERE, "glon.wasm");
const BOOTSTRAP = fs.readFileSync(path.join(HERE, "..", "..", "jupyter", "prelude.glon"), "utf8");
const ITERATIONS = Number(process.env.ABI_TEST_ITERATIONS || 10000);

const APP = [
  "[",
  "emit-clear: masm [ LIT 0 LIT SCRATCH_E ! ARITY 0 EXIT ]",
  "emit-byte: masm 1 [ LIT SCRATCH_E @ LIT G1_OUT_DATA ADD ! LIT SCRATCH_E @ LIT 1 ADD LIT SCRATCH_E ! ARITY 0 EXIT ]",
  "emit-finish: masm [ LIT SCRATCH_E @ LIT G1_OUT ! LIT G1_OUT LIT T_BLOCK ADD ARITY 1 EXIT ]",
  "do-event: [",
  "    emit-clear",
  "    either = current-event 'evt [",
  "        either str-eq current-value \"hello\" [ emit-byte 77 ] [ emit-byte 66 ]",
  "    ] [",
  "        either = current-event 'tick [ emit-byte 84 ] [ emit-byte 79 ]",
  "    ]",
  "    emit-finish",
  "]",
  "route: [ emit-clear emit-byte 82 emit-finish ]",
  "]"
].join("\n");

let mem = null;
let lastHtml = "";
const imports = { env: {
  host_print() {}, host_set_text() {},
  host_set_html(h, ptr, len) { lastHtml = new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)); },
  host_canvas_script() {}
} };

function fail(msg) { console.error("ABI_ARENA_WASM_TEST FAIL: " + msg); process.exit(1); }

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  const e = instance.exports;
  mem = new DataView(e.memory.buffer);
  const view = () => new Uint8Array(e.memory.buffer);
  const enc = new TextEncoder();
  let allocCalls = 0, allocFails = 0;
  function put(s) {
    const b = enc.encode(s);
    const p = e.glon_alloc(b.length);
    allocCalls++;
    if (p === 0) { allocFails++; return { p: 0, n: b.length }; }
    view().set(b, p);
    return { p, n: b.length };
  }

  if (e.glon_init() !== 0) fail("glon_init");
  const boot = put(BOOTSTRAP);
  if (e.glon_load(boot.p, boot.n) !== 0) fail("glon_load(prelude.glon)");
  const app = put(APP);
  if (e.glon_load(app.p, app.n) !== 0) fail("glon_load(abi app)");

  // --- sustained valued events: alloc(token) + alloc(value) + glon_event_value
  let alias = 0, rcBad = 0, badHtml = 0;
  for (let i = 0; i < ITERATIONS; i++) {
    const t = put("evt");
    const v = put("hello");
    if (t.p === 0 || v.p === 0) { alias++; continue; }              /* allocation failure */
    /* token/value must be distinct, non-overlapping live buffers */
    if (t.p < v.p + v.n && v.p < t.p + t.n) alias++;
    lastHtml = "";
    const rc = e.glon_event_value(t.p, t.n, v.p, v.n);
    if (rc !== 0) rcBad++;
    else if (lastHtml !== "M") badHtml++;
  }

  // --- sustained value-less events: alloc(token) + glon_event
  let evRcBad = 0, evBadHtml = 0;
  for (let i = 0; i < ITERATIONS; i++) {
    const t = put("tick");
    if (t.p === 0) continue;
    lastHtml = "";
    const rc = e.glon_event(t.p, t.n);
    if (rc !== 0) evRcBad++;
    else if (lastHtml !== "T") evBadHtml++;
  }

  if (allocFails !== 0) fail("glon_alloc failures: " + allocFails + " / " + allocCalls);
  if (alias !== 0) fail("token/value aliasing (or alloc failure) in " + alias + " valued iterations");
  if (rcBad !== 0) fail("glon_event_value rc!=0 in " + rcBad + " iterations");
  if (badHtml !== 0) fail("glon_event_value produced wrong fragment in " + badHtml + " iterations");
  if (evRcBad !== 0) fail("glon_event rc!=0 in " + evRcBad + " iterations");
  if (evBadHtml !== 0) fail("glon_event produced wrong fragment in " + evBadHtml + " iterations");

  // --- a single large argument buffer must still fit (arena >= 64 KiB)
  const big = e.glon_alloc(60000);
  if (big === 0) fail("glon_alloc(60000) returned 0 (arena too small)");
  const after = put("tick");
  lastHtml = "";
  const rcAfter = e.glon_event(after.p, after.n);
  if (rcAfter !== 0 || lastHtml !== "T")
    fail("event after a 60000-byte allocation did not dispatch (rc=" + rcAfter + ", html=" + JSON.stringify(lastHtml) + ")");

  console.log("ABI_ARENA_WASM_TEST PASS (" + allocCalls + " glon_alloc calls, " + ITERATIONS +
    " valued + " + ITERATIONS + " value-less events, 0 failures, 0 aliasing, 60000-byte buffer ok)");
  process.exit(0);
}).catch((e) => fail(e.stack || e.message || e));
