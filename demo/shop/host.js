/* demo/shop/host.js -- the handwritten JavaScript half of the G1A browser
 * boundary.
 *
 * JavaScript responsibilities are STRICTLY limited to browser capabilities
 * Glon cannot perform directly (per GLON-SHOP-G1A.md):
 *
 *   - instantiate glon.wasm and supply the host imports;
 *   - host_print  : console-log the runtime's plain-text output;
 *   - host_set_html : write rendered HTML into [data-glon-id="<handle>"];
 *   - host_set_text : (retained for the W2 counter demo) write a number into
 *                     [data-glon-id="<handle>"];
 *   - feed the GLON source (inlined as <script type="application/glon">) into
 *     glon_load exactly once at startup;
 *   - forward [data-glon-route] clicks and the initial location to glon_route;
 *   - forward [data-glon-event] clicks to glon_event.
 *
 * No routing decision, application state, or page logic lives here. Glon is
 * the application controller; this file is only the host bridge.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();

  function view() {
    return new Uint8Array(ex.memory.buffer);
  }

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
      }
    }
  };

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function route(token) {
    if (typeof ex.glon_route !== "function") return;
    var pair = alloc(token);
    ex.glon_route(pair[0], pair[1]);
  }

  function glonEvent(token) {
    if (typeof ex.glon_event !== "function") return;
    var pair = alloc(token);
    ex.glon_event(pair[0], pair[1]);
  }

  function initialToken() {
    var path = window.location.pathname.replace(/^\/+/, "");
    if (path === "index.html") path = "";
    return path === "" ? "home" : path;
  }

  function boot() {
    if (ex.glon_init() !== 0) {
      console.error("host.js: glon_init failed");
      return;
    }

    var src = document.querySelector('script[type="application/glon"]');
    if (!src) {
      console.error('host.js: no <script type="application/glon"> found');
      return;
    }
    var pair = alloc(src.textContent);
    if (ex.glon_load(pair[0], pair[1]) !== 0) {
      console.error("host.js: glon_load failed");
      return;
    }

    // Event delegation: [data-glon-route] clicks navigate; [data-glon-event]
    // clicks forward a generic application event. JS only forwards the token;
    // it never interprets what the route or event means.
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-route], [data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      if (el.hasAttribute("data-glon-route")) {
        route(el.getAttribute("data-glon-route"));
      } else if (el.hasAttribute("data-glon-event")) {
        glonEvent(el.getAttribute("data-glon-event"));
      }
    });

    route(initialToken());
  }

  function ready(result) {
    ex = result.instance.exports;
    boot();
  }

  function fail(err) {
    console.error("host.js: failed to load glon.wasm", err);
  }

  function loadBytes(bytes) {
    WebAssembly.instantiate(bytes, imports).then(ready).catch(fail);
  }

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
