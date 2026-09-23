// demo/shop/linda_node_test.js -- headless verification of the standalone Linda
// page wiring (no DOM, no browser, no Emscripten runtime).
//
// Mirrors traffic_node_test.js: it instantiates demo/shop/glon.wasm with the
// four host imports, extracts the bundled source from linda.html's main
// <script type="application/glon"> block, glon_load's it, then drives the demo
// through the same glon_event bridge linda-host.js uses, asserting:
//   1. the bundle is exactly the self-contained demos/linda.glon;
//   2. Reset renders A/B/C RUNNABLE, an empty tuple space, no waiters;
//   3. one Step reaches A WAITING at IN [go a] (tuple [started a] present);
//   4. the next Step lets B run while A is blocked: B observes stage-a = 1 and
//      OUTs [go a], which makes A RUNNABLE;
//   5. A resumes AFTER its IN and reaches stage-a = 2, then FINISHES;
//   6. B and C complete;
//   7. the final tuple/waiter state is correct and deterministic across
//      Reset + Run.
// This file implements no Linda semantics: every state it checks was rendered
// by Glon.
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon.wasm");
const HTML = fs.readFileSync(path.join(HERE, "linda.html"), "utf8");
const LINDA = fs.readFileSync(path.join(HERE, "demos", "linda.glon"), "utf8");

function stripComments(s) {
  return s.replace(/\n+$/, "").split("\n").map(l => l.split(";;")[0]).join("\n");
}

const mainMatch = /<script type="application\/glon">([\s\S]*?)<\/script>/.exec(HTML);
if (!mainMatch) {
  console.error("LINDA_TEST FAIL: linda.html has no main <script type=\"application/glon\"> block");
  process.exit(1);
}
const SRC = mainMatch[1];
const EXPECTED = stripComments(LINDA);
if (SRC !== EXPECTED) {
  console.error("LINDA_TEST FAIL: linda.html bundle differs from demos/linda.glon " +
    "(script " + SRC.length + " bytes vs expected " + EXPECTED.length + " bytes)");
  process.exit(1);
}

