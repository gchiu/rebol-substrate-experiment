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
 *   - feed the GLON bootstrap source (inlined as
 *     <script type="application/glon">) into glon_load at startup, and feed
 *     each on-demand demo source into glon_load when first selected;
 *   - fetch a [data-glon-load] resource (a generic path) and re-dispatch its
 *     [data-glon-route] after the fetch completes (async resume);
 *   - forward [data-glon-route] clicks and the initial location to glon_route;
 *   - forward [data-glon-event] clicks to glon_event (or glon_event_value when
 *     a [data-glon-input] element shares the token name).
 *
 * No routing decision, application state, or page logic lives here. JavaScript
 * knows how to fetch bytes and inject them; Glon decides WHICH resource to load
 * and what it means. This file is only the host bridge.
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
      },
      host_canvas_script: function (ptr, len) {
        var script = dec.decode(view().subarray(ptr, ptr + len));
        var canvas = document.querySelector("#glon-canvas");
        if (canvas) playCanvasScript(canvas, script);
      }
    }
  };

  // ---- Canvas visualization -----------------------------------------------
  // host_canvas_script receives a generic visual script (newline commands)
  // that Glon emitted during render, and draws/animates it on the canvas. The
  // script knows only graph-scene concepts: B labelled node, L edge,
  // D persistent token, M token motion. It never names a worker, reducer, or
  // transaction.

  function parseCanvasScript(script) {
    var statics = [];   // nodes + edges, drawn every frame
    var jaffas = {};    // id -> {x, y}  (persistent tokens)
    var moves = [];     // animated transitions (token traversal)
    var lines = script.split("\n");
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].replace(/^\s+|\s+$/g, "");
      if (!line) continue;
      var parts = line.split(" ");
      var op = parts[0];
      var nums = function (a, b) {
        var r = [];
        for (var k = a; k < b; k++) r.push(Number(parts[k]));
        return r;
      };
      if (op === "B") {
        var b = nums(1, 5);
        statics.push({ op: "B", x: b[0], y: b[1], w: b[2], h: b[3], label: parts.slice(5).join(" ") });
      } else if (op === "L") {
        var l = nums(1, 5);
        statics.push({ op: "L", x1: l[0], y1: l[1], x2: l[2], y2: l[3] });
      } else if (op === "D") {
        var d = nums(1, 4);
        jaffas[d[0]] = { x: d[1], y: d[2] };
      } else if (op === "M") {
        var m = nums(1, 6);
        moves.push({ id: m[0], x1: m[1], y1: m[2], x2: m[3], y2: m[4] });
      }
    }
    return { statics: statics, jaffas: jaffas, moves: moves };
  }

  // A small generic spread (presentation only) so Jaffas queued at the same
  // node do not stack on one pixel. Keyed on the token id, not on any meaning.
  function jaffaOffset(id) {
    return { ox: (id % 3) * 10 - 10, oy: ((id / 3 | 0) % 3) * 8 - 8 };
  }

  function drawCanvas(ctx, w, h, statics, jaffas) {
    ctx.clearRect(0, 0, w, h);
    ctx.font = "12px sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    var i;
    for (i = 0; i < statics.length; i++) {
      var s = statics[i];
      if (s.op === "B") {
        ctx.fillStyle = "#f5f5f5";
        ctx.strokeStyle = "#444";
        ctx.fillRect(s.x, s.y, s.w, s.h);
        ctx.strokeRect(s.x, s.y, s.w, s.h);
        ctx.fillStyle = "#222";
        ctx.fillText(s.label, s.x + s.w / 2, s.y + s.h / 2);
      } else if (s.op === "L") {
        ctx.strokeStyle = "#888";
        ctx.beginPath();
        ctx.moveTo(s.x1, s.y1);
        ctx.lineTo(s.x2, s.y2);
        ctx.stroke();
      }
    }
    ctx.fillStyle = "#d33";
    for (var id in jaffas) {
      var j = jaffas[id];
      var off = jaffaOffset(Number(id));
      ctx.beginPath();
      ctx.arc(j.x + off.ox, j.y + off.oy, 6, 0, 2 * Math.PI);
      ctx.fill();
    }
  }

  // The script is a temporal trace: each M command is one logical scheduler
  // step, in the order Glon produced them. The JS groups the moves back into
  // per-Jaffa paths and staggers each Jaffa by its first (claim) step, so every
  // Jaffa's hops stay sequential while different Jaffas overlap -- matching the
  // real M1 interleaving. JS only maps time; Glon decided what happened and
  // when.
  function playCanvasScript(canvas, script) {
    var ctx = canvas.getContext("2d");
    var w = canvas.width, h = canvas.height;
    var parsed = parseCanvasScript(script);
    var paths = {};      // id -> [ {x1,y1,x2,y2}, ... ] in temporal order
    var claimStep = {};  // id -> script index of its first (claim) move
    for (var i = 0; i < parsed.moves.length; i++) {
      var m = parsed.moves[i];
      if (!paths[m.id]) { paths[m.id] = []; claimStep[m.id] = i; }
      paths[m.id].push(m);
    }
    var STEP = 130;   // ms spacing between successive claims
    var DUR = 430;    // ms per hop (> STEP, so Jaffas overlap)
    var start = null;

    function frame(t) {
      if (start === null) start = t;
      var elapsed = t - start;
      var moving = false;
      for (var id in paths) {
        var segs = paths[id];
        var local = elapsed - claimStep[id] * STEP;
        if (local < 0) { moving = true; continue; }
        var seg = Math.floor(local / DUR);
        if (seg >= segs.length) continue;   // this Jaffa has arrived
        moving = true;
        var frac = (local % DUR) / DUR;
        var m = segs[seg];
        var j = parsed.jaffas[id];
        j.x = m.x1 + (m.x2 - m.x1) * frac;
        j.y = m.y1 + (m.y2 - m.y1) * frac;
      }
      drawCanvas(ctx, w, h, parsed.statics, parsed.jaffas);
      if (moving) requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  }

  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function route(token) {
    if (typeof ex.glon_route !== "function") return;
    var pair = alloc(token);
    var rc = ex.glon_route(pair[0], pair[1]);
    if (rc !== 0) console.error("host.js: glon_route('" + token + "') returned " + rc);
  }

  function glonEvent(token) {
    if (typeof ex.glon_event !== "function") return;
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("host.js: glon_event('" + token + "') returned " + rc);
  }

  // Generic load-on-demand: fetch a resource path, inject its source into the
  // persistent machine via glon_load, then re-dispatch the route that asked for
  // it. JavaScript only knows "fetch bytes and hand them back"; the resource
  // path and route token come from Glon-rendered attributes.
  function loadResource(path, token) {
    fetch(path)
      .then(function (resp) {
        if (!resp.ok) throw new Error("HTTP " + resp.status);
        return resp.text();
      })
      .then(function (src) {
        var pair = alloc(src);
        if (ex.glon_load(pair[0], pair[1]) !== 0) {
          console.error("host.js: glon_load('" + path + "') failed");
          glonEvent("load-failed");
          return;
        }
        route(token);
      })
      .catch(function (err) {
        console.error("host.js: fetch('" + path + "') failed", err);
        glonEvent("load-failed");
      });
  }

  function glonEventValue(token, value) {
    if (typeof ex.glon_event_value !== "function") return;
    var t = alloc(token);
    var v = alloc(value);
    var rc = ex.glon_event_value(t[0], t[1], v[0], v[1]);
    if (rc !== 0) console.error("host.js: glon_event_value('" + token + "', ...) returned " + rc);
  }

  function initialToken() {
    // The demo launcher is a single-page app served from a stable path
    // (e.g. /shop/). Its URL never changes during a session (clicks call
    // route(token) directly and there is no history.pushState), so the initial
    // route is always "home" regardless of the hosting path prefix.
    return "home";
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
    // clicks forward a generic application event. An event may carry a value
    // from a static [data-glon-value] attribute, or from a [data-glon-input]
    // element sharing the token name. JS only forwards the token/value pair;
    // it never interprets what either means.
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-route], [data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      if (el.hasAttribute("data-glon-route")) {
        var token = el.getAttribute("data-glon-route");
        if (el.hasAttribute("data-glon-load")) {
          loadResource(el.getAttribute("data-glon-load"), token);
        } else {
          route(token);
        }
      } else if (el.hasAttribute("data-glon-event")) {
        var token = el.getAttribute("data-glon-event");
        var value = null;
        if (el.hasAttribute("data-glon-value")) {
          value = el.getAttribute("data-glon-value");
        } else {
          var input = document.querySelector('[data-glon-input="' + token + '"]');
          if (input && input.value !== undefined) value = input.value;
        }
        if (value !== null) glonEventValue(token, value);
        else glonEvent(token);
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
