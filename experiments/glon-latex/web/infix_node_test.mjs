// experiments/glon-latex/web/infix_node_test.mjs -- GLON-LATEX v003 infix front end.
//
//   node infix_node_test.mjs [GLON.WASM]
//
// 1. translation: infix text -> the exact Glon prefix expected;
// 2. errors: rejected input, with a message and a position;
// 3. end to end: infix -> Glon -> glon.wasm (unchanged runtime, l/ vocabulary)
//    -> LaTeX, checked exactly and parsed by the vendored KaTeX;
// 4. Chris Ross-Gill's seven renders written as infix: RUN, and compared byte
//    for byte with what chris-samples.glon actually prints in the same runtime.
import fs from 'fs';
import { createRequire } from 'module';
import { fileURLToPath } from 'url';

const here = p => fileURLToPath(new URL(p, import.meta.url));
const require = createRequire(import.meta.url);
const { translate, program } = require('./infix.js');
const katex = require('./katex/katex.min.js');
const wasmPath = process.argv[2] || here('../../../demo/shop/glon.wasm');
const LIBS = [here('../../../jupyter/prelude.glon'), here('../../../demo/shop/strings.glon'), here('../latex.glon')];

let pass = 0, fail = 0;
const ok = (c, name, d) => { if (c) pass++; else { fail++; console.log('FAIL ' + name + (d !== undefined ? '\n     ' + d : '')); } };

// ---- the runtime, as latex-host.js drives it ----------------------------------------
const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
const libs = LIBS.map(f => fs.readFileSync(f, 'utf8'));
async function run(source) {
  let ex; const out = [];
  const view = () => new Uint8Array(ex.memory.buffer);
  ex = (await WebAssembly.instantiate(mod, { env: {
    host_print: (p, n) => { for (const b of view().subarray(p, p + n)) out.push(b); },
    host_set_text: (h, v) => out.push(...new TextEncoder().encode(String(h * 1e6 + v) + '\n')),
    host_set_html: () => {}, host_canvas_script: () => {} } })).exports;
  const put = s => { const b = new TextEncoder().encode(s); const p = ex.glon_alloc(b.length); view().set(b, p); return [p, b.length]; };
  if (ex.glon_init() !== 0) throw new Error('glon_init failed');
  libs.forEach((src, i) => { if (ex.glon_load(...put(src)) !== 0) throw new Error(LIBS[i] + ' failed to load'); });
  const before = out.length;
  if (ex.glon_run(...put(source)) !== 0) return { printed: [], result: 'program too long' };
  const result = new TextDecoder().decode(view().subarray(ex.glon_result_ptr(), ex.glon_result_ptr() + ex.glon_result_len()));
  const lines = new TextDecoder().decode(new Uint8Array(out.slice(before))).split('\n');
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  return { printed: lines, result };
}
async function latexOf(infix) {
  const t = translate(infix);
  if (t.error) return { error: t.error };
  const r = await run(program(t.glon));
  return { glon: t.glon, result: r.result, line: r.printed.length === 1 ? r.printed[0] : JSON.stringify(r.printed) };
}
function katexOk(line) {
  const m = /^\\\[([\s\S]*)\\\]$/.exec(line);
  if (!m) return false;
  try { katex.renderToString(m[1], { displayMode: true, throwOnError: true, strict: 'error' }); return true; } catch { return false; }
}

