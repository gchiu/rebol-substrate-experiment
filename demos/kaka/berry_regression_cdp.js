/* demos/kaka/berry_regression_cdp.js -- real-browser regression for the
 * long-session "Space eventually stopped firing ordinary berries" bug.
 *
 * It drives the ACTUAL published-style page (kaka.html + kaka-host.js) in a real
 * Chrome over the DevTools Protocol (no --dump-dom, no virtual time).  A shim
 * installed before the page scripts captures the Canvas script the Glon game
 * emits each tick, so the host can count LIVE ordinary berries by their unique
 * "O x y 5 5" paint (berry-art in kaka-draw.glon) with no game-source hooks.
 *
 * The bug: ordinary berries are fired UPWARD (F_VY < 0) but berry-step despawned
 * them at Y > 480, which only a downward actor can reach. Every missed berry
 * leaked from the fixed BERRY_MAX pool until the pool pinned and fire silently
 * died.  This regression holds fire across a long session with live actor
 * create/free churn (spawns + forced mutants), then asserts the pool drains
 * after release and that firing resumes.
 *
 * Usage (same pattern as tick_regression_cdp.js):
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 about:blank
 *   node demos/kaka/berry_regression_cdp.js
 *
 * Env: CDP_BASE (default http://127.0.0.1:9222),
 *      TARGET   (default http://127.0.0.1:8900/kaka.html),
 *      BERRY_TICKS (default 1500) long-session length in Glon ticks.
 * Exits 0 and prints KAKA_BERRY_REGRESSION_OK on success.
 */

const base = process.env.CDP_BASE || process.argv[3] || "http://127.0.0.1:9222";
const target = process.env.TARGET || process.argv[2] || "http://127.0.0.1:8900/kaka.html";
const minTicks = Number(process.env.BERRY_TICKS || 1500);

/* Injected before any page script: capture the latest Canvas script pointer and
 * the WASM exports so the driver can count live ordinary berries. */
