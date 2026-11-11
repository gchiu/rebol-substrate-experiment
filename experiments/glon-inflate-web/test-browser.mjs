// Real-browser smoke and correctness test over the Chrome DevTools Protocol.
//
// This does not launch the browser. Start a local server and a headless Chrome
// (or Edge) with a remote-debugging port, then run:
//
//   node test-browser.mjs [--port 9222] [--url http://localhost:8765/index.html]
//
// Node 22+ (global fetch/WebSocket) is required. The page must be served over
// HTTP so that its glon.wasm and sample fetches are same-origin.
const args = process.argv.slice(2);
function arg(name, dflt) { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; }
const PORT = Number(arg("--port", "9222"));
const PAGE_URL = arg("--url", "http://localhost:8765/index.html");

const tabs = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
const page = tabs.find(t => t.type === "page" && t.url === PAGE_URL) ||
             tabs.find(t => t.type === "page");
if (!page) throw new Error("no CDP page; start Chrome with --remote-debugging-port=" + PORT);
const ws = new WebSocket(page.webSocketDebuggerUrl);
let seq = 0; const pending = new Map(); const consoleErrors = [];
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
  if (m.method === "Runtime.exceptionThrown") consoleErrors.push(m.params.exceptionDetails.text || "exception");
  if (m.method === "Log.entryAdded" && m.params.entry.level === "error") consoleErrors.push(m.params.entry.text);
};
await new Promise(r => ws.onopen = r);
const call = (method, params = {}) => new Promise((res, rej) => {
  const id = ++seq; pending.set(id, m => m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result));
  ws.send(JSON.stringify({ id, method, params }));
});
const evalJS = expression => call("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
await call("Runtime.enable"); await call("Log.enable"); await call("Page.enable");
await call("Page.reload", { ignoreCache: true });
await new Promise(r => setTimeout(r, 2500));

let pass = 0, fail = 0;
const ok = (cond, name, detail) => { if (cond) { pass++; console.log("ok  " + name); } else { fail++; console.log("FAIL " + name + (detail ? "\n     " + detail : "")); } };

const ready = await evalJS('document.getElementById("status").textContent');
ok(/^Ready/.test(ready.result.value), "page runtime is ready", ready.result.value);

async function decodeSample(id) {
  const expr = `(async()=>{
    const s=document.getElementById('sample'); s.value=${JSON.stringify(id)}; s.dispatchEvent(new Event('change'));
    document.getElementById('decode').click();
    for(let i=0;i<600;i++){await new Promise(r=>setTimeout(r,50)); const st=document.getElementById('status').textContent;
      if(st.startsWith('Decoded')||st.startsWith('Could not')||st.startsWith('Glon reported')) break;}
    return {status:document.getElementById('status').textContent,outBytes:document.getElementById('out-bytes').textContent,
      verify:document.getElementById('verify').textContent,error:document.getElementById('result-error').textContent,
      text:document.getElementById('text-out').textContent,hex:document.getElementById('hex-out').textContent,
      downloadDisabled:document.getElementById('download').disabled};
  })()`;
  return (await evalJS(expr)).result.value;
}
const samples = JSON.parse((await (await fetch(new URL("samples.json", PAGE_URL))).text())).samples;
for (const s of samples) {
  const r = await decodeSample(s.id);
  ok(r.status === `Decoded by Glon on S1: ${s.output_bytes} bytes.`, s.id + ": decoded by Glon", r.status);
  ok(/^Matches the stored expected output/.test(r.verify), s.id + ": matches independent expected output", r.verify);
  ok(r.error === "", s.id + ": no error", r.error);
}
const binary = await decodeSample("binary");
ok(/00 01 02 03/.test(binary.hex), "binary: hex preview preserves NUL/byte sequence");
ok(binary.downloadDisabled === false, "binary: download is enabled");
const empty = await decodeSample("empty");
ok(empty.outBytes === "0" && /Matches/.test(empty.verify), "empty: zero-byte output verified");

async function decodeText(input, encoding, format) {
  const expr = `(async()=>{
    const s=document.getElementById('sample'); s.value=''; s.dispatchEvent(new Event('change'));
    document.getElementById('encoding').value=${JSON.stringify(encoding)};
    document.getElementById('format').value=${JSON.stringify(format)};
    document.getElementById('input').value=${JSON.stringify(input)};
    document.getElementById('decode').click();
    for(let i=0;i<400;i++){await new Promise(r=>setTimeout(r,50)); const st=document.getElementById('status').textContent;
      if(st.startsWith('Decoded')||st.startsWith('Could not')||st.startsWith('Glon reported')) break;}
    return {status:document.getElementById('status').textContent,error:document.getElementById('result-error').textContent};
  })()`;
  return (await evalJS(expr)).result.value;
}
const malformed = await decodeText("BwAA", "base64", "raw");
ok(/error/.test(malformed.status) || /error/i.test(malformed.error), "malformed DEFLATE reports an error", JSON.stringify(malformed));
for (const [name, input, enc] of [["invalid Base64", "!!!!", "base64"], ["invalid hex", "0", "hex"], ["empty input", "", "base64"]]) {
  const r = await decodeText(input, enc, "raw");
  ok(r.status === "Could not decode.", name + " is rejected", JSON.stringify(r));
}
const rep1 = await decodeSample("hello"), rep2 = await decodeSample("hello");
ok(rep1.status === rep2.status && rep1.outBytes === rep2.outBytes, "consecutive decodes are repeatable");

const tech = await evalJS('document.body.innerText');
ok(/seven primitives/.test(tech.result.value) && /480\/480/.test(tech.result.value) && /44\/44/.test(tech.result.value),
   "page states the seven-primitive core and repository baselines");
ok(/Maximum compressed input:\s*3,000 bytes/.test(tech.result.value) &&
   /LZ77 history window:\s*768 bytes/.test(tech.result.value) &&
   /Not all valid DEFLATE streams are supported/.test(tech.result.value),
   "page states its public limits accurately");
const resources = await evalJS('performance.getEntriesByType("resource").map(e=>e.name).filter(n=>!n.startsWith(location.origin))');
ok(resources.result.value.length === 0, "all resources are same-origin", JSON.stringify(resources.result.value));
const overflow = await evalJS('document.documentElement.scrollWidth <= window.innerWidth + 1');
ok(overflow.result.value, "page has no horizontal overflow at current viewport");

ok(consoleErrors.length === 0, "no browser console errors", consoleErrors.join("; "));
console.log(`${pass} passed, ${fail} failed`);
ws.close();
process.exit(fail ? 1 : 0);
