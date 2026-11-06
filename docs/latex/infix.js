/* experiments/glon-latex/web/infix.js -- GLON-LATEX v003: infix input -> Glon prefix.
 *
 * A FRONT-END translator. It turns conventional infix mathematics, as a person
 * types it, into Glon prefix source over the existing l/ vocabulary:
 *
 *     (a + b) * c      ->   l/* l/+ "a" "b" "c"
 *
 * The generated text is ordinary Glon. It runs on the unchanged Glon runtime
 * and is typeset by the unchanged l/ words. This file evaluates nothing and
 * builds no LaTeX: notation stays here in the front end, and evaluation stays
 * in the substrate. Glon's calls take a fixed number of arguments, so prefix
 * output needs no brackets. Input parentheses steer the parse only, and the
 * l/ words add whatever brackets the typeset form needs.
 *
 * Precedence, loosest first (binding more tightly further down):
 *   ,            list (top level or inside brackets)   right    l/seq
 *   =  ≈ (~=)    relation                              none     l/=  l/approx
 *   +  -         additive                              left     l/+  l/-
 *   *  /  ÷      multiplicative (× · alias *)          left     l/*  l/frac  l/div
 *   -x           unary minus (− alias -)               prefix   l/neg
 *   x y, 4x      implicit multiplication               left     l/.
 *   ^            power (the exponent may be -x)        right    l/^
 * So -a*b is (-a)*b, -a^2 is -(a^2), -2x is -(2x), and 1/2x is 1/(2x).
 *
 * Operands: integers (Glon integers); decimals, which become string atoms
 * ("2.15"); names [A-Za-z][A-Za-z0-9]*, which become string atoms ("x", "dx";
 * a multi-letter name is ONE variable, so write `a b` for a product); and
 * the symbols pi sigma theta inf/infinity (π ∞). Functions are listed in FUNCS.
 * `[ … ]` gives explicit square brackets, and `_` omits a limit.
 *
 * Only known words, digits and validated names reach the output, so the
 * generated Glon is well formed by construction.
 *
 * API (browser global GlonInfix; also usable from Node):
 *   GlonInfix.translate(text) -> { glon: "<prefix expression>" }
 *                             or { error: "message", pos: <index into text> }
 *   GlonInfix.program(glon)   -> "s/print l/render <prefix expression>"
 */