const INJECT = `(function(){
  window.__berry = { p: 0, l: 0, errors: [] };
  window.addEventListener("error", function(e){ window.__berry.errors.push("error: "+e.message); });
  window.addEventListener("unhandledrejection", function(e){ window.__berry.errors.push("rej: "+e.reason); });
  var _inst = WebAssembly.instantiate.bind(WebAssembly);
  WebAssembly.instantiate = function(bytes, imports){
    if (imports && imports.env && imports.env.host_canvas_script){
      var hc = imports.env.host_canvas_script;
      imports.env.host_canvas_script = function(p,l){ window.__berry.p = p; window.__berry.l = l; return hc(p,l); };
    }
    return _inst(bytes, imports).then(function(res){
      if (res && res.instance) window.__ex = res.instance.exports;
      return res;
    });
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
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  await send("Page.addScriptToEvaluateOnNewDocument", { source: INJECT });
  await send("Page.navigate", { url: target });

  const evalv = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.result.value;
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const state = async () => {
    const st = await evalv("(function(){var e=document.getElementById('kaka-state');return e?e.textContent.trim():'';})()");
    return st ? st.split(/\s+/).map(Number) : [];
  };
  /* Live ordinary berries straight from the real Canvas script. */
  const liveOrd = async () => await evalv(
    "(function(){if(!window.__ex||!window.__berry)return -1;" +
    "var p=window.__berry.p,l=window.__berry.l;" +
    "var s=new TextDecoder().decode(new Uint8Array(window.__ex.memory.buffer,p,l));" +
    "var m=s.match(/^O -?\\d+ -?\\d+ 5 5$/gm);return m?m.length:0;})()");
  const key = (down) => evalv("window.dispatchEvent(new KeyboardEvent('" + (down ? "keydown" : "keyup") + "',{code:'Space',bubbles:true,cancelable:true}))");

  function fail(msg) { console.log("KAKA_BERRY_REGRESSION_FAIL " + msg); process.exit(1); }

  /* boot */
  let bootDeadline = Date.now() + 30000, tick0 = 0;
  while (Date.now() < bootDeadline) {
    const s = await state();
    if (s.length >= 13 && s[0] > 0 && (await liveOrd()) >= 0) { tick0 = s[0]; break; }
    await sleep(500);
  }
  if (!tick0) fail("boot timeout: game state / render never appeared");

  /* create the churn button once (forces a kaka into mutant state each click) */
  await evalv("(function(){var b=document.createElement('button');b.setAttribute('data-glon-event','kaka-debug-mutant');b.style.display='none';document.body.appendChild(b);})()");
  const churn = () => evalv("(function(){var b=document.querySelector('button[data-glon-event=\"kaka-debug-mutant\"]');if(b)b.click();})()");

  /* long session: hold fire, let actors spawn/despawn, force mutant churn.
     Re-assert the key: headless Chrome can fire blur/visibilitychange, which
     makes the host reset input; a real player's finger keeps the key down. */
  await key(true);
  let maxOrd = 0, lastChurn = 0, lastKey = 0, sawBerry = 0;
  const soakDeadline = Date.now() + 120000;
  while (Date.now() < soakDeadline) {
    if (Date.now() - lastKey > 500) { await key(true); lastKey = Date.now(); }
    const s = await state();
    if (!s.length) { fail("state disappeared mid-session"); }
    const ord = await liveOrd();
    if (ord > maxOrd) maxOrd = ord;
    if (ord > 0) sawBerry++;
    if (Date.now() - lastChurn > 3000) { await churn(); lastChurn = Date.now(); }
    if (s[0] - tick0 >= minTicks) break;
    await sleep(50);
  }
  const ticks = (await state())[0] - tick0;
  if (ticks < minTicks) fail("long session did not reach " + minTicks + " ticks (got " + ticks + ")");
  if (sawBerry === 0) fail("ordinary berries never appeared during the long session");
  await key(false);

  /* The decisive check: after fire is released, every missed berry must leave
     the TOP of the field, so the live count drains to zero. The old Y>480
     despawn never matched an upward berry, so the pool pinned at BERRY_MAX and
     fire silently died -- a leaked pool can never drain. */
  let settled = -1;
  const drainDeadline = Date.now() + 15000;
  while (Date.now() < drainDeadline) {
    settled = await liveOrd();
    if (settled === 0) break;
    await sleep(250);
  }
  if (settled !== 0) fail("berry pool leaked: " + settled + " ordinary tuple(s) still live after fire released");
  if (maxOrd > 6) fail("live ordinary berries exceeded BERRY_MAX=" + 6 + " (max=" + maxOrd + ")");

  /* Firing must still work after the soak + churn: repeated fire/drain cycles. */
  let cycles = 0;
  for (let c = 0; c < 5; c++) {
    await key(true);
    let seen = 0;
    for (let i = 0; i < 8 && seen === 0; i++) { await key(true); if ((await liveOrd()) > 0) seen = 1; else await sleep(120); }
    await key(false);
    if (seen) cycles++;
    for (let i = 0; i < 24; i++) { const v = await liveOrd(); if (v === 0) break; await sleep(120); }
  }
  if (cycles < 5) fail("ordinary berry firing did not resume after long-session churn (" + cycles + "/5 cycles)");

  const errs = (await evalv("(window.__berry&&window.__berry.errors.length)||0")) || 0;
  const allErrors = errs + protocolErrors.length;
  if (allErrors !== 0) fail("browser errors: page=" + errs + " protocol=" + protocolErrors.length + " " + protocolErrors.join(" | "));

  console.log("KAKA_BERRY_REGRESSION_OK ticks=" + ticks + " maxOrdLive=" + maxOrd +
    " settled=0 refireCycles=" + cycles + "/5 errors=0");
  process.exit(0);
})().catch((e) => { console.log("KAKA_BERRY_REGRESSION_FAIL " + (e && e.stack ? e.stack : e)); process.exit(1); });
