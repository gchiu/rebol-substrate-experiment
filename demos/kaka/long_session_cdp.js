/* Attack of the Mutant Kaka long-session browser regression driver (D12A).
 *
 * Real-time Chrome via the DevTools Protocol.  It does NOT use --dump-dom or
 * --virtual-time-budget: those throttle requestAnimationFrame and cannot drive
 * the game clock.  Start a Chrome with an open remote-debugging port and
 * long_session.html loaded, e.g.:
 *
 *   python3 -m http.server 8900 --directory demos/kaka &
 *   chrome --remote-debugging-port=9222 http://127.0.0.1:8900/long_session.html
 *   node demos/kaka/long_session_cdp.js
 *
 * Exits 0 and prints KAKA_LONG_SESSION_OK when the page's #verify says so.
 */
const base = process.env.CDP_BASE || process.argv[2] || "http://127.0.0.1:9222";
const pageMatch = /long_session\.html/;
const deadlineMs = Number(process.env.CDP_DEADLINE_MS || 600000);

(async () => {
  const findDeadline = Date.now() + 30000;
  let page;
  while (Date.now() < findDeadline) {
    let targets = [];
    try { targets = await (await fetch(base + "/json")).json(); } catch (e) {}
    page = targets.find((t) => t.type === "page" && pageMatch.test(t.url));
    if (page) break;
    await new Promise((r) => setTimeout(r, 500));
  }
  if (!page) { console.log("NO_PAGE: open long_session.html in the debugged Chrome"); process.exit(2); }

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  let id = 1;
  const pending = new Map();
  ws.addEventListener("message", (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  });
  const send = (method, params = {}) =>
    new Promise((r) => { const i = id++; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await new Promise((r) => ws.addEventListener("open", r));
  await send("Runtime.enable");

  const deadline = Date.now() + deadlineMs;
  let last = "";
  while (Date.now() < deadline) {
    const res = await send("Runtime.evaluate", {
      expression: "(document.getElementById('verify')||{}).textContent||''",
      returnByValue: true
    });
    const txt = (res.result && res.result.result && res.result.result.value) || "";
    if (txt && txt !== last) {
      console.log(txt);
      last = txt;
      if (/^KAKA_LONG_SESSION_OK/.test(txt)) process.exit(0);
      if (/^KAKA_LONG_SESSION_FAIL/.test(txt)) process.exit(1);
    }
    await new Promise((r) => setTimeout(r, 1000));
  }
  console.log("TIMEOUT");
  process.exit(1);
})();
