// experiments/glon-iter/iter_corpus.mjs -- GLON-ITER v001: the lexer differential corpus.
//
//   node iter_corpus.mjs OUTDIR
//
// Writes OUTDIR/v004-NN.glon and OUTDIR/iter-NN.glon. For every input, the
// v004 file walks the frozen Pratt v004 lexer (p/token) over the whole string;
// the iter file walks iter-lexer.glon's token iterator over THREE fragmentings
// of the same bytes. Each prints one line per token ("kind start end first"),
// then "end POS" or "error POS: MESSAGE". The v004 walk is printed three
// times, so the two outputs must be byte-identical (the Makefile diffs them).
//
// Inputs: every input of the v003 translation, error and Chris tables (read
// from ../glon-latex/web/infix_node_test.mjs, not copied), and seeded random
// byte soup that includes whitespace 9-13, ".", "_", byte 0, 127, 200 and 255.
// JavaScript only writes inputs; all lexing happens in Glon.
import fs from 'fs';
import { fileURLToPath } from 'url';

const here = p => fileURLToPath(new URL(p, import.meta.url));
const out = process.argv[2];
if (!out) { console.error('usage: node iter_corpus.mjs OUTDIR'); process.exit(2); }

const v003 = fs.readFileSync(here('../glon-latex/web/infix_node_test.mjs'), 'utf8');
const table = name => {
  const start = v003.indexOf('const ' + name + ' = [');
  const end = v003.indexOf('\n];', start);
  return new Function('return [' + v003.slice(v003.indexOf('[', start) + 1, end) + '];')();
};
const fixtures = [...table('T'), ...table('E'), ...table('CHRIS')].map(r => [...Buffer.from(typeof r === 'string' ? r : r[0], 'utf8')]);

let seed = 20261107;
const rnd = n => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return (seed >>> 8) % n; };
const ALPHA = [...Buffer.from('0123456789..__abcxyzAZ+-*/^()=,[]  ', 'latin1'), 9, 10, 11, 12, 13, 0, 127, 200, 255, 207, 128];
const random = [];
for (let k = 0; k < 300; k++) {
  const n = rnd(48), bytes = [];
  for (let j = 0; j < n; j++) bytes.push(ALPHA[rnd(ALPHA.length)]);
  random.push(bytes);
}
const inputs = [[], [32, 32], ...fixtures, ...random];

const lit = bytes => 'mk-string [ ' + bytes.join(' ') + ' ]';
// three fragmentings: one fragment; one byte per fragment with "" between;
// random cuts with empty fragments at both ends. Every STRING! costs at least
// 16 managed-heap cells (32 once the block and the "" are counted), so a
// 130-byte input cut into single bytes does not fit beside the libraries:
// inputs over 40 bytes are cut into 40 pieces instead of single bytes.
function fragmentings(bytes) {
  const per = ['""'], w = Math.max(1, Math.ceil(bytes.length / 40));
  for (let j = 0; j < bytes.length; j += w) per.push(lit(bytes.slice(j, j + w)), '""');
  const cuts = ['""'], cw = Math.max(5, Math.ceil(bytes.length / 10));
  for (let j = 0; j < bytes.length;) {
    const n = 1 + rnd(cw);
    cuts.push(lit(bytes.slice(j, j + n)));
    if (rnd(3) === 0) cuts.push('""');
    j += n;
  }
  cuts.push('""');
  return [[lit(bytes)], per, cuts].map(f => 'reduce [ ' + f.join(' ') + ' ]');
}

fs.mkdirSync(out, { recursive: true });
let chunk = [], size = 0, nchunks = 0;
function flush() {
  if (!chunk.length) return;
  const name = String(nchunks++).padStart(2, '0');
  let a = '', b = '';
  for (const bytes of chunk) {
    const steps = Math.ceil((bytes.length + 2) / 16);
    const walk = 'd/steps 16\n'.repeat(steps);
    for (const f of fragmentings(bytes)) {
      a += 'd/start ' + lit(bytes) + '\n' + walk;
      b += 'd/start ' + f + '\n' + walk;
    }
  }
  fs.writeFileSync(out + '/v004-' + name + '.glon', a);
  fs.writeFileSync(out + '/iter-' + name + '.glon', b);
  chunk = []; size = 0;
}
for (const bytes of inputs) {
  if (size + bytes.length > 120) flush();
  chunk.push(bytes); size += bytes.length + 8;
}
flush();
console.log('wrote ' + inputs.length + ' inputs (x3 fragmentings) in ' + nchunks + ' chunks to ' + out);