// ---- 1. translation --------------------------------------------------------------
const T = [
  // the brief's examples
  ['(a + b) * c', 'l/* l/+ "a" "b" "c"'],
  ['a + b * c', 'l/+ "a" l/* "b" "c"'],
  // associativity
  ['a - b - c', 'l/- l/- "a" "b" "c"'],
  ['a - (b - c)', 'l/- "a" l/- "b" "c"'],
  ['a * b * c', 'l/* l/* "a" "b" "c"'],
  ['a / b / c', 'l/frac l/frac "a" "b" "c"'],
  ['a / (b / c)', 'l/frac "a" l/frac "b" "c"'],
  ['a ^ b ^ c', 'l/^ "a" l/^ "b" "c"'],
  ['(a ^ b) ^ c', 'l/^ l/^ "a" "b" "c"'],
  ['a / b * c', 'l/* l/frac "a" "b" "c"'],
  ['a ÷ b ÷ c', 'l/div l/div "a" "b" "c"'],
  // unary minus: tighter than * /, looser than juxtaposition and ^
  ['-a*b', 'l/* l/neg "a" "b"'],
  ['-(a*b)', 'l/neg l/* "a" "b"'],
  ['-a^2', 'l/neg l/^ "a" 2'],
  ['(-a)^2', 'l/^ l/neg "a" 2'],
  ['-2x', 'l/neg l/. 2 "x"'],
  ['--a', 'l/neg l/neg "a"'],
  ['a - -b', 'l/- "a" l/neg "b"'],
  ['a * -b', 'l/* "a" l/neg "b"'],
  ['x^-1', 'l/^ "x" l/neg 1'],
  ['2^-x^2', 'l/^ 2 l/neg l/^ "x" 2'],
  ['-a/b', 'l/frac l/neg "a" "b"'],
  // implicit multiplication: tighter than / and unary minus, looser than ^
  ['4x', 'l/. 4 "x"'],
  ['4x^2', 'l/. 4 l/^ "x" 2'],
  ['a b c', 'l/. l/. "a" "b" "c"'],
  ['1/2x', 'l/frac 1 l/. 2 "x"'],
  ['(1/2)x', 'l/. l/frac 1 2 "x"'],
  ['1/(2x)', 'l/frac 1 l/. 2 "x"'],
  ['2 sqrt(x)', 'l/. 2 l/sqrt "x"'],
  ['x (y + 1)', 'l/. "x" l/+ "y" 1'],
  ['(a + b)(a - b)', 'l/. l/+ "a" "b" l/- "a" "b"'],
  ['pi r^2', 'l/. l/pi l/^ "r" 2'],
  // operands
  ['2.15', '"2.15"'],
  ['.5', '".5"'],
  ['007', '7'],
  ['1234567890123456', '"1234567890123456"'],
  ['dx', '"dx"'],
  ['x2', '"x2"'],
  ['π + ∞', 'l/+ l/pi l/infinity'],
  ['pi + inf + infinity + sigma + theta', 'l/+ l/+ l/+ l/+ l/pi l/infinity l/infinity l/sigma l/theta'],
  // aliases
  ['a × b · c ⋅ d', 'l/* l/* l/* "a" "b" "c" "d"'],
  ['a − b', 'l/- "a" "b"'],
  ['−a', 'l/neg "a"'],
  ['a ≈ b', 'l/approx "a" "b"'],
  ['a ~= b', 'l/approx "a" "b"'],
  // relations and lists
  ['a + b = c', 'l/= l/+ "a" "b" "c"'],
  ['a, b, c', 'l/seq "a" l/seq "b" "c"'],
  ['a = 1, b = 2', 'l/seq l/= "a" 1 l/= "b" 2'],
  ['(a = b) = c', 'l/= l/= "a" "b" "c"'],
  // functions
  ['sqrt(x + 1)', 'l/sqrt l/+ "x" 1'],
  ['root(3, x)', 'l/root 3 "x"'],
  ['root(_, x)', 'l/sqrt "x"'],
  ['ln(x)', 'l/ln "x"'],
  ['ln(ln(x))', 'l/ln l/ln "x"'],
  ['sum(i = 1, n, i^2)', 'l/sum "n" l/= "i" 1 l/^ "i" 2'],
  ['sum(_, _, i)', 'l/sum none none "i"'],
  ['sum(i = 1, _, i)', 'l/sum none l/= "i" 1 "i"'],
  ['sum(_, n, i)', 'l/sum "n" none "i"'],
  ['prod(k = 1, n, k)', 'l/prod "n" l/= "k" 1 "k"'],
  ['int(0, 1, x^2 dx)', 'l/int 1 0 l/. l/^ "x" 2 "dx"'],
  ['lim(x, 0, sin)', 'l/lim "x" 0 "sin"'],
  ['lim_left(x, inf, 1/x)', 'l/lim "x" l/from-left l/infinity l/frac 1 "x"'],
  ['lim_right(x, 0, 1/x)', 'l/lim "x" l/from-right 0 l/frac 1 "x"'],
  ['paren(x)', 'l/paren "x"'],
  ['brace(x)', 'l/brace "x"'],
  ['bracket(a + b)', 'l/bracket l/+ "a" "b"'],
  ['[a + b]^2', 'l/^ l/bracket l/+ "a" "b" 2'],
  ['stack(pi, pi)', 'l/stack l/pi l/pi'],
  ['sqrt (x)', 'l/sqrt "x"'],
  // whitespace is insignificant
  ['  ( a+b )*c  ', 'l/* l/+ "a" "b" "c"'],
];
for (const [src, want] of T) {
  const t = translate(src);
  ok(t.glon === want, 'translate ' + src, 'got  ' + (t.glon || 'error: ' + t.error) + '\n     want ' + want);
}

