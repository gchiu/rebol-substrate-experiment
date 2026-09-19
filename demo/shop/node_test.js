// demo/shop/node_test.js -- headless verification of the Glon Shop G1C demo
// (no DOM, no browser, no Emscripten runtime).
//
// Instantiates demo/shop/glon.wasm with the three host imports, loads the
// bundled GLON source (demo/shop/bundle.glon, produced by build.py), then
// drives the router AND the event bridge exactly as the browser does and
// asserts the rendered HTML.
//
// This exercises the real WASM module + the G1C view dialect + generic event
// path + host bridge; the only thing stubbed is the DOM (host_set_html is
// captured instead of writing innerHTML).
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
    host_set_text(handle, value) { /* retained; unused */ },
    host_set_html(handle, ptr, len) {
      rendered.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    }
  }
};

function fail(msg) {
  console.error("GLON_G1C_TEST FAIL: " + msg);
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

  const event = (token) => {
    rendered.length = 0;
    const [p, n] = put(token);
    const rc = e.glon_event(p, n);
    if (rc !== 0) fail("glon_event('" + token + "') rc=" + rc);
    return rendered.join("");
  };

  if (e.glon_init() !== 0) fail("glon_init");

  const [p, n] = put(SRC);
  if (e.glon_load(p, n) !== 0) fail("glon_load");

  // 1. home route selects the home view
  const home = route("home");
  if (!/Glon Shop/.test(home) || !/data-glon-route='products'/.test(home))
    fail("home route: expected 'Glon Shop' + products button, got: " + home);

  // 2. products route shows visit 1 and cart 0
  const v1 = route("products");
  if (!/Products page visits: 1/.test(v1) || !/Cart: 0/.test(v1) || !/Tea/.test(v1) || !/Rice/.test(v1))
    fail("products visit 1: got: " + v1);

  // 3. add-one event increments cart (no re-render via route)
  const c1 = event("add-one");
  if (!/Cart: 1/.test(c1) || !/Products page visits: 1/.test(c1))
    fail("add-one -> cart 1: got: " + c1);

  const c2 = event("add-one");
  if (!/Cart: 2/.test(c2))
    fail("add-one -> cart 2: got: " + c2);

  // 4. navigate away and back; cart persists
  route("home");
  const v2 = route("products");
  if (!/Products page visits: 2/.test(v2) || !/Cart: 2/.test(v2))
    fail("products again: expected visits 2 cart 2, got: " + v2);

  // 5. unknown route -> not-found; unknown event -> current view (safe fallback)
  const nf = route("definitely-unknown");
  if (!/Not found/.test(nf))
    fail("unknown route: expected 'Not found', got: " + nf);

  console.log("GLON_G1C_TEST PASS (home / products / add-one x2 / persist / unknown)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
