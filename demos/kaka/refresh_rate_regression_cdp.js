/* demos/kaka/refresh_rate_regression_cdp.js -- display-refresh independence
 * regression for Attack of the Mutant Kaka (D12A.2).
 *
 * It loads the ACTUAL published-style page (kaka.html + kaka-host.js) in real
 * Chrome over the DevTools Protocol, but replaces requestAnimationFrame with a
 * fake clock that fires the callback at a chosen frequency (using a real
 * performance.now() timestamp, paced by setTimeout). It then runs the page for
 * the same wall-clock duration at 60, 120 and 144 Hz and asserts the Glon
 * simulation advances by approximately the same number of ticks -- i.e. game
 * speed is governed by the host's fixed simulation rate, not the display.
 *
 * It also checks the simulation actually advances, that the host ABI allocator
 * never fails and that there are no JS errors.
 *
 * Usage (same pattern as long_session_cdp.js):
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 about:blank
 *   node demos/kaka/refresh_rate_regression_cdp.js
 *
 * Env/args: argv[2]=TARGET (default http://127.0.0.1:8900/kaka.html),
 *           argv[3]=CDP_BASE (default http://127.0.0.1:9222),
 *           argv[4]=per-frequency duration ms (default 8000).
 * Exits 0 and prints KAKA_REFRESH_REGRESSION_OK on success.
 */

const base = process.env.CDP_BASE || process.argv[3] || "http://127.0.0.1:9222";
const target = process.env.TARGET || process.argv[2] || "http://127.0.0.1:8900/kaka.html";
const DURATION = Number(process.argv[4] || 8000);
const FREQS = [60, 120, 144];
const TOLERANCE = 1.35;          /* max/min ticks across refresh rates */
const MAX_SANE_HZ = 40;          /* the host targets 25 Hz; >40 means a runaway loop */

/* Injected before any page script. HZ is substituted per run. rAF becomes a
 * fake clock at HZ; the exports wrapper counts glon_alloc failures. */
function injectSource(hz) {
  return `(function(){
    var HZ = ${hz};
    window.__rr = { alloc:0, allocFail:0, raf:0, errors:[] };
    window.addEventListener("error", function(e){ window.__rr.errors.push("error: "+e.message); });
    window.addEventListener("unhandledrejection", function(e){ window.__rr.errors.push("rej: "+e.reason); });
    window.requestAnimationFrame = function(cb){ window.__rr.raf++; return setTimeout(function(){ cb(performance.now()); }, 1000/HZ); };
    var _inst = WebAssembly.instantiate.bind(WebAssembly);
    WebAssembly.instantiate = function(bytes, imports){
      return _inst(bytes, imports).then(function(res){
        var exp = res.instance.exports;
        var copy = {};
        Object.getOwnPropertyNames(exp).forEach(function(k){ copy[k] = exp[k]; });
        var ga = copy.glon_alloc;
        copy.glon_alloc = function(){ window.__rr.alloc++; var p = ga.apply(exp, arguments); if (p === 0) window.__rr.allocFail++; return p; };
        return { instance: { exports: copy } };
      });
    };
  })();`;
}

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
  const protocolErrors = [];
  ws.addEventListener("message", (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") protocolErrors.push("exception:" + JSON.stringify(m.params.exceptionDetails).slice(0, 200));
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") protocolErrors.push("console.error:" + JSON.stringify(m.params.args.map((a) => a.value || a.description)));
  });
  const send = (method, params = {}) => new Promise((r) => { const i = id++; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await new Promise((r) => ws.addEventListener("open", r));
  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  const evalv = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.result.value;
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const snap = async () => JSON.parse(await evalv(
    "JSON.stringify({tick:(function(){var e=document.getElementById('kaka-state');var a=e?e.textContent.trim().split(/\\s+/).map(Number):[];return a[0]||0;})()," +
    "over:((document.getElementById('status')||{}).textContent||'').indexOf('GAME OVER')>=0," +
    "allocFail:window.__rr?window.__rr.allocFail:0, alloc:window.__rr?window.__rr.alloc:0," +
    "errors:window.__rr?window.__rr.errors.length:0, raf:window.__rr?window.__rr.raf:0})"));

  const fail = (m) => { console.log("KAKA_REFRESH_REGRESSION_FAIL " + m); process.exit(1); };

  let injectId = null;
  async function useHz(hz) {
    if (injectId) { await send("Page.removeScriptToEvaluateOnNewDocument", { identifier: injectId }); injectId = null; }
    const r = await send("Page.addScriptToEvaluateOnNewDocument", { source: injectSource(hz) });
    injectId = r.result.identifier;
  }

  const results = [];
  for (const hz of FREQS) {
    await useHz(hz);
    await send("Page.navigate", { url: target + (target.indexOf("?") >= 0 ? "&" : "?") + "hz=" + hz });
    /* wait for boot */
    let booted = false;
    for (let i = 0; i < 120; i++) { const s = await snap(); if (s.tick > 0) { booted = true; break; } await sleep(250); }
    if (!booted) fail("boot timeout at " + hz + " Hz");
    await sleep(500);                       /* let the accumulator settle */
    const t0 = Date.now();
    const k0 = (await snap()).tick;
    await sleep(DURATION);
    const t1 = Date.now();
    const s1 = await snap();
    const rate = (s1.tick - k0) / ((t1 - t0) / 1000);
    results.push({ hz: hz, rate: rate, tick0: k0, tick1: s1.tick, allocFail: s1.allocFail, errors: s1.errors, raf: s1.raf });
  }

  const rates = results.map((r) => r.rate);
  const minR = Math.min.apply(null, rates), maxR = Math.max.apply(null, rates);
  const detail = results.map((r) => r.hz + "Hz=" + r.rate.toFixed(1)).join(" ");

  if (minR < 8) fail("simulation barely advanced: " + detail);
  if (maxR > MAX_SANE_HZ) fail("simulation ran faster than the fixed rate (refresh-dependent?): " + detail);
  if (maxR / minR > TOLERANCE) fail("simulation rate depends on refresh rate (" + maxR.toFixed(1) + "/" + minR.toFixed(1) + "): " + detail);
  for (const r of results) {
    if (r.allocFail !== 0) fail("allocFail=" + r.allocFail + " at " + r.hz + " Hz");
    if (r.errors !== 0) fail("js errors=" + r.errors + " at " + r.hz + " Hz");
  }
  if (protocolErrors.length) fail("protocol errors: " + protocolErrors.join(" | "));

  console.log("KAKA_REFRESH_REGRESSION_OK display-independent sim rate: " + detail +
    " (max/min=" + (maxR / minR).toFixed(2) + ", allocFail=0, errors=0)");
  process.exit(0);
})().catch((e) => { console.log("KAKA_REFRESH_REGRESSION_FAIL " + (e && e.stack ? e.stack : e)); process.exit(1); });