// ---- 2. errors ----------------------------------------------------------------------
const E = [
  ['', 'nothing to translate', 0],
  ['a +', 'expected a number', 3],
  ['(a + b', "unbalanced '('", 0],
  ['a + b)', "unmatched ')'", 5],
  ['[a', "unbalanced '['", 0],
  ['()', 'empty parentheses', 1],
  ['a = b = c', 'chained relations', 6],
  ['sin(x)', "unknown function 'sin'", 0],
  ['x(y + 1)', "unknown function 'x'", 0],
  ['sqrt x', 'needs its arguments in parentheses', 5],
  ['sqrt(x, y)', 'sqrt takes 1 argument, not 2', 0],
  ['sum(i = 1, n)', 'sum takes 3 arguments, not 2', 0],
  ['sum(i = 1, n, _)', "'_' is not allowed as argument 3", 14],
  ['ln(_)', "'_' is not allowed as argument 1", 3],
  ['_ + 1', "'_' may only stand for an omitted limit", 0],
  ['2 3', 'two numbers in a row', 2],
  ['+a', 'unary + is not supported', 0],
  ['a $ b', "unexpected character '$'", 2],
  ['"x"', "unexpected character '\"'", 0],
  ['a_b', "names may not contain '_'", 0],
  ['a ^', 'expected a number', 3],
];
for (const [src, msg, pos] of E) {
  const t = translate(src);
  ok(t.error && t.error.indexOf(msg) >= 0 && t.pos === pos, 'error for ' + JSON.stringify(src), JSON.stringify(t) + ' want ' + msg + ' @' + pos);
}
const long = Array.from({ length: 300 }, (_, i) => 'x' + i).join(' + ');
ok(/too long/.test(translate(long).error || ''), 'very long input is rejected before Glon sees it');

