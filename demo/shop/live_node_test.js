// demo/shop/live_node_test.js -- headless proof of the Glon text Live
// Translate application logic over the GLON_LIVE boundary.
//
// It instantiates demo/shop/glon-live.wasm, loads the three application
// <script type="application/glon"> blocks from live.html (common, strings,
// live), then:
//   - clicks Start via glon_event("start"): the host_call import must receive
//     op=qwen-connect arg=zh and no HTML;
//   - feeds the app-level Qwen events through glon_event_bytes exactly as the
//     browser host would after qwen-client.js maps the observed Qwen events;
//   - asserts Glon (not JS) decided the append-vs-replace semantics: source
//     deltas append, completed replaces the utterance; translation deltas
//     append, text.done replaces; error text is shown.
//
// No network and no real Qwen: the transport is the browser/relay's job.  This
// proves the Glon application and the boundary contract.
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon-live.wasm");
const HTML = fs.readFileSync(path.join(HERE, "live.html"), "utf8");

const scriptRe = /<script type="application\/glon"[^>]*>([\s\S]*?)<\/script>/g;
const blocks = [];
let m;
while ((m = scriptRe.exec(HTML)) !== null) blocks.push(m[1]);
if (blocks.length !== 3) {
  console.error("LIVE_TEST FAIL: expected 3 application/glon blocks, got " + blocks.length);
  process.exit(1);
}

const rendered = [];
const calls = [];
let mem, ex;

const imports = {
  env: {
    host_print(ptr, len) { /* console noise only */ },
    host_set_text() { /* unused */ },
    host_set_html(_handle, ptr, len) {
      rendered.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_canvas_script() { /* no canvas */ },
    host_call(opPtr, opLen, argPtr, argLen) {
      calls.push([
        new TextDecoder().decode(new Uint8Array(mem.buffer, opPtr, opLen)),
        new TextDecoder().decode(new Uint8Array(mem.buffer, argPtr, argLen))
      ]);
    }
  }
};

function fail(msg) {
  console.error("LIVE_TEST FAIL: " + msg);
  process.exit(1);
}

function putBytes(bytes) {
  const p = ex.glon_alloc(bytes.length);
  new Uint8Array(mem.buffer).set(bytes, p);
  return [p, bytes.length];
}

function put(str) {
  return putBytes(new TextEncoder().encode(str));
}

function event(token) {
  rendered.length = 0;
  const t = put(token);
  if (ex.glon_event(t[0], t[1]) !== 0) fail("glon_event('" + token + "') rc!=0");
  return rendered.join("");
}

function eventBytes(token, text) {
  rendered.length = 0;
  const t = put(token);
  const d = putBytes(new TextEncoder().encode(text));
  if (ex.glon_event_bytes(t[0], t[1], d[0], d[1]) !== 0) fail("glon_event_bytes('" + token + "') rc!=0");
  return rendered.join("");
}

function count(haystack, needle) {
  return haystack.split(needle).length - 1;
}

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  ex = instance.exports;
  mem = new DataView(ex.memory.buffer);

  if (ex.glon_init() !== 0) fail("glon_init");
  for (let i = 0; i < blocks.length; i++) {
    const [p, n] = put(blocks[i]);
    if (ex.glon_load(p, n) !== 0) fail("glon_load block " + i);
  }

  let page = event("init");
  if (!/Live Translate/.test(page) || !/Stopped/.test(page))
    fail("initial page missing heading/status: " + page.slice(0, 200));

  event("start");
  if (calls.length !== 1 || calls[0][0] !== "listen-start" || calls[0][1] !== "zh")
    fail("start should emit listen-start/zh, got " + JSON.stringify(calls));

  page = eventBytes("qwen-status", "Listening");
  if (!/Listening/.test(page)) fail("status event not shown: " + page.slice(0, 200));

  eventBytes("qwen-source-delta", "I'm speaking");
  page = eventBytes("qwen-source-delta", " English");
  if (page.indexOf("I'm speaking English") < 0)
    fail("source deltas did not append: " + page);

  page = eventBytes("qwen-source-done", "I'm speaking English. ");
  if (page.indexOf("I'm speaking English. ") < 0)
    fail("completed transcript missing: " + page);
  if (page.indexOf("I'm speaking English. I'm speaking English") >= 0)
    fail("completed transcript duplicated the streamed deltas: " + page);

  eventBytes("qwen-text-delta", "我");
  page = eventBytes("qwen-text-delta", "在说");
  if (page.indexOf("我在说") < 0) fail("translation deltas did not append: " + page);

  page = eventBytes("qwen-text-done", "我在说英语。");
  if (page.indexOf("我在说英语。") < 0) fail("final translation missing: " + page);
  if (count(page, "我在说英语。") !== 1)
    fail("final translation was not a replace (seen " + count(page, "我在说英语。") + " times)");

  page = eventBytes("qwen-error", "401 InvalidApiKey");
  if (page.indexOf("401 InvalidApiKey") < 0) fail("error event not shown: " + page);

  event("stop");
  if (calls.length !== 2 || calls[1][0] !== "listen-stop")
    fail("stop should emit listen-stop, got " + JSON.stringify(calls));

  console.log("LIVE_TEST PASS (Glon owns append/replace; host_call intents + glon_event_bytes events)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
