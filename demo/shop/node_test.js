// demo/shop/node_test.js -- headless verification of the Glon Shop G1E demo
// (no DOM, no browser, no Emscripten runtime).
//
// Instantiates demo/shop/glon.wasm with the three host imports, extracts the
// GLON source from the generated demo/shop/app.html <script type="application/glon">
// block (exactly as the browser's script.textContent would, with no character
// reference decoding), then drives the router AND the event bridge (token and
// token+value) exactly as the browser does and asserts the rendered HTML.
//
// This exercises the real WASM module + the G1E view dialect + repeated interactive composition
// path (with values) + host bridge; the only thing stubbed is the DOM
// (host_set_html is captured instead of writing innerHTML).
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon.wasm");
const APP_HTML = fs.readFileSync(path.join(HERE, "app.html"), "utf8");
const BUNDLE = fs.readFileSync(path.join(HERE, "bundle.glon"), "utf8");

// Regression guard for the build.py -> <script> boundary.  A <script> element's
// content is raw text: the HTML parser does not decode character references,
// and the browser's script.textContent is exactly the bytes between the tags.
// The source glon_load receives in the browser must therefore be byte-identical
// to bundle.glon (any html.escape() here would corrupt it: 'home -> &#x27;home,
// < -> &lt;, etc.).  Extract the raw text exactly as textContent would.
const scriptMatch = /<script type="application\/glon">([\s\S]*?)<\/script>/.exec(APP_HTML);
if (!scriptMatch) {
  console.error("GLON_G1E_TEST FAIL: app.html has no <script type=\"application/glon\"> block");
  process.exit(1);
}
const SRC = scriptMatch[1];
if (SRC !== BUNDLE) {
  console.error("GLON_G1E_TEST FAIL: app.html script textContent differs from bundle.glon " +
    "(script " + SRC.length + " bytes vs bundle " + BUNDLE.length + " bytes)");
  process.exit(1);
}

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
  console.error("GLON_G1E_TEST FAIL: " + msg);
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

  const eventValue = (token, value) => {
    rendered.length = 0;
    const [tp, tn] = put(token);
    const [vp, vn] = put(value);
    const rc = e.glon_event_value(tp, tn, vp, vn);
    if (rc !== 0) fail("glon_event_value('" + token + "', '" + value + "') rc=" + rc);
    return rendered.join("");
  };

  if (e.glon_init() !== 0) fail("glon_init");

  const [p, n] = put(SRC);
  if (e.glon_load(p, n) !== 0) fail("glon_load");

  // 1. home route selects the launcher view, offering all three demos
  const home = route("home");
  if (!/Glon Demos/.test(home) ||
      !/data-glon-route='shop'/.test(home) ||
      !/data-glon-route='guide'/.test(home) ||
      !/data-glon-route='merchant-flow'/.test(home))
    fail("home route: expected 'Glon Demos' launcher + three links, got: " + home);

  // 2. shop route renders the shop landing
  const shop = route("shop");
  if (!/Glon Shop/.test(shop) || !/data-glon-route='products'/.test(shop))
    fail("shop route: expected 'Glon Shop' + products button, got: " + shop);

  // 3. guide route renders the programmer's guide
  const guide = route("guide");
  if (!/Programmer's guide/.test(guide))
    fail("guide route: expected 'Programmer's guide', got: " + guide);

  // 4. merchant-flow route renders the Julia page + source link
  const mf = route("merchant-flow");
  if (!/Julia merchant flow/.test(mf) || !/merchant_flow\.jl/.test(mf))
    fail("merchant-flow route: expected 'Julia merchant flow' + source link, got: " + mf);

  // 5. back to home returns to the launcher
  const back = route("home");
  if (!/Glon Demos/.test(back))
    fail("back home: expected 'Glon Demos', got: " + back);

  // 6. products route shows visit 1, an empty basket, and the repeated list
  const v1 = route("products");
  if (!/Products page visits: 1/.test(v1) || !v1.includes("0 items") ||
      !/data-glon-value='Tea'/.test(v1) || !/data-glon-value='Rice'/.test(v1))
    fail("products visit 1: got: " + v1);

  // 7. add-product for Tea / Rice / Tea (per-product basket quantities)
  const c1 = eventValue("add-product", "Tea");
  if (!c1.includes("Tea</span><span class='qty'>× 1</span>") || !c1.includes("1 item"))
    fail("add Tea -> Tea x1: got: " + c1);

  const c2 = eventValue("add-product", "Rice");
  if (!c2.includes("Tea</span><span class='qty'>× 1</span>") ||
      !c2.includes("Rice</span><span class='qty'>× 1</span>") || !c2.includes("2 items"))
    fail("add Rice -> Tea x1, Rice x1: got: " + c2);

  const c3 = eventValue("add-product", "Tea");
  if (!c3.includes("Tea</span><span class='qty'>× 2</span>") ||
      !c3.includes("Rice</span><span class='qty'>× 1</span>") || !c3.includes("3 items"))
    fail("add Tea again -> Tea x2, Rice x1, 3 items: got: " + c3);

  // 8. search event with a value (G1D)
  const s1 = eventValue("search", "green tea");
  if (!/Search: green tea/.test(s1))
    fail("search 'green tea': got: " + s1);

  // 9. navigate away and back; basket persists
  route("home");
  const v2 = route("products");
  if (!/Products page visits: 2/.test(v2) ||
      !v2.includes("Tea</span><span class='qty'>× 2</span>") ||
      !v2.includes("Rice</span><span class='qty'>× 1</span>") || !v2.includes("3 items"))
    fail("products again: expected visits 2, Tea x2, Rice x1, 3 items: got: " + v2);

  // 10. unknown route -> not-found (safe fallback)
  const nf = route("definitely-unknown");
  if (!/Not found/.test(nf))
    fail("unknown route: expected 'Not found', got: " + nf);

  console.log("GLON_G1E_TEST PASS (launcher / shop / guide / merchant-flow / back / add-product x3 / basket quantities / search / persist / unknown)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
