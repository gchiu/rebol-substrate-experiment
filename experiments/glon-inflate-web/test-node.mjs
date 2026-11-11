// Headless verification of the same glon.wasm + Glon DEFLATE driver used by
// the page. Run: node test-node.mjs [glon.wasm]
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = process.argv[2] || path.join(HERE, "glon.wasm");
const LIBS = ["prelude.glon", "strings.glon", "strings-ext.glon",
              "f2buf.glon", "inflate.glon", "inflate-driver.glon"];
const MAX_SOURCE = 16384;

function minify(s) {
  return s.split(/\r?\n/).map(line => {
    let out = "", str = false;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (c === '"') { str = !str; out += c; continue; }
      if (!str && c === ';' && line[i + 1] === ';') break;
      out += c;
    }
    return out.trim();
  }).filter(Boolean).join('\n');
}
const source = LIBS.map(f => minify(fs.readFileSync(path.join(HERE, f), "utf8")));
const mod = await WebAssembly.compile(fs.readFileSync(WASM));
const samples = JSON.parse(fs.readFileSync(path.join(HERE, "samples.json"), "utf8")).samples;

let pass = 0, fail = 0;
function ok(cond, name, detail) {
  if (cond) { pass++; console.log("ok  " + name); }
  else { fail++; console.log("FAIL " + name + (detail ? "\n     " + detail : "")); }
}
function parseFrames(bytes) {
  const out = [], dec = new TextDecoder("latin1");
  let i = 0, chunks = 0, done = false, need = false, error = "";
  while (i < bytes.length) {
    const nl = bytes.indexOf(10, i);
    if (nl < 0) throw new Error("incomplete frame");
    const line = dec.decode(bytes.subarray(i, nl));
    if (/^O [0-9]+$/.test(line)) {
      const n = Number(line.slice(2)), start = nl + 1, end = start + n;
      if (end + 1 > bytes.length) throw new Error("truncated output frame");
      for (let k = start; k < end; k++) out.push(bytes[k]);
      if (bytes[end] !== 10) throw new Error("bad frame newline");
      chunks++; i = end + 1;
    } else if (line === "N") { need = true; i = nl + 1; }
    else if (line === "D") { done = true; i = nl + 1; }
    else if (line === "E") { error = "Glon DEFLATE error"; i = nl + 1; }
    else if (line.startsWith("[dump]")) { error = line; i = nl + 1; }
    else if (line === "") i = nl + 1;
    else throw new Error("unexpected frame: " + line);
  }
  return { bytes: new Uint8Array(out), chunks, done, need, error };
}
async function decode(raw, chunks) {
  let ex, captured = [];
  const view = () => new Uint8Array(ex.memory.buffer);
  const imports = { env: {
    host_print: (p, n) => captured.push(...view().subarray(p, p + n)),
    host_set_text: () => {}, host_set_html: () => {}, host_canvas_script: () => {}
  }};
  const inst = await WebAssembly.instantiate(mod, imports);
  ex = inst.exports;
  if (ex.glon_init() !== 0) throw new Error("glon_init failed");
  const put = s => { const b = new TextEncoder().encode(s); const p = ex.glon_alloc(b.length); view().set(b, p); return [p, b.length]; };
  for (let i = 0; i < LIBS.length; i++)
    if (ex.glon_load(...put(source[i])) !== 0) throw new Error(LIBS[i] + " failed");
  function run(src) {
    if (new TextEncoder().encode(src).length >= MAX_SOURCE) throw new Error("source too long");
    const before = captured.length;
    if (ex.glon_run(...put(src)) !== 0) throw new Error("glon_run rejected source");
    const f = parseFrames(new Uint8Array(captured.slice(before)));
    if (f.error) throw new Error(f.error);
    return f;
  }
  const all = []; let nchunks = 0, done = false;
  run("demo/browser-new");
  let off = 0;
  for (const n of chunks) {
    run("demo/browser-feed mk-string [ " + [...raw.subarray(off, off + n)].join(" ") + " ]");
    off += n;
    for (;;) {
      const r = run("demo/browser-read 256");
      all.push(...r.bytes); nchunks += r.chunks;
      if (r.done) { done = true; break; }
      if (r.need) break;
    }
    if (done) break;
  }
  if (!done) {
    run("demo/browser-finish");
    for (;;) {
      const r = run("demo/browser-read 256");
      all.push(...r.bytes); nchunks += r.chunks;
      if (r.done) { done = true; break; }
      if (r.need) throw new Error("need-input after finish");
    }
  }
  return { bytes: new Uint8Array(all), chunks: nchunks };
}
function b64(s) { return new Uint8Array(Buffer.from(s, "base64")); }
function same(a, b) { return a.length === b.length && a.every((v, i) => v === b[i]); }

for (const s of samples) {
  const container = b64(s.data);
  // The fixture's container is stripped only for zlib/gzip; raw goes direct.
  let raw = container;
  if (s.format === "zlib") raw = container.subarray(2, container.length - 4);
  if (s.format === "gzip") {
    let p = 10, flags = container[3];
    if (flags & 4) p += 2 + (container[p] | (container[p + 1] << 8));
    if (flags & 8) { while (container[p]) p++; p++; }
    if (flags & 16) { while (container[p]) p++; p++; }
    if (flags & 2) p += 2;
    raw = container.subarray(p, container.length - 8);
  }
  const got = await decode(raw, s.chunks);
  ok(same(got.bytes, b64(s.expected_base64)), s.id + ": output matches independent expected bytes",
     "got " + got.bytes.length + " expected " + s.output_bytes);
  if (s.id === "streaming") ok(got.chunks >= 1, "streaming: produced output in read calls", String(got.chunks));
  if (s.id === "binary") ok(got.bytes[0] === 0 && got.bytes[256] === 0, "binary: NUL bytes preserved");
  if (s.id === "empty") ok(got.bytes.length === 0, "empty: zero-byte output");
}
// Malformed block type 3, then good output; the driver must report a Glon error.
try {
  await decode(new Uint8Array([7, 0, 0]), [3]);
  ok(false, "malformed input reports an error");
} catch (e) {
  ok(/DEFLATE error|\[dump\]/.test(e.message), "malformed input reports an error", e.message);
}
// Repeatability: fresh machine per call must give the same bytes.
const hello = samples.find(s => s.id === "hello");
const h1 = await decode(b64(hello.data), hello.chunks);
const h2 = await decode(b64(hello.data), hello.chunks);
ok(same(h1.bytes, h2.bytes), "repeated decode is byte-identical");

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
