/* standalone/glon.js -- the handwritten JavaScript half of the standalone
 * WebAssembly boundary.
 *
 * This file REPLACES the generated Emscripten runtime from W1 (web/demo.js):
 * it instantiates the raw glon.wasm (built with `-nostdlib`, no Emscripten JS
 * runtime, no virtual filesystem) and supplies only the two host imports the
 * C side declares.  JavaScript responsibilities remain strictly limited to:
 *
 *   - host_print:  capture the host's plain text output (console.log);
 *   - host_set_text: decode a numeric DOM handle and write the value into the
 *     element tagged with data-glon-id="<handle>";
 *   - feed the R0/GLON source (inlined as <script type="application/glon">)
 *     into glon_load once at startup;
 *   - forward clicks on [data-glon-click="<word>"] to glon_call.
 *
 * No counter state or application logic lives here.
 */
(function () {
  "use strict";

  var ex = null;                 /* WebAssembly instance exports */
  var dec = new TextDecoder();
  var enc = new TextEncoder();

  /* The WASM memory can grow (ALLOW_MEMORY_GROWTH); always take a fresh view. */
  function view() {
    return new Uint8Array(ex.memory.buffer);
  }

  var imports = {
    env: {
      host_print: function (ptr, len) {
        var bytes = view().subarray(ptr, ptr + len);
        console.log("[glon]", dec.decode(bytes));
      },
      host_set_text: function (handle, value) {
        var el = document.querySelector('[data-glon-id="' + handle + '"]');
        if (el) el.textContent = String(value);
      }
    }
  };

  /* copy a JS string into WASM memory; returns [ptr, byteLength] */
  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function boot() {
    if (ex.glon_init() !== 0) {
      console.error("glon.js: glon_init failed");
      return;
    }

    var src = document.querySelector('script[type="application/glon"]');
    if (!src) {
      console.error('glon.js: no <script type="application/glon"> found');
      return;
    }

    var pair = alloc(src.textContent);
    var rc = ex.glon_load(pair[0], pair[1]);
    if (rc !== 0) {
      console.error("glon.js: glon_load failed (rc=" + rc + ")");
      return;
    }

    document.querySelectorAll("[data-glon-click]").forEach(function (btn) {
      var name = btn.getAttribute("data-glon-click");
      btn.addEventListener("click", function () {
        var p2 = alloc(name);
        ex.glon_call(p2[0], p2[1]);
      });
    });
  }

  function ready(result) {
    ex = result.instance.exports;
    boot();
  }

  function fail(err) {
    console.error("glon.js: failed to load glon.wasm", err);
  }

  function loadBytes(bytes) {
    WebAssembly.instantiate(bytes, imports).then(ready).catch(fail);
  }

  /* instantiateStreaming requires the correct application/wasm MIME type and
   * an http(s) origin; fall back to a plain fetch + instantiate otherwise. */
  if (typeof WebAssembly.instantiateStreaming === "function" && window.fetch) {
    WebAssembly.instantiateStreaming(fetch("glon.wasm"), imports)
      .then(ready)
      .catch(function () {
        fetch("glon.wasm")
          .then(function (resp) { return resp.arrayBuffer(); })
          .then(loadBytes)
          .catch(fail);
      });
  } else if (window.fetch) {
    fetch("glon.wasm")
      .then(function (resp) { return resp.arrayBuffer(); })
      .then(loadBytes)
      .catch(fail);
  } else {
    fail(new Error("no fetch / WebAssembly support"));
  }
})();
