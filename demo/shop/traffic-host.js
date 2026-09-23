/* demo/shop/traffic-host.js -- the minimal browser host for the standalone
 * traffic page (traffic.html).
 *
 * JavaScript responsibilities are strictly limited to browser capabilities:
 *
 *   - instantiate demo/shop/glon.wasm (the existing G1A runtime, unchanged);
 *   - glon_init, then glon_load the combined source inlined in the page's
 *     <script type="application/glon"> block (common.glon + traffic.glon);
 *   - schedule simulation work: Start/Resume/Advance/Reset map to
 *     glon_event("traffic-start"/"traffic-resume"/"traffic-advance"/
 *     "traffic-reset"); the Baseline/Pacing selector maps to
 *     glon_event_value("traffic-mode", "baseline"|"pacing");
 *   - decode the G1_VIS canvas script Glon emitted (host_canvas_script) and
 *     paint the road + vehicle dots;
 *   - show the status line Glon emitted (host_set_html).
 *
 * This file contains NO IDM physics, NO gap/speed computation, NO pacing
 * policy, and NO vehicle positions of its own: every number it shows, and
 * which vehicle is paced and by how much, is decided entirely by frozen Glon
 * (demos/traffic.glon). JS only decides *when* to ask Glon for the next tick.
 */
(function () {
  "use strict";

  var ex = null;
  var dec = new TextDecoder();
  var enc = new TextEncoder();

  var running = false;
  var speed = 2;             // 1x / 2x / 5x
  var lastEvent = 0;

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
        if (el) el.textContent = dec.decode(view().subarray(ptr, ptr + len));
      },
      host_canvas_script: function (ptr, len) {
        paint(dec.decode(view().subarray(ptr, ptr + len)));
      }
    }
  };

  // ---- Canvas painting -----------------------------------------------------
  // The G1_VIS script is a sequence of newline commands. The traffic render
  // emits one L (road line) and 25 D (vehicle dot) commands; nothing else is
  // interpreted here. Vehicle 0 (the disturbed vehicle) is drawn in red.
  var canvas, ctx;

  function paint(script) {
    if (!canvas) {
      canvas = document.querySelector("#glon-canvas");
      ctx = canvas.getContext("2d");
    }
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    var lines = script.split("\n");
    for (var i = 0; i < lines.length; i++) {
      var p = lines[i].replace(/^\s+|\s+$/g, "").split(" ");
      if (p[0] === "L") {           // road
        ctx.strokeStyle = "#999";
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(Number(p[1]), Number(p[2]));
        ctx.lineTo(Number(p[3]), Number(p[4]));
        ctx.stroke();
      } else if (p[0] === "D") {    // dot: D id x y
        var id = Number(p[1]);
        var x = Number(p[2]);
        var y = Number(p[3]);
        ctx.fillStyle = (id === 0) ? "#d33" : "#2456a6";
        ctx.beginPath();
        ctx.arc(x, y, 6, 0, 2 * Math.PI);
        ctx.fill();
      }
    }
  }

  // ---- WASM ABI helpers ----------------------------------------------------
  function alloc(str) {
    var bytes = enc.encode(str);
    var p = ex.glon_alloc(bytes.length);
    view().set(bytes, p);
    return [p, bytes.length];
  }

  function event(token) {
    if (typeof ex.glon_event !== "function") return;
    var pair = alloc(token);
    var rc = ex.glon_event(pair[0], pair[1]);
    if (rc !== 0) console.error("traffic-host.js: glon_event('" + token + "') rc=" + rc);
  }

  function eventValue(token, value) {
    if (typeof ex.glon_event_value !== "function") return true;
    var t = alloc(token);
    var v = alloc(value);
    var rc = ex.glon_event_value(t[0], t[1], v[0], v[1]);
    if (rc !== 0) console.error("traffic-host.js: glon_event_value('" + token + "', '" + value + "') rc=" + rc);
    return rc === 0;
  }

  // ---- playback scheduling -------------------------------------------------
  // Each advance event = advance 5 = 0.5 simulated seconds. One event period
  // at speed S therefore covers S*5 ticks/s; 1x real-time is 10 ticks/s, i.e.
  // period = 500 ms. We schedule with requestAnimationFrame and a wall-clock
  // gate, so the loop never builds a backlog: if an event is still executing
  // when its next slot arrives, the slot is simply skipped.
  function eventPeriodMs() {
    return 500 / speed;
  }

  function frame(t) {
    requestAnimationFrame(frame);
    if (!running) return;
    if (t - lastEvent >= eventPeriodMs()) {
      lastEvent = t;
      event("traffic-advance");
    }
  }

  // ---- controls ------------------------------------------------------------
  // everStarted distinguishes a fresh Start (init + first render, so a mode
  // switch or Reset always begins from tick 0) from a Resume after Pause
  // (no re-init -- Pause only stops this file's scheduling loop, it never
  // tells Glon anything, so the simulation state Glon holds is untouched and
  // resuming must not rewind it).
  var everStarted = false;

  function start() {
    if (running) return;
    running = true;
    lastEvent = performance.now();
    if (everStarted) event("traffic-resume");
    else { event("traffic-start"); everStarted = true; }
    requestAnimationFrame(function (t) { lastEvent = t; });
  }

  function pause() {
    running = false;
  }

  function reset() {
    running = false;
    everStarted = false;
    event("traffic-reset");
  }

  function setMode(value) {
    running = false;
    everStarted = false;
    eventValue("traffic-mode", value);
  }

  // Lazy model loading: the main bundle only carries common.glon + IDM +
  // the model dispatcher (see build-traffic.py for why -- combining all
  // four model families into one glon_load call exceeds the parser's
  // per-block form limit). The other three models each sit in their own
  // hidden <script data-model="..."> block and are glon_load'd the first
  // time they are selected, exactly like the main shop launcher's own
  // lazy-loaded demos (see host.js there).
  // NOTE: the standalone page's loader heap (a fixed, never-reclaimed budget
  // shared by every glon_load in this page's lifetime -- see
  // GLON-TRAFFIC-MODELS.md sec 7/10) does not have room for all three of
  // newell/ovm/nasch loaded in the SAME session on top of common+IDM+the
  // dispatcher (measured directly). Worse: attempting a third model's
  // glon_load can leave so little headroom that EVERY later event dispatch
  // fails, including switching back to an already-working model -- so this
  // is capped at ONE additional (non-IDM) model per session, refused BEFORE
  // attempting glon_load, not discovered by breaking the page. This is
  // reported as application-level friction (design doc sec 7/10), not
  // routed around by enlarging the heap, which this task forbids.
  var loadedModels = { idm: true };
  var extraModelLoaded = null;   // name of the one non-IDM model loaded this session, if any
  function ensureModelLoaded(name) {
    if (loadedModels[name]) return true;
    if (extraModelLoaded) {
      console.error("traffic-host.js: refusing to load '" + name + "' -- '" + extraModelLoaded +
        "' already used this session's loader-heap budget for a non-IDM model (see GLON-TRAFFIC-MODELS.md).");
      window.alert("This session already loaded '" + extraModelLoaded + "'. Only one non-IDM model " +
        "fits the standalone page's loader-heap budget per session (see GLON-TRAFFIC-MODELS.md). " +
        "Reload the page to pick a different one.");
      return false;
    }
    var block = document.querySelector('script[data-model="' + name + '"]');
    if (!block) { console.error("traffic-host.js: no model block for '" + name + "'"); return false; }
    var pair = alloc(block.textContent);
    if (ex.glon_load(pair[0], pair[1]) !== 0) {
      console.error("traffic-host.js: glon_load('" + name + "') failed unexpectedly (see GLON-TRAFFIC-MODELS.md).");
      window.alert("Could not load the '" + name + "' model. Reload the page and try again.");
      return false;
    }
    loadedModels[name] = true;
    extraModelLoaded = name;
    return true;
  }

  function setModel(value) {
    running = false;
    everStarted = false;
    if (!ensureModelLoaded(value)) return false;
    // The model's own definitions can parse+load successfully (glon_load
    // above) while the loader heap is too tight for the SELECT event's own
    // small throwaway allocation (see the note on ensureModelLoaded) -- this
    // is the failure mode actually observed for a third distinct model in
    // one session, so it is checked here too, not just on glon_load.
    if (!eventValue("traffic-model", value)) {
      window.alert("Could not switch to the '" + value + "' model in this session (loader-heap " +
        "budget exhausted -- see GLON-TRAFFIC-MODELS.md). Reload the page to try a different combination.");
      return false;
    }
    return true;
  }

  function boot() {
    if (ex.glon_init() !== 0) {
      console.error("traffic-host.js: glon_init failed");
      return;
    }
    var src = document.querySelector('script[type="application/glon"]:not([data-model])');
    if (!src) {
      console.error("traffic-host.js: no main <script type=\"application/glon\"> found");
      return;
    }
    var pair = alloc(src.textContent);
    if (ex.glon_load(pair[0], pair[1]) !== 0) {
      console.error("traffic-host.js: glon_load failed");
      return;
    }

    var startBtn = document.querySelector("#start");
    var resetBtn = document.querySelector("#reset");
    var speedSel = document.querySelector("#speed");
    var modeSel = document.querySelector("#mode");
    var modelSel = document.querySelector("#model");

    startBtn.addEventListener("click", function () {
      if (running) { pause(); startBtn.textContent = "Start"; }
      else { start(); startBtn.textContent = "Pause"; }
    });
    resetBtn.addEventListener("click", function () {
      reset();
      startBtn.textContent = "Start";
    });
    speedSel.addEventListener("change", function () {
      speed = Number(speedSel.value);
    });
    modeSel.addEventListener("change", function () {
      setMode(modeSel.value);
      startBtn.textContent = "Start";
    });
    // The Scenario (mode) selector is only meaningful for IDM (Pacing was
    // built and characterized against IDM's own dynamics -- see
    // GLON-TRAFFIC-MODELS.md); hide it for the other three models rather
    // than let it silently do nothing.
    var lastModel = modelSel.value;
    modelSel.addEventListener("change", function () {
      var chosen = modelSel.value;
      if (!setModel(chosen)) { modelSel.value = lastModel; return; }  // load failed; stay put
      lastModel = chosen;
      startBtn.textContent = "Start";
      var isIdm = chosen === "idm";
      modeSel.style.display = isIdm ? "" : "none";
      modeSel.previousElementSibling.style.display = isIdm ? "" : "none";
      if (!isIdm) { modeSel.value = "baseline"; }
    });

    reset();                       // initial render (tick 0, 25 dots)
    requestAnimationFrame(frame);
  }

  function ready(result) {
    ex = result.instance.exports;
    boot();
  }

  function fail(err) {
    console.error("traffic-host.js: failed to load glon.wasm", err);
  }

  function loadBytes(bytes) {
    WebAssembly.instantiate(bytes, imports).then(ready).catch(fail);
  }

  if (typeof WebAssembly.instantiateStreaming === "function" && window.fetch) {
    WebAssembly.instantiateStreaming(fetch("glon.wasm"), imports)
      .then(ready)
      .catch(function () {
        fetch("glon.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
      });
  } else if (window.fetch) {
    fetch("glon.wasm").then(function (r) { return r.arrayBuffer(); }).then(loadBytes).catch(fail);
  } else {
    fail(new Error("no fetch / WebAssembly support"));
  }
})();
