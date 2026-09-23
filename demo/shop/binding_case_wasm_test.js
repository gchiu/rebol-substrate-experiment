// demo/shop/binding_case_wasm_test.js -- minimal WASM regression for the
// c1a036c closure-origin binding law and the CASE vocabulary, run against the
// real G1A WASM module (demo/shop/glon.wasm) exactly as the browser loads it.
//
// The existing WASM suites (G1E/shop, traffic, Linda, web/demo) do not exercise
// CASE or the closure-origin binding law, so this probe loads the shipped
// bootstrap.glon + case.glon and then runs scenarios taken from the native
// r0_s1_case_tests.c.  Each scenario runs at the program's top level (as the
// native suite evaluates it) and `print`s its result; the probe compares the
// printed sequence.  A final sentinel is printed only if nothing fail-stopped.
//
// Usage: node binding_case_wasm_test.js [path/to/glon.wasm]
// (default: demo/shop/glon.wasm)
"use strict";

const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const WASM = process.argv[2] || path.join(HERE, "glon.wasm");
const BOOTSTRAP = fs.readFileSync(path.join(HERE, "bootstrap.glon"), "utf8");
const CASE_LIB = fs.readFileSync(path.join(HERE, "case.glon"), "utf8");

// Scenario programs from r0_s1_case_tests.c (binding Part 1, CASE Part 2), each
// ending with `print <result>` at top level.
const SCENARIOS = [
  [53,   "f: func [x] [ g: func [x] [ x ] + * 10 x g 3 ]  print f 5"],
  [7,    "k: 0  f: func [n] [ g: func [m] [ k: + k 1 either = k 1 [ f 0 ] [ + * 100 n m ] ] g + n 7 ]  print f 1"],
  [5,    "mk: func [x] [ does [ t: does [x] t ] ]  g: mk 5  w: func [a b] [ g ]  print w 66 77"],
  [5,    "f: func [x g] [ either = g none [ does [ t: does [x] t ] ] [ g ] ]  h: f 5 none  print f 9 h"],
  [2,    "f: func [x] [ y: 1  g: does [ y ]  y: 2  g ]  print f 0"],
  [9,    "f: func [x] [ g: does [ x: 9 ]  g  x ]  print f 0"],
  [105,  "th: func [b] [ func [] b ]  f: func [x] [ t: th [x]  + x 100 ]  print f 5"],
  [44,   "th: func [b q] [ t: func [] b  q ]  f: func [x] [ th [x] 44 ]  print f 5"],
  [7,    "sgn: func [x] [ case [ [> x 0] [x] [= x 0] [100] ] ]  print sgn 7"],
  [100,  "sgn: func [x] [ case [ [> x 0] [x] [= x 0] [100] ] ]  print sgn 0"],
  [2003, "n: 0  bump: func [v] [ n: + n 1  v ]  r: case [ [bump 0] [bump 10] [bump 1] [bump 20] [bump 1] [bump 30] ]  print + * 100 r n"],
  [20,   "cases: 5  i: 6  x: 9  print case [ [1] [ + x + cases i ] ]"],
  [34,   "f: func [cases i] [ case [ [1] [ + cases i ] ] ]  print f 30 4"],
  [53,   "f: func [x] [ g: func [x] [ case [ [1] [x] ] ] + * 10 x g 3 ]  print f 5"],
  [905,  "f: func [x] [ g: func [y] [ case [ [> y x] [y] [1] [x] ] ] + * 100 g 9 g 2 ]  print f 5"],
  [49,   "mk: func [x] [ does [ case [ [> x 0] [x] [1] [-1] ] ] ]  g: mk 5  h: mk -2  w: func [x] [ + * 10 g h ]  print w 99"],
  [7,    "f: func [x] [ y: 1 case [ [1] [ y: 7 ] ] y ]  print f 0"],
  [8,    "f: func [x] [ case [ [1] [ x: 8 ] ] x ]  print f 0"],
  [99,   "f: func [x d] [ either = d 0 [ f 99 1 ] [ case [ [1] [x] ] ] ]  print f 5 0"],
  [900,  "f: func [x] [ case [ [ case [ [> x 1] [1] ] ] [ case [ [> x 5] [x] [1] [0] ] ] [1] [-1] ] ]  print + * 100 f 9 f 3"],
  [610,  "fib: func [n] [ case [ [< n 2] [n] [1] [ + fib - n 1 fib - n 2 ] ] ]  print fib 15"],
];

let mem;
const lines = [];
const imports = {
  env: {
    host_print(ptr, len) {
      const s = new TextDecoder().decode(new Uint8Array(mem.buffer, ptr, len));
      for (const l of s.split("\n")) if (l.length) lines.push(l);
    },
    host_set_text() {},
    host_set_html() {},
    host_canvas_script() {}
  }
};

function fail(msg) { console.error("BINDING_CASE_WASM_TEST FAIL: " + msg); process.exit(1); }

WebAssembly.instantiate(fs.readFileSync(WASM), imports).then(({ instance }) => {
  const e = instance.exports;
  mem = new DataView(e.memory.buffer);
  const put = (s) => {
    const b = new TextEncoder().encode(s);
    const p = e.glon_alloc(b.length);
    new Uint8Array(e.memory.buffer).set(b, p);
    return [p, b.length];
  };
  const load = (name, src) => {
    const [p, n] = put(src);
    const rc = e.glon_load(p, n);
    if (rc !== 0) fail("glon_load('" + name + "') rc=" + rc);
  };

  if (e.glon_init() !== 0) fail("glon_init");
  load("bootstrap.glon", BOOTSTRAP);
  load("case.glon", CASE_LIB);

  // Mirror the native suite: each scenario is its own top-level `[ ... ]`
  // program (the bracketed form is what the loader and the native suite use).
  for (let i = 0; i < SCENARIOS.length; i++) {
    const [want, src] = SCENARIOS[i];
    const before = lines.length;
    const [p, n] = put("[" + src + "]");
    const rc = e.glon_load(p, n);
    if (rc !== 0) fail("scenario #" + i + " (want " + want + ") glon_load rc=" + rc +
                       " (fail-stop?); output so far: " + lines.join(" "));
    if (lines.length !== before + 1) {
      fail("scenario #" + i + " (want " + want + "): expected exactly one printed line, got " +
           (lines.length - before) + " (" + lines.slice(before).join(" ") + ")");
    }
    if (lines[before] !== String(want)) {
      fail("scenario #" + i + ": expected " + want + ", got " + lines[before]);
    }
  }

  console.log("BINDING_CASE_WASM_TEST PASS (" + SCENARIOS.length +
              " binding/closure + CASE checks on " + path.basename(WASM) + ")");
  process.exit(0);
}).catch((e) => fail(e.message || e));