(function (root) {
  "use strict";

  // name -> [Glon word, argument count, indexes of arguments that may be `_`]
  var FUNCS = {
    sqrt:      ["l/sqrt", 1, []],
    root:      ["l/root", 2, [0]],      // root(_, x) is sqrt(x)
    ln:        ["l/ln", 1, []],
    sum:       ["l/sum", 3, [0, 1]],    // sum(lower, upper, body)
    prod:      ["l/prod", 3, [0, 1]],
    int:       ["l/int", 3, [0, 1]],
    lim:       ["l/lim", 3, []],        // lim(var, target, body)
    lim_left:  ["l/lim", 3, []],
    lim_right: ["l/lim", 3, []],
    paren:     ["l/paren", 1, []],
    bracket:   ["l/bracket", 1, []],
    brace:     ["l/brace", 1, []],
    stack:     ["l/stack", 2, []]
  };
  var NAMES = { pi: "l/pi", sigma: "l/sigma", theta: "l/theta", inf: "l/infinity", infinity: "l/infinity" };
  var ALIAS = { "×": "*", "·": "*", "⋅": "*", "−": "-", "≈": "≈", "÷": "÷" };
  var SYMBOL_ALIAS = { "π": "pi", "∞": "inf" };
  var BINARY = { "+": "l/+", "-": "l/-", "*": "l/*", "/": "l/frac", "÷": "l/div", "^": "l/^",
                 "=": "l/=", "≈": "l/approx", ",": "l/seq", jux: "l/." };
  var MAX_VALUES = 480;   // a Glon block holds 512 values; leave room for the program wrapper

  function ParseError(message, pos) { this.message = message; this.pos = pos; }

  // ---- tokens: {k: kind, v: value, pos, call: name directly followed by "("} ----
  function tokenize(src) {
    var toks = [], i = 0, n = src.length;
    while (i < n) {
      var c = src[i];
      if (/\s/.test(c)) { i++; continue; }
      var start = i;
      if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(src[i + 1] || ""))) {
        while (i < n && /[0-9]/.test(src[i])) i++;
        var isInt = true;
        if (src[i] === "." && /[0-9]/.test(src[i + 1] || "")) {
          isInt = false; i++;
          while (i < n && /[0-9]/.test(src[i])) i++;
        }
        toks.push({ k: "num", v: src.slice(start, i), int: isInt, pos: start });
        continue;
      }
      if (/[A-Za-z]/.test(c)) {
        while (i < n && /[A-Za-z0-9_]/.test(src[i])) i++;
        toks.push({ k: "id", v: src.slice(start, i), pos: start, call: src[i] === "(" });
        continue;
      }
      if (SYMBOL_ALIAS[c]) { toks.push({ k: "id", v: SYMBOL_ALIAS[c], pos: start, call: false }); i++; continue; }
      if (c === "~" && src[i + 1] === "=") { toks.push({ k: "op", v: "≈", pos: start }); i += 2; continue; }
      if (c === "_") { toks.push({ k: "none", v: "_", pos: start }); i++; continue; }
      var op = ALIAS[c] || c;
      if ("+-*/^=(),[]".indexOf(op) >= 0 || op === "≈" || op === "÷") {
        toks.push({ k: "op", v: op, pos: start }); i++; continue;
      }
      throw new ParseError("unexpected character '" + c + "'", start);
    }
    toks.push({ k: "end", v: "", pos: n });
    return toks;
  }

  // ---- parser: recursive descent, one function per precedence level ----------
  function parse(src) {
    var toks = tokenize(src), p = 0;
    function peek() { return toks[p]; }
    function next() { return toks[p++]; }
    function isOp(v) { var t = toks[p]; return t.k === "op" && t.v === v; }
    function expect(v, what) {
      if (!isOp(v)) throw new ParseError("expected '" + v + "'" + (what ? " " + what : ""), peek().pos);
      return next();
    }
    function describe(t) { return t.k === "end" ? "end of input" : "'" + t.v + "'"; }

    function list() {                                     // a, b, c  (right-nested)
      var a = relation();
      if (isOp(",")) { next(); return { t: "bin", op: ",", a: a, b: list() }; }
      return a;
    }
    function relation() {                                 // non-associative
      var a = additive();
      if (isOp("=") || isOp("≈")) {
        var op = next().v, b = additive();
        if (isOp("=") || isOp("≈")) throw new ParseError("chained relations are ambiguous: add parentheses", peek().pos);
        return { t: "bin", op: op, a: a, b: b };
      }
      return a;
    }
    function additive() {
      var a = multiplicative();
      while (isOp("+") || isOp("-")) { var op = next().v; a = { t: "bin", op: op, a: a, b: multiplicative() }; }
      return a;
    }
    function multiplicative() {
      var a = unary();
      while (isOp("*") || isOp("/") || isOp("÷")) { var op = next().v; a = { t: "bin", op: op, a: a, b: unary() }; }
      return a;
    }
    function unary() {                                    // tighter than * /, looser than juxtaposition and ^
      if (isOp("-")) { next(); return { t: "neg", a: unary() }; }
      if (isOp("+")) throw new ParseError("unary + is not supported", peek().pos);
      return juxtaposition();
    }
    function startsOperand(t) {
      return t.k === "num" || t.k === "id" || (t.k === "op" && (t.v === "(" || t.v === "["));
    }
    function juxtaposition() {                            // implicit multiplication
      var first = peek(), a = power();
      var lastNum = first.k === "num" && a.t === "num";
      while (startsOperand(peek())) {
        var t = peek();
        if (lastNum && t.k === "num") throw new ParseError("two numbers in a row: write * between them", t.pos);
        var b = power();
        a = { t: "bin", op: "jux", a: a, b: b };
        lastNum = false;
      }
      return a;
    }
    function power() {                                    // right-associative
      var base = primary();
      if (isOp("^")) {
        next();
        var exp = isOp("-") ? (next(), { t: "neg", a: exponent() }) : power();
        return { t: "bin", op: "^", a: base, b: exp };
      }
      return base;
    }
    function exponent() { return isOp("-") ? (next(), { t: "neg", a: exponent() }) : power(); }

    function primary() {
      var t = peek();
      if (t.k === "num") { next(); return { t: "num", v: t.v, int: t.int, pos: t.pos }; }
      if (t.k === "none") throw new ParseError("'_' may only stand for an omitted limit", t.pos);
      if (t.k === "id") {
        next();
        if (FUNCS.hasOwnProperty(t.v)) return call(t);
        if (t.call) throw new ParseError("unknown function '" + t.v + "'", t.pos);
        if (NAMES.hasOwnProperty(t.v)) return { t: "sym", word: NAMES[t.v] };
        if (t.v.indexOf("_") >= 0) throw new ParseError("names may not contain '_'", t.pos);
        return { t: "var", name: t.v };
      }
      if (isOp("(")) {
        var open = next();
        if (isOp(")")) throw new ParseError("empty parentheses", peek().pos);
        var e = list();
        if (!isOp(")")) throw new ParseError("unbalanced '(' (opened here)", open.pos);
        next();
        return e;
      }
      if (isOp("[")) {
        var sq = next();
        if (isOp("]")) throw new ParseError("empty brackets", peek().pos);
        var inner = list();
        if (!isOp("]")) throw new ParseError("unbalanced '[' (opened here)", sq.pos);
        next();
        return { t: "bracket", a: inner };
      }
      throw new ParseError("expected a number, name or '(' but found " + describe(t), t.pos);
    }

    function call(t) {
      var f = FUNCS[t.v];
      if (!isOp("(")) throw new ParseError(t.v + " needs its arguments in parentheses", peek().pos);
      next();
      var args = [];
      if (!isOp(")")) {
        for (;;) {
          var at = peek();
          if (at.k === "none" && (toks[p + 1].v === "," || toks[p + 1].v === ")")) {
            if (f[2].indexOf(args.length) < 0) throw new ParseError("'_' is not allowed as argument " + (args.length + 1) + " of " + t.v, at.pos);
            next();
            args.push({ t: "none" });
          } else {
            args.push(relation());
          }
          if (isOp(",")) { next(); continue; }
          break;
        }
      }
      if (!isOp(")")) throw new ParseError("expected ',' or ')' in " + t.v + "(…)", peek().pos);
      next();
      if (args.length !== f[1])
        throw new ParseError(t.v + " takes " + f[1] + " argument" + (f[1] === 1 ? "" : "s") + ", not " + args.length, t.pos);
      return { t: "call", f: t.v, args: args };
    }

    var tree = list();
    if (peek().k !== "end") {
      var t = peek();
      throw new ParseError(t.v === ")" || t.v === "]" ? "unmatched '" + t.v + "'" : "unexpected " + describe(t), t.pos);
    }
    return tree;
  }

  // ---- emitter: pre-order walk into Glon prefix -----------------------------------
  function emit(node, out) {
    switch (node.t) {
      case "num":
        // integers stay Glon integers (up to 15 digits); decimals and longer numerals are atoms
        if (node.int && node.v.replace(/^0+(?=\d)/, "").length <= 15) out.push(node.v.replace(/^0+(?=\d)/, ""));
        else out.push('"' + node.v + '"');
        return;
      case "var": out.push('"' + node.name + '"'); return;
      case "sym": out.push(node.word); return;
      case "none": out.push("none"); return;
      case "neg": out.push("l/neg"); emit(node.a, out); return;
      case "bracket": out.push("l/bracket"); emit(node.a, out); return;
      case "bin": out.push(BINARY[node.op]); emit(node.a, out); emit(node.b, out); return;
      case "call": {
        var a = node.args, f = node.f;
        if (f === "root" && a[0].t === "none") { out.push("l/sqrt"); emit(a[1], out); return; }
        if (f === "sum" || f === "prod" || f === "int") {   // infix (lower, upper, body) -> l/ (upper, lower, body)
          out.push(FUNCS[f][0]); emit(a[1], out); emit(a[0], out); emit(a[2], out); return;
        }
        if (f === "lim_left" || f === "lim_right") {
          out.push("l/lim"); emit(a[0], out);
          out.push(f === "lim_left" ? "l/from-left" : "l/from-right"); emit(a[1], out);
          emit(a[2], out); return;
        }
        out.push(FUNCS[f][0]);
        for (var i = 0; i < a.length; i++) emit(a[i], out);
        return;
      }
    }
    throw new Error("internal: unknown node " + node.t);
  }

  function translate(text) {
    try {
      if (!/\S/.test(text)) return { error: "nothing to translate", pos: 0 };
      var out = [];
      emit(parse(text), out);
      // a Glon string literal costs two block values, any other value one
      var values = out.reduce(function (n, w) { return n + (w.charAt(0) === '"' ? 2 : 1); }, 0);
      if (values > MAX_VALUES) return { error: "expression too long for one Glon block (" + values + " values)", pos: 0 };
      return { glon: out.join(" ") };
    } catch (e) {
      if (e instanceof ParseError) return { error: e.message, pos: e.pos };
      throw e;
    }
  }

  function program(glon) { return "s/print l/render " + glon; }

  var api = { translate: translate, program: program };
  if (typeof module === "object" && module.exports) module.exports = api;
  root.GlonInfix = api;
})(typeof self !== "undefined" ? self : this);
