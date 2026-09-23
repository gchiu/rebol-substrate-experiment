/* demo/shop/linda-host.js -- the minimal browser host for the standalone Linda
 * page (linda.html).
 *
 * JavaScript responsibilities are strictly browser capabilities:
 *
 *   - instantiate demo/shop/glon.wasm (the existing G1A runtime, unchanged);
 *   - glon_init, then glon_load the bundled Glon source inlined in the page;
 *   - map the Reset / Step / Run controls to glon_event("linda-reset" /
 *     "linda-step"); Run just paces repeated Step events on a timer until the
 *     Glon-rendered state says COMPLETE;
 *   - write the HTML Glon rendered into [data-glon-id="1"].
 *
 * There is NO Linda semantics here: no tuple matching, no waiter ordering, no
 * task states, no block/wakeup.  Those are decided entirely by the Glon program
 * (demo/shop/demos/linda.glon); JS only decides *when* to ask Glon to advance
 * one real task slice.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var running = false;
  var timer = null;

  function view() { return new Uint8Array(ex.memory.buffer); }

  var imports = {
    env: {
      host_print: function (ptr, len) {
        console.log("[glon]", dec.decode(view().subarray(ptr, ptr + len)));
      },
      host_set_text: function (handle, value) {
        var el = document.querySelector('[data-glon-id="' + handle + '"]');
        if (el) el.textContent = String(value);
      },
      host_set_html: function (handle, ptr, len) {
        var el = document.querySelector('[data-glon-id="' + handle + '"]');
        if (el) el.innerHTML = dec.decode(view().subarray(ptr, ptr + len));
      },
      host_canvas_script: function (ptr, len) { /* this page draws no canvas */ }
    }
  };

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function event(token) {
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("linda-host.js: glon_event('" + token + "') rc=" + rc);
  }

  function statusText() {
    var el = document.querySelector('[data-glon-id="1"]');
    return el ? el.textContent : "";
  }

  var resetBtn, stepBtn, runBtn;

  function stopRun() {
    running = false;
    if (timer) { clearTimeout(timer); timer = null; }
    if (runBtn) runBtn.textContent = "Run";
  }

  function tick() {
    timer = null;
    if (!running) return;
    event("linda-step");
    if (/COMPLETE/.test(statusText())) { stopRun(); return; }
    timer = setTimeout(tick, 600);
  }

  function boot() {
    if (ex.glon_init() !== 0) { console.error("linda-host.js: glon_init failed"); return; }
    var src = document.querySelector('script[type="application/glon"]');
    if (!src) { console.error("linda-host.js: no <script type=\"application/glon\"> found"); return; }
    var pair = alloc(src.textContent);
    if (ex.glon_load(pair[0], pair[1]) !== 0) { console.error("linda-host.js: glon_load failed"); return; }

    resetBtn = document.querySelector("#reset");
    stepBtn = document.querySelector("#step");
    runBtn = document.querySelector("#run");

    resetBtn.addEventListener("click", function () { stopRun(); event("linda-reset"); });
    stepBtn.addEventListener("click", function () { stopRun(); event("linda-step"); });
    runBtn.addEventListener("click", function () {
      if (running) { stopRun(); return; }
      running = true;
      runBtn.textContent = "Pause";
      tick();
    });

    event("linda-reset");   // initial render
  }

  function ready(result) { ex = result.instance.exports; boot(); }
  function fail(err) { console.error("linda-host.js: failed to load glon.wasm", err); }
  function loadBytes(bytes) { WebAssembly.instantiate(bytes, imports).then(ready).catch(fail); }

  if (typeof WebAssembly.instantiateStreaming === "function" && window.fetch) {
    WebAssembly.instantiateStreaming(fetch("glon.wasm"), imports)
      .then(ready)
      .catch(function () {
        fetch("glon.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
      });
  } else if (window.fetch) {
    fetch("glon.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
  } else {
    fail(new Error("no fetch / WebAssembly support"));
  }
})();
