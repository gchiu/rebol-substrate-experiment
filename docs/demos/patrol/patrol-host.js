/* demos/patrol/patrol-host.js -- thin browser glue for Glon Patrol.
 *
 * It only: loads the Glon runtime + game source, maps keys to Glon events,
 * runs the browser animation loop (Glon tick -> Glon render -> draw), and draws
 * the small canvas script Glon emits. All game state and rules live in
 * patrol.glon. No game logic here.
 */
(function () {
  "use strict";
  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var lastHtml = "";

  function view() { return new Uint8Array(ex.memory.buffer); }

  /* colour index -> CSS (Glon chooses the index; JS only maps it) */
  var COL = ["#0b1020", "#e8e8e8", "#8a8f98", "#ff8c1a", "#3dff7a", "#ff5252", "#5aa9ff", "#ffe066"];

  function drawScript(script) {
    var canvas = document.querySelector("#glon-canvas");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var W = canvas.width, H = canvas.height;
    var lines = script.split("\n");
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].replace(/^\s+|\s+$/g, "");
      if (!line) continue;
      var p = line.split(" ");
      var op = p[0];
      if (op === "C") {
        ctx.fillStyle = COL[0];
        ctx.fillRect(0, 0, W, H);
      } else if (op === "R") {
        ctx.fillStyle = COL[+p[5]] || "#fff";
        ctx.fillRect(+p[1], +p[2], +p[3], +p[4]);
      } else if (op === "O") {
        ctx.fillStyle = COL[+p[4]] || "#fff";
        ctx.beginPath();
        ctx.arc(+p[1], +p[2], +p[3], 0, 2 * Math.PI);
        ctx.fill();
      } else if (op === "T") {
        ctx.fillStyle = COL[+p[7]] || "#fff";
        ctx.beginPath();
        ctx.moveTo(+p[1], +p[2]);
        ctx.lineTo(+p[3], +p[4]);
        ctx.lineTo(+p[5], +p[6]);
        ctx.closePath();
        ctx.fill();
      }
    }
  }

  var imports = {
    env: {
      host_print: function (ptr, len) { console.log("[glon]", dec.decode(view().subarray(ptr, ptr + len))); },
      host_set_text: function (h, v) {
        var el = document.querySelector('[data-glon-id="' + h + '"]');
        if (el) el.textContent = String(v);
      },
      host_set_html: function (h, ptr, len) {
        var el = document.querySelector('[data-glon-id="' + h + '"]');
        if (!el) return;
        var html = dec.decode(view().subarray(ptr, ptr + len));
        if (html !== lastHtml) { el.innerHTML = html; lastHtml = html; }
      },
      host_canvas_script: function (ptr, len) {
        drawScript(dec.decode(view().subarray(ptr, ptr + len)));
      }
    }
  };

  function alloc(str) {
    var b = enc.encode(str);
    var p = ex.glon_alloc(b.length);
    view().set(b, p);
    return [p, b.length];
  }
  function load(src) { var x = alloc(src); if (ex.glon_load(x[0], x[1]) !== 0) throw new Error("glon_load failed"); }
  function route(t) { var x = alloc(t); ex.glon_route(x[0], x[1]); }
  function ev(t) { var x = alloc(t); ex.glon_event(x[0], x[1]); }
  function evVal(t, v) { var a = alloc(t), b = alloc(v); ex.glon_event_value(a[0], a[1], b[0], b[1]); }

  var KEY = { ArrowLeft: "left", KeyA: "left", ArrowRight: "right", KeyD: "right",
              ArrowUp: "up", KeyW: "up", ArrowDown: "down", KeyS: "down", Space: "fire" };
  function key(e, down) {
    var k = KEY[e.code];
    if (!k) return;
    e.preventDefault();
    /* a focused button must not swallow Space/arrow keys */
    if (e.target && e.target.tagName === "BUTTON" && e.target.blur) e.target.blur();
    evVal(down ? "patrol-key-down" : "patrol-key-up", k);
  }

  /* Tell Glon to clear all held keys (after a lost keyup / blur / tab hide). */
  function resetInput() { try { ev("patrol-input-reset"); } catch (err) { console.error("patrol: input reset failed", err); } }

  function focusGame() {
    var cv = document.querySelector("#glon-canvas");
    if (cv) cv.focus();
  }

  function frame() {
    /* Never let one bad frame kill the animation loop. */
    try { ev("patrol-tick"); }
    catch (err) { console.error("patrol: tick failed", err); }
    requestAnimationFrame(frame);
  }

  function wireClicks() {
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      ev(el.getAttribute("data-glon-event"));
      focusGame();   /* return keyboard focus to the game after any selector/Restart click */
    });
  }

  function fetchText(u) {
    return fetch(u).then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status + " " + u); return r.text(); });
  }

  function boot() {
    if (ex.glon_init() !== 0) { console.error("patrol: glon_init failed"); return; }
    Promise.resolve()
      .then(function () { return fetchText("prelude.glon"); }).then(load)
      .then(function () { return fetchText("strings.glon"); }).then(load)
      .then(function () { return fetchText("patrol.glon"); }).then(load)
      .then(function () {
        wireClicks();
        window.addEventListener("keydown", function (e) { key(e, true); });
        window.addEventListener("keyup", function (e) { key(e, false); });
        /* losing focus/tab can drop the keyup: clear held keys so input cannot stick or die */
        window.addEventListener("blur", function () { resetInput(); });
        document.addEventListener("visibilitychange", function () {
          resetInput();
          if (document.visibilityState === "visible") focusGame();
        });
        document.addEventListener("mousedown", function (e) {
          if (e.target && e.target.id === "glon-canvas") e.preventDefault();
        });
        route("home");
        focusGame();
        requestAnimationFrame(frame);
      })
      .catch(function (err) { console.error("patrol: boot failed", err); });
  }

  fetch("glon.wasm")
    .then(function (r) { return r.arrayBuffer(); })
    .then(function (b) { return WebAssembly.instantiate(b, imports); })
    .then(function (res) { ex = res.instance.exports; boot(); })
    .catch(function (e) { console.error("patrol: wasm load failed", e); });
})();
