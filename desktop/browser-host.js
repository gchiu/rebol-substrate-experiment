/* desktop/browser-host.js -- the thin browser host for desktop/view.glon.
 *
 * It does only browser/transport work:
 *   - instantiate the existing Glon/WASM runtime (glon.wasm) and supply the
 *     four host imports;
 *   - fetch the ordinary Glon source files (prelude.glon, emit.glon,
 *     view.glon) and pass them to glon_load;
 *   - forward a click to glon_event, or -- for a [data-glon-native] element --
 *     fetch /native/read and forward the reply bytes to glon_event_value.
 *
 * It contains no view, routing, state or protocol logic; Glon decides the
 * logical file name and what the reply means.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();

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
      host_canvas_script: function () { /* no canvas in this proof */ },
      /* The GLON_LIVE build imports env.host_call. This proof does not use
       * outbound capability requests from the browser (it asks native Glon via
       * the same-origin fetch), so the import is a no-op. */
      host_call: function () { /* unused by this view */ }
    }
  };

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function loadSource(src) {
    var pair = alloc(src);
    var rc = ex.glon_load(pair[0], pair[1]);
    if (rc !== 0) throw new Error("glon_load failed (" + rc + ")");
  }

  function fetchText(url) {
    return fetch(url).then(function (resp) {
      if (!resp.ok) throw new Error("HTTP " + resp.status + " for " + url);
      return resp.text();
    });
  }

  function route(token) {
    var pair = alloc(token);
    var rc = ex.glon_route(pair[0], pair[1]);
    if (rc !== 0) console.error("browser-host: glon_route('" + token + "') rc=" + rc);
  }

  function glonEvent(token) {
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("browser-host: glon_event('" + token + "') rc=" + rc);
  }

  /* Hand a reply to Glon. The GLON_LIVE build exposes glon_event_bytes, which
   * binds current-value from raw bytes directly (no decimal source expansion,
   * no 200-byte value cap). When that export is present we use it; otherwise
   * (the base G1A build) we fall back to glon_event_value. Glon's do-event
   * semantics are identical either way. */
  function deliverReply(token, text) {
    var t = alloc(token);
    if (typeof ex.glon_event_bytes === "function") {
      var d = alloc(text);
      var rc = ex.glon_event_bytes(t[0], t[1], d[0], d[1]);
      if (rc !== 0) console.error("browser-host: glon_event_bytes('" + token + "') rc=" + rc);
      return;
    }
    var v = alloc(text);
    var rc2 = ex.glon_event_value(t[0], t[1], v[0], v[1]);
    if (rc2 !== 0) console.error("browser-host: glon_event_value('" + token + "') rc=" + rc2);
  }

  function nativeRead(path, token) {
    fetchText("/native/read?path=" + encodeURIComponent(path))
      .then(function (text) { deliverReply(token, text); })
      .catch(function (err) { console.error("browser-host: native read failed", err); });
  }

  function wireClicks() {
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest
        ? e.target.closest("[data-glon-native], [data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      if (el.hasAttribute("data-glon-native")) {
        nativeRead(el.getAttribute("data-glon-native"),
                   el.getAttribute("data-glon-event") || "native-reply");
      } else {
        glonEvent(el.getAttribute("data-glon-event"));
      }
    });
  }

  function boot() {
    if (ex.glon_init() !== 0) {
      console.error("browser-host: glon_init failed");
      return;
    }
    fetchText("prelude.glon")
      .then(loadSource)
      .then(function () { return fetchText("strings.glon"); })
      .then(loadSource)
      .then(function () { return fetchText("emit.glon"); })
      .then(loadSource)
      .then(function () { return fetchText("view.glon"); })
      .then(loadSource)
      .then(function () { wireClicks(); route("home"); })
      .catch(function (err) { console.error("browser-host: boot failed", err); });
  }

  fetch("glon.wasm")
    .then(function (resp) { return resp.arrayBuffer(); })
    .then(function (bytes) { return WebAssembly.instantiate(bytes, imports); })
    .then(function (result) { ex = result.instance.exports; boot(); })
    .catch(function (err) { console.error("browser-host: failed to load glon.wasm", err); });
})();
