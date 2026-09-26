// demo/shop/live_node_test.js -- headless verification of the experimental
// GLON_LIVE host-boundary round trip (no DOM, no browser, no emscripten).
//
// Instantiates demo/shop/glon-live.wasm with the five host imports, loads the
// three application <script type="application/glon"> blocks from live.html in
// order, then:
//   1. glon_event("go") -> the GLON program renders a host-call request; the
//      host_call import records op=echo arg=roundtrip and no HTML is written;
//   2. glon_event_bytes("reply", <long UTF-8>) -> the recorded reply is > 200
//      bytes and includes Chinese; host_set_html must contain it.
//
// This is the browser-equivalent proof of the raw inbound path and the generic
// outbound capability request.
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = path.join(HERE, "glon-live.wasm");
const HTML = fs.readFileSync(path.join(HERE, "live.html"), "utf8");

/* same reply as live-host.js: > 200 bytes of UTF-8 with Chinese and emoji */
const REPLY =
  "这是一个用于验证 Glon 主机调用和原始字节事件往返的中文测试字符串。" +
  "它包含中文、日本語、한국어、emoji 😀🎤🌐 以及 ASCII 边界。" +
  "JavaScript 通过 host_call 收到 Glon 的请求后，异步地把这些字节送回 Glon，并由 Glon 渲染到页面上。";

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
    host_set_html(handle, ptr, len) {
      rendered.push(new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len)));
    },
    host_canvas_script() { /* this probe draws no canvas */ },
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

function put(str) {
  const bytes = new TextEncoder().encode(str);
  const p = ex.glon_alloc(bytes.length);
  new Uint8Array(mem.buffer).set(bytes, p);
  return [p, bytes.length];
}

function putBytes(bytes) {
  const p = ex.glon_alloc(bytes.length);
  new Uint8Array(mem.buffer).set(bytes, p);
  return [p, bytes.length];
}

function event(token) {
  rendered.length = 0;
  const [p, n] = put(token);
  if (ex.glon_event(p, n) !== 0) fail("glon_event('" + token + "') rc!=0");
  return rendered.join("");
}

function eventBytes(token, bytes) {
  rendered.length = 0;
  const t = put(token);
  const d = putBytes(bytes);
  if (ex.glon_event_bytes(t[0], t[1], d[0], d[1]) !== 0) fail("glon_event_bytes('" + token + "') rc!=0");
  return rendered.join("");
}

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  ex = instance.exports;
  mem = new DataView(ex.memory.buffer);

  if (ex.glon_init() !== 0) fail("glon_init");

  for (let i = 0; i < blocks.length; i++) {
    const [p, n] = put(blocks[i]);
    if (ex.glon_load(p, n) !== 0) fail("glon_load block " + i);
  }

  const initial = event("init");
  if (!/Live Translate host-boundary probe/.test(initial))
    fail("initial render missing the probe heading: " + initial.slice(0, 200));

  event("go");
  if (calls.length !== 1 || calls[0][0] !== "echo" || calls[0][1] !== "roundtrip")
    fail("host_call expected echo/roundtrip, got " + JSON.stringify(calls));

  const bytes = new TextEncoder().encode(REPLY);
  if (bytes.length <= 200) fail("reply is not > 200 bytes: " + bytes.length);

  const replyOut = eventBytes("reply", bytes);
  if (replyOut.indexOf(REPLY) < 0)
    fail("rendered page does not contain the full UTF-8 reply (" + bytes.length + " bytes)");
  if (replyOut.indexOf("中文测试字符串") < 0)
    fail("rendered page does not contain the Chinese text");

  console.log("LIVE_TEST PASS (host_call echo/roundtrip -> glon_event_bytes " + bytes.length + " UTF-8 bytes)");
  process.exit(0);
}).catch((e) => fail(e.message || e));
