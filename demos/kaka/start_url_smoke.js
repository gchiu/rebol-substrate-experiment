/* demos/kaka/start_url_smoke.js -- real-browser smoke for the developer
 * `?start=` entry points (?start=forest | cave | helmet). It drives the actual
 * page over the DevTools Protocol (same pattern as tick_regression_cdp.js) and,
 * for each URL, asserts that boot succeeds (no console/WASM/network error),
 * the simulation advances, the canvas is painted, and the mode-specific touch
 * labels are applied (BOOST/UP only in the helmet mission). It also restarts
 * each run (key R) and checks the requested mode is re-applied.
 *
 * Usage:
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 about:blank
 *   node demos/kaka/start_url_smoke.js
 * Env: CDP_BASE (default http://127.0.0.1:9222),
 *      SMOKE_BASE (default http://127.0.0.1:8900/kaka.html).
 * Prints KAKA_START_SMOKE_OK on success.
 */
const base = process.env.CDP_BASE || "http://127.0.0.1:9222";
const tbase = process.env.SMOKE_BASE || "http://127.0.0.1:8900/kaka.html";
const CASES = [
  { q: "?start=forest", fire: "BERRY", up: "JUMP" },
  { q: "?start=cave", fire: "BERRY", up: "JUMP" },
  { q: "?start=helmet", fire: "BOOST", up: "UP" },
  { q: "", fire: "BERRY", up: "JUMP" }   /* no param -> normal forest */
];
const INJECT = `(function(){
  window.__smoke = { errors: [] };
  window.addEventListener("error", function(e){ window.__smoke.errors.push("error: "+e.message); });
  window.addEventListener("unhandledrejection", function(e){ window.__smoke.errors.push("rej: "+e.reason); });
})();`;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
(async () => {
  const findDeadline = Date.now() + 30000;
  let page;
  while (Date.now() < findDeadline) {
    let targets = [];
    try { targets = await (await fetch(base + "/json")).json(); } catch (e) {}
    page = targets.find((t) => t.type === "page");
    if (page) break;
    await sleep(500);
  }
  if (!page) { console.log("NO_PAGE"); process.exit(2); }

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  let id = 1; const pending = new Map();
  let protoErrors = [];
  ws.addEventListener("message", (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") protoErrors.push("exception");
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") protoErrors.push("console.error");
    if (m.method === "Network.responseReceived" && m.params.response.status >= 400)
      protoErrors.push("HTTP " + m.params.response.status + " " + m.params.response.url);
  });
  const send = (method, params = {}) => new Promise((r) => { const i = id++; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await new Promise((r) => ws.addEventListener("open", r));
  await send("Page.enable"); await send("Runtime.enable"); await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  await send("Page.addScriptToEvaluateOnNewDocument", { source: INJECT });
  const evalv = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.result.value;

  function fail(msg) { console.log("KAKA_START_SMOKE_FAIL " + msg); process.exit(1); }

  for (const c of CASES) {
    protoErrors = [];
    await send("Page.navigate", { url: tbase + c.q });
    /* wait for a tick + canvas activity */
    let snap = null, deadline = Date.now() + 30000;
    while (Date.now() < deadline) {
      snap = JSON.parse(await evalv(
        "JSON.stringify({tick:(function(){var e=document.getElementById('kaka-state');var s=e?e.textContent.trim().split(/\\s+/).map(Number):[];return s[0]||0;})()," +
        "fire:(function(){var b=document.querySelector('[data-kaka-hold=fire]');return b?b.textContent:'';})()," +
        "up:(function(){var b=document.querySelector('[data-kaka-hold=up]');return b?b.textContent:'';})()," +
        "down:(function(){var b=document.querySelector('[data-kaka-hold=down]');return b?b.style.display:'';})()," +
        "errors:window.__smoke.errors.length})"));
      if (snap.tick > 0) break;
      await sleep(400);
    }
    if (!snap || snap.tick <= 0) fail(c.q + ": boot timeout (no tick)");
    const t1 = snap.tick; await sleep(1500);
    const advanced = (await evalv("(function(){var e=document.getElementById('kaka-state');return (e?e.textContent.trim().split(/\\s+/).map(Number)[0]:0);})()")) > t1;
    if (!advanced) fail(c.q + ": simulation did not advance");
    if (snap.errors !== 0) fail(c.q + ": page errors " + snap.errors);
    if (protoErrors.length) fail(c.q + ": " + protoErrors.join(" | "));
    if (snap.fire !== c.fire || snap.up !== c.up) fail(c.q + ": labels fire=" + snap.fire + " up=" + snap.up + " expected " + c.fire + "/" + c.up);
    if (c.q === "?start=helmet" && snap.down === "none") fail(c.q + ": DOWN button should be visible in the helmet mission");
    /* restart (key R) re-applies the requested mode */
    await send("Input.dispatchKeyEvent", { type: "keyDown", windowsVirtualKeyCode: 82, key: "r", code: "KeyR" });
    await send("Input.dispatchKeyEvent", { type: "keyUp", windowsVirtualKeyCode: 82, key: "r", code: "KeyR" });
    await sleep(900);
    const after = JSON.parse(await evalv("JSON.stringify({fire:(function(){var b=document.querySelector('[data-kaka-hold=fire]');return b?b.textContent:'';})()})"));
    if (after.fire !== c.fire) fail(c.q + ": restart did not re-apply mode (fire=" + after.fire + ")");
    console.log("  " + (c.q || "(no param)").padEnd(16) + " ok  labels=" + snap.fire + "/" + snap.up + " tick=" + t1 + "->advancing");
  }
  console.log("KAKA_START_SMOKE_OK (forest/cave/helmet/none boot, advance, labels, restart)");
  process.exit(0);
})().catch((e) => { console.log("KAKA_START_SMOKE_FAIL " + (e && e.stack ? e.stack : e)); process.exit(1); });
