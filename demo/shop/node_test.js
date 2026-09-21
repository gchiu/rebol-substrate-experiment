// demo/shop/node_test.js -- headless verification of the Glon Demos launcher
// + load-on-demand demos (no DOM, no browser, no Emscripten runtime).
//
// Instantiates demo/shop/glon.wasm with the three host imports, extracts the
// bootstrap source from demo/shop/app.html <script type="application/glon">
// (exactly as the browser's script.textContent would), then loads each demo
// demo/shop/demos/*.glon via glon_load on first selection -- the same path the
// browser host's [data-glon-load] fetch + glon_load + re-route performs -- and
// asserts the rendered HTML.
//
// This exercises the real WASM module + the view dialect + the event bridge +
// load-on-demand composition; the only thing stubbed is the DOM (host_set_html
// is captured instead of writing innerHTML) and the network fetch (the demo
// file is read from disk and handed to glon_load, exactly as the fetched text
// would be).
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon.wasm");
const APP_HTML = fs.readFileSync(path.join(HERE, "app.html"), "utf8");
const BOOTSTRAP = fs.readFileSync(path.join(HERE, "bootstrap.glon"), "utf8");

// Regression guard for the build.py -> <script> boundary.  A <script> element's
// content is raw text: the HTML parser does not decode character references,
// and the browser's script.textContent is exactly the bytes between the tags.
// The source glon_load receives in the browser must therefore be byte-identical
// to bootstrap.glon (any html.escape() here would corrupt it).
const scriptMatch = /<script type="application\/glon">([\s\S]*?)<\/script>/.exec(APP_HTML);
if (!scriptMatch) {
  console.error("GLON_G1E_TEST FAIL: app.html has no <script type=\"application/glon\"> block");
  process.exit(1);
}
const SRC = scriptMatch[1];
if (SRC !== BOOTSTRAP) {
  console.error("GLON_G1E_TEST FAIL: app.html script textContent differs from bootstrap.glon " +
    "(script " + SRC.length + " bytes vs bootstrap " + BOOTSTRAP.length + " bytes)");
  process.exit(1);
}

const rendered = [];       // host_set_html(handle, html) captures
const canvasScripts = [];  // host_canvas_script(ptr, len) captures
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
    },
    host_canvas_script(ptr, len) {
      canvasScripts.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
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

  const load = (src) => {
    const [p, n] = put(src);
    const rc = e.glon_load(p, n);
    if (rc !== 0) fail("glon_load rc=" + rc + " for source:\n" + src.slice(0, 200));
  };

  const loadDemo = (name) => {
    load(fs.readFileSync(path.join(HERE, "demos", name + ".glon"), "utf8"));
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

  load(SRC);

  // 1. home route selects the launcher, offering all three demos via
  //    load-on-demand links (the demos are not yet loaded)
  const home = route("home");
  if (!/Glon Demos/.test(home) ||
      !/data-glon-load='demos\/shop\.glon'/.test(home) ||
      !/data-glon-load='demos\/guide\.glon'/.test(home) ||
      !/data-glon-load='demos\/merchant-flow\.glon'/.test(home) ||
      !/data-glon-load='demos\/tuple-space\.glon'/.test(home))
    fail("home route: expected 'Glon Demos' launcher + load-links, got: " + home);

  // 2. selecting Shop loads shop.glon and renders the shop landing
  loadDemo("shop");
  const shop = route("shop");
  if (!/Glon Shop/.test(shop) || !/data-glon-route='products'/.test(shop))
    fail("shop route: expected 'Glon Shop' + products button, got: " + shop);

  // 3. already-loaded demo is a plain link (no reload)
  const home2 = route("home");
  if (!/data-glon-route='shop'/.test(home2) ||
      /data-glon-load='demos\/shop\.glon'/.test(home2))
    fail("home after shop: expected shop to be a plain link, got: " + home2);

  // 4. selecting Guide loads guide.glon and renders it
  loadDemo("guide");
  const guide = route("guide");
  if (!/Programmer's guide/.test(guide))
    fail("guide route: expected 'Programmer's guide', got: " + guide);

  // 5. selecting Merchant flow loads merchant-flow.glon and renders it
  loadDemo("merchant-flow");
  const mf = route("merchant-flow");
  if (!/multitasking/.test(mf) || !/data-glon-event='start'/.test(mf))
    fail("merchant-flow route: expected 'multitasking' + Start control, got: " + mf);

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

  // 9. navigate away and back; basket persists (across loaded demos)
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

  // 11. load-failed event -> controlled error (not a hung machine)
  const err = event("load-failed");
  if (!/Load failed/.test(err))
    fail("load-failed event: expected 'Load failed', got: " + err);

  // 12. tuple-space demo: the canvas script follows the real Glon path
  loadDemo("tuple-space");
  canvasScripts.length = 0;
  const ts = route("tuple-space");
  const tsTopo = canvasScripts.join("");
  if (!/glon-canvas/.test(ts) ||
      !/B 420 40 160 36 Transaction space/.test(tsTopo) ||
      !/B 250 200 120 36 W1/.test(tsTopo))
    fail("tuple-space route: expected canvas topology, got: " + tsTopo);

  canvasScripts.length = 0;
  const tsRun = event("run");
  const tsFlow = canvasScripts.join("");
  if (!/M 0 500 58 310 218/.test(tsFlow) ||   // token 0 -> worker 1 (W1)
      !/M 0 310 218 500 378/.test(tsFlow) ||  // -> router
      !/M 0 500 378 310 538/.test(tsFlow))    // -> Stock
    fail("tuple-space run: expected token 0 path space->W1->router->Stock, got: " + tsFlow);

  canvasScripts.length = 0;
  const tsReset = event("space-reset");
  const tsResetScript = canvasScripts.join("");
  if (!/B 420 40 160 36 Transaction space/.test(tsResetScript) || /M /.test(tsResetScript))
    fail("tuple-space reset: expected topology-only canvas, got: " + tsResetScript);

  console.log("GLON_G1E_TEST PASS (launcher / lazy shop / lazy guide / lazy merchant-flow / basket / search / persist / unknown / load-failed / tuple-space canvas)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
