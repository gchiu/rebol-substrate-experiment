/* demos/kaka/tick_regression_cdp.js -- real-browser regression for "the first
 * frame renders but the simulation never advances" (D12A frozen-Kaka bug).
 *
 * It drives the ACTUAL published-style page (kaka.html + kaka-host.js) in a real
 * Chrome over the DevTools Protocol; it does not call the Glon game directly and
 * does not use --dump-dom or virtual time.  It installs a diagnostic shim on the
 * page BEFORE its scripts run (Page.addScriptToEvaluateOnNewDocument) so it can
 * see requestAnimationFrame, canvas-script and error activity, then:
 *
 *   1. waits for boot and asserts the tick advances on its own (rAF -> Glon tick);
 *   2. forces game over (kaka-debug-over hook) and asserts the tick STILL advances
 *      -- the exact state the old build froze in;
 *   3. asserts the animation loop is alive and no JS/WASM error occurred.
 *
 * Usage (same pattern as long_session_cdp.js):
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 about:blank
 *   node demos/kaka/tick_regression_cdp.js
 *
 * Env: CDP_BASE (default http://127.0.0.1:9222),
 *      TARGET   (default http://127.0.0.1:8900/kaka.html).
 * Exits 0 and prints KAKA_TICK_REGRESSION_OK on success.
 */

const base = process.env.CDP_BASE || process.argv[3] || "http://127.0.0.1:9222";
const target = process.env.TARGET || process.argv[2] || "http://127.0.0.1:8900/kaka.html";

/* Injected before any page script: count rAF/canvas, capture errors. */
const INJECT = `(function(){
  window.__tickreg = { raf:0, canvas:0, errors:[] };
  window.addEventListener("error", function(e){ window.__tickreg.errors.push("error: "+e.message); });
  window.addEventListener("unhandledrejection", function(e){ window.__tickreg.errors.push("rej: "+e.reason); });
  var _raf = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = function(cb){ window.__tickreg.raf++; return _raf(cb); };
  var _inst = WebAssembly.instantiate.bind(WebAssembly);
  WebAssembly.instantiate = function(bytes, imports){
    if (imports && imports.env && imports.env.host_canvas_script){
      var hc = imports.env.host_canvas_script;
      imports.env.host_canvas_script = function(p,l){ window.__tickreg.canvas++; return hc(p,l); };
    }
    return _inst(bytes, imports);
  };
})();`;

(async () => {
  const findDeadline = Date.now() + 30000;
  let page;
  while (Date.now() < findDeadline) {
    let targets = [];
    try { targets = await (await fetch(base + "/json")).json(); } catch (e) {}
    page = targets.find((t) => t.type === "page");
    if (page) break;
    await new Promise((r) => setTimeout(r, 500));
  }
  if (!page) { console.log("NO_PAGE: start a debug Chrome with a remote-debugging port"); process.exit(2); }

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  let id = 1;
  const pending = new Map();
  let protocolErrors = [];
  ws.addEventListener("message", (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") protocolErrors.push("exception: " + JSON.stringify(m.params.exceptionDetails).slice(0, 200));
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error")
      protocolErrors.push("console.error: " + JSON.stringify(m.params.args.map((a) => a.value || a.description)));
  });
  const send = (method, params = {}) => new Promise((r) => { const i = id++; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await new Promise((r) => ws.addEventListener("open", r));
  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });  /* always test fresh assets */
  await send("Page.addScriptToEvaluateOnNewDocument", { source: INJECT });
  await send("Page.navigate", { url: target });

  const evalv = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.result.value;
  const snapshot = async () => JSON.parse(await evalv(
    "JSON.stringify({tick:(function(){var e=document.getElementById('kaka-state');var s=e?e.textContent.trim().split(/\\s+/).map(Number):[];return s[0]||0;})()," +
    "over:((document.getElementById('status')||{}).textContent||'').indexOf('GAME OVER')>=0," +
    "raf:window.__tickreg.raf, canvas:window.__tickreg.canvas, errors:window.__tickreg.errors.length})"));

  function fail(msg) { console.log("KAKA_TICK_REGRESSION_FAIL " + msg); process.exit(1); }
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  /* 1. boot and autonomous advancement */
  let bootDeadline = Date.now() + 30000, first = null;
  while (Date.now() < bootDeadline) {
    const s = await snapshot();
    if (s.tick > 0 && s.raf > 0) { first = s; break; }
    await sleep(500);
  }
  if (!first) fail("boot timeout: no tick/rAF activity (page never started animating)");
  await sleep(4000);
  const second = await snapshot();
  if (!(second.tick > first.tick)) fail("simulation did not advance without input (" + first.tick + " -> " + second.tick + ")");
  if (!(second.raf > first.raf)) fail("requestAnimationFrame stopped after the first frames");
  if (!(second.canvas > first.canvas)) fail("no render script emitted after the first frames");

  /* 2. force game over via the debug hook (clicks a synthetic [data-glon-event]) */
  await evalv("(function(){var b=document.createElement('button');b.setAttribute('data-glon-event','kaka-debug-over');b.style.display='none';document.body.appendChild(b);b.click();})()");
  await sleep(1500);
  const overSnap = await snapshot();
  if (!overSnap.over) fail("kaka-debug-over did not reach game over");
  const overTick = overSnap.tick;
  await sleep(4000);
  const afterSnap = await snapshot();
  if (!(afterSnap.tick > overTick)) fail("FROZEN: tick did not advance in game-over state (" + overTick + " -> " + afterSnap.tick + ")");

  /* 3. loop alive, no errors */
  if (!(afterSnap.raf > overSnap.raf)) fail("rAF stopped in game-over state");
  if (!(afterSnap.canvas > overSnap.canvas)) fail("canvas stopped updating in game-over state");
  const allErrors = afterSnap.errors + protocolErrors.length;
  if (allErrors !== 0) fail("browser errors: page=" + afterSnap.errors + " protocol=" + protocolErrors.length + " " + protocolErrors.join(" | "));

  console.log("KAKA_TICK_REGRESSION_OK advancing (" + first.tick + "->" + second.tick + "), " +
    "game-over still advancing (" + overTick + "->" + afterSnap.tick + "), raf=" + afterSnap.raf +
    ", canvas=" + afterSnap.canvas + ", errors=0");
  process.exit(0);
})().catch((e) => { console.log("KAKA_TICK_REGRESSION_FAIL " + (e && e.stack ? e.stack : e)); process.exit(1); });
