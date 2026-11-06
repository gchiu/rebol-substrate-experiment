// experiments/glon-latex/web/latex_node_test.mjs -- headless test of the /latex/ page.
//
//   node latex_node_test.mjs [GLON.WASM]
//
// Drives glon.wasm exactly as latex-host.js does (fresh instance per run;
// glon_init; glon_load prelude, strings, latex; glon_run) and checks:
//   1. the page loads nothing from another origin and the vendored KaTeX is complete;
//   2. the 17 figure examples on the page print exactly the 17 figures of
//      chris-samples.glon (which the v002 suite pins), in order;
//   3. the numbers-and-notation example prints 17 and its two formulas;
//   4. every printed math line, page-wide, parses in the vendored KaTeX with
//      throwOnError and no strict-mode warning;
//   5. errors reach the page as Glon reports them (SIN!, parse error).
// The default WASM is the shop build (../../../demo/shop/glon.wasm).
import fs from 'fs';
import { createRequire } from 'module';
import { fileURLToPath } from 'url';

const here = p => fileURLToPath(new URL(p, import.meta.url));
const wasmPath = process.argv[2] || here('../../../demo/shop/glon.wasm');
const LIBS = [here('../../../jupyter/prelude.glon'), here('../../../demo/shop/strings.glon'), here('../latex.glon')];
const katex = createRequire(import.meta.url)('./katex/katex.min.js');

let pass = 0, fail = 0;
const ok = (cond, name, detail) => {
  if (cond) pass++;
  else { fail++; console.log('FAIL ' + name + (detail ? '\n     ' + detail : '')); }
};

// ---- the runtime, as latex-host.js drives it ----------------------------------
const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
const libs = LIBS.map(f => fs.readFileSync(f, 'utf8'));
async function run(source) {
  let ex; const out = [];
  const view = () => new Uint8Array(ex.memory.buffer);
  const inst = await WebAssembly.instantiate(mod, { env: {
    host_print: (p, n) => { for (const b of view().subarray(p, p + n)) out.push(b); },
    host_set_text: (h, v) => out.push(...new TextEncoder().encode(String(h * 1e6 + v) + '\n')),
    host_set_html: () => {}, host_canvas_script: () => {} } });
  ex = inst.exports;
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
function katexCheck(line, name) {
  const m = /^\\\[([\s\S]*)\\\]$/.exec(line) || /^\$([\s\S]+)\$$/.exec(line);
  if (!m) return;
  const warns = [];
  try {
    katex.renderToString(m[1], { displayMode: line[0] === '\\', throwOnError: true,
      strict: (code, msg) => { warns.push(code + ': ' + msg); return 'ignore'; } });
    ok(warns.length === 0, name + ': KaTeX strict', warns.join('; '));
  } catch (e) { ok(false, name + ': KaTeX', e.message); }
}

// ---- 1. static: same-origin only, vendored KaTeX complete -----------------------
const html = fs.readFileSync(here('latex.html'), 'utf8');
const refs = [...html.matchAll(/<(?:script|link)\b[^>]*\b(?:src|href)="([^"]+)"/g)].map(m => m[1]);
ok(refs.length >= 4, 'page loads its scripts and styles', refs.join(' '));
for (const r of refs) {
  if (/^data:/.test(r)) continue;   // inline (the empty favicon): no request at all
  ok(!/^(https?:)?\/\//.test(r), 'no CDN: ' + r);
  ok(fs.existsSync(here(r)), 'resource exists: ' + r);
}
for (const f of ['katex/LICENSE', 'katex/katex.min.js', 'katex/katex.min.css', 'katex/VENDOR.md'])
  ok(fs.existsSync(here(f)), 'vendored: ' + f);
const css = fs.readFileSync(here('katex/katex.min.css'), 'utf8');
const woff2 = [...new Set([...css.matchAll(/url\((fonts\/[^)]+\.woff2)\)/g)].map(m => m[1]))];
ok(woff2.length === 20, 'katex.min.css names 20 woff2 fonts', String(woff2.length));
for (const f of woff2) ok(fs.existsSync(here('katex/' + f)), 'font: ' + f);
ok(katex.version === '0.16.22', 'KaTeX version 0.16.22', katex.version);

// ---- the page's examples ----------------------------------------------------------
const unescape = s => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');
const examples = [...html.matchAll(/<div class="ex([^"]*)"((?: [a-z-]+="[^"]*")*)>[\s\S]*?<textarea[^>]*>([\s\S]*?)<\/textarea>/g)]
  .map(m => ({ solo: /solo/.test(m[1]), fig: (/data-fig="([^"]+)"/.exec(m[2]) || [])[1], id: (/id="([^"]+)"/.exec(m[2]) || [])[1], src: unescape(m[3]) }));