let mem;
const statuses = [];
const imports = {
  env: {
    host_print(ptr, len) { /* console noise only */ },
    host_set_text(handle, value) { /* unused */ },
    host_set_html(handle, ptr, len) {
      statuses.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_canvas_script(ptr, len) { /* this page draws no canvas */ }
  }
};

function fail(msg) { console.error("LINDA_TEST FAIL: " + msg); process.exit(1); }
function last() { return statuses[statuses.length - 1] || ""; }
function space(s) { const m = /tuple space: ([^<]*)</.exec(s); return m ? m[1] : ""; }
function waiters(s) { const m = /waiters: ([^<]*)</.exec(s); return m ? m[1] : ""; }

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  const e = instance.exports;
  mem = new DataView(e.memory.buffer);

  const put = (s) => {
    const bytes = new TextEncoder().encode(s);
    const p = e.glon_alloc(bytes.length);
    new Uint8Array(e.memory.buffer).set(bytes, p);
    return [p, bytes.length];
  };
  const load = (src) => { const [p, n] = put(src); if (e.glon_load(p, n) !== 0) fail("glon_load rc"); };
  const event = (token) => {
    const [p, n] = put(token);
    const rc = e.glon_event(p, n);
    if (rc !== 0) fail("glon_event('" + token + "') rc=" + rc);
  };

  if (e.glon_init() !== 0) fail("glon_init");
  load(SRC);

  // 2. reset -> all three tasks runnable, empty space, no waiters
  event("linda-reset");
  let s = last();
  for (const t of ["A: RUNNABLE", "B: RUNNABLE", "C: RUNNABLE"]) {
    if (s.indexOf(t) < 0) fail("reset: expected '" + t + "', got:\n" + s);
  }
  if (s.indexOf("tuple space: (empty)") < 0) fail("reset: expected empty tuple space, got:\n" + s);
  if (s.indexOf("waiters: (none)") < 0) fail("reset: expected no waiters, got:\n" + s);
  if (s.indexOf("STATUS: RUNNING") < 0) fail("reset: expected RUNNING, got:\n" + s);
  if (s.indexOf("stage-a = 0") < 0) fail("reset: expected stage-a = 0, got:\n" + s);

  // 3. step 1 -> A blocks at IN [go a]
  event("linda-step");
  s = last();
  if (s.indexOf("A: WAITING") < 0) fail("step 1: expected A WAITING (A must genuinely block), got:\n" + s);
  if (s.indexOf("B: RUNNABLE") < 0 || s.indexOf("C: RUNNABLE") < 0) fail("step 1: B and C should still be runnable, got:\n" + s);
  if (space(s).indexOf("[ started a ]") < 0) fail("step 1: expected tuple [started a], got:\n" + s);
  if (waiters(s).indexOf("IN [ go a ] waiting") < 0) fail("step 1: expected an IN [go a] waiter, got:\n" + s);

  // 4. step 2 -> B runs while A is blocked, observes stage-a=1, OUTs [go a] (wakes A)
  event("linda-step");
  s = last();
  if (s.indexOf("B observed stage-a = 1") < 0) fail("step 2: B should have observed A at stage-a=1 (A blocked), got:\n" + s);
  if (s.indexOf("A: RUNNABLE") < 0) fail("step 2: OUT [go a] should have made A RUNNABLE, got:\n" + s);
  if (s.indexOf("B: WAITING") < 0) fail("step 2: B should now be waiting on IN [finished a], got:\n" + s);
  if (space(s).indexOf("[ started b ]") < 0) fail("step 2: expected tuple [started b], got:\n" + s);
  if (space(s).indexOf("go a") >= 0) fail("step 2: [go a] should have been consumed by A's IN, got:\n" + s);
  if (waiters(s).indexOf("[ go a ] done") < 0) fail("step 2: A's [go a] waiter should be done, got:\n" + s);

  // 5. steps 3..6 -> C waits; A resumes AFTER IN and finishes; then B, then C
  event("linda-step");   // C blocks on IN [finished b]
  s = last();
  if (s.indexOf("C: WAITING") < 0) fail("step 3: expected C WAITING, got:\n" + s);

  event("linda-step");   // A resumes after IN, stage-a=2, OUT [finished a], finishes
  s = last();
  if (s.indexOf("A: FINISHED") < 0) fail("step 4: expected A FINISHED, got:\n" + s);
  if (s.indexOf("stage-a = 2") < 0) fail("step 4: expected stage-a = 2 (A resumed after IN), got:\n" + s);
  if (s.indexOf("B: RUNNABLE") < 0) fail("step 4: OUT [finished a] should have made B RUNNABLE, got:\n" + s);

  event("linda-step");   // B resumes after IN, OUT [finished b], finishes
  s = last();
  if (s.indexOf("B: FINISHED") < 0) fail("step 5: expected B FINISHED, got:\n" + s);
  if (s.indexOf("C: RUNNABLE") < 0) fail("step 5: OUT [finished b] should have made C RUNNABLE, got:\n" + s);

  event("linda-step");   // C resumes after IN, finishes
  s = last();
  if (s.indexOf("C: FINISHED") < 0) fail("step 6: expected C FINISHED, got:\n" + s);
  if (s.indexOf("STATUS: COMPLETE") < 0) fail("step 6: expected COMPLETE, got:\n" + s);
  if (space(s).indexOf("[ started a ]") < 0 || space(s).indexOf("[ started b ]") < 0)
    fail("final: expected remaining tuples [started a] [started b], got:\n" + s);
  const finalRun = s;

  // 6. determinism: Reset + the same six steps reproduces the exact final render
  event("linda-reset");
  for (const t of ["A: RUNNABLE", "B: RUNNABLE", "C: RUNNABLE"]) {
    if (last().indexOf(t) < 0) fail("re-run reset: expected '" + t + "'");
  }
  for (let i = 0; i < 6; i++) event("linda-step");
  if (last() !== finalRun) fail("determinism: Reset + Run produced a different final render");

  console.log("LINDA_TEST PASS (embedded source / reset / A blocks at IN / B runs while A blocked / " +
              "OUT [go a] wakes A / A resumes after IN to stage 2 / all tasks finish / deterministic)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
