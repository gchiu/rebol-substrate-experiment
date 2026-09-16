// standalone/node_test.js -- headless verification of the standalone WASM
// demo (no DOM, no browser, no Emscripten runtime).
//
// Instantiates standalone/glon.wasm directly with the two host imports,
// loads the authoritative source standalone/app.glon, then calls the
// `increment` word three times and asserts the counter emits host_set_text
// (handle=1, value=1/2/3).  This exercises the exact R0 application path the
// browser uses, without a DOM.
"use strict";

const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const WASM = path.join(__dirname, "glon.wasm");
const SRC = fs.readFileSync(path.join(__dirname, "app.glon"), "utf8");

const calls = [];
const logs = [];
let mem;

const imports = {
  env: {
    host_print(ptr, len) {
      logs.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_set_text(handle, value) {
      calls.push([handle, value]);
    }
  }
};

function fail(msg) {
  console.error("GLON_TEST FAIL: " + msg);
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

  if (e.glon_init() !== 0) fail("glon_init");

  const [p, n] = put(SRC);
  if (e.glon_load(p, n) !== 0) fail("glon_load");

  for (let i = 0; i < 3; i++) {
    const [q, m] = put("increment");
    if (e.glon_call(q, m) !== 0) fail("glon_call click " + (i + 1));
  }

  const expected = [[1, 1], [1, 2], [1, 3]];
  const ok =
    calls.length === 3 &&
    expected.every((ev, i) => calls[i][0] === ev[0] && calls[i][1] === ev[1]);

  if (!ok) fail("expected " + JSON.stringify(expected) + ", got " + JSON.stringify(calls));

  console.log("GLON_TEST PASS (3 clicks -> counter 3)");
  if (logs.length) console.log("host_print:", JSON.stringify(logs));
  process.exit(0);
}).catch((e) => fail(e.message || e));
