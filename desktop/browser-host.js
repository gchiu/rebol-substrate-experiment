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
        if (el) {
          el.innerHTML = dec.decode(view().subarray(ptr, ptr + len));
          restoreInputs();   /* DOM glue: keep form values across re-renders */
        }
      },
      host_canvas_script: function () { /* no canvas in this proof */ },
      /* The GLON_LIVE build imports env.host_call. Glon emits a host-call when
       * it wants a native action; this is browser transport only. */
      host_call: function (opPtr, opLen, argPtr, argLen) {
        var op = dec.decode(view().subarray(opPtr, opPtr + opLen));
        var arg = dec.decode(view().subarray(argPtr, argPtr + argLen));
        if (op === "fetch") startFetch(arg);
        else if (op === "cancel") cancelFetch();
        else if (op === "open-folder") openFolder();
        else console.error("browser-host: unknown host_call op '" + op + "'");
      }
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

  /* Deliver a raw-byte event to Glon (prefer glon_event_bytes). */
  function deliver(token, text) {
    var t = alloc(token);
    if (typeof ex.glon_event_bytes === "function") {
      var d = alloc(text);
      var rc = ex.glon_event_bytes(t[0], t[1], d[0], d[1]);
      if (rc !== 0) console.error("browser-host: glon_event_bytes('" + token + "') rc=" + rc);
      return;
    }
    deliverReply(token, text);
  }

  /* An event carrying a value.  Must use the live dispatcher so Glon can emit
   * an outbound host-call request (e.g. start a download). */
  function glonEventValue(token, value) { deliver(token, value); }

  /* The in-flight download, if any. JS owns the AbortController because
   * aborting the HTTP request is browser transport; Glon owns the decision to
   * cancel and the resulting state. Each download has its own session object
   * so a cancelled session can never block the next one. */
  var activeFetch = null;

  function handleFetchLine(session, line) {
    if (!session) return;
    if (line.indexOf("sha256 ") === 0) {       /* SHA may follow "done" */
      deliver("fetch-sha", line.slice(7));
      return;
    }
    if (line === "done") {
      if (!session.terminal) { session.terminal = true; deliver("fetch-done", ""); }
      return;
    }
    if (session.terminal) return;
    if (line.indexOf("error ") === 0) {
      session.terminal = true;
      deliver("fetch-error", line.slice(6));
    } else {
      deliver("fetch-progress", line);
    }
  }

  /* Start a download: Glon decided the action; JS carries the request and
   * streams the newline-delimited progress records back to Glon. */
  function startFetch(spec) {
    if (activeFetch && !activeFetch.cancelled) return;
    var session = { controller: new AbortController(), cancelled: false, terminal: false };
    activeFetch = session;
    fetch("/api/fetch", { method: "POST", body: spec, signal: session.controller.signal })
      .then(function (resp) {
        if (!resp.ok) {
          session.terminal = true;
          var msg = resp.status === 403 ? "not authorised"
                  : resp.status === 400 ? "invalid request or destination"
                  : "server error " + resp.status;
          deliver("fetch-error", msg);
          return;
        }
        var reader = resp.body.getReader();
        var buf = "";
        function pump() {
          return reader.read().then(function (r) {
            if (r.done) {
              if (buf) handleFetchLine(session, buf);
              if (!session.terminal && !session.cancelled) {
                session.terminal = true;
                deliver("fetch-done", "");
              }
              return;
            }
            buf += dec.decode(r.value, { stream: true });
            var i;
            while ((i = buf.indexOf("\n")) >= 0) {
              handleFetchLine(session, buf.slice(0, i));
              buf = buf.slice(i + 1);
            }
            return pump();
          });
        }
        return pump();
      })
      .catch(function (err) {
        if (session.cancelled) return;  /* cancel already handled */
        if (err && err.name === "AbortError") deliver("fetch-cancelled", "");
        else deliver("fetch-error", "download failed");
      })
      .then(function () { if (activeFetch === session) activeFetch = null; });
  }

  function cancelFetch() {
    if (activeFetch && !activeFetch.cancelled) {
      var session = activeFetch;
      session.cancelled = true;
      session.terminal = true;
      session.controller.abort();
      deliver("fetch-cancelled", "");
      if (activeFetch === session) activeFetch = null;   /* allow a new download now */
    }
  }

  function openFolder() {
    fetch("/api/open", { method: "POST" })
      .catch(function (err) { console.error("browser-host: open folder failed", err); });
  }

  /* Preserve form input values across Glon re-renders (DOM glue only). */
  var lastInputs = {};
  function inputValues(spec) {
    var parts = spec.split(",");
    var vals = [];
    for (var i = 0; i < parts.length; i++) {
      var key = parts[i].replace(/^\s+|\s+$/g, "");
      var inp = document.querySelector('[data-glon-input="' + key + '"]');
      var v = inp && inp.value !== undefined ? inp.value : "";
      lastInputs[key] = v;
      vals.push(v);
    }
    return vals.join("\n");
  }
  function restoreInputs() {
    for (var k in lastInputs) {
      if (!Object.prototype.hasOwnProperty.call(lastInputs, k)) continue;
      var inp = document.querySelector('[data-glon-input="' + k + '"]');
      if (inp) inp.value = lastInputs[k];
    }
  }

  /* A demo target fills the URL and file-name fields ONLY.  It never starts a
   * download; the user must still press Download. */
  function pickTarget(el) {
    var ui = document.querySelector('[data-glon-input="url"]');
    var ni = document.querySelector('[data-glon-input="name"]');
    if (ui) ui.value = el.getAttribute("data-url") || "";
    if (ni) ni.value = el.getAttribute("data-name") || "";
    lastInputs.url = ui ? ui.value : "";
    lastInputs.name = ni ? ni.value : "";
  }

  function wireClicks() {
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest
        ? e.target.closest("[data-glon-native], [data-glon-event], [data-glon-pick]") : null;
      if (!el) return;
      e.preventDefault();
      if (el.hasAttribute("data-glon-pick")) {
        pickTarget(el);
      } else if (el.hasAttribute("data-glon-inputs")) {
        glonEventValue(el.getAttribute("data-glon-event"),
                       inputValues(el.getAttribute("data-glon-inputs")));
      } else if (el.hasAttribute("data-glon-native")) {
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