// ---- 3. end to end: infix -> Glon -> WASM -> LaTeX ---------------------------------
const L = [
  ['(a + b) * c', '\\[\\left(a+b\\right) \\cdot c\\]'],
  ['a + b * c', '\\[a+b \\cdot c\\]'],
  ['a - (b - c)', '\\[a-\\left(b-c\\right)\\]'],
  ['a - b - c', '\\[a-b-c\\]'],
  ['a * (b * c)', '\\[a \\cdot \\left(b \\cdot c\\right)\\]'],
  ['(1 + x)^2', '\\[\\left(1+x\\right)^{2}\\]'],
  ['e^(1 + x)', '\\[e^{1+x}\\]'],
  ['(a^b)^c', '\\[\\left(a^{b}\\right)^{c}\\]'],
  ['a^b^c', '\\[a^{b^{c}}\\]'],
  ['-a*b', '\\[\\left(-a\\right) \\cdot b\\]'],
  ['-(a*b)', '\\[-a \\cdot b\\]'],
  ['-a^2', '\\[-a^{2}\\]'],
  ['(-a)^2', '\\[\\left(-a\\right)^{2}\\]'],
  ['(-3)^2', '\\[\\left(-3\\right)^{2}\\]'],
  ['x^-1', '\\[x^{-1}\\]'],
  ['1/(1 - x)', '\\[\\frac{1}{1-x}\\]'],
  ['(a + b)/(c - d)', '\\[\\frac{a+b}{c-d}\\]'],
  ['1/2x', '\\[\\frac{1}{2 x}\\]'],
  ['(1/2)x', '\\[\\frac{1}{2} x\\]'],
  ['1/(2x)', '\\[\\frac{1}{2 x}\\]'],
  ['-A/A^2', '\\[\\frac{-A}{A^{2}}\\]'],
  ['-(A/A^2)', '\\[-\\frac{A}{A^{2}}\\]'],
  ['x = (-b + sqrt(b^2 - 4 a c)) / (2a)', '\\[x=\\frac{-b+\\sqrt{b^{2}-4 a c}}{2 a}\\]'],
  ['sum(i = 1, n, i) = n(n + 1)/2', null],   // n( is a call: error, checked below
  ['sum(i = 1, n, i) = n (n + 1)/2', '\\[\\sum_{i=1}^{n} i=\\frac{n \\left(n+1\\right)}{2}\\]'],
  ['int(0, 1, x^2 dx)', '\\[\\int_{0}^{1} x^{2} dx\\]'],
  ['lim(x, 0, ln(1 + x)/x)', '\\[\\lim_{x\\to 0} \\frac{\\ln \\left(1+x\\right)}{x}\\]'],
  ['2 + 3 = 5', '\\[2+3=5\\]'],
];
for (const [src, want] of L) {
  const r = await latexOf(src);
  if (want === null) { ok(r.error && /unknown function 'n'/.test(r.error), 'end to end: ' + src + ' is rejected', JSON.stringify(r)); continue; }
  ok(r.result === 'none' && r.line === want, 'end to end: ' + src, 'got  ' + (r.line || r.error) + '\n     want ' + want + '\n     glon ' + r.glon);
  ok(katexOk(r.line || ''), 'KaTeX parses: ' + src, r.line);
}

// ---- 4. Chris Ross-Gill's math.test.reb, as infix: run and compare byte for byte ------
const CHRIS = [
  'sum(t = 1, inf, x^t) = 1/(1 - x)',
  'pi ≈ 3.141592653589793, [sum(_, _, 1 + 2)], sum(i = 1, n, 3 + 4), sum(_, n, 3 + 4), sum(i = 1, _, 3 + 4), int(_, _, 3 + 4x) = 3x + 4x^2 + C',
  '(1/3 * 2 ÷ 3 * 5x) / (10 * (20 + a)^(5x)) + 100',
  'x + -ln(3*(4/x) + 6^(7 + 8))',
  'paren(stack(pi, pi)), a = pi r^2, root(3, x + 2.15), root(_, x + 5), dx/dy + dx/dt = 100',
  '-(A/A^2) = -(1/A), lim_left(x, inf, x + 1)',
  '(d/dx) sqrt(x) = 1/(2 sqrt(x))',
];
const samples = (await run(fs.readFileSync(here('../chris-samples.glon'), 'utf8'))).printed.filter(l => l.startsWith('\\['));
ok(samples.length === 7, 'chris-samples.glon prints its 7 renders', String(samples.length));
const report = [];
for (let i = 0; i < CHRIS.length; i++) {
  const r = await latexOf(CHRIS[i]);
  const same = r.line === samples[i];
  ok(same, 'Chris render ' + (i + 1) + ' from infix is byte-identical', 'infix ' + CHRIS[i] + '\n     got   ' + (r.line || r.error) + '\n     want  ' + samples[i]);
  ok(katexOk(r.line || ''), 'KaTeX parses Chris render ' + (i + 1) + ' from infix');
  report.push({ render: i + 1, identical: same, bytes: (r.line || '').length });
}
// the natural spelling of figure 6a differs, by the unary-minus rule (documented)
const natural6a = await latexOf('-A/A^2 = -1/A');
ok(natural6a.line === '\\[\\frac{-A}{A^{2}}=\\frac{-1}{A}\\]', 'figure 6a spelled -A/A^2 = -1/A is (-A)/A^2, by design', natural6a.line);

console.log('Chris renders from infix: ' + report.map(r => r.render + (r.identical ? ' identical' : ' DIFFERENT')).join(', '));
console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