const figs = examples.filter(e => e.fig);
const ORDER = ['1', '2a', '2b', '2c', '2d', '2e', '2f', '3', '4', '5a', '5b', '5c', '5d', '5e', '6a', '6b', '7'];
ok(figs.map(f => f.fig).join(' ') === ORDER.join(' '), '17 figure examples in sample order', figs.map(f => f.fig).join(' '));

// ---- 2. figures == chris-samples.glon, split on Chris's `|` ------------------------
const samples = await run(fs.readFileSync(here('../chris-samples.glon'), 'utf8'));
const renders = samples.printed.filter(l => l.startsWith('\\['));
ok(renders.length === 7, 'chris-samples.glon prints 7 renders', String(renders.length));
const expected = renders.flatMap(r => r.slice(2, -2).split(',\\ ')).map(f => '\\[' + f + '\\]');
ok(expected.length === 17, 'the 7 renders hold 17 figures', String(expected.length));
for (let i = 0; i < figs.length; i++) {
  const r = await run(figs[i].src);
  const name = 'figure ' + figs[i].fig;
  ok(r.result === 'none', name + ': runs cleanly', r.result);
  ok(r.printed.length === 1 && r.printed[0] === expected[i], name + ': prints the chris-samples figure',
    'got  ' + r.printed.join(' | ') + '\n     want ' + expected[i]);
  r.printed.forEach(l => katexCheck(l, name));
}

// ---- 3. numbers and notation ---------------------------------------------------------
const numeric = examples.find(e => e.solo && !e.id);
const nr = await run(numeric.src);
ok(JSON.stringify(nr.printed) === JSON.stringify(['17', '\\[\\left(2+3\\right) \\cdot 4-\\frac{9}{3}\\]', '\\[x^{3}\\]']),
  'numbers and notation prints 17 and its two formulas', JSON.stringify(nr.printed));
nr.printed.forEach(l => katexCheck(l, 'numbers and notation'));

// ---- 4. playground -----------------------------------------------------------------------
const pg = examples.find(e => e.id === 'playground');
const pr = await run(pg.src);
ok(pr.result === 'none' && pr.printed.length === 3, 'playground example runs and prints 3 formulas', JSON.stringify(pr));
ok(pr.printed[1] === '$\\left(a+b\\right) \\cdot c$' && pr.printed[2] === '$a+b \\cdot c$', 'playground grouping pair', pr.printed.slice(1).join(' | '));
pr.printed.forEach(l => katexCheck(l, 'playground'));

// ---- 5. errors as Glon reports them ----------------------------------------------------
const e1 = await run("s/print l/render l/+ 'x 1");
ok(/^\*\* uncaught #\[SIN! type l\/term x\]$/.test(e1.result), 'a word operand is reported as SIN! type l/term', e1.result);
// a missing argument mid-program consumes the next expression's value (none here)
const e2 = await run('x: l/+ 1 s/print "next"');
ok(/^\*\* uncaught #\[SIN! type l\/term none\]$/.test(e2.result), 'a missing argument mid-program is reported', e2.result);
// KNOWN runtime behaviour, recorded rather than fixed: an argument missing at the
// very END of the program reads as integer 0 (natively too: `print + 1` prints 1)
const e2b = await run('s/print l/render l/+ 1');
ok(e2b.printed[0] === '\\[1+0\\]', 'known: a missing final argument reads as 0', JSON.stringify(e2b));
const e3 = await run('s/print l/render [');
ok(e3.result.startsWith('**'), 'a parse error is reported', e3.result);
const e4 = await run('s/print l/render l/frac "\\frac{" 1');
ok(e4.result === 'none' && e4.printed.length === 1, 'raw LaTeX passes through unchanged', JSON.stringify(e4));
let katexRejects = false;
try { katex.renderToString(e4.printed[0].slice(2, -2), { displayMode: true, throwOnError: true }); } catch (e) { katexRejects = true; }
ok(katexRejects, 'KaTeX rejects broken raw LaTeX (the page shows its message)');

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
