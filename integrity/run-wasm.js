// integrity/run-wasm.js -- run Glon source files, in order, in one standalone
// glon.wasm session (built from the unchanged standalone/glon.c by
// `make machine-integrity-wasm`), printing what Glon prints. Exit 1 if a load
// fails. Used to compare the WASM build's output with the native ./glon CLI.
//   node integrity/run-wasm.js integrity/build/glon.wasm FILE...
"use strict";
const fs = require("fs");
const [wasmPath, ...files] = process.argv.slice(2);
let mem;
const imports = { env: {
  host_print(ptr, len) { process.stdout.write(Buffer.from(new Uint8Array(mem.buffer, ptr, len))); },
  host_set_text() {}, host_set_html() {}, host_canvas_script() {}
} };
WebAssembly.instantiate(fs.readFileSync(wasmPath), imports).then(({ instance }) => {
  const e = instance.exports; mem = e.memory;
  if (e.glon_init() !== 0) { console.error("glon_init failed"); process.exit(1); }
  for (const f of files) {
    const bytes = fs.readFileSync(f);
    const p = e.glon_alloc(bytes.length);
    new Uint8Array(e.memory.buffer).set(bytes, p);
    const rc = e.glon_load(p, bytes.length);
    if (process.env.TRACE) console.error(`[${f}: ${rc}]`);
    if (rc !== 0) { console.error(`\n${f}: glon_load returned ${rc}`); process.exit(1); }
  }
});
