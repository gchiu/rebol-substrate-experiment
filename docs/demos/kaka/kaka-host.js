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
  var BUILD = "D12S.2b";   /* human-visible label; SHA injected at deploy */
  var VER = "d12s2b";
  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();
  var lastHtml = "";

  /* High score bridge. Glon owns the current score and the comparison; the
   * host only (a) reads the single persisted number and hands it to Glon at
   * boot and (b) mirrors Glon's `#kaka-hs` value back into localStorage when
   * it changes. JS never invents score rules. */
  var HS_KEY = "glon.kaka.highscore.v1";
  function sanitizeHigh(v) {
    v = parseInt(v, 10);
    if (!isFinite(v) || v < 0) return 0;
    return Math.floor(v);
  }
  function readStoredHigh() {
    try { return sanitizeHigh(localStorage.getItem(HS_KEY)); } catch (e) { return 0; }
  }
  var lastSaved = readStoredHigh();
  function persistHighFromHtml(html) {
    var m = /id='kaka-hs'[^>]*>(\d+)</.exec(html);
    if (!m) return;
    var v = sanitizeHigh(m[1]);
    if (v !== lastSaved) {
      try { localStorage.setItem(HS_KEY, String(v)); } catch (e) {}
      lastSaved = v;
    }
  }

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
   * (foreground). The world scrolls right forever, so each layer is repeated as
   * ORDINARY (unmirrored) copies with an OVERLAP-wide crossfade between
   * neighbours: the incoming copy's first OVERLAP px are a linear alpha ramp, so
   * the outgoing copy's right edge dissolves into the incoming copy's left edge.
   * This removes the mirror symmetry axis the old mirror tiling produced (a
   * reflected tree/hill reads as a hard join) with no hard opacity edge, no
   * uncovered strip, no scale change and no camera-dependent jump. The faded
   * copy is precomputed ONCE per layer -- no per-frame offscreen canvas. The
   * gameplay plane scrolls 1:1 with the same cam. Nothing here is game logic. */
  var LAYERS = [
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_38 AM-1.png", factor: 0.12, h: 480, yb: 480, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_40 AM-2.png", factor: 0.35, h: 430, yb: 470, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_41 AM-3.png", factor: 0.70, h: 440, yb: 500, alpha: 1.0 },
    { src: "assets/ChatGPT Image Oct 6, 2026, 08_56_43 AM-4.png", factor: 1.20, h: 560, yb: 540, alpha: 1.0 }
  ];
  /* Crossfade band width in logical px: the span over which two neighbouring
     copies dissolve. ~120 is enough to hide the join in the supplied 3:1 art
     without a wide double-image. */
  var OVERLAP = 120;
  var layerImg = [null, null, null, null];
  var layerFade = [null, null, null, null];   /* left-edge alpha-ramp copy */
  var layerW = [0, 0, 0, 0];
  var layerDH = [0, 0, 0, 0];

  function loadImage(url) {
    return new Promise(function (res) {
      var im = new Image();
      im.onload = function () { res(im); };
      im.onerror = function () { console.error("kaka: asset failed: " + url); res(null); };
      im.src = url + "?v=" + VER;    /* bust the Pages 10-minute cache */
    });
  }
  /* Precompute, once per layer, a scaled copy whose LEFT OVERLAP px ramp from
     alpha 0 to 1 (fully opaque after). Pure Canvas; done at load, never per
     frame. The original Image is reused for the opaque base tile. */
  function buildFade(idx) {
    var img = layerImg[idx];
    if (!img) return;
    var L = LAYERS[idx];
    var w = Math.max(1, Math.round(img.naturalWidth * (L.h / img.naturalHeight)));
    var dh = L.h;
    layerW[idx] = w; layerDH[idx] = dh;
    var f = document.createElement("canvas"); f.width = w; f.height = dh;
    var fx = f.getContext("2d");
    fx.drawImage(img, 0, 0, w, dh);
    /* destination-in with a horizontal ramp: only the first OVERLAP px lose
       alpha (0 -> 1); everything else stays fully opaque. */
    var g = fx.createLinearGradient(0, 0, w, 0);
    var o = Math.min(OVERLAP, w) / w;
    g.addColorStop(0, "rgba(255,255,255,0)");
    g.addColorStop(o, "rgba(255,255,255,1)");
    g.addColorStop(1, "rgba(255,255,255,1)");
    fx.globalCompositeOperation = "destination-in";
    fx.fillStyle = g;
    fx.fillRect(0, 0, w, dh);
    layerFade[idx] = f;
  }
  function loadParallax() {
    return Promise.all(LAYERS.map(function (l) { return loadImage(l.src); }))
      .then(function (imgs) {
        layerImg = imgs;
        for (var i = 0; i < LAYERS.length; i++) buildFade(i);
      });
  }
  function paintLayer(ctx, W, idx, cam) {
    var img = layerImg[idx];
    if (!img) return;
    var L = LAYERS[idx];
    var w = layerW[idx], dh = layerDH[idx];
    /* copies are spaced `step` apart (step < w), so neighbours overlap by
       OVERLAP px and crossfade there. */
    var step = Math.max(1, w - OVERLAP);
    var base = cam * L.factor;
    var n = Math.floor(base / step);
    var firstN = n;
    ctx.globalAlpha = L.alpha;
    /* The first (leftmost) copy is the opaque base; every later copy fades in
       over its predecessor's right OVERLAP px. The layer's own painted alpha
       blends it into the scene -- no clip, so there is no ruler-straight
       horizontal edge anywhere on the canvas. */
    for (var px = n * step - base; px < W; px += step, n++) {
      if (px + w <= 0) continue;
      ctx.drawImage(n === firstN ? img : layerFade[idx], px, L.yb - dh, w, dh);
    }
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
        var html = dec.decode(view().subarray(ptr, ptr + len));
        persistHighFromHtml(html);
        if (!el) return;
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
              Space: "fire", KeyG: "glon", KeyF: "rock" };
  function key(e, down) {
    if (e.code === "KeyR") { if (down) { acc = 0; ev("kaka-restart"); } e.preventDefault(); return; }
    var k = KEY[e.code];
    if (!k) return;
    e.preventDefault();
    if (e.target && e.target.tagName === "BUTTON" && e.target.blur) e.target.blur();
    evVal(down ? "kaka-key-down" : "kaka-key-up", k);
  }

  function resetInput() { pendingUp = {}; try { ev("kaka-input-reset"); } catch (err) { console.error("kaka: input reset failed", err); } }

  function focusGame() {
    var cv = document.querySelector("#glon-canvas");
    if (cv) cv.focus();
  }

  /* Discrete BERRY/GLON taps must not be lost when pointerdown+pointerup both
     land between two fixed simulation ticks. We latch: if no tick has run since
     the press, the key-up is deferred until after the next tick, so exactly one
     tick always sees the press (the game's own cooldown/pool still gate firing).
     LEFT/RIGHT stay plain held-state controls. */
  var tickCount = 0;
  var pressTick = {};
  var pendingUp = {};

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
      tickCount++;
    }
    if (acc >= SIM_DT) acc = 0;         /* still behind: drop it, never spiral */
    for (var nm in pendingUp) {
      if (pendingUp[nm]) { pendingUp[nm] = false; evVal("kaka-key-up", nm); }
    }
    requestAnimationFrame(frame);
  }

  function fireGon(el) {
    var tok = el.getAttribute("data-glon-event");
    if (!tok) return;
    try { ev(tok); } catch (err) { console.error("kaka: event failed", err); }
    focusGame();
  }
  /* Buttons that map to a Glon event (RESTART during play, PLAY AGAIN after
     game over). Pointer Events are the reliable mobile path, so act on
     pointerup directly rather than assuming a synthesised click will arrive.
     The trailing click is de-duplicated so one tap fires exactly one event;
     `click` is kept as a fallback for keyboard/assistive activation. */
  function wireClicks() {
    document.addEventListener("pointerup", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-event]") : null;
      if (!el) return;
      if (e.cancelable) e.preventDefault();
      el.__glonFiredAt = Date.now();
      fireGon(el);
    });
    document.addEventListener("click", function (e) {
      var el = e.target && e.target.closest ? e.target.closest("[data-glon-event]") : null;
      if (!el) return;
      e.preventDefault();
      /* pointerup already handled this press a moment ago */
      if (el.__glonFiredAt && Date.now() - el.__glonFiredAt < 700) return;
      fireGon(el);
    });
  }

  /* Touch controls: map Pointer Events onto the SAME Glon key events the
     keyboard emits (kaka-key-down/up). Press-and-hold moves/fires; pointer
     capture + pointercancel/lostpointercapture guarantee a release is never
     lost, so movement can never get stuck. No mobile-specific game rules. */
  function wireTouch() {
    var hold = document.querySelectorAll("[data-kaka-hold]");
    for (var i = 0; i < hold.length; i++) {
      (function (b) {
        var name = b.getAttribute("data-kaka-hold");
        function down(e) {
          e.preventDefault();
          if (b.setPointerCapture && e.pointerId !== undefined) { try { b.setPointerCapture(e.pointerId); } catch (err) {} }
          b.classList.add("on");
          pressTick[name] = tickCount;
          evVal("kaka-key-down", name);
        }
        function up(e) {
          e.preventDefault();
          b.classList.remove("on");
          var discrete = (name === "fire" || name === "glon" || name === "rock");
          if (discrete && tickCount === pressTick[name]) pendingUp[name] = true;  /* let one tick see it */
          else evVal("kaka-key-up", name);
        }
        b.addEventListener("pointerdown", down);
        b.addEventListener("pointerup", up);
        b.addEventListener("pointercancel", up);
        b.addEventListener("lostpointercapture", up);
        b.addEventListener("contextmenu", function (e) { e.preventDefault(); });
      })(hold[i]);
    }
  }

  function fetchText(u) {
    var v = u + "?v=" + VER;         /* bust the Pages 10-minute cache */
    return fetch(v).then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status + " " + v); return r.text(); });
  }

  /* Track the real visible viewport (mobile browser chrome can show/hide) as a
     CSS variable, so the landscape layout can size the canvas from available
     height instead of guessing. Desktop ignores it. */
  var viewW = 0;                       /* logical viewport width currently applied */
  function fitView() {
    var vv = window.visualViewport;
    var W = Math.round((vv && vv.width) || window.innerWidth);
    var H = Math.round((vv && vv.height) || window.innerHeight);
    document.documentElement.style.setProperty("--app-h", H + "px");
    var canvas = document.querySelector("#glon-canvas");
    if (!canvas) return;
    /* landscape phones use a WIDER logical viewport (more world, not stretched):
       visible height stays 480, width follows the display aspect. */
    var landscape = window.matchMedia("(pointer: coarse) and (orientation: landscape)").matches;
    var vw = 640;
    if (landscape) vw = Math.max(640, Math.min(1600, Math.round(480 * (W / H) / 32) * 32));
    if (canvas.width !== vw || canvas.height !== 480) { canvas.width = vw; canvas.height = 480; }
    if (landscape) {
      var scale = Math.min((W * 0.98) / vw, (H * 0.98) / 480);
      canvas.style.width = Math.round(vw * scale) + "px";
      canvas.style.height = Math.round(480 * scale) + "px";
    } else {
      canvas.style.width = ""; canvas.style.height = "";
    }
    if (vw !== viewW) {
      viewW = vw;
      try { evVal("kaka-viewport", String(vw)); } catch (e) {}
    }
  }

  /* Optional fullscreen affordance for normal browser tabs. Hidden when the app
     is already installed/standalone or the API is unavailable; never automatic. */
  function isStandalone() {
    try {
      return window.matchMedia("(display-mode: standalone)").matches
          || window.matchMedia("(display-mode: fullscreen)").matches
          || window.navigator.standalone === true;
    } catch (e) { return false; }
  }
  function fsElement() { return document.fullscreenElement || document.webkitFullscreenElement || null; }
  function setupFullscreen() {
    var btn = document.getElementById("kaka-fs");
    if (!btn) return;
    var supported = !!(document.fullscreenEnabled || document.webkitFullscreenEnabled);
    function refresh() {
      if (!supported || (isStandalone() && !fsElement())) { btn.hidden = true; return; }
      btn.hidden = false;
      btn.textContent = fsElement() ? "\u2715 EXIT" : "\u26f6 FULLSCREEN";
    }
    btn.addEventListener("click", function (e) {
      e.preventDefault();
      try {
        if (fsElement()) {
          var x = document.exitFullscreen || document.webkitExitFullscreen; x.call(document);
        } else {
          var el = document.getElementById("kaka-stage") || document.documentElement;
          var rq = el.requestFullscreen || el.webkitRequestFullscreen;
          var p = rq.call(el);
          if (p && p.catch) p.catch(function () {});
        }
      } catch (err) { console.error("kaka: fullscreen failed", err); }
      setTimeout(fitView, 50);
      focusGame();
    });
    document.addEventListener("fullscreenchange", refresh);
    document.addEventListener("webkitfullscreenchange", refresh);
    refresh();
  }

  function boot() {
    if (ex.glon_init() !== 0) { console.error("kaka: glon_init failed"); return; }
    Promise.resolve()
      .then(function () { return fetchText("prelude.glon"); }).then(load)
      .then(function () { return fetchText("strings.glon"); }).then(load)
      .then(function () { return fetchText("kaka-lib.glon"); }).then(load)
      .then(function () { return fetchText("kaka.glon"); }).then(load)
      .then(function () { return fetchText("kaka-draw.glon"); }).then(load)
      .then(function () { return fetchText("kaka-wave.glon"); }).then(load)
      /* kaka-selftest.glon is test-only and is intentionally not loaded here:
         the production page needs the loader budget for input dispatch. */
      .then(loadParallax)            /* PNG scenery is presentation-only */
      .then(loadSprites)             /* actor sheets are presentation-only */
      .then(function () {
        wireClicks();
        wireTouch();
        /* Hand the one persisted number to Glon BEFORE the viewport-driven
           render below, so it cannot mirror a stale 0 over the stored high score. */
        evVal("kaka-highscore", String(readStoredHigh()));
        fitView();
        window.addEventListener("resize", fitView);
        window.addEventListener("orientationchange", fitView);
        if (window.visualViewport) {
          window.visualViewport.addEventListener("resize", fitView);
          window.visualViewport.addEventListener("scroll", fitView);
        }
        setupFullscreen();
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
        /* Visible build identifier: the deploy workflow injects the source SHA
           as window.__BUILD_SHA; locally it reads "dev". */
        var be = document.getElementById("kaka-build");
        if (be) be.textContent = "Kākā " + BUILD + " \u00b7 " + (window.__BUILD_SHA || "dev");
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
