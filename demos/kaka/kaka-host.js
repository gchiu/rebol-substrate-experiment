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
  function rangerLeg(ctx, hipx, footx, knee, C) {
    ctx.strokeStyle = C.pants; ctx.lineWidth = 11; ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(hipx, -30);
    ctx.lineTo((hipx + footx) / 2 + knee, -18);
    ctx.lineTo(footx, -6);
    ctx.stroke();
    ctx.fillStyle = C.boot; rrect(ctx, footx - 8, -10, 17, 12, 4); ctx.fill();
  }
  function rangerArm(ctx, shx, handx, handy, C) {
    ctx.strokeStyle = C.tunic; ctx.lineWidth = 9; ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(shx, -52);
    ctx.lineTo((shx + handx) / 2, (handy - 52) / 2);
    ctx.lineTo(handx, handy);
    ctx.stroke();
    ctx.fillStyle = C.skin; ctx.beginPath(); ctx.arc(handx, handy, 5, 0, 2 * Math.PI); ctx.fill();
  }
  var RANGER = { skin: "#ecb27e", hair: "#3a2a1a", hat: "#83713f", hatD: "#5c4c24",
                 tunic: "#3f9146", tunicD: "#2c6d32", belt: "#5a3a22",
                 pants: "#5c6a3e", boot: "#4a2f18", eye: "#241f16" };
  /* pose 5: the terminal, still, fallen ranger (no run cycle) */
  function drawFallenRanger(ctx, C) {
    ctx.fillStyle = "rgba(0,0,0,.22)";
    ctx.beginPath(); ctx.ellipse(-2, 2, 30, 7, 0, 0, 2 * Math.PI); ctx.fill();
    ctx.strokeStyle = C.pants; ctx.lineWidth = 11; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(-8, -10); ctx.lineTo(-30, -6); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(-8, -14); ctx.lineTo(-32, -16); ctx.stroke();
    ctx.fillStyle = C.boot;
    rrect(ctx, -44, -13, 15, 12, 4); ctx.fill();
    rrect(ctx, -46, -24, 15, 12, 4); ctx.fill();
    ctx.fillStyle = C.tunic; rrect(ctx, -12, -28, 32, 19, 8); ctx.fill();
    ctx.fillStyle = C.tunicD; rrect(ctx, -12, -28, 32, 9, 8); ctx.fill();
    ctx.strokeStyle = C.tunic; ctx.lineWidth = 9; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(8, -24); ctx.lineTo(26, -32); ctx.stroke();
    ctx.fillStyle = C.skin; ctx.beginPath(); ctx.arc(26, -32, 5, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = C.skin; ctx.beginPath(); ctx.arc(30, -18, 13, 0, 2 * Math.PI); ctx.fill();
    ctx.strokeStyle = C.eye; ctx.lineWidth = 2.4;
    ctx.beginPath(); ctx.moveTo(26, -23); ctx.lineTo(31, -18); ctx.moveTo(31, -23); ctx.lineTo(26, -18); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(34, -23); ctx.lineTo(39, -18); ctx.moveTo(39, -23); ctx.lineTo(34, -18); ctx.stroke();
    ctx.fillStyle = C.hat; ctx.beginPath(); ctx.ellipse(48, -6, 16, 5, 0.25, 0, 2 * Math.PI); ctx.fill();
    rrect(ctx, 41, -18, 20, 13, 5); ctx.fill();
    ctx.fillStyle = C.hatD; rrect(ctx, 41, -9, 20, 4, 2); ctx.fill();
  }
  function drawRanger(ctx, pose, x, y, dir, tx) {
    if (pose === 6) pose = 3;          /* climb pose reuses the jump limbs */
    var cx = x + tx, fy = y + 28, C = RANGER;
    ctx.save();
    ctx.translate(cx, fy);
    if (dir < 0) ctx.scale(-1, 1);
    if (caveMode) ctx.scale(CAVE_RANGI_S, CAVE_RANGI_S);  /* cave presentation only; feet anchored */
    ctx.globalAlpha = rangerAlpha;      /* phone route dissolves the ranger */
    if (pose === 5) { drawFallenRanger(ctx, C); ctx.restore(); return; }
    ctx.fillStyle = "rgba(0,0,0,.22)";
    ctx.beginPath(); ctx.ellipse(0, 2, 22, 6, 0, 0, 2 * Math.PI); ctx.fill();
    var lean = 0, lf = 4, rf = -4, lk = 0, rk = 0, lh = -14, rh = -14, lhx = 20, rhx = 20, hit = false;
    if (pose === 1) { lean = -0.07; lf = 12; rf = -10; lk = 0; rk = 0; lhx = 2; rhx = 30; lh = -4; rh = -24; }
    else if (pose === 2) { lean = -0.07; lf = -10; rf = 12; lhx = 30; rhx = 2; lh = -24; rh = -4; }
    else if (pose === 3) { lf = 9; rf = -9; lk = -9; rk = -9; lhx = 6; rhx = 6; lh = -38; rh = -38; }
    else if (pose === 4) { lean = 0.12; lhx = 32; rhx = 30; lh = -30; rh = -28; hit = true; }
    ctx.save();
    ctx.rotate(lean);
    rangerLeg(ctx, -8, -8 + lf, lk, C);
    rangerLeg(ctx, 8, 8 + rf, rk, C);
    ctx.fillStyle = C.tunic; rrect(ctx, -14, -58, 28, 32, 9); ctx.fill();
    ctx.fillStyle = C.tunicD; rrect(ctx, -14, -58, 28, 11, 9); ctx.fill();
    ctx.fillStyle = C.belt; rrect(ctx, -14, -34, 28, 7, 3); ctx.fill();
    ctx.fillStyle = "#d9b24a"; rrect(ctx, -4, -34, 8, 7, 2); ctx.fill();
    rangerArm(ctx, -13, -13 + lhx, lh, C);
    rangerArm(ctx, 13, 13 + rhx, rh, C);
    ctx.fillStyle = C.skin;
    ctx.beginPath(); ctx.arc(0, -70, 13, 0, 2 * Math.PI); ctx.fill();
    ctx.beginPath(); ctx.arc(-11, -70, 3.5, 0, 2 * Math.PI); ctx.fill();   /* ear */
    ctx.fillStyle = C.hair;
    ctx.beginPath(); ctx.arc(-3, -76, 11, Math.PI * 0.95, Math.PI * 2.05); ctx.fill();
    ctx.fillStyle = C.eye;
    if (hit) {
      ctx.strokeStyle = C.eye; ctx.lineWidth = 2.4;
      ctx.beginPath(); ctx.moveTo(-9, -73); ctx.lineTo(-3, -67); ctx.moveTo(-3, -73); ctx.lineTo(-9, -67); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(4, -73); ctx.lineTo(10, -67); ctx.moveTo(10, -73); ctx.lineTo(4, -67); ctx.stroke();
    } else {
      ctx.beginPath(); ctx.arc(-5, -71, 2.4, 0, 2 * Math.PI); ctx.fill();
      ctx.beginPath(); ctx.arc(5, -71, 2.4, 0, 2 * Math.PI); ctx.fill();
      ctx.strokeStyle = C.eye; ctx.lineWidth = 1.8;
      ctx.beginPath(); ctx.arc(0, -66, 5, 0.15 * Math.PI, 0.85 * Math.PI); ctx.stroke();
    }
    ctx.fillStyle = C.hat;
    ctx.beginPath(); ctx.ellipse(0, -80, 25, 6, 0, 0, 2 * Math.PI); ctx.fill();
    rrect(ctx, -12, -94, 24, 16, 6); ctx.fill();
    ctx.fillStyle = C.hatD; rrect(ctx, -12, -81, 24, 5, 2); ctx.fill();
    ctx.restore();
    ctx.restore();
  }

  /* ---- Skull Cave distance signs (host paints the semantic `N` op) ------- */
  var SIGN_LABELS = [
    "SKULL CAVE 100 km", "SKULL CAVE 75 km", "SKULL CAVE 50 km",
    "SKULL CAVE 25 km", "SKULL CAVE 5 km", "SKULL CAVE \u2014 NEXT EXIT"
  ];
  function drawSign(ctx, x, label, tx) {
    var t = SIGN_LABELS[label];
    if (!t) return;
    var sx = Math.round(x + tx), gy = 345;
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
  var fFoes = [], fBolts = [], fGate = null, fBomb = 0, fFlash = 0, fPrevShield = -1;
  var prevFoes = 0, prevBolts = 0, lastLaserMs = 0, laserSide = false, wasBoost = false;
  var FXCOL = { wall: "#39b7ff", dim: "rgba(80,180,255,.45)", foe: "#ff3b3b",
                bolt: "#ff8a3b", gate: "#5fd0ff", good: "#7fd18a", hot: "#ffd23f" };
  function loadFighterImage() {
    return loadImage("assets/vector-trench-cockpit.png").then(function (im) { fighterImg = im; });
  }
  function fScale(W) { return fighterImg ? W / fighterImg.naturalWidth : 1; }
  function fCenter(W, H) {
    if (!fighterImg) return { x: W * 0.5, y: H * 0.62, sc: 1 };
    var sc = fScale(W);
    return { x: W * 0.5, y: (H - fighterImg.naturalHeight * sc) + 520 * sc, sc: sc };
  }
  function fProj(W, H, ox, oy, oz) {
    var c = fCenter(W, H);
    var z = Math.max(oz, 16);
    var s = 230 / z;
    return { x: c.x + (ox - fPx) * s, y: c.y + (oy - fPy) * s * 0.62, s: s };
  }
  /* Altitude ride: the fighter's real Glon altitude (F_Y -> fPy) shifts the
     cockpit frame so the ship visibly climbs (fPy<0 -> frame up) or dives. The
     world already moves the opposite way via fProj(oy - fPy), so the trench
     sinks when the ship climbs — the unambiguous altitude cue. */
  function fAlt(W, H) { return fPy * fCenter(W, H).sc * 0.42; }
  /* Ship gun muzzles: cockpit weapon pods sampled from the rendered artwork
     geometry (image fractions of the bottom-anchored cockpit PNG). */
  function fGun(W, H, side) {
    var sc = fScale(W);
    var iw = fighterImg ? fighterImg.naturalWidth * sc : W;
    var ih = fighterImg ? fighterImg.naturalHeight * sc : H;
    var ix = (W - iw) / 2, iy = H - ih + fAlt(W, H);
    return { x: ix + (side ? 0.62 : 0.38) * iw, y: iy + 0.84 * ih };
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
    ctx.save(); ctx.lineCap = "round";
    var N = 124, ca, sa, i, ph, r, len, x0, y0, x1, y1, mx, my, a, head;
    var maxR = Math.sqrt(W * W + H * H) * 0.64;
    for (i = 0; i < N; i++) {
      var ang = i * 2.399963;
      ca = Math.cos(ang); sa = Math.sin(ang) * 0.62;
      ph = ((i * 0.117 + t * (0.72 + (i % 11) * 0.04) * sp) % 1 + 1) % 1;
      r = 6 + ph * ph * maxR;
      len = (16 + ph * ph * (210 + (boost ? 170 : 0)));
      x0 = c.x + ca * r; y0 = c.y + sa * r;                 /* leading head */
      x1 = c.x + ca * (r - len); y1 = c.y + sa * (r - len); /* tail */
      mx = c.x + ca * (r - len * 0.28); my = c.y + sa * (r - len * 0.28);
      a = 0.14 + ph * 0.86;
      /* fading tail */
      ctx.strokeStyle = "rgba(120,205,255," + (a * 0.35).toFixed(3) + ")";
      ctx.lineWidth = 1 + ph * (boost ? 2.6 : 1.8);
      ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(mx, my); ctx.stroke();
      /* bright head */
      ctx.strokeStyle = (boost && (i % 3 === 0)) ? "rgba(225,250,255," + a + ")"
                       : "rgba(160,230,255," + a + ")";
      ctx.lineWidth = 1.4 + ph * (boost ? 4.6 : 3.4);
      ctx.beginPath(); ctx.moveTo(mx, my); ctx.lineTo(x0, y0); ctx.stroke();
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
  /* perspective trench ribs expanding from the vanishing point + the four
     longitudinal rails the ribs slide along (the strongest speed read) */
  function fTrench(ctx, W, H, c, t, sp) {
    ctx.save();
    var NR = 16, k, ph, s, hw, hh, yT, yB, a;
    for (k = 1; k <= NR; k++) {
      ph = (((k / NR) + t * 0.64 * sp) % 1 + 1) % 1;
      s = ph * ph * ph;
      hw = 10 + s * W * 0.66; hh = 8 + s * H * 0.54;
      yT = c.y - hh * 0.5; yB = c.y + hh * 0.6;
      a = 0.06 + s * 0.6;
      ctx.strokeStyle = "rgba(70,190,255," + a.toFixed(3) + ")";
      ctx.lineWidth = 1 + s * 2;
      ctx.beginPath();
      ctx.moveTo(c.x - hw, yT); ctx.lineTo(c.x - hw, yB);
      ctx.moveTo(c.x + hw, yT); ctx.lineTo(c.x + hw, yB);
      ctx.moveTo(c.x - hw, yB); ctx.lineTo(c.x + hw, yB);
      ctx.moveTo(c.x - hw, yT); ctx.lineTo(c.x + hw, yT);
      ctx.stroke();
    }
    var R = Math.max(W, H) * 1.15;
    ctx.strokeStyle = "rgba(90,205,255,0.20)"; ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(c.x, c.y); ctx.lineTo(c.x - R, c.y - R * 0.62);
    ctx.moveTo(c.x, c.y); ctx.lineTo(c.x + R, c.y - R * 0.62);
    ctx.moveTo(c.x, c.y); ctx.lineTo(c.x - R, c.y + R * 0.62);
    ctx.moveTo(c.x, c.y); ctx.lineTo(c.x + R, c.y + R * 0.62);
    ctx.stroke();
    ctx.restore();
  }
  function drawFighterBackground(ctx, W, H) {
    var c = fCenter(W, H), t = performance.now() / 1000, sp = fSpeed();
    var sx = 0, sy = 0;
    if (fBoost > 0) { sx = Math.sin(t * 63) * 3 + Math.sin(t * 41) * 1.5; sy = Math.cos(t * 55) * 2.4; }
    var ay = fAlt(W, H);                         /* ship altitude ride (F_Y) */
    /* the world shifts the OPPOSITE way, so a climb sinks the trench */
    var cw = { x: c.x, y: c.y - ay, sc: c.sc };
    ctx.save();
    ctx.translate(sx, sy);
    ctx.fillStyle = "#01020a"; ctx.fillRect(-10, -10, W + 20, H + 20);
    if (fighterImg) {
      var sc = fScale(W), iw = fighterImg.naturalWidth * sc, ih = fighterImg.naturalHeight * sc;
      ctx.drawImage(fighterImg, (W - iw) / 2, H - ih + ay, iw, ih);
      if (ay < 0) { ctx.fillStyle = "#01020a"; ctx.fillRect(-10, H + ay - 2, W + 20, -ay + 16); }
      /* subdue the baked "fixed cabin light" streaks: a dark wash over the
         trench region (above the dashboard) so the dynamic streaks dominate */
      var dashTop = (H - ih) + ay + 628 * sc;
      var g = ctx.createLinearGradient(0, 0, 0, dashTop);
      g.addColorStop(0, "rgba(1,3,14,0.72)");
      g.addColorStop(0.72, "rgba(1,3,14,0.52)");
      g.addColorStop(1, "rgba(1,3,14,0.0)");
      ctx.fillStyle = g; ctx.fillRect(-10, -10, W + 20, dashTop + 10);
    }
    /* bright vanishing-point "warp core" — a strong depth/motion anchor */
    var wc = ctx.createRadialGradient(cw.x, cw.y, 1, cw.x, cw.y, 46 + (fBoost > 0 ? 24 : 0));
    wc.addColorStop(0, "rgba(190,240,255,.85)");
    wc.addColorStop(0.4, "rgba(90,200,255,.28)");
    wc.addColorStop(1, "rgba(90,200,255,0)");
    ctx.fillStyle = wc; ctx.beginPath(); ctx.arc(cw.x, cw.y, 46 + (fBoost > 0 ? 24 : 0), 0, 2 * Math.PI); ctx.fill();
    fTrench(ctx, W, H, cw, t, sp);
    fStars(ctx, W, H, cw, t, sp);
    fStreaks(ctx, W, H, cw, t, sp, fBoost > 0);
    if (fBoost > 0) {
      ctx.fillStyle = "rgba(120,200,255,.09)"; ctx.fillRect(-10, -10, W + 20, H + 20);
    }
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
    ctx.restore();
  }
  function drawBolt(ctx, bx, by, bz) {
    var W = ctx.canvas.width, H = ctx.canvas.height;
    var q = fProj(W, H, bx, by, bz);
    var s = Math.max(3, Math.min(16, q.s * 3));
    ctx.save();
    var g = ctx.createRadialGradient(q.x, q.y, 1, q.x, q.y, s * 2.4);
    g.addColorStop(0, "#ffd9a0"); g.addColorStop(0.4, FXCOL.bolt); g.addColorStop(1, "rgba(255,80,0,0)");
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(q.x, q.y, s * 2.4, 0, 2 * Math.PI); ctx.fill();
    ctx.restore();
  }
  function drawGate(ctx, pattern, pct) {
    var W = ctx.canvas.width, H = ctx.canvas.height;
    var c = fCenter(W, H);
    var s = Math.max(0, Math.min(1, pct / 100));
    var scale = s * s * 7 + 0.12;
    var hw = 18 + scale * 300, hh = 12 + scale * 150;
    var px = c.x - fPx * scale * 0.9, py = c.y - fPy * scale * 0.5;
    ctx.save();
    ctx.strokeStyle = FXCOL.gate; ctx.lineWidth = 2 + s * 3;
    ctx.globalAlpha = 0.35 + s * 0.65;
    ctx.strokeRect(px - hw, py - hh, hw * 2, hh * 2);
    ctx.fillStyle = "rgba(95,208,255,.16)";
    if (pattern === 0) ctx.fillRect(px - hw, py - hh, hw * 2, hh * 0.7);
    else if (pattern === 1) ctx.fillRect(px - hw, py + hh * 0.3, hw * 2, hh * 0.7);
    else if (pattern === 2) ctx.fillRect(px - hw, py - hh, hw * 0.7, hh * 2);
    else if (pattern === 3) ctx.fillRect(px + hw * 0.3, py - hh, hw * 0.7, hh * 2);
    else { ctx.fillRect(px - hw, py - hh, hw * 2, hh * 0.5); ctx.fillRect(px - hw, py + hh * 0.5, hw * 2, hh * 0.5); }
    ctx.restore();
  }
  function drawTarget(ctx, W, H) {
    var c = fCenter(W, H);
    var present = (fProg >= 850 && fPhase === 0);
    if (!present) return;
    var aligned = Math.abs(fPx) < 70;
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
    ctx.fillText(aligned ? "GLON LOCKED" : "ALIGN + GLON", c.x, c.y - r - 10);
    ctx.restore();
  }
  function drawFighterHud(ctx, W) {
    var names = ["LAUNCH", "TRENCH", "PRESSURE", "TARGET", "ESCAPE", "SUCCESS"];
    var phase = fPhase === 0 ? (fProg < 300 ? "TRENCH RUN" : (fProg < 850 ? "ENEMY PRESSURE" : "TARGET APPROACH"))
              : (fPhase === 1 ? "ESCAPE" : (fPhase === 2 ? "SUCCESS" : "FAILED"));
    ctx.save();
    ctx.font = "bold 16px system-ui, sans-serif"; ctx.textAlign = "left"; ctx.textBaseline = "top";
    ctx.fillStyle = "rgba(0,0,0,.45)"; ctx.fillRect(8, 8, 300, 56);
    ctx.fillStyle = "#cfeeff";
    ctx.fillText("HELMET RUN  " + phase, 16, 12);
    ctx.fillStyle = fShield > 1 ? "#7fd18a" : "#ffd23f";
    ctx.fillText("SHIELD " + fShield + "   BOMB " + (fBomb ? "READY" : "SPENT") + "   T" + Math.floor(fProg / 25) + "s", 16, 34);
    ctx.restore();
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
      if (op === "A") {
        fActive = true; setFighterUi(true);
        prevFoes = fFoes.length; prevBolts = fBolts.length;
        fPx = +p[1]; fPy = +p[2]; fPhase = +p[3]; fShield = +p[4];
        fBoost = +p[5]; fLaser = +p[6]; fProg = +p[7];
        fFoes = []; fBolts = []; fGate = null; fBomb = 0;
        if (fShield < fPrevShield) { fFlash = performance.now(); playHit(); }
        fPrevShield = fShield;
        if (fLaser) {                        /* fire once per press, then rapid-fire */
          var nowMs = performance.now();
          if (nowMs - lastLaserMs > 205) { lastLaserMs = nowMs; laserSide = !laserSide; playLaser(laserSide ? 0.28 : -0.28); }
        }
        if (fBoost > 0 && !wasBoost) { wasBoost = true; startBoost(); }
        else if (fBoost === 0 && wasBoost) { wasBoost = false; stopBoost(); }
        drawFighterBackground(ctx, W, H);
      } else if (op === "G") {
        fGate = { p: +p[1], pct: +p[2] };
        drawGate(ctx, fGate.p, fGate.pct);
      } else if (op === "U") {
        drawFoe(ctx, +p[1], +p[2], +p[3], +p[4]);
      } else if (op === "K") {
        drawBolt(ctx, +p[1], +p[2], +p[3]);
      } else if (op === "Y") {
        fBomb = +p[1];
        drawTarget(ctx, W, H);
        drawFighterReticle(ctx, W, H, fLaser);
        drawFighterHud(ctx, W);
        if (fFlash && performance.now() - fFlash < 220) {
          ctx.fillStyle = "rgba(255,60,60," + (0.35 * (1 - (performance.now() - fFlash) / 220)) + ")";
          ctx.fillRect(0, 0, W, H);
        }
      } else if (op === "B") {
        if (fActive) { fActive = false; setFighterUi(false); }
        cam = +p[1];
        tx = -cam;
        caveMode = false;
        paintLayer(ctx, W, 0, cam);   /* distant sky / hills  (slowest) */
        paintLayer(ctx, W, 1, cam);   /* native bush / treeline           */
        paintLayer(ctx, W, 2, cam);   /* orchard / fence / near landscape */
      } else if (op === "F") {
        paintLayer(ctx, W, 3, cam);   /* foreground foliage   (fastest)   */
      } else if (op === "S") {
        drawSprite(ctx, +p[1], +p[2], +p[3], +p[4], tx);
      } else if (op === "P") {
        drawRanger(ctx, +p[1], +p[2], +p[3], +p[4], tx);
      } else if (op === "N") {
        drawSign(ctx, +p[1], +p[2], tx);
      } else if (op === "V") {
        if (fActive) { fActive = false; setFighterUi(false); }
        cam = +p[1]; tx = -cam; caveMode = true;   /* cave plane scrolls 1:1 */
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
      } else if (op === "D") {
        /* semantic death event: species, id, world x, world y */
        addGhost(+p[1], +p[2], +p[3], +p[4], tx);
      }
    }
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
  window.__kaka = { ev: ev, evVal: evVal, route: route, applyStart: applyStart };

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
