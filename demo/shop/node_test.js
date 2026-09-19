// demo/shop/node_test.js -- headless verification of the Glon Shop G1A demo
// (no DOM, no browser, no Emscripten runtime).
//
// Instantiates demo/shop/glon.wasm with the three host imports, loads the
// bundled GLON source (demo/shop/bundle.glon, produced by build.py), then
// drives the router exactly as the browser does and asserts the rendered HTML.
//
// This exercises the real WASM module + the G1A host bridge; the only thing
// stubbed is the DOM (host_set_html is captured instead of writing innerHTML).
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon.wasm");
const SRC = fs.readFileSync(path.join(HERE, "bundle.glon"), "utf8");

const rendered = [];       // host_set_html(handle, html) captures
const logs = [];
let mem;

const imports = {
  env: {
    host_print(ptr, len) {
      logs.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_set_text(handle, value) { /* retained; unused by G1A */ },
    host_set_html(handle, ptr, len) {
      rendered.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    }
  }
};

function fail(msg) {
  console.error("GLON_G1A_TEST FAIL: " + msg);
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

  const route = (token) => {
    rendered.length = 0;
    const [p, n] = put(token);
    const rc = e.glon_route(p, n);
    if (rc !== 0) fail("glon_route('" + token + "') rc=" + rc);
    return rendered.join("");
  };

  if (e.glon_init() !== 0) fail("glon_init");

  const [p, n] = put(SRC);
  if (e.glon_load(p, n) !== 0) fail("glon_load");

  // 1. home route selects the home fragment
  const home = route("home");
  if (!/Glon Shop/.test(home) || !/data-glon-route="products"/.test(home))
    fail("home route: expected 'Glon Shop' + products button, got: " + home);

  // 2..4. products route increments visit-count across entries
  const v1 = route("products");
  if (!/Products page visits: 1/.test(v1) || !/Tea/.test(v1) || !/Rice/.test(v1))
    fail("products visit 1: got: " + v1);

  route("home"); // back home (state must survive)

  const v2 = route("products");
  if (!/Products page visits: 2/.test(v2))
    fail("products visit 2: got: " + v2);

  // 5. unknown route renders not-found
  const nf = route("definitely-unknown");
  if (!/Not found/.test(nf))
    fail("unknown route: expected 'Not found', got: " + nf);

  console.log("GLON_G1A_TEST PASS (home / products x2 visits / unknown -> not found)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
