/* demos/kaka/kaka-host.js -- thin browser glue for Attack of the Mutant Kaka.
 *
 * It only: loads the Glon runtime + game source, maps keys to Glon events, runs
 * the browser animation loop (Glon tick -> Glon render -> paint), and paints the
 * compact canvas script Glon emits. All game state, collisions, targeting,
 * physics, damage, spawning, mutant timers, scoring and tree regeneration live
 * in kaka.glon / kaka-lib.glon / kaka-draw.glon / kaka-selftest.glon.
 * No game logic here.
 */
(function () {
  "use strict";
  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var lastHtml = "";

  /* Fixed-timestep driver. requestAnimationFrame is only the browser clock and
   * render driver; the Glon simulation advances at a fixed wall-clock rate so
   * game speed is independent of the display refresh rate (60/120/144 Hz).
   * The WASM tick measured ~23/s idle and ~13/s under heavy load, so the target
   * is 25 Hz (not 60). Catch-up is capped, a long stall (hidden tab) is clamped,
   * and any leftover backlog is dropped, so the loop can never spiral. */
  var SIM_HZ = 25;
  var SIM_DT = 1000 / SIM_HZ;
  var MAX_CATCHUP = 5;
  var lastTime = 0, acc = 0;

  function view() { return new Uint8Array(ex.memory.buffer); }

  /* colour index -> CSS (Glon chooses the index; JS only maps it) */
  var COL = ["#0b1020", "#f2e9d8", "#8a8f98", "#ff8c1a", "#3fa34d", "#d64545",
             "#3b6ea5", "#ffd23f", "#6b4a2b", "#1f5c2e", "#e08fb0", "#4fd1c5",
             "#8e6bd8", "#7fd18a", "#ff3df0", "#ffffff"];

  /* ---- parallax scenery (host-side presentation only) --------------------
   * The four generated PNGs are painted by JS; Glon owns the camera scalar it
   * emits as `B <cam>` (backgrounds + scroll gameplay) and `F <cam>`
   * (foreground). Factors are the presentation mapping of the supplied art to
   * the requested depths. The art is 3:1 and each layer is drawn wider than the
   * viewport, so the small +/-120 px camera pan never reaches an image edge:
   * no tiling and no seams. Nothing here is gameplay. */
  var LAYERS = [
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_38 AM-1.png", factor: 0.12, h: 480, yb: 480, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_40 AM-2.png", factor: 0.35, h: 430, yb: 470, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_41 AM-3.png", factor: 0.70, h: 440, yb: 500, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_43 AM-4.png", factor: 1.20, h: 560, yb: 540, alpha: 1.0 }
  ];
  var layerImg = [null, null, null, null];

  function loadImage(url) {
    return new Promise(function (res) {
      var im = new Image();
      im.onload = function () { res(im); };
      im.onerror = function () { console.error("kaka: parallax asset failed: " + url); res(null); };
      im.src = url;
    });
  }
  function loadParallax() {
    return Promise.all(LAYERS.map(function (l) { return loadImage(l.src); }))
      .then(function (imgs) { layerImg = imgs; });
  }
  function paintLayer(ctx, W, idx, cam) {
    var img = layerImg[idx];
    if (!img) return;
    var L = LAYERS[idx];
    var w = img.naturalWidth * (L.h / img.naturalHeight);
    var x = (W - w) / 2 - cam * L.factor;
    ctx.globalAlpha = L.alpha;
    ctx.drawImage(img, x, L.yb - L.h, w, L.h);
    ctx.globalAlpha = 1;
  }

  function drawScript(script) {
    var canvas = document.querySelector("#glon-canvas");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var W = canvas.width, H = canvas.height;
    var lines = script.split("\n");
    var cam = 0, tx = 0;              /* tx: gameplay plane scrolls 1:1 with cam */
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].replace(/^\s+|\s+$/g, "");
      if (!line) continue;
      var p = line.split(" ");
      var op = p[0];
      if (op === "B") {
        cam = +p[1];
        tx = -cam;
        paintLayer(ctx, W, 0, cam);   /* distant sky / hills  (slowest) */
        paintLayer(ctx, W, 1, cam);   /* native bush / treeline           */
        paintLayer(ctx, W, 2, cam);   /* orchard / fence / near landscape */
      } else if (op === "F") {
        paintLayer(ctx, W, 3, cam);   /* foreground foliage   (fastest)   */
      } else if (op === "C") {
        ctx.fillStyle = COL[0];
        ctx.fillRect(0, 0, W, H);
      } else if (op === "R") {
        ctx.fillStyle = COL[+p[5]] || "#fff";
        ctx.fillRect(+p[1] + tx, +p[2], +p[3], +p[4]);
      } else if (op === "O") {
        ctx.fillStyle = COL[+p[4]] || "#fff";
        ctx.beginPath();
        ctx.arc(+p[1] + tx, +p[2], +p[3], 0, 2 * Math.PI);
        ctx.fill();
      } else if (op === "T") {
        ctx.fillStyle = COL[+p[7]] || "#fff";
        ctx.beginPath();
        ctx.moveTo(+p[1] + tx, +p[2]);
        ctx.lineTo(+p[3] + tx, +p[4]);
        ctx.lineTo(+p[5] + tx, +p[6]);
        ctx.closePath();
        ctx.fill();
      } else if (op === "L") {
        ctx.strokeStyle = COL[+p[6]] || "#fff";
        ctx.lineWidth = +p[5] || 1;
        ctx.beginPath();
        ctx.moveTo(+p[1] + tx, +p[2]);
        ctx.lineTo(+p[3] + tx, +p[4]);
        ctx.stroke();
      } else if (op === "E") {
        ctx.fillStyle = COL[+p[5]] || "#fff";
        ctx.beginPath();
        ctx.ellipse(+p[1] + tx, +p[2], +p[3], +p[4], 0, 0, 2 * Math.PI);
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
              ArrowUp: "up", KeyW: "up", ArrowDown: "down", KeyS: "down",
              Space: "fire", KeyG: "glon" };
  function key(e, down) {
    if (e.code === "KeyR") { if (down) { acc = 0; ev("kaka-restart"); } e.preventDefault(); return; }
    var k = KEY[e.code];
    if (!k) return;
    e.preventDefault();
    if (e.target && e.target.tagName === "BUTTON" && e.target.blur) e.target.blur();
    evVal(down ? "kaka-key-down" : "kaka-key-up", k);
  }

  function resetInput() { try { ev("kaka-input-reset"); } catch (err) { console.error("kaka: input reset failed", err); } }

  function focusGame() {
    var cv = document.querySelector("#glon-canvas");
    if (cv) cv.focus();
  }

  function frame(now) {
    if (!lastTime) lastTime = now;      /* first callback: start the clock */
    var dt = now - lastTime;
    lastTime = now;
    if (dt > 500) dt = 500;             /* hidden/stalled tab: do not accrue a backlog */
    acc += dt;
    var steps = 0;
    while (acc >= SIM_DT && steps < MAX_CATCHUP) {
      try { ev("kaka-tick"); }
      catch (err) { console.error("kaka: tick failed", err); acc = 0; break; }
      acc -= SIM_DT;
      steps++;
    }
    if (acc >= SIM_DT) acc = 0;         /* still behind: drop it, never spiral */
    requestAnimationFrame(frame);
  }

  function wireClicks() {
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      ev(el.getAttribute("data-glon-event"));
      focusGame();
    });
  }

  function fetchText(u) {
    return fetch(u).then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status + " " + u); return r.text(); });
  }

  function boot() {
    if (ex.glon_init() !== 0) { console.error("kaka: glon_init failed"); return; }
    Promise.resolve()
      .then(function () { return fetchText("prelude.glon"); }).then(load)
      .then(function () { return fetchText("strings.glon"); }).then(load)
      .then(function () { return fetchText("kaka-lib.glon"); }).then(load)
      .then(function () { return fetchText("kaka.glon"); }).then(load)
      .then(function () { return fetchText("kaka-draw.glon"); }).then(load)
      /* kaka-selftest.glon is test-only and is intentionally not loaded here:
         the production page needs the loader budget for input dispatch. */
      .then(loadParallax)            /* PNG scenery is presentation-only */
      .then(function () {
        wireClicks();
        window.addEventListener("keydown", function (e) { key(e, true); });
        window.addEventListener("keyup", function (e) { key(e, false); });
        window.addEventListener("blur", function () { resetInput(); });
        document.addEventListener("visibilitychange", function () {
          /* drop any time that passed while hidden so we resume cleanly */
          lastTime = 0; acc = 0;
          if (document.visibilityState === "hidden") resetInput();
          else focusGame();
        });
        document.addEventListener("mousedown", function (e) {
          if (e.target && e.target.id === "glon-canvas") e.preventDefault();
        });
        route("home");
        focusGame();
        requestAnimationFrame(frame);
      })
      .catch(function (err) { console.error("kaka: boot failed", err); });
  }

  fetch("glon.wasm")
    .then(function (r) { return r.arrayBuffer(); })
    .then(function (b) { return WebAssembly.instantiate(b, imports); })
    .then(function (res) { ex = res.instance.exports; boot(); })
    .catch(function (e) { console.error("kaka: wasm load failed", e); });
})();
