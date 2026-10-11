/* demos/kaka/kaka-host.js -- thin browser glue for Attack of the Mutant Kaka.
 *
 * It only: loads the Glon runtime + game source, maps keys to Glon events, runs
 * the browser animation loop (Glon tick -> Glon render -> paint), and paints the
 * compact canvas script Glon emits. All game state, collisions, targeting,
 * physics, damage, spawning, mutant timers, scoring and tree regeneration live
 * in kaka.glon / kaka-lib.glon / kaka-rangi.glon / kaka-draw.glon /
 * kaka-wave.glon / kaka-selftest.glon. No game logic here.
 */
(function () {
  "use strict";
  /* Build token. GitHub Pages serves with Cache-Control: max-age=600, so a
   * stale cached kaka.glon / kaka-draw.glon / wasm / PNG would keep an old
   * frame (e.g. the flat background or geometric actors) alive for minutes.
   * Bump this whenever published game assets change. */
  var BUILD = "D12S.17";   /* human-visible label; SHA injected at deploy */
  var VER = "d12s17";
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
             "#8e6bd8", "#7fd18a", "#ff3df0", "#ffffff",
             "#b9a888", "#a9aeb6"];   /* 16,17: muted pest belly tones (not white) */

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
        buildOccluders();
      });
  }
  /* ---- deliberate forest occluders --------------------------------------
     Glon draws the foreground layer BEFORE Rangi (he is never lost behind
     it) and rats behind it. For depth and ambush spots, a few vegetation
     clumps cut once from the foreground artwork are stamped AFTER Rangi at
     world-fixed x (1:1 with the gameplay plane), bottom-anchored, so they
     cover part of Rangi as he passes and can hide rats completely. Purely
     presentation: no collision or game-logic change. */
  var OCC_CROPS = [[1420, 380, 300, 344], [170, 330, 310, 394], [1840, 400, 260, 324]];
  var OCC_SCALE = 0.42, OCC_STEP = 540, OCC_PATTERN = [0, -1, 1, -1, 2, -1, 1, -1, 0, 2];
  var occImgs = [];
  function buildOccluders() {
    var img = layerImg[3]; occImgs = [];
    if (!img) return;
    for (var i = 0; i < OCC_CROPS.length; i++) {
      var r = OCC_CROPS[i], cw = Math.round(r[2] * OCC_SCALE), ch = Math.round(r[3] * OCC_SCALE);
      var c = document.createElement("canvas"); c.width = cw; c.height = ch;
      var g = c.getContext("2d"), fx, k;
      g.drawImage(img, r[0], r[1], r[2], r[3], 0, 0, cw, ch);
      /* feather the cut edges (left, right, top) so no straight seam shows */
      g.globalCompositeOperation = "destination-out";
      var ramps = [[0, 0, cw * 0.28, 0], [cw, 0, cw * 0.72, 0], [0, 0, 0, ch * 0.22]];
      for (k = 0; k < 3; k++) {
        var q = ramps[k]; fx = g.createLinearGradient(q[0], q[1], q[2], q[3]);
        fx.addColorStop(0, "rgba(0,0,0,1)"); fx.addColorStop(1, "rgba(0,0,0,0)");
        g.fillStyle = fx; g.fillRect(0, 0, cw, ch);
      }
      occImgs.push(c);
    }
  }
  function drawOccluders(ctx, W, H, tx) {
    if (!occImgs.length) return;
    var k0 = Math.floor((-tx - 200) / OCC_STEP), k1 = Math.ceil((-tx + W + 200) / OCC_STEP), k;
    for (k = k0; k <= k1; k++) {
      var t = OCC_PATTERN[((k % OCC_PATTERN.length) + OCC_PATTERN.length) % OCC_PATTERN.length];
      if (t < 0 || k < 1) continue;
      var c = occImgs[t], x = k * OCC_STEP + tx, flip = (k % 3) === 1;
      ctx.save(); ctx.translate(x, H + 6);
      if (flip) ctx.scale(-1, 1);
      ctx.drawImage(c, -c.width / 2, -c.height);
      ctx.restore();
    }
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

  /* ---- ranger (host-side cartoon protagonist) ----------------------------
   * Glon emits only `P <pose> <x> <y> <dir>` (0 idle, 1 run-a, 2 run-b, 3 jump,
   * 4 hit). The host draws a recognisable cartoon ranger and mirrors it for
   * facing; all gameplay state (x/y/jump/hit) stays in Glon. */
  function rrect(ctx, x, y, w, h, r) {
    r = Math.min(r, w / 2, h / 2);
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }
  /* ---- Rangi, the young explorer (forest + Skull Cave) -------------------
   * Pith helmet with compass badge, messy brown hair, big brown eyes, toothy
   * grin, green neckerchief, cream rolled-sleeve shirt, olive vest, satchel on
   * a cross strap, belt + gold buckle + compass pendant, khaki shorts, cream
   * socks, chunky lace-up boots; warm brown skin. Dark outline + two-tone flat
   * shading so he reads at phone size. Feet at y = 0, ~100 tall. Poses (Glon
   * owns which): 0 idle (hands on hips), 1/2 run frames, 3 jump, 4 hit,
   * 5 fallen, 6 climb (drawn as the jump pose). Host presentation only. */
  var RX = { skin: "#b8753f", skinD: "#93582a", cheek: "rgba(200,80,60,.45)",
    hair: "#4a2c16", hairD: "#2e1a0b", iris: "#6b3d18",
    helm: "#a49a72", helmD: "#7d7452", helmL: "#c4bb92", strap: "#3b2f20",
    gold: "#e2b23c", navy: "#24315a", kerch: "#3f8a2e", kerchD: "#2b6620",
    shirt: "#efe6cf", shirtD: "#cfc3a3", vest: "#6b7a32",
    leather: "#7a4a24", leatherD: "#55321a", shorts: "#a39668", shortsD: "#7f7550",
    sock: "#e9e2cf", boot: "#5e3a1d", bootD: "#3a2210", lace: "#e09a3a", ink: "#1d140b" };
  function rxOl(ctx, w) { ctx.strokeStyle = RX.ink; ctx.lineWidth = w || 2; ctx.lineJoin = "round"; ctx.lineCap = "round"; ctx.stroke(); }
  function rxBlob(ctx, x, y, rx, ry, fill) { ctx.beginPath(); ctx.ellipse(x, y, rx, ry, 0, 0, 2 * Math.PI); ctx.fillStyle = fill; ctx.fill(); rxOl(ctx); }
  function rxBox(ctx, x, y, w, h, r, fill) { rrect(ctx, x, y, w, h, r); ctx.fillStyle = fill; ctx.fill(); rxOl(ctx); }
  function rxLimb(ctx, pts, w, col) {          /* outlined thick polyline */
    var i;
    ctx.lineCap = "round"; ctx.lineJoin = "round";
    ctx.strokeStyle = RX.ink; ctx.lineWidth = w + 3.4; ctx.beginPath(); ctx.moveTo(pts[0], pts[1]);
    for (i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]); ctx.stroke();
    ctx.strokeStyle = col; ctx.lineWidth = w; ctx.beginPath(); ctx.moveTo(pts[0], pts[1]);
    for (i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]); ctx.stroke();
  }
  function rxCompass(ctx, x, y, r) {
    rxBlob(ctx, x, y, r, r, RX.gold); rxBlob(ctx, x, y, r * 0.68, r * 0.68, RX.navy);
    ctx.fillStyle = RX.gold; ctx.beginPath();
    for (var i = 0; i < 8; i++) { var a = i * Math.PI / 4 - Math.PI / 2, d = i % 2 ? r * 0.22 : r * 0.62; ctx.lineTo(x + Math.cos(a) * d, y + Math.sin(a) * d); }
    ctx.closePath(); ctx.fill();
  }
  function rxLeg(ctx, hx, fx, kneeDx, lift) {
    var kx = (hx + fx) / 2 + kneeDx, ky = -19 - lift / 2, sx = hx + (kx - hx) * 0.35;
    rxLimb(ctx, [kx, ky, fx, -11 - lift], 6.5, RX.skin);                   /* bare shin */
    rxBox(ctx, fx - 4.6, -15 - lift, 9.2, 6, 2, RX.sock);                  /* sock */
    ctx.beginPath(); ctx.moveTo(fx - 6, -11 - lift); ctx.lineTo(fx + 5, -11 - lift); ctx.lineTo(fx + 6, -6 - lift);   /* boot */
    ctx.quadraticCurveTo(fx + 13, -6 - lift, fx + 13, -1.5 - lift); ctx.lineTo(fx + 13, -lift); ctx.lineTo(fx - 7, -lift); ctx.closePath();
    ctx.fillStyle = RX.boot; ctx.fill(); rxOl(ctx);
    ctx.fillStyle = RX.bootD; ctx.fillRect(fx - 7, -2.2 - lift, 20, 2.2);
    ctx.strokeStyle = RX.lace; ctx.lineWidth = 1.3; ctx.beginPath();
    ctx.moveTo(fx - 2, -10 - lift); ctx.lineTo(fx + 3, -8 - lift); ctx.moveTo(fx + 3, -10 - lift); ctx.lineTo(fx - 2, -8 - lift);
    ctx.moveTo(fx - 1, -7 - lift); ctx.lineTo(fx + 4, -5 - lift); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(hx - 7, -34); ctx.lineTo(hx + 7, -34); ctx.lineTo(sx + 7, -22); ctx.lineTo(sx - 7, -22); ctx.closePath();   /* shorts */
    ctx.fillStyle = RX.shorts; ctx.fill(); rxOl(ctx);
    ctx.fillStyle = RX.shortsD; ctx.fillRect(sx - 6.4, -25, 12.8, 2);
  }
  function rxArm(ctx, sx, ex, ey, hx, hy) {     /* shoulder -> elbow (sleeve) -> hand */
    rxLimb(ctx, [ex, ey, hx, hy], 5.5, RX.skin);
    rxLimb(ctx, [sx, -50, ex, ey], 7.5, RX.shirt);
    rxBox(ctx, ex - 4.5, ey - 2.8, 9, 5.6, 2.5, RX.shirtD);                 /* rolled cuff */
    rxBlob(ctx, hx, hy, 4.4, 4.4, RX.skin);
  }
  function rxHead(ctx, hit) {
    var hy = -74, xs = [-6.5, 7], i, x;
    ctx.beginPath(); ctx.ellipse(-2, hy - 1, 19, 16, 0, 0, 2 * Math.PI); ctx.fillStyle = RX.hair; ctx.fill(); rxOl(ctx);
    rxBlob(ctx, -15.5, hy + 2, 4.2, 5.4, RX.skin);                           /* ear */
    ctx.fillStyle = RX.skinD; ctx.beginPath(); ctx.ellipse(-15.5, hy + 2.5, 1.8, 2.8, 0, 0, 2 * Math.PI); ctx.fill();
    rxBlob(ctx, 0.5, hy + 1, 16.5, 16, RX.skin);                             /* face */
    ctx.fillStyle = RX.skinD; ctx.beginPath(); ctx.ellipse(0.5, hy + 11, 12, 4.4, 0, 0, Math.PI); ctx.fill();
    ctx.beginPath(); ctx.moveTo(-16, hy - 1); ctx.quadraticCurveTo(-15, hy - 15, -2, hy - 14);       /* messy fringe */
    ctx.lineTo(-6, hy - 6); ctx.lineTo(0, hy - 11); ctx.lineTo(1, hy - 4); ctx.lineTo(7, hy - 11); ctx.lineTo(9, hy - 5);
    ctx.quadraticCurveTo(14, hy - 9, 16, hy - 4); ctx.quadraticCurveTo(16, hy - 18, 0, hy - 18); ctx.quadraticCurveTo(-18, hy - 18, -16, hy - 1); ctx.closePath();
    ctx.fillStyle = RX.hair; ctx.fill(); rxOl(ctx, 1.8);
    if (hit) {
      ctx.strokeStyle = RX.ink; ctx.lineWidth = 2.4;
      for (i = 0; i < 2; i++) { x = xs[i]; ctx.beginPath(); ctx.moveTo(x - 3.4, hy - 2); ctx.lineTo(x + 3.4, hy + 5); ctx.moveTo(x + 3.4, hy - 2); ctx.lineTo(x - 3.4, hy + 5); ctx.stroke(); }
    } else {
      for (i = 0; i < 2; i++) {                                              /* big brown eyes */
        x = xs[i];
        ctx.beginPath(); ctx.ellipse(x, hy + 1.5, 4.3, 5.4, 0, 0, 2 * Math.PI); ctx.fillStyle = "#fff"; ctx.fill(); ctx.strokeStyle = RX.ink; ctx.lineWidth = 1.4; ctx.stroke();
        ctx.fillStyle = RX.iris; ctx.beginPath(); ctx.arc(x + 0.9, hy + 2.4, 3.1, 0, 2 * Math.PI); ctx.fill();
        ctx.fillStyle = RX.ink; ctx.beginPath(); ctx.arc(x + 1, hy + 2.6, 1.7, 0, 2 * Math.PI); ctx.fill();
        ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(x + 2, hy + 1.1, 1, 0, 2 * Math.PI); ctx.fill();
      }
      ctx.strokeStyle = RX.hairD; ctx.lineWidth = 2;                           /* brows */
      ctx.beginPath(); ctx.moveTo(-10.5, hy - 5); ctx.quadraticCurveTo(-6.5, hy - 7.5, -2.5, hy - 5.5);
      ctx.moveTo(3.5, hy - 5.5); ctx.quadraticCurveTo(7.5, hy - 7.5, 11, hy - 5); ctx.stroke();
    }
    ctx.fillStyle = RX.cheek;
    ctx.beginPath(); ctx.ellipse(-10.5, hy + 9, 3.4, 2.2, 0, 0, 2 * Math.PI); ctx.fill();
    ctx.beginPath(); ctx.ellipse(12.5, hy + 9, 3.4, 2.2, 0, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = RX.skinD; ctx.beginPath(); ctx.ellipse(2, hy + 6, 2.6, 2, 0, 0, 2 * Math.PI); ctx.fill();   /* nose */
    ctx.beginPath(); ctx.moveTo(-5, hy + 9.5); ctx.quadraticCurveTo(1.5, hy + 18, 9, hy + 9.5); ctx.closePath();  /* grin */
    ctx.fillStyle = "#6e2418"; ctx.fill(); ctx.strokeStyle = RX.ink; ctx.lineWidth = 1.5; ctx.stroke();
    ctx.beginPath(); ctx.moveTo(-4.4, hy + 10); ctx.lineTo(8.4, hy + 10); ctx.lineTo(7.5, hy + 12); ctx.lineTo(-3.5, hy + 12); ctx.closePath(); ctx.fillStyle = "#fff"; ctx.fill();
    ctx.save(); ctx.translate(0, hy - 13); ctx.rotate(-0.07);               /* pith helmet */
    ctx.beginPath(); ctx.ellipse(1, 2, 25, 6, 0, 0, 2 * Math.PI); ctx.fillStyle = RX.helmD; ctx.fill(); rxOl(ctx);
    ctx.beginPath(); ctx.moveTo(-17, 1); ctx.quadraticCurveTo(-17, -19, 1, -19); ctx.quadraticCurveTo(19, -19, 19, 1); ctx.closePath();
    ctx.fillStyle = RX.helm; ctx.fill(); rxOl(ctx);
    ctx.fillStyle = RX.helmL; ctx.beginPath(); ctx.ellipse(-6, -11, 6, 3.4, -0.5, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = RX.strap; rrect(ctx, -17, -4.5, 36, 4.2, 1.5); ctx.fill();
    rxBox(ctx, 9, -6, 6, 7, 1.2, RX.gold);
    rxCompass(ctx, 0, -11, 5.4);
    ctx.restore();
  }
  function rxBody(ctx, pose) {
    var lf = -4, rf = 5, lk = -2, rk = 2, ll = 0, rl = 0, hit = false;
    var la = [-25, -43, -16.5, -32], ra = [25, -43, 16.5, -32];            /* elbow x,y, hand x,y */
    if (pose === 1) { lf = 12; rf = -11; lk = 0; rk = 0; rl = 5; la = [-12, -38, -4, -27]; ra = [22, -44, 30, -38]; }
    else if (pose === 2) { lf = -11; rf = 12; lk = 0; rk = 0; ll = 5; la = [22, -44, 30, -38]; ra = [-12, -38, -4, -27]; }
    else if (pose === 3) { lf = 8; rf = -8; lk = -7; rk = -7; ll = 7; rl = 7; la = [-20, -58, -22, -70]; ra = [20, -58, 24, -72]; }
    else if (pose === 4) { hit = true; la = [-16, -40, -12, -30]; ra = [24, -44, 32, -36]; }
    rxLeg(ctx, -6, -6 + lf, lk, ll); rxLeg(ctx, 6, 6 + rf, rk, rl);
    ctx.beginPath(); ctx.moveTo(-14, -31); ctx.quadraticCurveTo(-16, -54, 0, -56); ctx.quadraticCurveTo(16, -54, 14, -31); ctx.closePath();   /* shirt */
    ctx.fillStyle = RX.shirt; ctx.fill(); rxOl(ctx);
    ctx.beginPath(); ctx.moveTo(-14, -31); ctx.quadraticCurveTo(-16, -52, -6, -54); ctx.lineTo(-4, -31); ctx.closePath(); ctx.fillStyle = RX.vest; ctx.fill(); rxOl(ctx, 1.6);
    ctx.beginPath(); ctx.moveTo(14, -31); ctx.quadraticCurveTo(16, -52, 6, -54); ctx.lineTo(4, -31); ctx.closePath(); ctx.fillStyle = RX.vest; ctx.fill(); rxOl(ctx, 1.6);
    ctx.strokeStyle = RX.ink; ctx.lineWidth = 4.6; ctx.beginPath(); ctx.moveTo(11, -54); ctx.lineTo(-11, -30); ctx.stroke();   /* satchel strap */
    ctx.strokeStyle = RX.leather; ctx.lineWidth = 2.8; ctx.beginPath(); ctx.moveTo(11, -54); ctx.lineTo(-11, -30); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(-10, -55); ctx.quadraticCurveTo(0, -51, 10, -55); ctx.lineTo(2, -45); ctx.lineTo(-1, -45); ctx.closePath();   /* neckerchief */
    ctx.fillStyle = RX.kerch; ctx.fill(); rxOl(ctx, 1.6);
    ctx.fillStyle = RX.kerchD; ctx.beginPath(); ctx.moveTo(-1, -48); ctx.lineTo(2, -48); ctx.lineTo(1.5, -45.5); ctx.lineTo(-0.5, -45.5); ctx.fill();
    rxBox(ctx, -15, -35, 30, 6, 2.5, RX.leather); rxBox(ctx, -4, -36, 8, 8, 1.5, RX.gold);   /* belt + buckle */
    ctx.fillStyle = RX.leatherD; ctx.fillRect(-2, -34, 4, 4);
    rxCompass(ctx, 7, -25.5, 3.3);
    rxBox(ctx, -21, -33, 11, 10, 3, RX.leather); rxBox(ctx, -21, -33, 11, 4, 2, RX.leatherD);   /* satchel */
    ctx.fillStyle = RX.gold; ctx.fillRect(-17, -30, 3, 3);
    rxArm(ctx, -12, la[0], la[1], la[2], la[3]); rxArm(ctx, 12, ra[0], ra[1], ra[2], ra[3]);
    rxHead(ctx, hit);
  }
  function drawRanger(ctx, pose, x, y, dir, tx) {
    if (pose === 6) pose = 3;          /* climb pose reuses the jump limbs */
    var cx = x + tx, fy = y + 28;
    ctx.save();
    ctx.translate(cx, fy);
    if (dir < 0) ctx.scale(-1, 1);
    if (caveMode) ctx.scale(CAVE_RANGI_S, CAVE_RANGI_S);  /* cave presentation only; feet anchored */
    ctx.globalAlpha = rangerAlpha;      /* phone route dissolves the ranger */
    ctx.fillStyle = "rgba(0,0,0,.25)";
    if (pose === 5) {                   /* fallen: lying on his back, X eyes, head forward */
      ctx.beginPath(); ctx.ellipse(8, 2, 44, 6, 0, 0, 2 * Math.PI); ctx.fill();
      ctx.translate(-6, -15); ctx.rotate(Math.PI / 2);
      rxBody(ctx, 4);
      ctx.restore();
      return;
    }
    ctx.beginPath(); ctx.ellipse(0, 2, 22, 5.5, 0, 0, 2 * Math.PI); ctx.fill();
    ctx.save();
    if (pose === 1 || pose === 2) ctx.rotate(-0.06);
    else if (pose === 4) ctx.rotate(0.12);
    rxBody(ctx, pose);
    ctx.restore();
    ctx.restore();
  }

  /* ---- Skull Cave distance signs ----------------------------------------
   * Fixed, purely cosmetic world markers. Glon emitted them as `N` ops until
   * f33e086, which dropped draw-signs/signs to fit the loader heap; there is
   * still no room for them in Glon (the browser loader heap is tighter than the
   * native harness), so the host paints them from the camera Glon already
   * emits in `B`. Positions are the original world-x table (no gameplay). */
  var SIGN_X = [500, 1250, 2000, 2750, 3500, 4000];
  /* Forest ground calibration (presentation only). Glon's ground plane
     (Rangi F_Y 330 -> feet at canvas y 358; pests F_Y 345; signs 345) dates
     from the procedural ground and sits at the fence-rail height of the PNG
     scenery, whose walkable lawn runs ~y 395-450 in the near-landscape layer.
     Every forest gameplay-plane draw (actors, Rangi, trees, laser, ghosts,
     signs) is shifted by this one amount, so relative positions, jumps and
     collisions are unchanged and everything stands on the painted lawn. The
     parallax layers themselves are not moved; the cave and fighter use 0. */
  var FOREST_GROUND_DY = 48;
  /* The walkable forest trail. Measured: the painted lawn's front edge under
     Rangi swings ~70 px (canvas y ~360..432) as the near-landscape layer
     scrolls at 0.70x while the gameplay plane moves at 1.0x, so no constant
     offset can keep his feet on painted ground everywhere (the median lawn
     line, ~401-405, is what FOREST_GROUND_DY = 48 matches). Nothing painted
     moved with the gameplay plane; this band does. It sits on Glon's ground
     line (Rangi F_Y 330 -> feet y 358 + ty) and scrolls exactly with the world
     (world-fixed pebbles and tufts), so actors are grounded at every scroll
     position. Cheap: one gradient band + ~20 small marks per frame. */
  var TRAIL_FEET = 358;
  function drawTrail(ctx, W, tx, ty) {
    var y0 = TRAIL_FEET + ty, top = y0 - 9, bot = y0 + 15, g, x, wx, h, k;
    g = ctx.createLinearGradient(0, top - 6, 0, bot + 8);
    g.addColorStop(0, "rgba(96,74,44,0)"); g.addColorStop(0.16, "rgba(110,84,50,.92)");
    g.addColorStop(0.55, "rgba(92,68,40,.95)"); g.addColorStop(0.85, "rgba(70,52,30,.85)"); g.addColorStop(1, "rgba(60,46,26,0)");
    ctx.fillStyle = g; ctx.fillRect(0, top - 6, W, bot - top + 14);
    /* world-fixed pebbles and grass tufts (hash of world x) -> 1:1 motion */
    var x0 = Math.floor(-tx / 23) * 23;
    for (wx = x0; wx < -tx + W + 23; wx += 23) {
      k = (wx * 2654435761) >>> 0; x = wx + tx;
      h = (k >>> 8) % 100;
      if (h < 38) {                                   /* pebble */
        ctx.fillStyle = h < 19 ? "rgba(150,132,104,.8)" : "rgba(58,44,28,.7)";
        ctx.beginPath(); ctx.ellipse(x + (k % 13), y0 - 2 + (k >>> 4) % 12, 2.5 + h % 3, 1.6, 0, 0, 2 * Math.PI); ctx.fill();
      } else if (h < 70) {                            /* grass tuft on the trail edge */
        var ex = x + (k % 17), ey = (h & 1) ? top + 2 : bot - 1;
        ctx.strokeStyle = "rgba(120,160,60,.85)"; ctx.lineWidth = 1.4;
        ctx.beginPath(); ctx.moveTo(ex, ey); ctx.lineTo(ex - 3, ey - 6); ctx.moveTo(ex, ey); ctx.lineTo(ex + 1, ey - 8);
        ctx.moveTo(ex, ey); ctx.lineTo(ex + 4, ey - 5); ctx.stroke();
      }
    }
  }
  var SIGN_LABELS = [
    "SKULL CAVE 100 km", "SKULL CAVE 75 km", "SKULL CAVE 50 km",
    "SKULL CAVE 25 km", "SKULL CAVE 5 km", "SKULL CAVE \u2014 NEXT EXIT"
  ];
  var signsDue = false;
  function drawSigns(ctx, W, cam, tx, ty) {
    for (var i = 0; i < SIGN_X.length; i++)
      if (SIGN_X[i] > cam - 120 && SIGN_X[i] < cam + W + 120) drawSign(ctx, SIGN_X[i], i, tx, ty);
  }
  function drawSign(ctx, x, label, tx, ty) {
    var t = SIGN_LABELS[label];
    if (!t) return;
    var sx = Math.round(x + tx), gy = 345 + (ty || 0);
    ctx.save();
    ctx.font = "bold 20px system-ui, sans-serif";
    ctx.textAlign = "center"; ctx.textBaseline = "middle";
    var w = Math.max(96, ctx.measureText(t).width + 30), h = 38, cy = gy - 120;
    ctx.fillStyle = "rgba(0,0,0,.24)";
    ctx.beginPath(); ctx.ellipse(sx, gy + 2, w * 0.5, 7, 0, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = "#6b4a26"; ctx.fillRect(sx - 6, cy + h / 2 - 2, 12, gy - (cy + h / 2));
    ctx.fillStyle = "#e6c98a"; rrect(ctx, sx - w / 2, cy - h / 2, w, h, 6); ctx.fill();
    ctx.strokeStyle = "#5c3f20"; ctx.lineWidth = 4;
    rrect(ctx, sx - w / 2, cy - h / 2, w, h, 6); ctx.stroke();
    ctx.fillStyle = "#2a1b0c"; ctx.fillText(t, sx, cy + 1);
    ctx.restore();
  }

  /* ---- cave route animation state (host-only; not a Glon binding) -------- */
  var caveRoute = 0, caveRouteT0 = 0, rangerAlpha = 1;
  /* the cave backdrop IMAGE (replaceable via drawCaveBackground); null until
     loaded, and null forever if the asset fails -> procedural fallback. */
  var caveImg = null;
  var caveMode = false;              /* set from the V/B ops each frame */
  var CAVE_W = 1800;                 /* cave world width (matches Glon bounds) */
  /* The cave image IS the visual reference now: 1 image px = 1 world px, drawn
     at world x=0, so the baked pedestals land at world x ~1120/1300/1480 (the
     Glon cave pedestal centres were moved there to match). CAVE_IMG_Y shifts the
     backdrop vertically so the image's floor sits on the cave ground line. */
  var CAVE_IMG_S = 1.0;
  var CAVE_IMG_X = 0;
  var CAVE_IMG_Y = -320;
  var CAVE_RANGI_S = 1.6;            /* cave-only player presentation scale */
  function loadCaveImage() {
    return loadImage("assets/Moonlit Skull Cave Adventure.png")
      .then(function (im) { caveImg = im; });
  }

  /* ---- Skull Cave scene (host paints the semantic `V` bg + `J` props) ----
   * Presentation is layered far-bg / props / ranger so the procedural backdrop
   * below can later be swapped for a host image (`ctx.drawImage(caveBg, ...)`)
   * WITHOUT touching Glon semantics. This function is the single replaceable
   * backdrop path; Glon only ever emits `V <cam>` + `J <world-x> <kind>`. */
  function drawCaveBackground(ctx, W, H, cam) {
    if (caveImg) {
      /* One uniform transform aligns the image's baked pedestal/relic trio with
         the live Glon pedestal positions (760/1000/1240); aspect preserved, no
         stretch. Cropped horizontally/vertically as needed; the base fill shows
         where the image does not reach (near the cave's right wall). Gameplay
         coordinates are untouched (drawn 1:1 with the camera offset). */
      ctx.fillStyle = "#201c26"; ctx.fillRect(0, 0, W, H);   /* gap base (the image's right-edge colour) */
      var sw = caveImg.naturalWidth * CAVE_IMG_S, sh = caveImg.naturalHeight * CAVE_IMG_S;
      var ix = CAVE_IMG_X - cam;
      ctx.drawImage(caveImg, ix, CAVE_IMG_Y, sw, sh);
      /* Softly blend the seam where the aligned image ends (~world 1497, before
         the 1800 wall) into the dark gap -- reads as deeper cave, no hard line. */
      var seam = ix + sw;
      var g = ctx.createLinearGradient(seam, 0, seam + 100, 0);
      g.addColorStop(0, "rgba(32,28,38,0)"); g.addColorStop(1, "rgba(32,28,38,1)");
      ctx.fillStyle = g; ctx.fillRect(seam, 0, 100, H);
      return;
    }
    /* procedural fallback (unchanged) if the asset is missing */
    var g = ctx.createLinearGradient(0, 0, 0, H);
    g.addColorStop(0, "#07060d"); g.addColorStop(0.6, "#161126"); g.addColorStop(1, "#241a2e");
    ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = "#0c0916";
    for (var i = 0; i < 16; i++) {
      var sx = ((i * 173) % (W + 80)) - 40;
      ctx.beginPath(); ctx.moveTo(sx - 26, 0); ctx.lineTo(sx + 26, 0);
      ctx.lineTo(sx, 54 + ((i * 47) % 72)); ctx.closePath(); ctx.fill();
    }
    ctx.fillStyle = "rgba(0,0,0,.35)"; ctx.fillRect(0, 350, W, H - 350);
  }
  function drawCaveProp(ctx, x, kind, tx) {
    /* With the cave IMAGE as the backdrop, its baked-in art already supplies the
       skull (0), pedestals (1-3), relics (4-6) and guardian (7): skip those so we
       don't double them. The live dynamic overlays -- highlight/glow (10) and the
       route effects (11 portal / 12 beam / 13 time-box) -- still draw, as does
       Rangi (P). The procedural fallback (no image) draws everything. */
    /* The HELMET relic is the fighter exit: always draw a strong, obvious
       portal beacon for it (even when the cave IMAGE supplies the artwork),
       plus an off-screen pointer so the player never has to hunt for it. */
    if (kind === 4) {
      var cw = ctx.canvas.width, hx = x + tx, oy = 185, t4 = performance.now() / 1000;
      ctx.save();
      if (hx < -20 || hx > cw + 20) {
        var left = hx < 0, ex = left ? 18 : cw - 18, dir = left ? -1 : 1;
        ctx.globalAlpha = 0.55 + 0.45 * Math.sin(t4 * 5);
        ctx.fillStyle = "#5fd0ff";
        ctx.beginPath(); ctx.moveTo(ex + dir * 16, 150); ctx.lineTo(ex - dir * 12, 118); ctx.lineTo(ex - dir * 12, 182); ctx.closePath(); ctx.fill();
        ctx.globalAlpha = 1;
        var g = ctx.createRadialGradient(ex - dir * 12, 150, 2, ex - dir * 12, 150, 40);
        g.addColorStop(0, "rgba(170,240,255,.7)"); g.addColorStop(1, "rgba(95,208,255,0)");
        ctx.fillStyle = g; ctx.beginPath(); ctx.arc(ex - dir * 12, 150, 40, 0, 2 * Math.PI); ctx.fill();
        /* helmet glyph facing the exit direction */
        ctx.fillStyle = "#cfefff";
        ctx.beginPath(); ctx.arc(ex - dir * 30, 150, 9, Math.PI, 0); ctx.fill();
        ctx.fillRect(ex - dir * 39, 150, 18, 5); ctx.fillStyle = "#16203a"; ctx.fillRect(ex - dir * 35, 147, 10, 6);
      } else {
        var glow = ctx.createRadialGradient(hx, oy, 2, hx, oy, 74);
        glow.addColorStop(0, "rgba(160,242,255,.6)"); glow.addColorStop(1, "rgba(95,208,255,0)");
        ctx.fillStyle = glow; ctx.beginPath(); ctx.arc(hx, oy, 74, 0, 2 * Math.PI); ctx.fill();
        var cg = ctx.createLinearGradient(0, oy - 160, 0, oy + 12);
        cg.addColorStop(0, "rgba(120,230,255,0)"); cg.addColorStop(1, "rgba(160,242,255,.45)");
        ctx.fillStyle = cg; ctx.fillRect(hx - 26, oy - 160, 52, 172);
        var rr = 30 + 5 * Math.sin(t4 * 4);
        for (var ri = 0; ri < 3; ri++) {
          ctx.globalAlpha = 0.72 - ri * 0.2; ctx.lineWidth = 4; ctx.strokeStyle = "rgba(185,246,255,.95)";
          ctx.beginPath(); ctx.arc(hx, oy, rr + ri * 12, 0, 2 * Math.PI); ctx.stroke();
        }
        ctx.globalAlpha = 1;
        ctx.fillStyle = "#dff6ff";
        ctx.beginPath(); ctx.moveTo(hx, oy - 46); ctx.lineTo(hx - 10, oy - 62); ctx.lineTo(hx + 10, oy - 62); ctx.closePath(); ctx.fill();
      }
      ctx.restore();
      return;
    }
    if (caveImg && kind <= 7) return;
    var sx = Math.round(x + tx), gy = 380;
    ctx.save();
    if (kind === 0) {                          /* skull on the cave wall */
      var cy = 188;
      ctx.fillStyle = "#e8e2d2";
      ctx.beginPath(); ctx.arc(sx, cy, 54, Math.PI, 0); ctx.fill();
      ctx.fillRect(sx - 54, cy - 2, 108, 34);
      ctx.beginPath(); ctx.arc(sx, cy + 30, 28, 0, Math.PI); ctx.fill();
      ctx.fillStyle = "#0a0a12";
      ctx.beginPath(); ctx.ellipse(sx - 23, cy + 4, 15, 17, 0, 0, 2 * Math.PI); ctx.fill();
      ctx.beginPath(); ctx.ellipse(sx + 23, cy + 4, 15, 17, 0, 0, 2 * Math.PI); ctx.fill();
      ctx.beginPath(); ctx.moveTo(sx, cy + 20); ctx.lineTo(sx - 7, cy + 34); ctx.lineTo(sx + 7, cy + 34); ctx.closePath(); ctx.fill();
      for (var t = -20; t <= 20; t += 10) ctx.fillRect(sx + t - 2, cy + 42, 4, 12);
    } else if (kind === 7) {                   /* sleeping Phantom in a hammock */
      var hy = 200;
      ctx.globalAlpha = 0.55;
      ctx.strokeStyle = "#7a5fbf"; ctx.lineWidth = 5;
      ctx.beginPath(); ctx.moveTo(sx - 60, hy - 24); ctx.quadraticCurveTo(sx, hy + 22, sx + 60, hy - 24); ctx.stroke();
      ctx.fillStyle = "#5b4a92";
      ctx.beginPath(); ctx.ellipse(sx, hy - 6, 44, 15, 0.06, 0, 2 * Math.PI); ctx.fill();
      ctx.beginPath(); ctx.arc(sx + 40, hy - 14, 12, 0, 2 * Math.PI); ctx.fill();
      ctx.fillStyle = "#9a8ad8"; ctx.fillRect(sx + 30, hy - 20, 20, 9);       /* mask */
      ctx.fillStyle = "#1a1330"; ctx.fillRect(sx + 36, hy - 17, 3, 3); ctx.fillRect(sx + 43, hy - 17, 3, 3);
      ctx.fillStyle = "#2a2340"; ctx.fillRect(sx - 66, hy - 24, 6, 64); ctx.fillRect(sx + 60, hy - 24, 6, 64);
      ctx.globalAlpha = 1;
    } else if (kind === 10) {                  /* selected-object highlight */
      ctx.strokeStyle = "#ffe066"; ctx.lineWidth = 4;
      ctx.beginPath(); ctx.arc(sx, 122, 34, 0, 2 * Math.PI); ctx.stroke();
      ctx.fillStyle = "#ffe066";
      ctx.beginPath(); ctx.moveTo(sx, 62); ctx.lineTo(sx - 11, 76); ctx.lineTo(sx + 11, 76); ctx.closePath(); ctx.fill();
    } else if (kind >= 1 && kind <= 3) {       /* pedestal block, top at 280 */
      ctx.fillStyle = "#241f3a"; ctx.fillRect(sx - 42, 210, 84, gy - 210);
      ctx.fillStyle = "#4b4376"; ctx.fillRect(sx - 42, 202, 84, 10);
      ctx.fillStyle = "rgba(150,120,255,.3)"; ctx.fillRect(sx - 34, 204, 68, 5);
    } else if (kind === 4 || kind === 5 || kind === 6) {   /* pedestal objects */
      var oy = 185;
      ctx.fillStyle = "rgba(150,180,255,.22)";               /* soft glow */
      ctx.beginPath(); ctx.arc(sx, oy, 27, 0, 2 * Math.PI); ctx.fill();
      if (kind === 4) {                        /* sci-fi helmet */
        ctx.fillStyle = "#a9bede"; ctx.beginPath(); ctx.arc(sx, oy, 17, Math.PI, 0); ctx.fill();
        ctx.fillRect(sx - 17, oy, 34, 12);
        ctx.fillStyle = "#16203a"; ctx.fillRect(sx - 13, oy - 3, 26, 12);
        ctx.fillStyle = "#e9f1ff"; ctx.fillRect(sx - 13, oy - 6, 26, 3);
      } else if (kind === 5) {                 /* flip phone / communicator */
        ctx.fillStyle = "#2b3550"; rrect(ctx, sx - 11, oy - 21, 22, 42, 4); ctx.fill();
        ctx.fillStyle = "#6fe0c0"; ctx.fillRect(sx - 8, oy - 18, 16, 18);
        ctx.fillStyle = "#c9d4e6"; ctx.fillRect(sx - 8, oy + 3, 16, 15);
      } else {                                 /* key (blue-box route) */
        ctx.strokeStyle = "#f0cc54"; ctx.lineWidth = 6; ctx.lineCap = "round";
        ctx.beginPath(); ctx.arc(sx - 10, oy - 8, 9, 0, 2 * Math.PI); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(sx - 3, oy - 2); ctx.lineTo(sx + 17, oy + 17); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(sx + 7, oy + 7); ctx.lineTo(sx + 2, oy + 12); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(sx + 13, oy + 13); ctx.lineTo(sx + 8, oy + 18); ctx.stroke();
      }
    } else if (kind === 11) {                  /* helmet route: hangar/portal */
      var t = (performance.now() % 1200) / 1200;
      ctx.strokeStyle = "#5fd0ff"; ctx.lineWidth = 5;
      ctx.strokeRect(sx - 74, 48, 148, 158);
      ctx.beginPath(); ctx.moveTo(sx - 74, 48); ctx.lineTo(sx, 6); ctx.lineTo(sx + 74, 48); ctx.stroke();
      for (var r = 0; r < 3; r++) {
        ctx.globalAlpha = 0.55 - r * 0.14; ctx.lineWidth = 4; ctx.strokeStyle = "#8fe0ff";
        ctx.beginPath(); ctx.arc(sx, 120, 30 + r * 12 + t * 10, 0, 2 * Math.PI); ctx.stroke();
      }
      ctx.globalAlpha = 1;
    } else if (kind === 12) {                  /* phone route: transporter beam */
      var t2 = performance.now() / 1000;
      var bg = ctx.createLinearGradient(0, 20, 0, 380);
      bg.addColorStop(0, "rgba(120,255,220,0)");
      bg.addColorStop(0.5, "rgba(120,255,220,.55)");
      bg.addColorStop(1, "rgba(120,255,220,0)");
      ctx.fillStyle = bg; ctx.fillRect(sx - 28, 20, 56, 360);
      ctx.fillStyle = "rgba(190,255,238,.85)";
      for (var s = 0; s < 12; s++) {
        var yy = (s * 41 + (t2 * 160) % 380) % 380;
        ctx.fillRect(sx - 22 + (s * 9) % 44, 386 - yy, 4, 4);
      }
    } else if (kind === 13) {                  /* key route: time-box materialises */
      var t3 = Math.min(1, (performance.now() - caveRouteT0) / 1500);
      ctx.globalAlpha = t3; ctx.strokeStyle = "#e8c24a"; ctx.lineWidth = 5;
      var bw = 124 * t3, bh = 150 * t3;
      ctx.strokeRect(sx - bw / 2, 210 - bh, bw, bh);
      ctx.strokeStyle = "rgba(235,215,150,.7)"; ctx.lineWidth = 3;
      ctx.strokeRect(sx - bw / 2 + 12, 210 - bh + 12, bw - 24, bh - 24);
      ctx.globalAlpha = 1;
      ctx.fillStyle = "rgba(240,200,90,.4)";
      ctx.beginPath(); ctx.arc(sx, 125, 24 + 6 * Math.sin(performance.now() / 200), 0, 2 * Math.PI); ctx.fill();
    } else if (kind === 14) {                  /* bundled rope near the cave roof */
      var rb = 44;
      ctx.strokeStyle = "#c8a05a"; ctx.lineWidth = 5; ctx.lineCap = "round";
      for (var q = 0; q < 4; q++) { ctx.beginPath(); ctx.arc(sx, rb, 9 + q * 5, 0.2, Math.PI - 0.2); ctx.stroke(); }
      ctx.fillStyle = "#e0c07a"; ctx.fillRect(sx - 13, rb - 6, 26, 9);
    } else if (kind === 15) {                  /* dropped vertical rope */
      var swy = 5 * Math.sin(performance.now() / 350);
      ctx.strokeStyle = "#c8a05a"; ctx.lineWidth = 6; ctx.lineCap = "round";
      ctx.beginPath(); ctx.moveTo(sx, 16); ctx.quadraticCurveTo(sx + swy, 120, sx, 214); ctx.stroke();
      ctx.fillStyle = "#a8823f";
      for (var q2 = 0; q2 < 5; q2++) ctx.fillRect(sx - 6, 34 + q2 * 36, 12, 6);
    }
    ctx.restore();
  }
  /* night overlay for the forest as the ranger nears Skull Cave */
  function drawNight(ctx, W, H) {
    var g = ctx.createLinearGradient(0, 0, 0, H);
    g.addColorStop(0, "rgba(6,10,32,.74)"); g.addColorStop(1, "rgba(6,10,32,.5)");
    ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = "rgba(255,255,255,.85)";
    for (var i = 0; i < 44; i++) ctx.fillRect((i * 211) % W, (i * 97) % 150, 2, 2);
  }

  /* ---- pest-heaven ghosts (host-side presentation only) ------------------
   * Glon emits a semantic death event `D sp id wx wy` the moment a pest dies.
   * The HOST stages the funeral: a small BOUNDED list of presentation-only
   * ghosts. Each rises in SCREEN space from its death position (x is frozen at
   * birth, so camera scrolling never drags a soul sideways), wobbles, fades near
   * the top and is removed once it exits the top of the viewport. Ghosts have no
   * collision, HP, targeting, score or tuple cost -- pure theatre. */
  var GHOST_CAP = 32, GHOST_DUR = 2.4, GHOST_RISE = 175, GHOST_SEEN = 192;
  var ghosts = [];
  var ghostSeen = [];
  function clearGhosts() { ghosts.length = 0; ghostSeen.length = 0; }
  function addGhost(sp, id, wx, wy, tx) {
    /* `sp` (species) is retained on the record for future sound/stats/special
       events, but every species gets the SAME funeral: one golden halo. */
    if (ghostSeen.indexOf(id) >= 0) return;
    ghostSeen.push(id);
    if (ghostSeen.length > GHOST_SEEN) ghostSeen.shift();
    ghosts.push({ sp: sp, x: wx + tx, y: wy, t0: performance.now(), seed: id % 17 });
    if (ghosts.length > GHOST_CAP) ghosts.shift();
  }
  function drawGhosts(ctx, W) {
    var now = performance.now();
    for (var i = ghosts.length - 1; i >= 0; i--) {
      var g = ghosts[i];
      var e = (now - g.t0) / 1000;
      var prog = e / GHOST_DUR;
      var y = g.y - GHOST_RISE * e;
      if (prog >= 1 || y < -50) { ghosts.splice(i, 1); continue; }
      var x = g.x + Math.sin(e * 7 + g.seed) * 6;
      var a = prog < 0.65 ? 0.95 : Math.max(0, 0.95 * (1 - (prog - 0.65) / 0.35));
      var pulse = 1 + 0.12 * Math.sin(e * 9 + g.seed);
      ctx.save();
      /* soft golden glow */
      ctx.globalAlpha = a * 0.35;
      var grd = ctx.createRadialGradient(x, y, 2, x, y, 20 * pulse);
      grd.addColorStop(0, "#ffe9a8");
      grd.addColorStop(1, "rgba(255,210,63,0)");
      ctx.fillStyle = grd;
      ctx.beginPath(); ctx.arc(x, y, 20 * pulse, 0, 2 * Math.PI); ctx.fill();
      /* golden ring (the halo) */
      ctx.globalAlpha = a;
      ctx.strokeStyle = "#ffd23f";
      ctx.lineWidth = 3.5;
      ctx.beginPath(); ctx.arc(x, y, 9 * pulse, 0, 2 * Math.PI); ctx.stroke();
      /* pale core */
      ctx.globalAlpha = a * 0.9;
      ctx.fillStyle = "#fff6d8";
      ctx.beginPath(); ctx.arc(x, y, 4.5, 0, 2 * Math.PI); ctx.fill();
      ctx.restore();
    }
    window.__ghostN = ghosts.length;
    window.__ghostInfo = ghosts.map(function (g) { return { sp: g.sp, x: Math.round(g.x), y: Math.round(g.y - GHOST_RISE * ((now - g.t0) / 1000)) }; });
  }

  /* ---- D12S.12 helmet route: first-person trench mission -----------------
   * Glon emits a semantic fighter frame (`A` header + `G`/`U`/`K`/`Y`), the
   * host projects it over the static cockpit art and paints the dynamic
   * overlays (foes, bolts, gates, reticle, target, HUD, speed FX). No game
   * rule is computed here; only presentation. */
  var fighterImg = null;
  var fighterUiOn = false;
  /* In the fighter the free FIRE control is the boost and the vertical axis is
     UP/DOWN, so relabel the touch buttons and reveal the DOWN key. */
  function setFighterUi(on) {
    if (on === fighterUiOn) return;
    fighterUiOn = on;
    var berry = document.querySelector('[data-kaka-hold="fire"]');
    var jump = document.querySelector('[data-kaka-hold="up"]');
    var down = document.querySelector('[data-kaka-hold="down"]');
    if (berry) berry.textContent = on ? "BOOST" : "BERRY";
    if (jump) jump.textContent = on ? "UP" : "JUMP";
    if (down) down.style.display = on ? "" : "none";
    if (!on && wasBoost) { wasBoost = false; stopBoost(); }   /* clean boost cut on exit */
  }
  var fActive = false, lastCanvasScript = "";
  var fPx = 0, fPy = 0, fPhase = 0, fShield = 0, fBoost = 0, fLaser = 0, fProg = 0;
  var fLives = 0, fGaugeShield = 99, fGaugeLives = 99, fGaugeFlashS = 0, fGaugeFlashL = 0;
  var fFoes = [], fBolts = [], fGate = null, fBomb = 0, fFlash = 0, fPrevShield = -1;
  /* Winding canyon (D12S.23). Glon owns can-x (the collision centre) and emits
     the half-width in the `A` header. The host MIRRORS the exact same integer
     sine LUT + amplitude ramp + integer division here (keep in sync with
     kaka-fighter.glon) so rendering and collision share one smooth curve -- a
     piecewise-linear table aliased into the angular kinks that read as zig-zag. */
  var CAN_L = 480, CAN_SIN_STEP = 15, CAN_STRAIGHT = 60;
  var CAN_SIN = [0,195,383,556,707,831,924,981,1000,981,924,831,707,556,383,195,
                 0,-195,-383,-556,-707,-831,-924,-981,-1000,-981,-924,-831,-707,-556,-383,-195,0];
  function truncDiv(a, b) { return a >= 0 ? Math.trunc(a / b) : -Math.trunc(-a / b); }
  function canAmp(z) { return 40 + truncDiv(110 * (z < CAN_L ? z : CAN_L), CAN_L); }
  function canX(z) {
    z = Math.max(0, Math.floor(z) - CAN_STRAIGHT);
    var i = z % CAN_L, k = Math.floor(i / CAN_SIN_STEP), f = i - k * CAN_SIN_STEP;
    var sv = CAN_SIN[k] + truncDiv((CAN_SIN[k + 1] - CAN_SIN[k]) * f, CAN_SIN_STEP);
    return truncDiv(canAmp(z) * sv, 1000);
  }
  var fCanX = 0, fCanW = 110, fYaw = 0, fWallHit = 0, fWallSide = 0;
  /* ONE world scale for everything in the trench: a canyon sample dd ticks
     ahead sits at render depth dd * CAN_Z. Foes close 3 depth/tick while fprog
     advances 1, so CAN_Z = 3 also places every foe/bolt (emitted in that depth
     unit) in the same canyon cross-section Glon used to spawn it. */
  var CAN_Z = 3;
  var FCAN_NEAR = 6, FCAN_END = 200, FCAN_STEP = 4;   /* sampled ticks ahead */
  var FCAN_RIB = 25;
  var FDECO = 10;
  var FSURF = [26, 31, 41];   /* Death Star surface metal */
  var FSURF_LINES = 8;        /* longitudinal surface seams each side (every 80 units) */      /* one wall-detail slot per 10 ticks of flight, world-fixed */   /* one structural rib per second of flight, world-fixed */
  var FFOCAL = 230;
  /* WALL SURFACE COORDINATES. The canyon walls are continuous, addressable
     surfaces, not just a collision limit: a point on a wall is (side, s, v)
     with side -1 left / +1 right, s = mission distance (fprog units, the same
     axis as can-x) and v = height (CAN_TOP wall top .. CAN_BOT floor). The wall
     renderer is built from canWall(), so anything attached later (gun
     emplacements, machinery, vents, gantries, targetable structures) placed
     with the same (side, s, v) sits exactly on the drawn and collided surface,
     and scrolls/bends with it. Glon owns such entities' state; it only needs
     can-x and the half-width to agree with this mapping. */
  var CAN_TOP = -150, CAN_BOT = 150;
  function canWall(side, s, v) { return { x: canX(s) + side * fCanW, y: v, s: s }; }
  /* project a world point at mission distance s (a wall point, an attachment) */
  function fProjS(W, H, p) { return fProj(W, H, p.x, p.y, (p.s - fProg) * CAN_Z, 1); }
  var prevFoes = 0, prevBolts = 0, lastLaserMs = 0, laserSide = false, wasBoost = false;
  var FXCOL = { wall: "#39b7ff", dim: "rgba(80,180,255,.45)", foe: "#ff3b3b",
                bolt: "#ff8a3b", gate: "#5fd0ff", good: "#7fd18a", hot: "#ffd23f" };
  function loadFighterImage() {
    return loadImage("assets/vector-trench-cockpit.png").then(function (im) { fighterImg = im; });
  }
  function fScale(W) { return fighterImg ? W / fighterImg.naturalWidth : 1; }
  var fCenterMemo = { W: -1, H: -1, img: null, v: null };
  function fCenter(W, H) {
    if (fCenterMemo.W === W && fCenterMemo.H === H && fCenterMemo.img === fighterImg) return fCenterMemo.v;
    fCenterMemo.W = W; fCenterMemo.H = H; fCenterMemo.img = fighterImg; fCenterMemo.v = fCenterRaw(W, H);
    return fCenterMemo.v;
  }
  function fCenterRaw(W, H) {
    if (!fighterImg) return { x: W * 0.5, y: H * 0.62, sc: 1 };
    var sc = fScale(W);
    return { x: W * 0.5, y: (H - fighterImg.naturalHeight * sc) + 520 * sc, sc: sc };
  }
  function fProj(W, H, ox, oy, oz, zmin) {
    var c = fCenter(W, H);
    var z = Math.max(oz, zmin || 16);
    /* The world uses the cockpit's own scale (design width 640) so the canyon
       keeps its size relative to the frame on wider phone canvases. */
    var vk = Math.max(1, W / 640);
    var s = FFOCAL * vk / z;
    return { x: c.x + fYawPx(W) + (ox - fPx) * s, y: c.y - fPitchPx(H) + (oy - fPy) * s * 0.62, s: s };
  }
  /* The camera heads along the canyon tangent at the ship (small-angle yaw =
     a constant screen shift of focal * slope / CAN_Z), so the corridor near the
     ship runs straight ahead and the bend ahead visibly sweeps off to one side. */
  /* Above the trench rim the camera tilts down with height (no extra
     controls): the horizon rises so the surface and the trench winding below
     come into view. Inside the trench there is no tilt. */
  function fPitchPx(H) { return fPy < CAN_TOP ? Math.min(H * 0.38, (CAN_TOP - fPy) * 0.62) : 0; }
  function fYawPx(W) {
    var px = -FFOCAL * Math.max(1, W / 640) * fYaw / CAN_Z;
    return Math.max(-W * 0.3, Math.min(W * 0.3, px));
  }
  /* The cockpit's SOLID body (central console, sills and the two canopy
     struts with their red bars), traced from the artwork in image pixels
     (the art is left/right symmetric about x = 836). Everything outside it is
     canopy window: the baked straight trench there is blacked out and the live
     canyon is drawn into it, including the lower side windows, so a wall
     beside the ship reaches the bottom corners of the view (no look-past at
     the near end of a wall). The outline runs past the image edges so a
     climbing ship's frame stays covered. */
  var FCOCKPIT = [345, 1341, 345, 835, -10, 336, -10, 137, 370, 770, 522, 765, 582, 697, 690, 670,
                  982, 670, 1090, 697, 1150, 765, 1302, 770, 1682, 137, 1682, 336, 1327, 835, 1327, 1341];
  /* The cockpit body as a cached overlay: the artwork cut down to FCOCKPIT
     (window pixels transparent), built ONCE per canvas width. Each frame the
     sky and canyon are drawn unclipped and this is stamped on top with one
     drawImage. (A per-frame complex clip here multiplied GPU-process work ~10x
     and was the cause of the long-run phone freeze / black screen.) */
  var fCockpitCache = null;
  /* Dashboard instrument wells (image px x, y, w, h), measured from the art's
     two red bar banks: left = shield (Glon F_C), right = Rangi's lives (F_B).
     The host only presents these Glon values. */
  var FGAUGE = [[605, 755, 54, 104], [1015, 755, 53, 104]];
  var FGAUGE_SEGS = 3;   /* shield steps (fighter-enter: 3) and LIVES_MAX (3) */
  function drawGauges(ctx, ox, oy, sc) {
    var now = performance.now(), vals = [fShield, fLives], flash = [fGaugeFlashS, fGaugeFlashL];
    var lit = [["#7fe8ff", "#2a9fd0"], ["#ff5a4a", "#b3261a"]], i, k;
    for (i = 0; i < 2; i++) {
      var r = FGAUGE[i], x = ox + r[0] * sc, y = oy + r[1] * sc, w = r[2] * sc, h = r[3] * sc;
      var gap = 4 * sc, sh = (h - gap * (FGAUGE_SEGS - 1)) / FGAUGE_SEGS;
      var fl = flash[i] && now - flash[i] < 600 ? (Math.floor((now - flash[i]) / 100) % 2 === 0) : false;
      for (k = 0; k < FGAUGE_SEGS; k++) {
        var sy = y + h - (k + 1) * sh - k * gap, on = k < vals[i];
        ctx.fillStyle = fl ? "#ffffff" : (on ? lit[i][0] : "#1a1f2a");
        ctx.fillRect(x, sy, w, sh);
        if (on && !fl) { ctx.fillStyle = lit[i][1]; ctx.fillRect(x, sy + sh * 0.62, w, sh * 0.38); }
      }
    }
  }
  function fCockpitOverlay(W) {
    if (!fighterImg) return null;
    if (fCockpitCache && fCockpitCache.W === W) return fCockpitCache.c;
    var sc = fScale(W), iw = Math.round(fighterImg.naturalWidth * sc), ih = Math.round(fighterImg.naturalHeight * sc);
    var c = document.createElement("canvas"), g, i;
    c.width = iw; c.height = ih; g = c.getContext("2d");
    g.drawImage(fighterImg, 0, 0, iw, ih);
    g.globalCompositeOperation = "destination-in";
    g.beginPath(); g.moveTo(FCOCKPIT[0] * sc, FCOCKPIT[1] * sc);
    for (i = 2; i < FCOCKPIT.length; i += 2) g.lineTo(FCOCKPIT[i] * sc, FCOCKPIT[i + 1] * sc);
    g.closePath(); g.fill();
    /* empty the two painted bar wells: the live gauges are drawn into them */
    g.globalCompositeOperation = "source-over"; g.fillStyle = "#06090f";
    for (i = 0; i < FGAUGE.length; i++)
      g.fillRect((FGAUGE[i][0] - 2) * sc, (FGAUGE[i][1] - 2) * sc, (FGAUGE[i][2] + 4) * sc, (FGAUGE[i][3] + 4) * sc);
    fCockpitCache = { W: W, c: c };
    return c;
  }
  /* cockpit body over the canyon (shakes with the boost like the cannons) */
  function drawCockpit(ctx, W, H) {
    var ov = fCockpitOverlay(W), sh = fShake(performance.now() / 1000), ay = fAlt(W, H);
    ctx.save(); ctx.translate(sh.x, sh.y);
    if (ov) {
      ctx.drawImage(ov, (W - ov.width) / 2, H - ov.height + ay);
      drawGauges(ctx, (W - ov.width) / 2, H - ov.height + ay, ov.width / fighterImg.naturalWidth);
      if (ay < 0) { ctx.fillStyle = "#01020a"; ctx.fillRect(-10, H + ay - 2, W + 20, -ay + 16); }
    } else {
      ctx.fillStyle = "#01020a"; ctx.fillRect(0, H * 0.72, W, H * 0.28);
    }
    if (fBoost > 0) { ctx.fillStyle = "rgba(120,200,255,.09)"; ctx.fillRect(-10, -10, W + 20, H + 20); }
    ctx.restore();
  }
  /* Corridor rendering: a SOLID canyon. Every cross-section is derived from
     the SAME canX(fProg + dd) +/- fCanW that Glon collides against, at render
     depth dd * CAN_Z, so the surface that kills the ship is the surface drawn.
     Between successive samples three dark surfaces are filled (floor,
     left wall, right wall), painted FAR to NEAR so a near wall on the inside of
     a bend genuinely hides the corridor behind it. Neon edges ride on top:
     continuous floor and wall-top rails (plus a faint mid-wall rail) carry the path,
     and sparse world-fixed ribs (one per second of flight) stream past as the
     depth/speed cue. Surfaces fog into the void with distance. Gate barriers
     are drawn in the same cross-section, in their exact blocked region. */
  /* Surfaces are OPAQUE and clearly lighter than the void: side walls are a
     solid slate-blue slab, floor darker; the trench is open overhead. Fog is capped (kmax) so even
     the farthest wall stays visibly wall, never dissolving into the void.
     `hot` tints toward red (a wall hit), `warn` toward amber (closing on it). */
  var FBG = [1, 2, 10];
  var FWALL = [22, 54, 98], FFLOOR = [11, 28, 50];
  function fFog(rgb, t, kmax, hot, warn) {
    var k = Math.min(kmax, t * t * 0.9), r = rgb[0], g = rgb[1], b = rgb[2];
    if (warn > 0) { r += (215 - r) * warn; g += (120 - g) * warn; b += (30 - b) * warn; }
    if (hot > 0) { r += (190 - r) * hot; g += (28 - g) * hot; b += (34 - b) * hot; }
    return "rgb(" + Math.round(r + (FBG[0] - r) * k) + "," + Math.round(g + (FBG[1] - g) * k) +
           "," + Math.round(b + (FBG[2] - b) * k) + ")";
  }
  /* scrape warning 0..1 over the last 30 units before the wall at fCanW */
  function fWallWarn() {
    var d = fPy < CAN_TOP ? 0 : Math.abs(fPx - fCanX);   /* above the rim nothing to scrape */
    return { amt: Math.max(0, Math.min(1, (d - (fCanW - 30)) / 30)), side: fPx > fCanX ? 1 : -1 };
  }
  function drawCanyonWalls(ctx, W, H, gate) {
    var top = CAN_TOP, mid = 0, bot = CAN_BOT, w = fCanW, j, d, a, b, t, e;
    var S = [], ribs = [], decos = [], gd = -1, ri, di, gateMark = null, gatePat = 0;
    var hot = fWallHit ? Math.max(0, 1 - (performance.now() - fWallHit) / 450) : 0;
    var ww = fWallWarn(), wl, wr, near, above = fPy < CAN_TOP, visL = true, visR = true;
    ctx.save();
    ctx.lineCap = "round"; ctx.lineJoin = "round";
    /* one cross-section dd ticks ahead, built from the wall-surface mapping */
    function cross(dd) {
      var s = fProg + dd;
      function wp(side, v) { return fProjS(W, H, canWall(side, s, v)); }
      var c = { d: dd, cx: canX(s), z: dd * CAN_Z,
        fl: wp(-1, bot), cl: wp(-1, top), ml: wp(-1, mid),
        fr: wp(1, bot), cr: wp(1, top), mr: wp(1, mid),
        ul: wp(-1, -75), ur: wp(1, -75), ll: wp(-1, 75), lr: wp(1, 75) };
      if (above) {
        /* Death Star surface at the rim height: outer edge sized to just cover
           the screen at this depth, plus grid points every 80 units outward */
        var L = W / c.fl.s + Math.abs(fPx - c.cx) + 160, k;
        c.sol = fProj(W, H, c.cx - w - L, top, c.z, 1); c.sor = fProj(W, H, c.cx + w + L, top, c.z, 1);
        c.gl = []; c.gr = [];
        for (k = 1; k <= FSURF_LINES; k++) {
          c.gl.push(fProj(W, H, c.cx - w - k * 80, top, c.z, 1)); c.gr.push(fProj(W, H, c.cx + w + k * 80, top, c.z, 1));
        }
      }
      return c;
    }
    /* a wall face is drawn only when the camera can see its inner side:
       inside/below the rim always; above the rim, not the wall you are over */
    function wallVis(side, cx) { return !above || (side < 0 ? fPx > cx - w : fPx < cx + w); }
    /* ---- wall surface detail ------------------------------------------
       Flat, wall-mounted, world-fixed items every FDECO ticks, staggered
       between the walls (they stream past = speed/depth cue, and their
       perspective shows each wall's orientation). Painted inside the
       far-to-near pass, so near walls still hide them; nothing protrudes into
       the flight path, so none reads as an obstacle. ~40 small fills/frame. */
    function wq(side, s0, s1, v0, v1) {           /* wall-surface quad */
      var a0 = fProjS(W, H, canWall(side, s0, v0)), a1 = fProjS(W, H, canWall(side, s1, v0));
      var b1 = fProjS(W, H, canWall(side, s1, v1)), b0 = fProjS(W, H, canWall(side, s0, v1));
      ctx.beginPath(); ctx.moveTo(a0.x, a0.y); ctx.lineTo(a1.x, a1.y); ctx.lineTo(b1.x, b1.y); ctx.lineTo(b0.x, b0.y); ctx.closePath();
    }
    function decor(dd) {
      var s = fProg + dd, n = Math.round(s / FDECO), f = Math.max(0, 1 - dd / FCAN_END), side, kind, q, r;
      if (f < 0.12) return;
      for (side = -1; side <= 1; side += 2) {
        if (!wallVis(side, canX(s))) continue;
        kind = (n + (side > 0 ? 3 : 0)) % 8;
        ctx.globalAlpha = 0.25 + 0.75 * f;
        if (kind === 0 || kind === 5) {           /* running light high on the wall */
          /* capped size: a near light stays a small lamp, never a big disc */
          q = fProjS(W, H, canWall(side, s, -112)); r = Math.min(4, Math.max(1.2, 5 * q.s));
          ctx.fillStyle = kind === 0 ? "#7fe8ff" : "#ffcf5a";
          ctx.beginPath(); ctx.arc(q.x, q.y, r, 0, 2 * Math.PI); ctx.fill();
          ctx.globalAlpha *= 0.3; ctx.beginPath(); ctx.arc(q.x, q.y, r * 2.2, 0, 2 * Math.PI); ctx.fill();
        } else if (kind === 1) {                  /* vent grille */
          wq(side, s - 3, s + 3, 30, 70); ctx.fillStyle = "#0a1626"; ctx.fill();
          ctx.strokeStyle = "#4a7aa8"; ctx.lineWidth = Math.max(0.6, 1.4 * f); ctx.stroke();
          for (var k = 1; k < 4; k++) { var v = 30 + k * 10;
            q = fProjS(W, H, canWall(side, s - 3, v)); r = fProjS(W, H, canWall(side, s + 3, v));
            ctx.beginPath(); ctx.moveTo(q.x, q.y); ctx.lineTo(r.x, r.y); ctx.stroke(); }
        } else if (kind === 2) {                  /* conduit pipe with brackets */
          wq(side, s - 5, s + 5, -58, -50); ctx.fillStyle = "#3d5d80"; ctx.fill();
          ctx.strokeStyle = "#9cc6ee"; ctx.lineWidth = Math.max(0.6, 1.2 * f); ctx.stroke();
          wq(side, s - 0.6, s + 0.6, -62, -46); ctx.fillStyle = "#c8d6e8"; ctx.fill();
        } else if (kind === 4) {                  /* machinery panel + status light */
          wq(side, s - 3, s + 3, -30, 15); ctx.fillStyle = "#1b3a5e"; ctx.fill();
          ctx.strokeStyle = "#6fb0e6"; ctx.lineWidth = Math.max(0.6, 1.3 * f); ctx.stroke();
          q = fProjS(W, H, canWall(side, s, -18)); ctx.fillStyle = (n & 1) ? "#7dffa0" : "#ff6a5a";
          ctx.beginPath(); ctx.arc(q.x, q.y, Math.min(3, Math.max(1, 3 * q.s)), 0, 2 * Math.PI); ctx.fill();
        } else if (kind === 6) {                  /* low yellow/black warning band */
          wq(side, s - 4, s + 4, 118, 132); ctx.fillStyle = "#c9a227"; ctx.fill();
          ctx.strokeStyle = "#1a1408"; ctx.lineWidth = Math.max(0.8, 2 * f);
          for (var m2 = -3; m2 <= 3; m2 += 2) {
            q = fProjS(W, H, canWall(side, s + m2 - 1, 132)); r = fProjS(W, H, canWall(side, s + m2 + 1, 118));
            ctx.beginPath(); ctx.moveTo(q.x, q.y); ctx.lineTo(r.x, r.y); ctx.stroke(); }
        }
      }
      ctx.globalAlpha = 1;
    }
    /* q/r are the panel's FAR corners. Panels are painted far to near, so each
       nearer panel is stretched ~1.5 px past the seam it shares with the panel
       behind it: the anti-aliased hairline is covered and nothing leaks
       through, without a per-panel stroke (which was costly on the GPU). */
    function over(f, n) {
      var dx = f.x - n.x, dy = f.y - n.y, l = Math.sqrt(dx * dx + dy * dy) || 1;
      return { x: f.x + 1.5 * dx / l, y: f.y + 1.5 * dy / l };
    }
    function quad(p, q, r, s, col) {
      var q2 = over(q, p), r2 = over(r, s);
      ctx.fillStyle = col; ctx.beginPath();
      ctx.moveTo(p.x, p.y); ctx.lineTo(q2.x, q2.y); ctx.lineTo(r2.x, r2.y); ctx.lineTo(s.x, s.y);
      ctx.closePath(); ctx.fill();
    }
    function edge(k, alpha, width, color) {
      ctx.globalAlpha = alpha; ctx.strokeStyle = color; ctx.lineWidth = width;
      ctx.beginPath();
      if (visL || k === "c") { ctx.moveTo(a[k + "l"].x, a[k + "l"].y); ctx.lineTo(b[k + "l"].x, b[k + "l"].y); }
      if (visR || k === "c") { ctx.moveTo(a[k + "r"].x, a[k + "r"].y); ctx.lineTo(b[k + "r"].x, b[k + "r"].y); }
      ctx.stroke();
    }
    function rib(c) {
      var f = Math.max(0, 1 - c.d / FCAN_END);
      ctx.globalAlpha = 0.65 * Math.pow(f, 1.1); ctx.strokeStyle = "#8fe0ff";
      ctx.lineWidth = 0.8 + 2.6 * f;
      /* posts on the two wall faces only: nothing bridges the open trench */
      ctx.beginPath();
      if (wallVis(-1, c.cx)) { ctx.moveTo(c.fl.x, c.fl.y); ctx.lineTo(c.cl.x, c.cl.y); }
      if (wallVis(1, c.cx)) { ctx.moveTo(c.fr.x, c.fr.y); ctx.lineTo(c.cr.x, c.cr.y); }
      ctx.stroke();
    }
    /* The near Death Star surface, one polygon per side (rim line out to the
       screen-covering outer edge) plus all longitudinal seams in one stroke.
       Seen from above it never overlaps the trench opening, so it is painted
       before the trench pass in 2 fills instead of ~100 overlapping slabs. */
    function nearSurface() {
      var i, k, side, n = S.length, g = ctx.createLinearGradient(0, S[n - 1].cl.y, 0, S[0].cl.y);
      g.addColorStop(0, fFog(FSURF, 1, 0.55, 0, 0)); g.addColorStop(1, fFog(FSURF, 0, 0.55, 0, 0));
      ctx.globalAlpha = 1; ctx.fillStyle = g;
      for (side = 0; side < 2; side++) {
        var rim = side ? "cr" : "cl", out = side ? "sor" : "sol";
        ctx.beginPath(); ctx.moveTo(S[0][rim].x, S[0][rim].y);
        for (i = 1; i < n; i++) ctx.lineTo(S[i][rim].x, S[i][rim].y);
        for (i = n - 1; i >= 0; i--) ctx.lineTo(S[i][out].x, S[i][out].y);
        ctx.closePath(); ctx.fill();
      }
      ctx.globalAlpha = 0.4; ctx.strokeStyle = "#4b6080"; ctx.lineWidth = 1;
      ctx.beginPath();
      for (k = 0; k < FSURF_LINES; k++) {
        ctx.moveTo(S[0].gl[k].x, S[0].gl[k].y); for (i = 1; i < n; i++) ctx.lineTo(S[i].gl[k].x, S[i].gl[k].y);
        ctx.moveTo(S[0].gr[k].x, S[0].gr[k].y); for (i = 1; i < n; i++) ctx.lineTo(S[i].gr[k].x, S[i].gr[k].y);
      }
      ctx.stroke(); ctx.globalAlpha = 1;
    }
    /* re-entry funnel: dashed centreline along the opening (where Glon's
       assist pulls the ship), brighter while the assist is engaged */
    function landingFunnel() {
      var i, n = S.length, on = fAssistOn();
      ctx.save(); ctx.setLineDash([10, 9]); ctx.lineDashOffset = -(performance.now() / 40) % 19;
      ctx.globalAlpha = on ? 0.95 : 0.55; ctx.strokeStyle = on ? "#7dffa0" : "#cfe9ff"; ctx.lineWidth = on ? 3 : 2;
      ctx.beginPath();
      for (i = 0; i < n; i += 2) {
        var q = fProj(W, H, S[i].cx, top, S[i].z, 1);
        if (i === 0) ctx.moveTo(q.x, q.y); else ctx.lineTo(q.x, q.y);
      }
      ctx.stroke(); ctx.restore();
    }
    /* strong readable rim: where the solid surface ends and the opening is */
    function rimEdges() {
      var i, n = S.length, side;
      for (side = 0; side < 2; side++) {
        var k = side ? "cr" : "cl";
        ctx.globalAlpha = 1; ctx.strokeStyle = "#0a0f18"; ctx.lineWidth = 5;
        ctx.beginPath(); ctx.moveTo(S[0][k].x, S[0][k].y); for (i = 1; i < n; i++) ctx.lineTo(S[i][k].x, S[i][k].y); ctx.stroke();
        ctx.strokeStyle = "#9fe8ff"; ctx.lineWidth = 2.4;
        ctx.beginPath(); ctx.moveTo(S[0][k].x, S[0][k].y); for (i = 1; i < n; i++) ctx.lineTo(S[i][k].x, S[i][k].y); ctx.stroke();
      }
    }
    /* Flat detail ON the surface (painted after it): seams every 12.5 ticks,
       armour channels, vents and lamps. Nothing here has height, so nothing
       within flying reach looks like an obstacle; tall structures are placed
       out of reach by surfaceStructures(). */
    function surfaceDetail() {
      var d, f, c, side, q0, q1, n, seams = new Path2D(), vents = new Path2D(), lampsC = new Path2D(), lampsA = new Path2D();
      for (d = 12.5 - (fProg % 12.5); d <= FCAN_END; d += 12.5) {
        if (d < FCAN_NEAR) continue;
        f = Math.max(0, 1 - d / FCAN_END); c = canX(fProg + d);
        var z = d * CAN_Z, L = W / (FFOCAL * Math.max(1, W / 640) / z) + Math.abs(fPx - c) + 160;
        for (side = -1; side <= 1; side += 2) {
          q0 = fProj(W, H, c + side * w, top, z, 1); q1 = fProj(W, H, c + side * (w + L), top, z, 1);
          seams.moveTo(q0.x, q0.y); seams.lineTo(q1.x, q1.y);
        }
        n = Math.round((fProg + d) / 12.5);
        for (side = -1; side <= 1; side += 2) {                /* flat vents / hatch plates */
          if ((n + (side > 0 ? 2 : 0)) % 4) continue;
          var off = w + 90 + ((n * 61) % 5) * 70;
          var a0 = fProj(W, H, c + side * off, top, z, 1), a1 = fProj(W, H, c + side * (off + 50), top, z, 1);
          var b0 = fProj(W, H, c + side * off, top, (d + 4) * CAN_Z, 1), b1 = fProj(W, H, c + side * (off + 50), top, (d + 4) * CAN_Z, 1);
          vents.moveTo(a0.x, a0.y); vents.lineTo(a1.x, a1.y); vents.lineTo(b1.x, b1.y); vents.lineTo(b0.x, b0.y); vents.closePath();
        }
        if (n % 2 === 0) surfLights(fProg + d, n, lampsC, lampsA);
      }
      ctx.globalAlpha = 0.4; ctx.strokeStyle = "#566b86"; ctx.lineWidth = 1; ctx.stroke(seams);
      ctx.globalAlpha = 0.8; ctx.fillStyle = "#11161f"; ctx.fill(vents); ctx.strokeStyle = "#3e536d"; ctx.stroke(vents);
      ctx.globalAlpha = 0.9; ctx.fillStyle = "#7fe8ff"; ctx.fill(lampsC); ctx.fillStyle = "#ffcf5a"; ctx.fill(lampsA);
      /* armour channels: darker double grooves along the trench at 320 / 640 */
      var i, k, m = S.length;
      ctx.globalAlpha = 0.55; ctx.strokeStyle = "#0b0f16"; ctx.lineWidth = 2;
      ctx.beginPath();
      for (k = 3; k < FSURF_LINES; k += 4) {
        ctx.moveTo(S[0].gl[k].x, S[0].gl[k].y); for (i = 1; i < m; i++) ctx.lineTo(S[i].gl[k].x, S[i].gl[k].y);
        ctx.moveTo(S[0].gr[k].x, S[0].gr[k].y); for (i = 1; i < m; i++) ctx.lineTo(S[i].gr[k].x, S[i].gr[k].y);
      }
      ctx.stroke(); ctx.globalAlpha = 1;
      surfaceStructures();
    }
    /* Structures with height: gun batteries, antennae, machinery clusters --
       ALL at |dx| >= 950 from the trench centre while the ship is limited to
       |dx| <= 850, so none is reachable (no collision needed). Batteries track
       and fire tracer bursts into the sky. Far-to-near; host-only, no Glon. */
    function surfaceStructures() {
      var now = performance.now() / 1000, base = Math.floor(fProg / 40) * 40, i, side;
      for (i = 18; i >= 0; i--) {
        var sB = base + i * 40, d = sB - fProg;
        if (d < 4 || d > 700) continue;
        var z = d * CAN_Z, f = Math.max(0.25, 1 - d / 700), idx = sB / 40, c = canX(sB);
        for (side = -1; side <= 1; side += 2) {
          var h = (idx * 2654435761 + (side > 0 ? 97 : 0)) >>> 0, kind = h % 3;
          var X = c + side * (960 + (h >>> 5) % 560);
          var pb = fProj(W, H, X, top, z, 1), sc = pb.s;
          if (pb.x < -200 || pb.x > W + 200) continue;
          ctx.globalAlpha = 0.45 + 0.55 * f;
          if (kind === 0) {                                  /* gun battery */
            var bw = Math.max(6, 70 * sc), bh = Math.max(2.5, 22 * sc), aim = Math.sin(now * 0.7 + idx) * 0.6 - side * 0.4;
            sc = Math.max(sc, 0.09);
            ctx.fillStyle = "#1c2433"; ctx.fillRect(pb.x - bw / 2, pb.y - bh, bw, bh);
            ctx.beginPath(); ctx.arc(pb.x, pb.y - bh, bw * 0.28, Math.PI, 0); ctx.fill();
            ctx.strokeStyle = "#5c7aa0"; ctx.lineWidth = Math.max(1, 1.4 * sc * 4); ctx.strokeRect(pb.x - bw / 2, pb.y - bh, bw, bh);
            var tipx = pb.x + Math.sin(aim) * 46 * sc, tipy = pb.y - bh - Math.cos(aim) * 46 * sc;
            ctx.strokeStyle = "#8fa6c4"; ctx.lineWidth = Math.max(1.2, 6 * sc);
            ctx.beginPath(); ctx.moveTo(pb.x, pb.y - bh - bw * 0.2); ctx.lineTo(tipx, tipy); ctx.stroke();
            var ph = (now * 1.3 + idx * 0.37 + (side > 0 ? 0.5 : 0)) % 1;
            if (ph < 0.35) {                                 /* burst: muzzle flash + tracers */
              ctx.fillStyle = "#ffe08a"; ctx.beginPath(); ctx.arc(tipx, tipy, Math.max(1.5, 7 * sc), 0, 2 * Math.PI); ctx.fill();
              ctx.strokeStyle = (idx % 2) ? "#ff5a3a" : "#7dff8a"; ctx.lineWidth = Math.max(1.3, 2.2 * sc * 2);
              for (var b = 0; b < 3; b++) {
                var u = ph * 2.4 + b * 0.18, len = Math.max(140, 900 * sc);
                var ex = tipx + Math.sin(aim) * len * u, ey = tipy - Math.cos(aim) * len * u;
                var tl = Math.max(8, 40 * sc);
                ctx.beginPath(); ctx.moveTo(ex, ey); ctx.lineTo(ex + Math.sin(aim) * tl, ey - Math.cos(aim) * tl); ctx.stroke();
              }
            }
          } else if (kind === 1) {                           /* antenna mast */
            var ah = 160 * sc;
            ctx.strokeStyle = "#6f86a6"; ctx.lineWidth = Math.max(1, 3 * sc);
            ctx.beginPath(); ctx.moveTo(pb.x, pb.y); ctx.lineTo(pb.x, pb.y - ah); ctx.moveTo(pb.x - 18 * sc, pb.y - ah * 0.7); ctx.lineTo(pb.x + 18 * sc, pb.y - ah * 0.7); ctx.stroke();
            ctx.fillStyle = ((now * 2 + idx) % 1) < 0.5 ? "#ff5a4a" : "#3a1410";
            ctx.beginPath(); ctx.arc(pb.x, pb.y - ah, Math.max(1.5, 4 * sc), 0, 2 * Math.PI); ctx.fill();
          } else {                                           /* machinery cluster */
            ctx.fillStyle = "#18202d"; ctx.strokeStyle = "#4b6587"; ctx.lineWidth = 1;
            var parts = [[-40, 50, 30], [10, 40, 48], [42, 28, 22]];
            for (var q = 0; q < 3; q++) {
              var px = pb.x + parts[q][0] * sc, pw = parts[q][1] * sc, ph2 = parts[q][2] * sc;
              ctx.fillRect(px - pw / 2, pb.y - ph2, pw, ph2); ctx.strokeRect(px - pw / 2, pb.y - ph2, pw, ph2);
            }
            ctx.fillStyle = "#ffcf5a"; ctx.fillRect(pb.x + 8 * sc, pb.y - 40 * sc, Math.max(1, 4 * sc), Math.max(1, 4 * sc));
          }
        }
      }
      ctx.globalAlpha = 1;
    }
    /* Ambient battle in the sky: fixed pools cycled deterministically from
       time (no allocation, no Glon, no collision): flak bursts, laser streaks,
       distant fighters (some exploding). Drawn before the trench and surface,
       so walls and metal occlude them; visible from inside the trench too. */
    function hash1(a) { a = (a ^ 61) ^ (a >>> 16); a = (a + (a << 3)) | 0; a ^= a >>> 4; a = Math.imul(a, 0x27d4eb2d); a ^= a >>> 15; return (a >>> 0) / 4294967296; }
    function battleSky() {
      var now = performance.now() / 1000, i, cyc, u, P, q, r, X, Y, Z;
      /* the battle swells and ebbs (~12 s): quieter and fiercer moments */
      var heat = 0.5 + 0.5 * Math.sin(now * 0.52);
      /* flak */
      for (i = 0; i < 16; i++) {
        if (i / 16 > 0.12 + 0.88 * heat) continue;
        P = 1.5 + (i % 5) * 0.37; u = (now / P + i * 0.731); cyc = Math.floor(u); u -= cyc;
        if (u > 0.55) continue;
        X = fCanX + (hash1(cyc * 31 + i) - 0.5) * 3600; Y = -420 - hash1(cyc * 17 + i * 7) * 900; Z = 1500 + hash1(cyc * 13 + i * 3) * 9000;
        q = fProj(W, H, X, Y, Z, 1); if (q.x < -40 || q.x > W + 40 || q.y < -40) continue;
        r = Math.max(3.5, Math.min(30, 220 * q.s * (0.3 + u)));
        ctx.globalAlpha = Math.max(0, 1 - u / 0.55);
        ctx.fillStyle = u < 0.12 ? "#fff6d0" : "#ff9a3a"; ctx.beginPath(); ctx.arc(q.x, q.y, r, 0, 2 * Math.PI); ctx.fill();
        ctx.fillStyle = "rgba(60,50,45,.6)"; ctx.beginPath(); ctx.arc(q.x + r * 0.3, q.y - r * 0.2, r * 0.7, 0, 2 * Math.PI); ctx.fill();
      }
      /* laser streaks crossing the sky */
      for (i = 0; i < 10; i++) {
        if (i / 10 > 0.15 + 0.85 * heat) continue;
        P = 0.9 + (i % 4) * 0.23; u = now / P + i * 0.413; cyc = Math.floor(u); u -= cyc;
        var h1 = hash1(cyc * 7 + i * 101), h2 = hash1(cyc * 11 + i * 37), h3 = hash1(cyc * 5 + i * 53);
        var ax = fCanX + (h1 - 0.5) * 4000, ay = -350 - h2 * 800, az = 2000 + h3 * 8000;
        var bx = ax + (h2 - 0.5) * 3000, by = ay - 200 + h1 * 400, bz = az + (h1 - 0.5) * 3000;
        var t0 = Math.max(0, u - 0.18), p0 = fProj(W, H, ax + (bx - ax) * t0, ay + (by - ay) * t0, az + (bz - az) * t0, 1);
        var p1 = fProj(W, H, ax + (bx - ax) * u, ay + (by - ay) * u, az + (bz - az) * u, 1);
        ctx.strokeStyle = (i % 3) ? "#ff3b2e" : "#4dff7a";
        ctx.globalAlpha = 0.3; ctx.lineWidth = 6; ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
        ctx.globalAlpha = 1; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
      }
      /* distant fighters; some are hit */
      for (i = 0; i < 8; i++) {
        P = 4 + (i % 3) * 1.3; u = now / P + i * 0.29; cyc = Math.floor(u); u -= cyc;
        var g1 = hash1(cyc * 3 + i * 211), g2 = hash1(cyc * 19 + i * 7);
        X = fCanX + (g1 - 0.5) * 3000 + (u - 0.5) * 2600 * (i % 2 ? 1 : -1); Y = -500 - g2 * 600; Z = 2500 + g1 * 6000 - u * 1500;
        q = fProj(W, H, X, Y, Z, 1); if (q.x < -40 || q.x > W + 40) continue;
        var sz = Math.max(4, Math.min(11, 150 * q.s)), dir = i % 2 ? 1 : -1;
        if (g2 < 0.25 && u > 0.7) {                          /* shot down */
          var e2 = (u - 0.7) / 0.3; ctx.globalAlpha = 1 - e2;
          ctx.fillStyle = "#ffd27a"; ctx.beginPath(); ctx.arc(q.x, q.y, sz * (1 + 3 * e2), 0, 2 * Math.PI); ctx.fill();
          continue;
        }
        ctx.globalAlpha = 0.9; ctx.fillStyle = "#0d1118";
        ctx.beginPath(); ctx.moveTo(q.x + dir * sz, q.y); ctx.lineTo(q.x - dir * sz, q.y - sz * 0.6); ctx.lineTo(q.x - dir * sz * 0.6, q.y); ctx.lineTo(q.x - dir * sz, q.y + sz * 0.6); ctx.closePath(); ctx.fill();
        ctx.fillStyle = "#7fe8ff"; ctx.fillRect(q.x - dir * sz - 1, q.y - 1, 2, 2);
      }
      ctx.globalAlpha = 1;
    }
    /* sparse lamps on the surface either side of the trench */
    function surfLights(s, n, pathC, pathA) {
      for (var side = -1; side <= 1; side += 2) {
        var cx = canX(s), off = [60 + ((n * 53) % 7) * 45, 320 + ((n * 37) % 5) * 70];
        for (var i = 0; i < 2; i++) {
          if ((n + i + (side > 0 ? 1 : 0)) % 3) continue;
          var q = fProjS(W, H, { x: cx + side * (w + off[i]), y: top, s: s });
          var r = Math.min(3.5, Math.max(1, 4 * q.s)), pth = i ? pathA : pathC;
          pth.moveTo(q.x + r, q.y); pth.arc(q.x, q.y, r, 0, 2 * Math.PI);
        }
      }
    }
    /* the far surface: one fogged band from the end of the drawn trench to the
       horizon, a haze line, and distant towers placed out of flying reach
       (|dx| >= 1100 while the ship is limited to |x| <= 700) */
    function farSurface() {
      var zEnd = FCAN_END * CAN_Z, pe = fProj(W, H, fCanX, top, zEnd, 1).y, ph = fProj(W, H, fCanX, top, 80000, 1).y;
      var g = ctx.createLinearGradient(0, ph, 0, pe);
      g.addColorStop(0, "#2a3446"); g.addColorStop(1, fFog(FSURF, 1, 0.55, 0, 0));
      ctx.globalAlpha = 1; ctx.fillStyle = g; ctx.fillRect(0, ph, W, Math.max(0, pe - ph) + 2);
      var hz = ctx.createLinearGradient(0, ph - 46, 0, ph);
      hz.addColorStop(0, "rgba(80,120,170,0)"); hz.addColorStop(1, "rgba(120,170,220,.35)");
      ctx.fillStyle = hz; ctx.fillRect(0, ph - 46, W, 46);
      ctx.strokeStyle = "#3d5068"; ctx.lineWidth = 1; ctx.globalAlpha = 0.5;  /* far panel rows */
      ctx.beginPath();
      for (var zz = zEnd * 1.35; zz < 40000; zz *= 1.35) {
        var y = fProj(W, H, fCanX, top, zz, 1).y; ctx.moveTo(0, y); ctx.lineTo(W, y);
      }
      ctx.stroke();
      var base = Math.ceil((fProg + FCAN_END) / 150) * 150, i, side;
      for (i = 0; i < 16; i++) {
        var sT = base + i * 150, z = (sT - fProg) * CAN_Z, idx = sT / 150;
        if (z > 30000) break;
        var fa = Math.min(1, (z - zEnd) / zEnd);
        for (side = -1; side <= 1; side += 2) {
          var X = canX(sT) + side * (1100 + ((idx * 397 + (side > 0 ? 211 : 0)) % 600));
          var Ht = 70 + (idx * 53 + (side > 0 ? 29 : 0)) % 170, wd = 45 + (idx * 17) % 30;
          var pb = fProj(W, H, X, top, z, 1), pt = fProj(W, H, X, top - Ht, z, 1), hw = wd * pb.s / 2;
          ctx.globalAlpha = fa; ctx.fillStyle = "#161d2a"; ctx.fillRect(pb.x - hw, pt.y, hw * 2, pb.y - pt.y);
          ctx.strokeStyle = "#4f6d8f"; ctx.lineWidth = 1; ctx.strokeRect(pb.x - hw, pt.y, hw * 2, pb.y - pt.y);
          ctx.fillStyle = (idx + side) % 2 ? "#ff5a4a" : "#ffcf5a";
          ctx.fillRect(pb.x - 1, pt.y - 2, 2, 2);
          var hh = (idx * 2654435761 + (side > 0 ? 7 : 0)) >>> 0;
          if (hh % 4 === 0) {                                /* damaged: smoke plume */
            var tn = performance.now() / 1000;
            for (var sm = 0; sm < 3; sm++) {
              var su = (tn * 0.35 + sm / 3 + idx * 0.13) % 1;
              ctx.globalAlpha = fa * 0.45 * (1 - su); ctx.fillStyle = "#4a4d55";
              ctx.beginPath(); ctx.arc(pb.x + su * 10 * side, pt.y - su * 60 * pb.s * 6, (4 + su * 14) * Math.max(0.4, pb.s * 4), 0, 2 * Math.PI); ctx.fill();
            }
            ctx.globalAlpha = fa * (0.5 + 0.5 * Math.sin(tn * 9 + idx)); ctx.fillStyle = "#ff7a2a";
            ctx.fillRect(pb.x - 2, pt.y - 2, 4, 3);
          } else if (hh % 4 === 1) {                         /* far battery firing */
            var fp = (performance.now() / 1000 * 1.1 + idx * 0.29) % 1;
            if (fp < 0.3) {
              ctx.globalAlpha = fa; ctx.strokeStyle = side > 0 ? "#ff5a3a" : "#7dff8a"; ctx.lineWidth = 1.5;
              ctx.beginPath(); ctx.moveTo(pb.x, pt.y); ctx.lineTo(pb.x - side * 40 * fp, pt.y - 140 * fp - 20); ctx.stroke();
            }
          }
        }
      }
      /* distant surface explosions near the horizon */
      var tq = performance.now() / 1000;
      for (i = 0; i < 3; i++) {
        var Pq = 2.3 + i * 0.7, uq = tq / Pq + i * 0.41, cq = Math.floor(uq); uq -= cq;
        if (uq > 0.4) continue;
        var hx = hash1(cq * 23 + i * 5), zq = zEnd * 2 + hash1(cq * 29 + i) * 14000;
        var pq = fProj(W, H, fCanX + (hx - 0.5) * 5000, top, zq, 1), rq = Math.max(3, 260 * pq.s) * (0.4 + uq * 2);
        ctx.globalAlpha = 1 - uq / 0.4; ctx.fillStyle = uq < 0.1 ? "#fff3c0" : "#ff8a2a";
        ctx.beginPath(); ctx.arc(pq.x, pq.y, rq, Math.PI, 0); ctx.fill();
        ctx.strokeStyle = "#ffd27a"; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(pq.x, pq.y, rq * 1.8, Math.PI, 0); ctx.stroke();
      }
      ctx.globalAlpha = 1;
    }
    /* gate barrier: exactly the region fighter-gates treats as blocked */
    function gatePanel(c, p) {
      var x0 = c.cx - w, x1 = c.cx + w, R;
      if (p === 0) R = [[x0, x1, top, 0]];
      else if (p === 1) R = [[x0, x1, 0, bot]];
      else if (p === 2) R = [[x0, c.cx, top, bot]];
      else if (p === 3) R = [[c.cx, x1, top, bot]];
      else R = [[x0, x1, top, -60], [x0, x1, 60, bot]];
      var f = Math.max(0, 1 - c.d / FCAN_END), i, r, p0, p1, O;
      /* the OPEN gap (fly here), outlined green */
      if (p === 0) O = [x0, x1, 0, bot];
      else if (p === 1) O = [x0, x1, top, 0];
      else if (p === 2) O = [c.cx, x1, top, bot];
      else if (p === 3) O = [x0, c.cx, top, bot];
      else O = [x0, x1, -60, 60];
      /* the BLOCKED region: solid hazard panel with warning hatching. No frame
         is drawn across the whole cross-section any more -- its top bar read as
         an overhead beam even when the top of the gate was open. */
      for (i = 0; i < R.length; i++) {
        r = R[i];
        p0 = fProj(W, H, r[0], r[2], c.z, 1); p1 = fProj(W, H, r[1], r[3], c.z, 1);
        ctx.globalAlpha = 0.55 + 0.40 * f; ctx.fillStyle = "#a3260f";
        ctx.fillRect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y);
        ctx.globalAlpha = 0.6 + 0.4 * f; ctx.strokeStyle = "#ffb347"; ctx.lineWidth = 1 + 3 * f;
        ctx.strokeRect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y);
        ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y);
        ctx.moveTo(p1.x, p0.y); ctx.lineTo(p0.x, p1.y); ctx.stroke();
      }
      p0 = fProj(W, H, O[0], O[2], c.z, 1); p1 = fProj(W, H, O[1], O[3], c.z, 1);
      ctx.globalAlpha = 0.6 + 0.4 * f; ctx.strokeStyle = "#7dffa0"; ctx.lineWidth = 1 + 2.5 * f;
      ctx.setLineDash([8, 6]); ctx.strokeRect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y); ctx.setLineDash([]);
      gateMark = c; gatePat = p;
    }
    /* where the ship will cross the gate if it holds its offset from the canyon
       centre and its altitude (gates are canyon-relative); drawn as a HUD cue
       on top of everything so a bend can never hide it */
    function gateMarker(c) {
      if (fPy <= CAN_TOP) return;     /* above the rim gates cannot hit: no lane cue */
      var m = fProj(W, H, c.cx + fPx - fCanX, fPy, c.z, 1), mr = Math.max(5, 22 * m.s);
      /* red when that crossing point is in the blocked region (the same rule
         Glon's fighter-gates applies at the crossing tick), else white */
      var dx = fPx - fCanX, y = fPy, hit = y <= CAN_TOP ? false : gatePat === 0 ? y <= 0 : gatePat === 1 ? y >= 0 :
                gatePat === 2 ? dx <= 0 : gatePat === 3 ? dx >= 0 : Math.abs(y) >= 60;
      ctx.globalAlpha = 0.95; ctx.strokeStyle = hit ? "#ff3b3b" : "#eaffff"; ctx.lineWidth = hit ? 3 : 2;
      ctx.beginPath(); ctx.moveTo(m.x, m.y - mr); ctx.lineTo(m.x + mr, m.y); ctx.lineTo(m.x, m.y + mr);
      ctx.lineTo(m.x - mr, m.y); ctx.closePath(); ctx.stroke();
    }
    /* the nearest section sits almost at the camera so a wall beside the ship
       fills its whole side of the screen (no void showing past a close wall) */
    S.push(cross(0.5));
    for (d = FCAN_NEAR; d <= FCAN_END; d += FCAN_STEP) S.push(cross(d));
    for (d = FCAN_RIB - (fProg % FCAN_RIB); d <= FCAN_END; d += FCAN_RIB) if (d > FCAN_NEAR) ribs.push(d);
    for (d = FDECO - (fProg % FDECO); d <= FCAN_END; d += FDECO) if (d > FCAN_NEAR) decos.push(d);
    di = decos.length - 1;
    if (gate && fProg > 120) gd = 198 - ((fProg - 120) % 200);
    ri = ribs.length - 1;
    if (above) farSurface();
    battleSky();                      /* ambient battle in the sky (host only) */
    for (j = S.length - 1; j > 0; j--) {
      a = S[j - 1]; b = S[j]; t = b.d / FCAN_END; e = Math.max(0, 1 - t);
      ctx.globalAlpha = 1;
      /* scrape warning lights the near stretch of the wall being approached */
      near = ww.amt * Math.max(0, 1 - b.d / 70);
      wl = ww.side < 0 ? near : 0; wr = ww.side > 0 ? near : 0;
      visL = wallVis(-1, a.cx); visR = wallVis(1, a.cx);
      quad(a.fl, b.fl, b.fr, a.fr, fFog(FFLOOR, t, 0.7, 0, 0));                           /* floor */
      if (visL) quad(a.cl, b.cl, b.fl, a.fl, fFog(FWALL, t, 0.5, fWallSide < 0 ? hot : 0, wl));     /* LEFT wall  */
      if (visR) quad(a.cr, b.cr, b.fr, a.fr, fFog(FWALL, t, 0.5, fWallSide > 0 ? hot : 0, wr));     /* RIGHT wall */

      edge("f", 0.95 * Math.pow(e, 1.1), 1.2 + 5.5 * e, "#8feaff");   /* floor edges   */
      edge("c", 0.80 * Math.pow(e, 1.1), 1.0 + 3.8 * e, "#6fd8ff");   /* wall-top edges */
      edge("m", 0.25 * e, 0.8 + 1.6 * e, "#3aa8d8");                   /* mid-wall rail */
      edge("u", 0.16 * e, 0.6 + 1.0 * e, "#3a86b8");                   /* panel seams */
      edge("l", 0.16 * e, 0.6 + 1.0 * e, "#3a86b8");
      while (di >= 0 && decos[di] > a.d) { if (decos[di] <= b.d) decor(decos[di]); di--; }
      while (ri >= 0 && ribs[ri] > a.d) { if (ribs[ri] <= b.d) rib(cross(ribs[ri])); ri--; }
      if (gd > a.d && gd <= b.d) gatePanel(cross(gd), gate.p);
    }
    /* Seen from above, everything in the trench lies BELOW the surface plane,
       so the solid surface is painted AFTER the trench pass: it hides every
       part of the canyon that is under metal, and only the opening between
       the two rim lines -- exactly where Glon lets the ship descend -- shows
       the canyon. Then surface detail, structures and a strong rim edge. */
    if (above) { nearSurface(); surfaceDetail(); rimEdges(); landingFunnel(); }
    if (gateMark) gateMarker(gateMark);
    /* scrape warning: an amber glow on that screen edge, before any shield loss */
    if (ww.amt > 0) {
      var gx = ww.side < 0 ? 0 : W, gw = W * 0.22;
      var gg = ctx.createLinearGradient(gx, 0, gx - ww.side * gw, 0);
      gg.addColorStop(0, "rgba(255,150,40," + (0.55 * ww.amt).toFixed(3) + ")");
      gg.addColorStop(1, "rgba(255,150,40,0)");
      ctx.globalAlpha = 1; ctx.fillStyle = gg;
      ctx.fillRect(ww.side < 0 ? 0 : W - gw, 0, gw, H);
    }
    ctx.restore();
  }
  /* Altitude ride: the fighter's real Glon altitude (F_Y -> fPy) shifts the
     cockpit frame so the ship visibly climbs (fPy<0 -> frame up) or dives. The
     world already moves the opposite way via fProj(oy - fPy), so the trench
     sinks when the ship climbs — the unambiguous altitude cue. */
  function fAlt(W, H) { return Math.max(-120, Math.min(120, fPy)) * fCenter(W, H).sc * 0.42; }
  /* Ship gun muzzles: cockpit weapon pods sampled from the rendered artwork
     geometry (image fractions of the bottom-anchored cockpit PNG). */
  /* Explicit forward cannon barrels: base on the ship's frame, projecting
     forward (up toward the vanishing point) and slightly inward. Both the
     rendered barrel and the weapon origin share this one geometry. */
  function fBarrel(W, H, side) {
    var sc = fScale(W);
    var iw = fighterImg ? fighterImg.naturalWidth * sc : W;
    var ih = fighterImg ? fighterImg.naturalHeight * sc : H;
    var ix = (W - iw) / 2, ay = fAlt(W, H);
    var bx = ix + (side ? 0.70 : 0.30) * iw, by = (H - ih) + 0.82 * ih;
    /* The barrel extends FORWARD in ship space, so its tip is projected toward
       the trench's vanishing point (fCenter), not an image fraction. Both
       barrels therefore converge on the same forward axis; base and tip share
       the same altitude ride (ay) so the barrels stay rigidly on the ship. */
    var c = fCenter(W, H), k = 0.42;
    return {
      base: { x: bx, y: by + ay },
      tip:  { x: bx + (c.x - bx) * k, y: by + (c.y - by) * k + ay }
    };
  }
  /* fGun is the authoritative muzzle = the rendered barrel tip */
  function fGun(W, H, side) { return fBarrel(W, H, side).tip; }
  function drawCannons(ctx, W, H) {
    var side, b, dx, dy, len;
    ctx.save();
    for (side = 0; side < 2; side++) {
      b = fBarrel(W, H, side === 1);
      dx = b.tip.x - b.base.x; dy = b.tip.y - b.base.y; len = Math.hypot(dx, dy);
      ctx.lineCap = "round";
      ctx.strokeStyle = "rgba(120,210,255,.85)"; ctx.lineWidth = Math.max(2, len * 0.16);
      ctx.beginPath(); ctx.moveTo(b.base.x, b.base.y); ctx.lineTo(b.tip.x, b.tip.y); ctx.stroke();
      ctx.strokeStyle = "rgba(200,245,255,.95)"; ctx.lineWidth = Math.max(1, len * 0.05);
      ctx.beginPath(); ctx.moveTo(b.base.x, b.base.y); ctx.lineTo(b.tip.x, b.tip.y); ctx.stroke();
      ctx.fillStyle = "#eafaff";
      ctx.beginPath(); ctx.arc(b.tip.x, b.tip.y, Math.max(2, len * 0.09), 0, 2 * Math.PI); ctx.fill();
    }
    ctx.restore();
  }
  /* ---- D12S.16 layered arcade audio (WebAudio, host-only) -----------------
   * Semantics unchanged: the host still reacts to the same render events. The
   * AudioContext is created only after a real user gesture (no autoplay
   * warnings) and a master gain keeps headroom so rapid fire never clips. */
  var audioCtx = null, audioMaster = null, audioUnlocked = false, noiseBuf = null;
  function ensureAudio() {
    if (audioCtx) return audioCtx;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    try { audioCtx = new AC(); } catch (e) { return null; }
    audioMaster = audioCtx.createGain(); audioMaster.gain.value = 0.5;
    /* gentle master limiter so overlapping rapid fire does not clip */
    var comp = audioCtx.createDynamicsCompressor();
    comp.threshold.value = -10; comp.knee.value = 10; comp.ratio.value = 6;
    comp.attack.value = 0.003; comp.release.value = 0.22;
    audioMaster.connect(comp); comp.connect(audioCtx.destination);
    return audioCtx;
  }
  function unlockAudio() {
    var c = ensureAudio(); if (!c) return;
    if (c.state === "suspended") { try { c.resume(); } catch (e) {} }
    audioUnlocked = true;
    preloadSfx();
  }
  window.addEventListener("keydown", unlockAudio);
  window.addEventListener("pointerdown", unlockAudio);
  window.addEventListener("touchstart", unlockAudio, { passive: true });
  function noise() {
    if (noiseBuf) return noiseBuf;
    var n = Math.floor(audioCtx.sampleRate * 0.6);
    noiseBuf = audioCtx.createBuffer(1, n, audioCtx.sampleRate);
    var d = noiseBuf.getChannelData(0);
    for (var i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
    return noiseBuf;
  }
  function bus(pan) {
    if (audioCtx.createStereoPanner) { var p = audioCtx.createStereoPanner(); p.pan.value = pan || 0; p.connect(audioMaster); return p; }
    return audioMaster;
  }
  function osc(type, f0, f1, t0, dur, vol, node) {
    var o = audioCtx.createOscillator(), g = audioCtx.createGain();
    o.type = type; o.frequency.setValueAtTime(f0, t0);
    if (f1 !== f0) o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t0 + dur);
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(vol, t0 + 0.005);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    o.connect(g); g.connect(node); o.start(t0); o.stop(t0 + dur + 0.02);
  }
  function noiseTail(freq, q, t0, dur, vol, node) {
    var s = audioCtx.createBufferSource(); s.buffer = noise();
    var bp = audioCtx.createBiquadFilter(); bp.type = "bandpass"; bp.frequency.value = freq; bp.Q.value = q;
    var g = audioCtx.createGain(); g.gain.setValueAtTime(vol, t0); g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    s.connect(bp); bp.connect(g); g.connect(node); s.start(t0); s.stop(t0 + dur + 0.02);
  }
  /* broadband transient (high-passed noise) */
  function noiseBurst(t0, dur, vol, hp, node) {
    var s = audioCtx.createBufferSource(); s.buffer = noise();
    var f = audioCtx.createBiquadFilter(); f.type = "highpass"; f.frequency.value = hp;
    var g = audioCtx.createGain(); g.gain.setValueAtTime(vol, t0); g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    s.connect(f); f.connect(g); g.connect(node); s.start(t0); s.stop(t0 + dur + 0.01);
  }
  /* band-passed noise sweeping f0 -> f1 (resonant tail / zip) */
  function noiseSweep(t0, f0, f1, dur, vol, node) {
    var s = audioCtx.createBufferSource(); s.buffer = noise();
    var bp = audioCtx.createBiquadFilter(); bp.type = "bandpass"; bp.Q.value = 2.6;
    bp.frequency.setValueAtTime(f0, t0); bp.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t0 + dur);
    var g = audioCtx.createGain(); g.gain.setValueAtTime(vol, t0); g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    s.connect(bp); bp.connect(g); g.connect(node); s.start(t0); s.stop(t0 + dur + 0.02);
  }
  /* Optional sampled-SFX seam: set SFX_ASSETS = { LASER: "assets/laser.ogg", ... }
     and the host uses the buffer instead of synthesis. Empty = pure WebAudio. */
  var SFX_ASSETS = {}, sfxBuf = {};
  function preloadSfx() {
    if (!audioCtx) return;
    for (var k in SFX_ASSETS) (function (k) {
      fetch(SFX_ASSETS[k]).then(function (r) { return r.arrayBuffer(); })
        .then(function (b) { return audioCtx.decodeAudioData(b); })
        .then(function (buf) { sfxBuf[k] = buf; }).catch(function () {});
    })(k);
  }
  function playSample(name, vol, pan) {
    var buf = sfxBuf[name]; if (!buf) return false;
    try { var s = audioCtx.createBufferSource(); s.buffer = buf;
      var g = audioCtx.createGain(); g.gain.value = vol || 1;
      var out = bus(pan || 0); s.connect(g); g.connect(out); s.start(); return true; } catch (e) { return false; }
  }
  /* player laser: broadband snap + bright body + detuned width + sub + resonant tail */
  function playLaser(pan) {
    if (!audioUnlocked || !audioCtx) return;
    if (playSample("LASER", 0.95, pan)) return;
    try {
      var t0 = audioCtx.currentTime, out = bus(pan || 0), out2 = bus(-(pan || 0) * 0.7);
      noiseBurst(t0, 0.03, 0.7, 3600, out);               /* broadband attack/snap */
      osc("square", 1500, 560, t0, 0.10, 0.26, out);      /* upper-mid bite */
      osc("sawtooth", 900, 320, t0, 0.13, 0.22, out2);    /* 300-1000 Hz body, wide */
      osc("sawtooth", 420, 150, t0, 0.14, 0.18, out);     /* lower body / weight */
      osc("sine", 160, 68, t0, 0.12, 0.12, out);          /* sub as reinforcement */
      noiseSweep(t0, 2200, 480, 0.14, 0.17, out);         /* resonant tail */
    } catch (e) {}
  }
  /* enemy fire: lower, rougher, opposite (upward) pitch contour */
  function playEnemyFire() {
    if (!audioUnlocked || !audioCtx) return;
    if (playSample("ENEMY_FIRE", 0.95, 0)) return;
    try {
      var t0 = audioCtx.currentTime;
      osc("sawtooth", 330, 900, t0, 0.17, 0.16, audioMaster);
      osc("square", 150, 70, t0, 0.15, 0.12, audioMaster);
      noiseSweep(t0, 400, 1400, 0.14, 0.10, audioMaster);
    } catch (e) {}
  }
  /* boost: deep engine surge (start/stop around the boost window) */
  var boostNodes = null;
  function startBoost() {
    if (!audioUnlocked || !audioCtx || boostNodes) return;
    try {
      var t0 = audioCtx.currentTime;
      var o1 = audioCtx.createOscillator(); o1.type = "sawtooth";
      o1.frequency.setValueAtTime(66, t0); o1.frequency.linearRampToValueAtTime(150, t0 + 0.5);
      var o2 = audioCtx.createOscillator(); o2.type = "triangle";
      o2.frequency.setValueAtTime(44, t0); o2.frequency.linearRampToValueAtTime(96, t0 + 0.5);
      var f = audioCtx.createBiquadFilter(); f.type = "lowpass";
      f.frequency.setValueAtTime(320, t0); f.frequency.linearRampToValueAtTime(1500, t0 + 0.5);
      var g = audioCtx.createGain(); g.gain.setValueAtTime(0.0001, t0); g.gain.linearRampToValueAtTime(0.24, t0 + 0.14);
      o1.connect(f); o2.connect(f); f.connect(g); g.connect(audioMaster);
      o1.start(t0); o2.start(t0);
      boostNodes = { o: [o1, o2], g: g };
    } catch (e) {}
  }
  function stopBoost() {
    if (!boostNodes) return;
    try {
      var t = audioCtx.currentTime, g = boostNodes.g;
      g.gain.cancelScheduledValues(t); g.gain.setValueAtTime(g.gain.value, t); g.gain.linearRampToValueAtTime(0.0001, t + 0.16);
      for (var i = 0; i < boostNodes.o.length; i++) boostNodes.o[i].stop(t + 0.2);
    } catch (e) {}
    boostNodes = null;
  }
  /* one-shot impact / explosion */
  function playHit() {
    if (!audioUnlocked || !audioCtx) return;
    try { var t0 = audioCtx.currentTime; osc("square", 220, 70, t0, 0.16, 0.18, audioMaster); noiseTail(1400, 0.6, t0, 0.14, 0.14, audioMaster); } catch (e) {}
  }
  function playExplosion() {
    if (!audioUnlocked || !audioCtx) return;
    try { var t0 = audioCtx.currentTime; osc("sine", 90, 40, t0, 0.5, 0.26, audioMaster); noiseTail(500, 0.4, t0, 0.5, 0.22, audioMaster); } catch (e) {}
  }
  function playSuccess() { if (audioUnlocked && audioCtx) { try { var t0 = audioCtx.currentTime; osc("sine", 523, 784, t0, 0.35, 0.16, audioMaster); osc("sine", 392, 587, t0 + 0.12, 0.35, 0.12, audioMaster); } catch (e) {} } }
  function playFail() { if (audioUnlocked && audioCtx) { try { var t0 = audioCtx.currentTime; osc("square", 220, 90, t0, 0.45, 0.16, audioMaster); } catch (e) {} } }
  /* ---- D12S.14 forward-speed cues (host-only, over the static cockpit) ----
   * The cockpit PNG is the frame/background; all motion is layered and driven
   * by wall-clock time, so it animates every rAF even though the Glon tick is
   * 25 Hz. Normal flight is already fast; BOOST multiplies speed, lengthens the
   * streaks, quickens the trench expansion and shakes/parallaxes the frame. */
  var FSPEED = 1.5, FSPEED_BOOST = 3.8;
  function fSpeed() { return fBoost > 0 ? FSPEED_BOOST : FSPEED; }
  /* longitudinal streaks: radiate from the vanishing point, accelerate and
     lengthen with depth (perspective) — the primary "rushing past you" cue.
     Each is drawn as a bright leading head + a long fading tail so it reads as
     travel, not as static wireframe. */
  function fStreaks(ctx, W, H, c, t, sp, boost) {
    /* Batched by brightness band: each streak goes into one of NB Path2Ds
       (tail + head), so the whole field is 2*NB strokes instead of ~250
       (this was the largest JS cost of every fighter frame). */
    ctx.save(); ctx.lineCap = "round";
    var N = 124, NB = 4, ca, sa, i, ph, r, len, mx, my, bnd, k;
    var tails = [], heads = [];
    for (k = 0; k < NB; k++) { tails.push(new Path2D()); heads.push(new Path2D()); }
    var maxR = Math.sqrt(W * W + H * H) * 0.64;
    for (i = 0; i < N; i++) {
      var ang = i * 2.399963;
      ca = Math.cos(ang); sa = Math.sin(ang) * 0.62;
      ph = ((i * 0.117 + t * (0.72 + (i % 11) * 0.04) * sp) % 1 + 1) % 1;
      r = 6 + ph * ph * maxR;
      len = (16 + ph * ph * (210 + (boost ? 170 : 0)));
      mx = c.x + ca * (r - len * 0.28); my = c.y + sa * (r - len * 0.28);
      bnd = Math.min(NB - 1, Math.floor(ph * NB));
      tails[bnd].moveTo(c.x + ca * (r - len), c.y + sa * (r - len)); tails[bnd].lineTo(mx, my);   /* fading tail */
      heads[bnd].moveTo(mx, my); heads[bnd].lineTo(c.x + ca * r, c.y + sa * r);                   /* bright head */
    }
    for (k = 0; k < NB; k++) {
      var pm = (k + 0.5) / NB, a = 0.14 + pm * 0.86;
      ctx.strokeStyle = "rgba(120,205,255," + (a * 0.35).toFixed(3) + ")";
      ctx.lineWidth = 1 + pm * (boost ? 2.6 : 1.8); ctx.stroke(tails[k]);
      ctx.strokeStyle = (boost ? "rgba(200,245,255," : "rgba(160,230,255,") + a.toFixed(3) + ")";
      ctx.lineWidth = 1.4 + pm * (boost ? 4.6 : 3.4); ctx.stroke(heads[k]);
    }
    ctx.restore();
  }
  /* small particles/star streaks radiating outward */
  function fStars(ctx, W, H, c, t, sp) {
    ctx.save();
    for (var i = 0; i < 58; i++) {
      var ang = i * 1.7;
      var ph = ((i * 0.211 + t * 1.5 * sp) % 1 + 1) % 1;
      var r = 5 + ph * ph * Math.max(W, H) * 0.95;
      var x = c.x + Math.cos(ang) * r, y = c.y + Math.sin(ang) * r * 0.62;
      if (x < -2 || x > W + 2 || y < -2 || y > H + 2) continue;
      ctx.globalAlpha = 0.25 + ph * 0.75;
      ctx.fillStyle = "#eafaff";
      var sz = 1 + ph * 2.6;
      ctx.fillRect(x, y, sz, sz);
    }
    ctx.restore();
  }
  function drawFighterBackground(ctx, W, H) {
    var c = fCenter(W, H), t = performance.now() / 1000, sp = fSpeed();
    var sh = fShake(t);
    var ay = fAlt(W, H);                         /* ship altitude ride (F_Y) */
    /* the world shifts the OPPOSITE way, so a climb sinks the trench; the
       vanishing point follows the camera's canyon-tangent heading */
    var cw = { x: c.x + fYawPx(W), y: c.y - ay - fPitchPx(H), sc: c.sc };
    ctx.save();
    ctx.translate(sh.x, sh.y);
    ctx.fillStyle = "#01020a"; ctx.fillRect(-10, -10, W + 20, H + 20);
    /* The baked cockpit art contains a STRAIGHT trench in its windows, so the
       art is not painted here: the cockpit body is stamped over the canyon
       afterwards (drawCockpit) with its windows cut out. Sky cues go first;
       the opaque walls and the cockpit body then hide them. */
    /* bright vanishing-point "warp core" — a strong depth/motion anchor */
    var wc = ctx.createRadialGradient(cw.x, cw.y, 1, cw.x, cw.y, 46 + (fBoost > 0 ? 24 : 0));
    wc.addColorStop(0, "rgba(190,240,255,.85)");
    wc.addColorStop(0.4, "rgba(90,200,255,.28)");
    wc.addColorStop(1, "rgba(90,200,255,0)");
    ctx.fillStyle = wc; ctx.beginPath(); ctx.arc(cw.x, cw.y, 46 + (fBoost > 0 ? 24 : 0), 0, 2 * Math.PI); ctx.fill();
    fStars(ctx, W, H, cw, t, sp);
    /* above the rim the surface gives the motion cue: let the battle show */
    if (fPy < CAN_TOP) ctx.globalAlpha = 0.35;
    fStreaks(ctx, W, H, cw, t, sp, fBoost > 0);
    ctx.globalAlpha = 1;
    ctx.restore();
  }
  /* boost frame shake (shared by the background and the cannons) */
  function fShake(t) {
    if (fBoost <= 0) return { x: 0, y: 0 };
    return { x: Math.sin(t * 63) * 3 + Math.sin(t * 41) * 1.5, y: Math.cos(t * 55) * 2.4 };
  }
  /* explicit ship cannons (tips = fGun), drawn OVER the solid canyon walls */
  function drawFighterCannons(ctx, W, H) {
    var sh = fShake(performance.now() / 1000);
    ctx.save(); ctx.translate(sh.x, sh.y);
    drawCannons(ctx, W, H);
    ctx.restore();
  }
  function drawFighterReticle(ctx, W, H, firing) {
    var c = fCenter(W, H);
    /* forward aim point is pinned to the ship's forward line (fixed on screen),
       so UP/DOWN never reads as the reticle aiming independently */
    var px = c.x, py = c.y;
    if (firing) {                            /* hitscan: beam runs muzzle -> aim point */
      var g = fGun(W, H, laserSide);
      ctx.save();
      var lg = ctx.createLinearGradient(g.x, g.y, px, py);
      lg.addColorStop(0, "rgba(255,232,90,0.30)");
      lg.addColorStop(1, "rgba(255,248,175,0.95)");
      ctx.strokeStyle = lg; ctx.lineWidth = 6; ctx.lineCap = "round";
      ctx.beginPath(); ctx.moveTo(g.x, g.y); ctx.lineTo(px, py); ctx.stroke();
      ctx.strokeStyle = "rgba(255,255,255,.95)"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(g.x, g.y); ctx.lineTo(px, py); ctx.stroke();
      var mf = ctx.createRadialGradient(g.x, g.y, 1, g.x, g.y, 24);
      mf.addColorStop(0, "rgba(255,252,210,.95)"); mf.addColorStop(1, "rgba(255,190,50,0)");
      ctx.fillStyle = mf; ctx.beginPath(); ctx.arc(g.x, g.y, 24, 0, 2 * Math.PI); ctx.fill();
      ctx.restore();
    }
    ctx.save();
    ctx.strokeStyle = fLaser ? FXCOL.hot : "rgba(120,255,160,.85)";
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(px, py, 18, 0, 2 * Math.PI); ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(px - 28, py); ctx.lineTo(px - 8, py);
    ctx.moveTo(px + 8, py); ctx.lineTo(px + 28, py);
    ctx.moveTo(px, py - 28); ctx.lineTo(px, py - 8);
    ctx.moveTo(px, py + 8); ctx.lineTo(px, py + 28);
    ctx.stroke();
    ctx.fillStyle = fLaser ? FXCOL.hot : "rgba(120,255,160,.9)";
    ctx.beginPath(); ctx.arc(px, py, 2.5, 0, 2 * Math.PI); ctx.fill();
    ctx.restore();
  }
  function drawFoe(ctx, ex, ey, ez, beh) {
    var W = ctx.canvas.width, H = ctx.canvas.height;
    var q = fProj(W, H, ex, ey, ez);
    if (q.x < -80 || q.x > W + 80 || q.y < -60 || q.y > H + 60) return;
    var s = Math.max(10, Math.min(46, q.s * 9));
    ctx.save();
    ctx.translate(q.x, q.y);
    ctx.fillStyle = "rgba(255,40,40,.22)";
    ctx.beginPath(); ctx.arc(0, 0, s * 0.9, 0, 2 * Math.PI); ctx.fill();
    ctx.strokeStyle = FXCOL.foe; ctx.lineWidth = Math.max(1.5, s * 0.12);
    ctx.beginPath();                         /* readable red arrow fighter */
    ctx.moveTo(-s, -s * 0.55); ctx.lineTo(s, -s * 0.55);
    ctx.lineTo(s * 0.5, 0); ctx.lineTo(s, s * 0.55);
    ctx.lineTo(-s, s * 0.55); ctx.lineTo(-s * 0.5, 0); ctx.closePath();
    ctx.stroke();
    ctx.beginPath(); ctx.arc(0, 0, s * 0.30, 0, 2 * Math.PI); ctx.stroke();
    ctx.fillStyle = FXCOL.foe;
    ctx.beginPath(); ctx.arc(0, 0, s * 0.16, 0, 2 * Math.PI); ctx.fill();
    /* on a ram course (Glon rams within |dx|<40 and |dy|<52 at depth <30) */
    if (ez < 150 && Math.abs(ex - fPx) < 40 && Math.abs(ey - fPy) < 52) fWarnRing(ctx, s * 1.5);
    ctx.restore();
  }
  /* pulsing red ring: this object will hit the ship if nothing changes */
  function fWarnRing(ctx, r) {
    ctx.globalAlpha = 0.6 + 0.4 * Math.sin(performance.now() / 70);
    ctx.strokeStyle = "#ff2a2a"; ctx.lineWidth = 3;
    ctx.beginPath(); ctx.arc(0, 0, r, 0, 2 * Math.PI); ctx.stroke();
    ctx.globalAlpha = 1;
  }
  function drawBolt(ctx, bx, by, bz) {
    var W = ctx.canvas.width, H = ctx.canvas.height;
    var q = fProj(W, H, bx, by, bz);
    var s = Math.max(3, Math.min(16, q.s * 3));
    ctx.save();
    var g = ctx.createRadialGradient(q.x, q.y, 1, q.x, q.y, s * 2.4);
    g.addColorStop(0, "#ffd9a0"); g.addColorStop(0.4, FXCOL.bolt); g.addColorStop(1, "rgba(255,80,0,0)");
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(q.x, q.y, s * 2.4, 0, 2 * Math.PI); ctx.fill();
    /* on a hit course (Glon: |dx|<60 and |dy|<52 when it arrives) */
    if (bz < 180 && Math.abs(bx - fPx) < 60 && Math.abs(by - fPy) < 52) { ctx.translate(q.x, q.y); fWarnRing(ctx, s * 3.2); }
    ctx.restore();
  }
  function drawTarget(ctx, W, H) {
    var c = fCenter(W, H);
    if (fPhase === 1 || fPhase === 2) { drawJammed(ctx, W, H, c); return; }
    var present = (fProg >= 850 && fPhase === 0);
    if (!present) return;
    /* Glon's drop rule: < 70 off the centre AND inside the trench */
    var inside = fPy > CAN_TOP, aligned = inside && Math.abs(fPx - fCanX) < 70;
    var r = 46 + 8 * Math.sin(performance.now() / 180);
    ctx.save();
    ctx.strokeStyle = aligned ? FXCOL.good : FXCOL.hot;
    ctx.lineWidth = aligned ? 4 : 2.5;
    ctx.beginPath(); ctx.arc(c.x, c.y, r, 0, 2 * Math.PI); ctx.stroke();
    ctx.beginPath(); ctx.arc(c.x, c.y, r * 0.5, 0, 2 * Math.PI); ctx.stroke();
    ctx.fillStyle = aligned ? "rgba(127,209,138,.28)" : "rgba(255,210,63,.16)";
    ctx.beginPath(); ctx.arc(c.x, c.y, r, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = aligned ? FXCOL.good : FXCOL.hot;
    ctx.font = "bold 14px system-ui, sans-serif"; ctx.textAlign = "center";
    ctx.fillText(aligned ? "DROP BERRIES (GLON)" : (inside ? "ALIGN + DROP BERRIES" : "DESCEND TO DROP"), c.x, c.y - r - 10);
    ctx.restore();
  }
  /* The payload result: berry goo splatters over the station machinery, the
     rings stop turning and spark. Shown through the escape (phase 1) until the
     debrief; presentation only. */
  var fJamT0 = 0;
  function drawJammed(ctx, W, H, c) {
    var now = performance.now(), t = Math.min(1, (now - fJamT0) / 900), i, k = Math.max(1, W / 640);
    var r = 54 * k;
    ctx.save();
    ctx.strokeStyle = "#8f9bb0"; ctx.lineWidth = 4;
    ctx.beginPath(); ctx.arc(c.x, c.y, r, 0, 2 * Math.PI); ctx.stroke();
    ctx.beginPath(); ctx.arc(c.x, c.y, r * 0.5, 0, 2 * Math.PI); ctx.stroke();
    for (i = 0; i < 6; i++) {                                  /* stopped gear teeth */
      var a = i * Math.PI / 3; ctx.beginPath();
      ctx.moveTo(c.x + Math.cos(a) * r * 0.5, c.y + Math.sin(a) * r * 0.5); ctx.lineTo(c.x + Math.cos(a) * r, c.y + Math.sin(a) * r); ctx.stroke();
    }
    for (i = 0; i < 14; i++) {                                 /* berry goo blobs grow in */
      var ang = i * 2.399963, d = r * (0.2 + (i * 37 % 10) / 12), br = (5 + (i * 13 % 9)) * k * t;
      ctx.fillStyle = i % 3 ? "#8a1f6b" : "#c2185b";
      ctx.beginPath(); ctx.arc(c.x + Math.cos(ang) * d, c.y + Math.sin(ang) * d * 0.8, br, 0, 2 * Math.PI); ctx.fill();
      ctx.fillStyle = "rgba(255,255,255,.35)"; ctx.beginPath(); ctx.arc(c.x + Math.cos(ang) * d - br * 0.3, c.y + Math.sin(ang) * d * 0.8 - br * 0.3, br * 0.3, 0, 2 * Math.PI); ctx.fill();
    }
    if ((now / 90) % 2 < 1) {                                  /* sparks from the jammed rings */
      ctx.strokeStyle = "#ffe08a"; ctx.lineWidth = 2;
      for (i = 0; i < 4; i++) { var sa = (now / 300 + i * 1.7) % (2 * Math.PI);
        ctx.beginPath(); ctx.moveTo(c.x + Math.cos(sa) * r, c.y + Math.sin(sa) * r); ctx.lineTo(c.x + Math.cos(sa) * (r + 12 * k), c.y + Math.sin(sa) * (r + 12 * k)); ctx.stroke(); }
    }
    ctx.fillStyle = "#ff8ad8"; ctx.font = "bold " + Math.round(16 * k) + "px system-ui, sans-serif"; ctx.textAlign = "center";
    ctx.fillText("BERRIES DELIVERED  -  MACHINERY JAMMED", c.x, c.y - r - 14 * k);
    if (fPhase === 1) { ctx.fillStyle = "#cfeeff"; ctx.fillText("ESCAPE!", c.x, c.y + r + 22 * k); }
    ctx.restore();
  }
  /* ---- end-of-mission debrief (Glon phase 4 = complete, 5 = failed) --------
   * Glon freezes the mission and owns the stats (Z op) and the continue rule;
   * the host only presents them over the frozen last frame. */
  var fStats = null, fPrevPhase = 0;
  /* Glon's fwhy codes (the last thing that damaged the ship) */
  var FWHY = ["", "COLLISION: LEFT WALL", "COLLISION: RIGHT WALL", "COLLISION: GATE",
              "RAMMED BY FIGHTER", "DESTROYED BY FIGHTER FIRE", "ABANDONED THE TRENCH RUN",
              "COLLISION: SURFACE"];
  function drawDebrief(ctx, W, H) {
    var st = fStats || { score: 0, kills: 0, shots: 0, boosts: 0, hold: 0 };
    var win = fPhase === 4, k = Math.max(1, W / 640);
    var pw = Math.min(W - 24 * k, 420 * k), ph = Math.min(H - 16 * k, 300 * k);
    var x0 = (W - pw) / 2, y0 = (H - ph) / 2, i;
    var acc = st.shots > 0 ? Math.round(100 * st.kills / st.shots) : 0;
    var secs = Math.floor(fProg / 25);
    var time = Math.floor(secs / 60) + ":" + ("0" + (secs % 60)).slice(-2);
    var rows = [["SCORE", "" + st.score], ["FIGHTERS DESTROYED", "" + st.kills],
                ["SHOTS FIRED", "" + st.shots], ["HITS / ACCURACY", st.kills + " / " + acc + "%"],
                ["SHIELD REMAINING", "" + fShield], ["LIVES REMAINING", "" + fLives],
                ["BOOSTS USED", "" + st.boosts], ["MISSION TIME", time]];
    if (win) rows.unshift(["TARGET", "JAMMED WITH BERRIES"]);
    /* abandoning (6) ends the run with shields left, so the reason code wins;
       otherwise shields left at the end means the target was not destroyed */
    if (!win) rows.unshift(["CAUSE", st.why === 6 ? FWHY[6] : fShield > 0 ? "TARGET NOT DESTROYED" : (FWHY[st.why] || "SHIELDS LOST")]);
    var step = Math.min(26 * k, (ph - 108 * k) / rows.length);
    ctx.save();
    ctx.globalAlpha = 1;
    ctx.fillStyle = "rgba(1,3,12,.72)"; ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = "rgba(4,14,30,.94)"; ctx.fillRect(x0, y0, pw, ph);
    ctx.strokeStyle = win ? "#7fd18a" : "#ff5a4a"; ctx.lineWidth = 2 * k;
    ctx.strokeRect(x0, y0, pw, ph);
    ctx.textBaseline = "middle"; ctx.textAlign = "center";
    ctx.fillStyle = win ? "#7fd18a" : "#ff6a5a";
    ctx.font = "bold " + Math.round(26 * k) + "px system-ui, sans-serif";
    ctx.fillText(win ? "MISSION COMPLETE" : "MISSION FAILED", W / 2, y0 + 30 * k);
    ctx.font = "bold " + Math.round(14 * k) + "px system-ui, sans-serif";
    for (i = 0; i < rows.length; i++) {
      var ry = y0 + 66 * k + i * step;
      ctx.textAlign = "left"; ctx.fillStyle = "#9fc8e8"; ctx.fillText(rows[i][0], x0 + 28 * k, ry);
      ctx.textAlign = "right"; ctx.fillStyle = "#eaffff"; ctx.fillText(rows[i][1], x0 + pw - 28 * k, ry);
    }
    ctx.textAlign = "center";
    ctx.fillStyle = st.hold > 0 ? "rgba(255,210,63,.35)"
                  : "rgba(255,210,63," + (0.65 + 0.35 * Math.sin(performance.now() / 220)).toFixed(3) + ")";
    ctx.font = "bold " + Math.round(16 * k) + "px system-ui, sans-serif";
    ctx.fillText("FIRE TO CONTINUE  →  " + (win ? "FOREST" : "SKULL CAVE"), W / 2, y0 + ph - 22 * k);
    ctx.restore();
  }
  /* Glon's re-entry assist engages while DOWN is held above the rim within
     340 of the trench centre; the host only mirrors that to show the cue. */
  function fAssistOn() {
    return fPy < CAN_TOP && Math.abs(fPx - fCanX) < 340 && !!(held.down || applied.down || tapped.down);
  }
  /* Mission discipline cues (Glon owns the rule: > 400 from the trench centre
     for 75 ticks abandons the run). Outside the trench an arrow points back
     to it; once Glon's stray clock runs, RETURN TO TRENCH counts down. */
  function drawTrenchGuide(ctx, W, H) {
    var dx = fCanX - fPx, stray = fStats ? fStats.stray : 0, k = Math.max(1, W / 640);
    if (fAssistOn()) {
      ctx.save(); ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.font = "bold " + Math.round(15 * k) + "px system-ui, sans-serif";
      ctx.globalAlpha = 0.7 + 0.3 * Math.sin(performance.now() / 120); ctx.fillStyle = "#7dffa0";
      ctx.fillText("RE-ENTRY GUIDANCE", W / 2, H * 0.2);
      ctx.restore();
    }
    if (Math.abs(dx) <= fCanW && !stray) return;
    var left = dx < 0, ax = left ? 26 * k : W - 26 * k, ay = H * 0.42, dir = left ? -1 : 1;
    ctx.save();
    ctx.globalAlpha = 0.75 + 0.25 * Math.sin(performance.now() / 160);
    ctx.fillStyle = stray ? "#ff5a4a" : "#7fe8ff";
    ctx.beginPath(); ctx.moveTo(ax + dir * 14 * k, ay); ctx.lineTo(ax - dir * 8 * k, ay - 14 * k); ctx.lineTo(ax - dir * 8 * k, ay + 14 * k); ctx.closePath(); ctx.fill();
    ctx.font = "bold " + Math.round(12 * k) + "px system-ui, sans-serif"; ctx.textAlign = left ? "left" : "right"; ctx.textBaseline = "middle";
    ctx.fillText("TRENCH", ax + (left ? 18 : -18) * k, ay + 24 * k);
    if (stray) {
      var secs = Math.max(0, (75 - stray) / 25);
      ctx.textAlign = "center"; ctx.font = "bold " + Math.round(22 * k) + "px system-ui, sans-serif";
      ctx.fillStyle = "#ff5a4a"; ctx.fillText("RETURN TO TRENCH  " + secs.toFixed(1) + "s", W / 2, H * 0.24);
    }
    ctx.restore();
  }
  function drawFighterHud(ctx, W) {
    var names = ["LAUNCH", "TRENCH", "PRESSURE", "TARGET", "ESCAPE", "SUCCESS"];
    var phase = fPhase === 0 ? (fProg < 300 ? "TRENCH RUN" : (fProg < 850 ? "ENEMY PRESSURE" : "TARGET APPROACH"))
              : (fPhase === 1 ? "ESCAPE" : ((fPhase === 2 || fPhase === 4) ? "SUCCESS" : "FAILED"));
    ctx.save();
    ctx.font = "bold 16px system-ui, sans-serif"; ctx.textAlign = "left"; ctx.textBaseline = "top";
    ctx.fillStyle = "rgba(0,0,0,.45)"; ctx.fillRect(8, 8, 300, 56);
    ctx.fillStyle = "#cfeeff";
    ctx.fillText("HELMET RUN  " + phase, 16, 12);
    ctx.fillStyle = fShield > 1 ? "#7fd18a" : "#ffd23f";
    ctx.fillText("SHIELD " + fShield + "   BERRIES " + (fBomb ? "READY" : "DROPPED") + "   T" + Math.floor(fProg / 25) + "s", 16, 34);
    ctx.restore();
  }

  function drawScript(script) {
    var canvas = document.querySelector("#glon-canvas");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var W = canvas.width, H = canvas.height;
    var lines = script.split("\n");
    var cam = 0, tx = 0, ty = 0, forestFrame = false, forestTx = 0;      /* tx: gameplay plane scrolls 1:1 with cam; ty: forest ground calibration */
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].replace(/^\s+|\s+$/g, "");
      if (!line) continue;
      var p = line.split(" ");
      var op = p[0];
      if (op === "A") {
        fActive = true; setFighterUi(true);
        prevFoes = fFoes.length; prevBolts = fBolts.length;
        fPx = +p[1]; fPy = +p[2]; fPhase = +p[3]; fShield = +p[4];
        fBoost = +p[5]; fLaser = +p[6]; fProg = +p[7];
        if (fPhase === 1 && fPrevPhase === 0) fJamT0 = performance.now();
        if (fPhase >= 4) {                   /* frozen debrief: no fire/boost feedback */
          fLaser = 0; fBoost = 0;
          if (fPrevPhase < 4) { if (fPhase === 4) playSuccess(); else playFail(); }
        }
        fPrevPhase = fPhase;
        if (p[8] !== undefined && !isNaN(+p[8])) fCanW = +p[8];
        if (p[9] !== undefined && !isNaN(+p[9])) fLives = +p[9];
        var gnow = performance.now();
        if (fShield < fGaugeShield) fGaugeFlashS = gnow;    /* damage -> that bank flashes */
        if (fLives < fGaugeLives) fGaugeFlashL = gnow;
        fGaugeShield = fShield; fGaugeLives = fLives;
        fCanX = canX(fProg);
        /* camera heading = canyon tangent at the ship */
        fYaw = (canX(fProg + 8) - canX(Math.max(0, fProg - 8))) / (fProg + 8 - Math.max(0, fProg - 8));
        fFoes = []; fBolts = []; fGate = null; fBomb = 0;
        /* the gate barrier is drawn inside the canyon pass (depth-sorted), so
           read its pattern from the frame's G op before painting the walls */
        for (var gi = i + 1; gi < lines.length; gi++) {
          var gl = lines[gi].replace(/^\s+/, "");
          if (gl.charAt(0) === "G" && gl.charAt(1) === " ") { var gp = gl.split(/\s+/); fGate = { p: +gp[1], pct: +gp[2] }; break; }
        }
        if (fShield < fPrevShield) {
          fFlash = performance.now(); playHit();
          /* Glon bounces a wall scrape to exactly centre +/- (half-width - 10): light that wall */
          if (Math.abs(fPx - fCanX) === fCanW - 10) { fWallHit = fFlash; fWallSide = fPx > fCanX ? 1 : -1; }
        }
        fPrevShield = fShield;
        if (fLaser) {                        /* fire once per press, then rapid-fire */
          var nowMs = performance.now();
          if (nowMs - lastLaserMs > 205) { lastLaserMs = nowMs; laserSide = !laserSide; playLaser(laserSide ? 0.28 : -0.28); }
        }
        if (fBoost > 0 && !wasBoost) { wasBoost = true; startBoost(); }
        else if (fBoost === 0 && wasBoost) { wasBoost = false; stopBoost(); }
        drawFighterBackground(ctx, W, H);
        drawCanyonWalls(ctx, W, H, fGate);   /* solid winding canyon over the void */
        drawCockpit(ctx, W, H);              /* cockpit body over the canyon */
        drawFighterCannons(ctx, W, H);
      } else if (op === "G") {
        /* already drawn in the canyon pass (see op A) */
      } else if (op === "U") {
        drawFoe(ctx, +p[1], +p[2], +p[3], +p[4]);
      } else if (op === "K") {
        drawBolt(ctx, +p[1], +p[2], +p[3]);
      } else if (op === "Y") {
        fBomb = +p[1];
        drawTarget(ctx, W, H);
        drawFighterReticle(ctx, W, H, fLaser);
        drawFighterHud(ctx, W);
        if (fPhase < 2) drawTrenchGuide(ctx, W, H);
        if (fFlash && performance.now() - fFlash < 220) {
          ctx.fillStyle = "rgba(255,60,60," + (0.35 * (1 - (performance.now() - fFlash) / 220)) + ")";
          ctx.fillRect(0, 0, W, H);
        }
        if (fPhase >= 4) drawDebrief(ctx, W, H);
      } else if (op === "Z") {
        fStats = { score: +p[1], kills: +p[2], shots: +p[3], boosts: +p[4], hold: +p[5], why: +p[6] || 0, stray: +p[7] || 0 };
      } else if (op === "B") {
        if (fActive) { fActive = false; setFighterUi(false); }
        cam = +p[1];
        tx = -cam;
        ty = FOREST_GROUND_DY;
        caveMode = false; forestFrame = true; forestTx = tx;
        signsDue = true;              /* signs go after the trees, before actors */
        paintLayer(ctx, W, 0, cam);   /* distant sky / hills  (slowest) */
        paintLayer(ctx, W, 1, cam);   /* native bush / treeline           */
        paintLayer(ctx, W, 2, cam);   /* orchard / fence / near landscape */
        drawTrail(ctx, W, tx, ty);    /* the walkable ground: moves 1:1 with play */
      } else if (op === "F") {
        if (signsDue) { signsDue = false; drawSigns(ctx, W, cam, tx, ty); }
        paintLayer(ctx, W, 3, cam);   /* foreground foliage   (fastest)   */
      } else if (op === "S") {
        if (signsDue) { signsDue = false; drawSigns(ctx, W, cam, tx, ty); }
        drawSprite(ctx, +p[1], +p[2], +p[3] + ty, +p[4], tx);
      } else if (op === "P") {
        if (signsDue) { signsDue = false; drawSigns(ctx, W, cam, tx, ty); }
        drawRanger(ctx, +p[1], +p[2], +p[3] + ty, +p[4], tx);
      } else if (op === "N") {
        drawSign(ctx, +p[1], +p[2], tx, ty);
      } else if (op === "V") {
        if (fActive) { fActive = false; setFighterUi(false); }
        cam = +p[1]; tx = -cam; ty = 0; caveMode = true; forestFrame = false;   /* cave plane scrolls 1:1 */
        drawCaveBackground(ctx, W, H, cam);
      } else if (op === "J") {
        drawCaveProp(ctx, +p[1], +p[2], tx);
      } else if (op === "Q") {
        drawNight(ctx, W, H);
      } else if (op === "C") {
        ctx.fillStyle = COL[0];
        ctx.fillRect(0, 0, W, H);
      } else if (op === "R") {
        ctx.fillStyle = COL[+p[5]] || "#fff";
        ctx.fillRect(+p[1] + tx, +p[2] + ty, +p[3], +p[4]);
      } else if (op === "O") {
        ctx.fillStyle = COL[+p[4]] || "#fff";
        ctx.beginPath();
        ctx.arc(+p[1] + tx, +p[2] + ty, +p[3], 0, 2 * Math.PI);
        ctx.fill();
      } else if (op === "T") {
        ctx.fillStyle = COL[+p[7]] || "#fff";
        ctx.beginPath();
        ctx.moveTo(+p[1] + tx, +p[2] + ty);
        ctx.lineTo(+p[3] + tx, +p[4] + ty);
        ctx.lineTo(+p[5] + tx, +p[6] + ty);
        ctx.closePath();
        ctx.fill();
      } else if (op === "L") {
        ctx.strokeStyle = COL[+p[6]] || "#fff";
        ctx.lineWidth = +p[5] || 1;
        ctx.beginPath();
        ctx.moveTo(+p[1] + tx, +p[2] + ty);
        ctx.lineTo(+p[3] + tx, +p[4] + ty);
        ctx.stroke();
      } else if (op === "E") {
        ctx.fillStyle = COL[+p[5]] || "#fff";
        ctx.beginPath();
        ctx.ellipse(+p[1] + tx, +p[2] + ty, +p[3], +p[4], 0, 0, 2 * Math.PI);
        ctx.fill();
      } else if (op === "D") {
        /* semantic death event: species, id, world x, world y */
        addGhost(+p[1], +p[2], +p[3], +p[4] + ty, tx);
      }
    }
    if (forestFrame) drawOccluders(ctx, W, H, forestTx);   /* vegetation in front of the actors */
    drawGhosts(ctx, W);
    if (fActive) {                              /* audio cues from frame deltas */
      if (fBolts.length > prevBolts) playEnemyFire();
      if (fFoes.length < prevFoes) playExplosion();
    }
    lastCanvasScript = fActive ? script : "";   /* enemy-free redraw source for rAF */
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
  /* developer/test entry: `?start=forest|cave|helmet` initialises that world
   * directly so a tester need not replay the forest. The host only forwards
   * the URL token; Glon (kaka-start-mode) owns the mode setup. The token is
   * re-applied on every restart while the parameter is present. */
  function startToken() {
    var m = /[?&]start=([a-z]+)/.exec(window.location.search);
    return m ? m[1] : "";
  }
  function applyStart() {
    var t = startToken();
    if (t === "forest" || t === "cave" || t === "helmet") { try { evVal("kaka-start-mode", t); } catch (e) {} }
  }
  /* debug hook for headless screenshot/QA harnesses (no game rule lives here) */
  window.__kaka = { ev: ev, evVal: evVal, route: route, applyStart: applyStart,
    /* read-only fighter read-out for screenshot/QA harnesses (no game rule) */
    fighter: function () { return { on: fActive, prog: fProg, x: fPx, y: fPy, can: fCanX, w: fCanW, shield: fShield, phase: fPhase }; },
    /* QA only: freeze the sim and paint one synthetic fighter frame (e.g. to
       screenshot the canyon at a given mission distance); pin(null) resumes */
    pin: function (script) { pinnedScript = script || null; if (script) drawScript(script); } };
  var pinnedScript = null;

  var KEY = { ArrowLeft: "left", KeyA: "left", ArrowRight: "right", KeyD: "right",
              ArrowUp: "up", KeyW: "up", ArrowDown: "down", KeyS: "down",
              Space: "fire", KeyG: "glon", KeyF: "rock" };
  /* Glon key state is a pure function of the PHYSICAL held state, re-synced once
     per frame. There is no per-control latch that can get stranded, so no hit,
     knockback, swarm, frame stall or dropped pointer event can leave LEFT held,
     RIGHT suppressed or ROCK disabled. A press that begins and ends between two
     frames is still applied for exactly one frame (the `tapped` set), so a quick
     tap is never lost. LEFT/RIGHT are included, so turning is never dropped, and
     a reset re-sends whatever is still physically held. */
  var held = {}, tapped = {}, applied = {};
  function setHeld(name, down) {
    if (down) { held[name] = 1; tapped[name] = 1; } else { held[name] = 0; }
  }
  function syncInput() {
    var want = {}, k;
    for (k in held) if (held[k]) want[k] = 1;
    for (k in tapped) want[k] = 1;              /* quick tap: down for this frame */
    for (k in want) if (!applied[k]) evVal("kaka-key-down", k);
    for (k in applied) if (!want[k]) evVal("kaka-key-up", k);
    applied = want;
    /* NB: `tapped` is cleared by frame() only AFTER a tick has run, so a tap
       that lands on a zero-tick frame cannot be lost. */
  }
  function resetInput() {
    held = {}; tapped = {}; applied = {};
    try { ev("kaka-input-reset"); } catch (err) { console.error("kaka: input reset failed", err); }
  }

  function key(e, down) {
    if (e.code === "KeyR") { if (down) { acc = 0; clearGhosts(); resetInput(); ev("kaka-restart"); applyStart(); } e.preventDefault(); return; }
    var k = KEY[e.code];
    if (!k) return;
    e.preventDefault();
    if (e.target && e.target.tagName === "BUTTON" && e.target.blur) e.target.blur();
    setHeld(k, down);
  }

  function focusGame() {
    var cv = document.querySelector("#glon-canvas");
    if (cv) cv.focus();
  }

  function frame(now) {
    if (pinnedScript) { lastTime = now; acc = 0; drawScript(pinnedScript); requestAnimationFrame(frame); return; }
    if (!lastTime) lastTime = now;      /* first callback: start the clock */
    var dt = now - lastTime;
    lastTime = now;
    if (dt > 500) dt = 500;             /* hidden/stalled tab: do not accrue a backlog */
    acc += dt;
    syncInput();                        /* physical input -> Glon key edges */
    var steps = 0;
    while (acc >= SIM_DT && steps < MAX_CATCHUP) {
      try { ev("kaka-tick"); }
      catch (err) { console.error("kaka: tick failed", err); acc = 0; break; }
      acc -= SIM_DT;
      steps++;
    }
    if (acc >= SIM_DT) acc = 0;         /* still behind: drop it, never spiral */
    /* A press is released only once at least one tick has consumed it. Until
       then it stays applied, so a quick tap that lands on a frame with no tick
       (common at 60 Hz with a 25 Hz sim, and more so under jump render load) is
       never dropped. Jump state therefore cannot reduce ROCK (or JUMP) taps. */
    if (steps > 0) tapped = {};
    /* In the helmet mission, re-render the last frame on tick-free frames so the
       time-driven speed cues animate at the display rate, not just 25 Hz. */
    if (steps === 0 && fActive && lastCanvasScript) {
      try { drawScript(lastCanvasScript); } catch (err) {}
    }
    requestAnimationFrame(frame);
  }

  function fireGon(el) {
    var tok = el.getAttribute("data-glon-event");
    if (!tok) return;
    if (tok === "kaka-restart") { clearGhosts(); resetInput(); }
    try { ev(tok); } catch (err) { console.error("kaka: event failed", err); }
    if (tok === "kaka-restart") applyStart();
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
          setHeld(name, true);
        }
        function up(e) {
          e.preventDefault();
          b.classList.remove("on");
          setHeld(name, false);
        }
        b.addEventListener("pointerdown", down);
        b.addEventListener("pointerup", up);
        b.addEventListener("pointercancel", up);
        b.addEventListener("lostpointercapture", up);
        b.addEventListener("contextmenu", function (e) { e.preventDefault(); });
      })(hold[i]);
    }
  }

  /* Direction control: four separate hold buttons in a cross around a central
     gap. They are ordinary hold buttons (wireTouch emits the SAME Glon key
     names as the keyboard), so two fingers on adjacent keys steer a diagonal
     and the gap between them is dead. No gameplay logic lives here. */

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
      /* kaka-fighter.glon is loaded EARLY (right after the core) so its parse
         has loader-heap headroom; it only needs core names (resolved at run). */
      .then(function () { return fetchText("kaka-fighter.glon"); }).then(load)
      .then(function () { return fetchText("kaka-draw.glon"); }).then(load)
      .then(function () { return fetchText("kaka-rangi.glon"); }).then(load)
      .then(function () { return fetchText("kaka-wave.glon"); }).then(load)
      /* kaka-selftest.glon is test-only and is intentionally not loaded here
         (and does not fit beside kaka-fighter.glon in the loader heap). */
      .then(loadParallax)            /* PNG scenery is presentation-only */
      .then(loadSprites)             /* actor sheets are presentation-only */
      .then(loadCaveImage)           /* cave backdrop image (procedural fallback) */
      .then(loadFighterImage)        /* helmet-route cockpit backdrop */
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
        applyStart();          /* ?start=forest|cave|helmet developer entry */
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
