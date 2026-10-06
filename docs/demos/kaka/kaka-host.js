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
  /* Build token. GitHub Pages serves with Cache-Control: max-age=600, so a
   * stale cached kaka.glon / kaka-draw.glon / wasm / PNG would keep an old
   * frame (e.g. the flat background or geometric actors) alive for minutes.
   * Bump this whenever published game assets change. */
  var VER = "d12a8";
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
      im.onerror = function () { console.error("kaka: asset failed: " + url); res(null); };
      im.src = url + "?v=" + VER;    /* bust the Pages 10-minute cache */
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

  /* ---- actor sprites (host-side presentation only) -----------------------
   * Four supplied sheets; Glon emits `S id x y dir` and this table maps the id
   * to a source rectangle. Kaka/mutant/rat use the full sheet height so every
   * frame shares one art scale (bodies stay consistent); the small-asset items
   * use tight rects. ax/ay are the fraction of the frame placed at the actor's
   * logical (x,y); a rat's long tail sits left of its body, hence ax > 0.5.
   * Gameplay collision never reads these rectangles. */
  var SPRITE_SRC = [
    "assets/ChatGPT Image Oct 6, 2026, 12_37_49 PM-1.png",   /* normal kaka  */
    "assets/ChatGPT Image Oct 6, 2026, 12_37_51 PM-2.png",   /* mutant kaka  */
    "assets/ChatGPT Image Oct 6, 2026, 12_37_53 PM-3.png",   /* rat          */
    "assets/ChatGPT Image Oct 6, 2026, 12_37_54 PM-4.png"    /* small assets */
  ];
  var SPRITES = {
    0:  { s: 0, sx: 14,   sy: 0, sw: 604, sh: 724, sc: 0.11 },              /* glide */
    1:  { s: 0, sx: 650,  sy: 0, sw: 414, sh: 724, sc: 0.11 },              /* flap  */
    2:  { s: 0, sx: 1160, sy: 0, sw: 428, sh: 724, sc: 0.11 },              /* perch */
    3:  { s: 0, sx: 1588, sy: 0, sw: 573, sh: 724, sc: 0.11 },              /* swoop */
    10: { s: 1, sx: 14,   sy: 0, sw: 544, sh: 724, sc: 0.12 },              /* mutant powered */
    11: { s: 1, sx: 590,  sy: 0, sw: 470, sh: 724, sc: 0.12 },              /* mutant flap    */
    12: { s: 1, sx: 1060, sy: 0, sw: 640, sh: 724, sc: 0.12 },              /* mutant laser   */
    13: { s: 1, sx: 1700, sy: 0, sw: 461, sh: 724, sc: 0.12 },              /* mutant attack  */
    20: { s: 2, sx: 14,   sy: 0, sw: 636, sh: 724, sc: 0.11, ax: 0.72, ay: 0.55 }, /* rat run  */
    21: { s: 2, sx: 650,  sy: 0, sw: 638, sh: 724, sc: 0.11, ax: 0.72, ay: 0.55 }, /* rat run2 */
    22: { s: 2, sx: 1288, sy: 0, sw: 452, sh: 724, sc: 0.11, ax: 0.60, ay: 0.55 }, /* rat sniff */
    23: { s: 2, sx: 1740, sy: 0, sw: 411, sh: 724, sc: 0.11, ax: 0.62, ay: 0.55 }, /* rat startle */
    30: { s: 3, sx: 20,   sy: 340, sw: 220, sh: 115, dh: 16 },             /* ordinary berry */
    31: { s: 3, sx: 268,  sy: 240, sw: 245, sh: 275, dh: 22 },             /* Mystery Glon Berry */
    32: { s: 3, sx: 812,  sy: 330, sw: 126, sh: 145, dh: 15 }              /* poop/seed drop */
  };
  var spriteImg = [null, null, null, null];
  function loadSprites() {
    return Promise.all(SPRITE_SRC.map(loadImage))
      .then(function (imgs) { spriteImg = imgs; });
  }
  function drawSprite(ctx, id, x, y, dir, tx) {
    var S = SPRITES[id];
    if (!S) return;
    var img = spriteImg[S.s];
    if (!img) return;
    var scale = S.sc || (S.dh / S.sh);
    var dw = S.sw * scale, dh = S.sh * scale;
    var ax = (S.ax === undefined) ? 0.5 : S.ax;
    var ay = (S.ay === undefined) ? 0.5 : S.ay;
    var cx = x + tx;                       /* gameplay plane scrolls 1:1 */
    var dx = cx - dw * ax, dy = y - dh * ay;
    if (dir < 0) {
      ctx.save();
      ctx.translate(2 * cx, 0);
      ctx.scale(-1, 1);
      ctx.drawImage(img, S.sx, S.sy, S.sw, S.sh, dx, dy, dw, dh);
      ctx.restore();
    } else {
      ctx.drawImage(img, S.sx, S.sy, S.sw, S.sh, dx, dy, dw, dh);
    }
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
      } else if (op === "S") {
        drawSprite(ctx, +p[1], +p[2], +p[3], +p[4], tx);
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
    var v = u + "?v=" + VER;         /* bust the Pages 10-minute cache */
    return fetch(v).then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status + " " + v); return r.text(); });
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
      .then(loadSprites)             /* actor sheets are presentation-only */
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

  fetch("glon.wasm?v=" + VER)
    .then(function (r) { return r.arrayBuffer(); })
    .then(function (b) { return WebAssembly.instantiate(b, imports); })
    .then(function (res) { ex = res.instance.exports; boot(); })
    .catch(function (e) { console.error("kaka: wasm load failed", e); });
})();
