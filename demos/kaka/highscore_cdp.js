/* demos/kaka/highscore_cdp.js -- real-browser regression for the persisted
 * Kaka high score (localStorage bridge).  Drives the ACTUAL published page
 * (kaka.html + kaka-host.js) in real Chrome over the DevTools Protocol; no
 * --dump-dom and no virtual time.
 *
 * Glon owns the score and the comparison; the host only loads the one stored
 * number at boot and mirrors Glon's `#kaka-hs` value back when it changes.
 * This exercises that bridge end to end:
 *   - a seeded value reaches Glon and is rendered back;
 *   - a LOWER score never overwrites the stored high score;
 *   - a HIGHER score updates the stored high score;
 *   - restart keeps the high score;
 *   - a reload (new WASM instance) reloads the stored value;
 *   - absent/invalid storage reads as 0.
 *
 * Valued events are delivered through the page's own WASM exports (the same
 * `glon_event_value` path the host uses for key events), because the Kaka
 * host's DOM click delegation carries no value.
 *
 * Usage:
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 --headless=new about:blank
 *   node demos/kaka/highscore_cdp.js
 *
 * Env: CDP_BASE, TARGET.  Exits 0 and prints KAKA_HIGHSCORE_CDP_OK.
 */
const base = process.env.CDP_BASE || process.argv[3] || "http://127.0.0.1:9222";
const target = process.env.TARGET || process.argv[2] || "http://127.0.0.1:8900/kaka.html";
const KEY = "glon.kaka.highscore.v1";

const INJECT = `(function(){
  window.__hs = { errors: [] };
  window.addEventListener("error", function(e){ window.__hs.errors.push("error: "+e.message); });
  window.addEventListener("unhandledrejection", function(e){ window.__hs.errors.push("rej: "+e.reason); });
  var _inst = WebAssembly.instantiate.bind(WebAssembly);
  WebAssembly.instantiate = function(bytes, imports){
    return _inst(bytes, imports).then(function(res){
      if (res && res.instance) window.__ex = res.instance.exports;
      return res;
    });
  };
  window.__sendVal = function(tok, val){
    var ex = window.__ex; if (!ex) return -1;
    var enc = new TextEncoder();
    function put(s){ var b = enc.encode(s); var p = ex.glon_alloc(b.length); new Uint8Array(ex.memory.buffer).set(b, p); return [p, b.length]; }
    var a = put(tok), b = put(val);
    return ex.glon_event_value(a[0], a[1], b[0], b[1]);
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
  if (!page) { console.log("NO_PAGE: start a debug Chrome (remote-debugging-port)"); process.exit(2); }

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

  const evalv = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true })).result.result.value;
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const fail = (m) => { console.log("KAKA_HIGHSCORE_CDP_FAIL " + m); process.exit(1); };
  const store = async () => await evalv("localStorage.getItem('" + KEY + "')");
  const hs = async () => await evalv("(function(){var e=document.getElementById('kaka-hs');return e?parseInt(e.textContent,10):NaN;})()");
  const lives = async () => { const t = await evalv("(function(){var e=document.getElementById('kaka-state');return e?e.textContent.trim():'';})()"); const a = t.split(/\s+/).map(Number); return a.length >= 15 ? a[14] : -1; };
  const sendVal = async (tok, val) => await evalv("window.__sendVal(" + JSON.stringify(tok) + "," + JSON.stringify(String(val)) + ")");
  const clickRestart = async () => await evalv("(function(){var b=document.createElement('button');b.setAttribute('data-glon-event','kaka-restart');b.style.display='none';document.body.appendChild(b);b.click();})()");
  async function boot() {
    for (let i = 0; i < 120; i++) { if ((await lives()) >= 0) return true; await sleep(250); }
    return false;
  }

  /* 1. seed the stored high score, then load the page fresh */
  await send("Page.navigate", { url: target });
  if (!(await boot())) fail("boot timeout (initial)");
  await evalv("localStorage.clear();localStorage.setItem('" + KEY + "','4242');");
  await send("Page.reload", { ignoreCache: true });
  if (!(await boot())) fail("boot timeout (after seed)");
  if ((await store()) !== "4242") fail("seeded value not stored: " + (await store()));
  if ((await hs()) !== 4242) fail("seeded high score not rendered: " + (await hs()));

  /* 2. K: a lower score must not overwrite the stored high score */
  await sendVal("kaka-debug-score", 100);
  await sleep(100);
  if ((await store()) !== "4242") fail("lower score overwrote storage: " + (await store()));
  if ((await hs()) !== 4242) fail("lower score lowered the high score: " + (await hs()));

  /* 3. L: a higher score updates the stored high score */
  await sendVal("kaka-debug-score", 9999);
  await sleep(100);
  if ((await hs()) !== 9999) fail("higher score did not raise the high score: " + (await hs()));
  if ((await store()) !== "9999") fail("higher score not persisted: " + (await store()));

  /* 4. restart keeps the high score and restores three lives */
  await clickRestart();
  await sleep(100);
  if ((await lives()) !== 3) fail("restart did not restore 3 lives: " + (await lives()));
  if ((await store()) !== "9999") fail("restart erased the stored high score: " + (await store()));
  if ((await hs()) !== 9999) fail("restart erased the rendered high score: " + (await hs()));

  /* 5. J: reload (new WASM instance) reloads the persisted value */
  await send("Page.reload", { ignoreCache: true });
  if (!(await boot())) fail("boot timeout (after reload)");
  if ((await store()) !== "9999") fail("reload lost the stored high score: " + (await store()));
  if ((await hs()) !== 9999) fail("reload did not reload the high score: " + (await hs()));

  /* 6. invalid storage reads as 0 */
  await evalv("localStorage.setItem('" + KEY + "','not-a-number');");
  await send("Page.reload", { ignoreCache: true });
  if (!(await boot())) fail("boot timeout (after junk seed)");
  if ((await hs()) !== 0) fail("invalid stored value did not read as 0: " + (await hs()));

  const pageErrors = (await evalv("(window.__hs&&window.__hs.errors.length)||0")) || 0;
  if (pageErrors + protocolErrors.length !== 0)
    fail("browser errors: page=" + pageErrors + " protocol=" + protocolErrors.length + " " + protocolErrors.join(" | "));

  console.log("KAKA_HIGHSCORE_CDP_OK stored/reloaded/raised/lower-kept/restart/invalid=0 errors=0");
  process.exit(0);
})().catch((e) => { console.log("KAKA_HIGHSCORE_CDP_FAIL " + (e && e.stack ? e.stack : e)); process.exit(1); });
