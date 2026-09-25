// demo/shop/primer_node_test.js -- run every Glon primer example through the
// real WASM runtime, exactly as primer-host.js does in the browser.
//
// The examples and their expected outcomes come from demo/shop/primer.txt (the
// primer's single source). The environment (bootstrap + strings + case [+ task
// library]) comes from the <script type="application/glon" data-env> blocks
// embedded in demo/shop/primer.html, so this also proves the page carries a
// working environment and shows exactly the tested sources. Each example runs in a
// FRESH instance: glon_init, glon_load the environment, glon_run the source,
// then glon_result_ptr/len; the text must equal the example's @expect line
// (where "<site>" stands for one positive integer, an internal site id).
//
// Usage: node primer_node_test.js [path/to/glon.wasm]
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = process.argv[2] || path.join(HERE, "glon.wasm");
const MANIFEST = fs.readFileSync(path.join(HERE, "primer.txt"), "utf8");
const HTML = fs.readFileSync(path.join(HERE, "primer.html"), "utf8");

function fail(msg) { console.error("PRIMER_TEST FAIL: " + msg); process.exit(1); }

// got === expect, where one "<site>" in expect matches a positive integer (an
// internal lexical site id); everything else must match exactly
function matches(got, expect) {
  const k = expect.indexOf("<site>");
  if (k < 0) return got === expect;
  const pre = expect.slice(0, k), post = expect.slice(k + 6);
  if (!got.startsWith(pre) || !got.endsWith(post)) return false;
  return /^[1-9][0-9]*$/.test(got.slice(pre.length, got.length - post.length));
}

// ---- the manifest's examples (same record format build-primer.py parses) ----
const examples = [];
{
  const lines = MANIFEST.split("\n");
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith("@example ")) continue;
    const ex = { id: lines[i].slice(9), env: "core", source: [], expect: null };
    let mode = null;
    for (i++; lines[i] !== "@end"; i++) {
      const l = lines[i];
      if (l === "@env tasks") ex.env = "tasks";
      else if (l === "@source") mode = "s";
      else if (l === "@expect") mode = "e";
      else if (mode === "s") ex.source.push(l);
      else if (mode === "e" && ex.expect === null) ex.expect = l;
    }
    while (ex.source.length && !ex.source[ex.source.length - 1].trim()) ex.source.pop();
    examples.push(ex);
  }
}
if (examples.length < 20) fail("only " + examples.length + " examples in primer.txt");

// ---- the page: embedded environment + the example textareas ----------------
const unescape = (s) => s.replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
const env = {};
for (const m of HTML.matchAll(/<script type="application\/glon" data-env="([a-z]+)">([\s\S]*?)<\/script>/g)) {
  env[m[1]] = m[2];
}
for (const name of ["bootstrap", "strings", "case", "tasks"]) if (!env[name]) fail("primer.html has no data-env=\"" + name + "\" block");
const shown = [...HTML.matchAll(/<div class="ex" data-ex="([^"]+)" data-env="([a-z]+)">\n<textarea[^>]*>([\s\S]*?)<\/textarea>/g)]
  .map((m) => ({ id: m[1], env: m[2], source: unescape(m[3]) }));
if (shown.length !== examples.length) fail("page shows " + shown.length + " examples, manifest has " + examples.length);
examples.forEach((ex, k) => {
  const s = shown[k];
  if (s.id !== ex.id || s.env !== ex.env || s.source !== ex.source.join("\n")) {
    fail("page example #" + k + " (" + s.id + ") differs from the manifest's " + ex.id);
  }
});
if (/@expect|data-expect/.test(HTML)) fail("primer.html must not carry expected results");

// ---- run each example in a fresh instance ------------------------------------
const mod = new WebAssembly.Module(fs.readFileSync(WASM));
const exported = WebAssembly.Module.exports(mod).map((e) => e.name);
for (const f of ["glon_run", "glon_result_ptr", "glon_result_len"]) {
  if (!exported.includes(f)) fail(path.basename(WASM) + " has no " + f + " export (stale build?)");
}

let lastOut = [];   // the raw bytes the last run printed (for the s/print check)
function run(source, envName) {
  const printed = [];
  const out = [];
  lastOut = out;
  let ex;
  const view = () => new Uint8Array(ex.memory.buffer);
  const instance = new WebAssembly.Instance(mod, {
    env: {
      host_print(ptr, len) {
        const b = view().subarray(ptr, ptr + len);
        for (const c of b) out.push(c);
        for (const l of new TextDecoder().decode(b).split("\n")) if (l) printed.push(l);
      },
      host_set_text(h, v) { printed.push(String(h * 1000000 + v)); },
      host_set_html() {},
      host_canvas_script() {}
    }
  });
  ex = instance.exports;
  const put = (s) => {
    const b = new TextEncoder().encode(s);
    const p = ex.glon_alloc(b.length);
    view().set(b, p);
    return [p, b.length];
  };
  if (ex.glon_init() !== 0) fail("glon_init");
  for (const name of ["bootstrap", "strings", "case"].concat(envName === "tasks" ? ["tasks"] : [])) {
    const [p, n] = put(env[name]);
    const rc = ex.glon_load(p, n);
    if (rc !== 0) fail("environment " + name + " glon_load rc=" + rc);
  }
  const [p, n] = put(source);
  if (ex.glon_run(p, n) !== 0) fail("glon_run refused the source");
  return new TextDecoder().decode(view().subarray(ex.glon_result_ptr(), ex.glon_result_ptr() + ex.glon_result_len()));
}

for (const ex of examples) {
  const got = run(ex.source.join("\n"), ex.env);
  if (!matches(got, ex.expect)) fail(ex.id + ": expected \"" + ex.expect + "\", got \"" + got + "\"");
}
// s/print writes a string's raw bytes (then a newline) through the host's output
run("0", "core");
const envBytes = lastOut.length;        // the environment itself prints nothing
if (run('s/print s/+ "hello " "world"', "core") !== "none"
    || Buffer.from(lastOut.slice(envBytes)).toString() !== "hello world\n") {
  fail("s/print: expected the bytes \"hello world\\n\", got " + JSON.stringify(Buffer.from(lastOut).toString()));
}
run("s/print mk-string [104 0 255 105]", "core");
if (Buffer.compare(Buffer.from(lastOut.slice(envBytes)), Buffer.from([104, 0, 255, 105, 10])) !== 0) {
  fail("s/print: raw bytes 0 and 255 were not written unchanged");
}

// isolation: a definition made in one run is gone in the next (fresh instance)
if (run("leak: 42  leak", "core") !== "42") fail("isolation probe setup");
if (run("leak", "core") !== "** halted (no SIN!: a machine-level fail-stop)") fail("a previous run's global leaked");

console.log("PRIMER_TEST PASS (" + examples.length + " primer examples through glon_run on " +
            path.basename(WASM) + "; s/print bytes intact; page sources match primer.txt; runs are isolated)");
process.exit(0);
