/* Textual PARSE rule reader for the Glon playground.
 *
 * This layer only tokenises and lowers the supported subset into the existing
 * encoded Glon rule data. It performs no input matching, searching, repetition,
 * charset membership or captures. The Glon engine still executes the lowered
 * data in glon.wasm.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.GlonRules = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  function error(message, pos) {
    var e = new Error(message + (pos !== undefined ? " at offset " + pos : ""));
    e.name = "RuleSyntaxError";
    return e;
  }

  function tokenize(src) {
    var t = [], i = 0, n = src.length;
    while (i < n) {
      var c = src[i];
      if (c === " " || c === "\t" || c === "\r" || c === "\n") { i++; continue; }
      if (c === ";") { while (i < n && src[i] !== "\n") i++; continue; }
      if (c === "[" || c === "]" || c === "|" || c === ":") { t.push({k:c,v:c,pos:i}); i++; continue; }
      if (c === '"') {
        var start = i++, bytes = [];
        while (i < n && src[i] !== '"') {
          var ch = src[i++];
          if (ch === "\\") {
            if (i >= n) throw error("unfinished string escape", start);
            var e = src[i++];
            if (e === "n") bytes.push(10);
            else if (e === "r") bytes.push(13);
            else if (e === "t") bytes.push(9);
            else if (e === "0") bytes.push(0);
            else if (e === "\\") bytes.push(92);
            else if (e === '"') bytes.push(34);
            else if (e === "x") {
              var h = src.substr(i, 2);
              if (!/^[0-9a-fA-F]{2}$/.test(h)) throw error("bad \\x escape", i);
              bytes.push(parseInt(h, 16)); i += 2;
            } else throw error("unknown string escape \\" + e, i - 1);
          } else {
            var b = src.codePointAt(i - 1);
            if (b < 128) bytes.push(b);
            else {
              // Preserve arbitrary Unicode as UTF-8 bytes, matching the input
              // encoding used by the browser.
              var s = String.fromCodePoint(b), bb = new TextEncoder().encode(s);
              for (var j = 0; j < bb.length; j++) bytes.push(bb[j]);
            }
          }
        }
        if (i >= n) throw error("unterminated string", start);
        i++;
        t.push({k:"str", v:bytes, pos:start});
        continue;
      }
      if (/[0-9]/.test(c)) {
        var p = i; while (i < n && /[0-9]/.test(src[i])) i++;
        t.push({k:"num", v:Number(src.slice(p, i)), pos:p});
        continue;
      }
      if (/[A-Za-z_]/.test(c)) {
        var w = i; while (i < n && /[A-Za-z0-9_-]/.test(src[i])) i++;
        t.push({k:"word", v:src.slice(w, i), pos:w});
        continue;
      }
      throw error("unexpected character '" + c + "'", i);
    }
    return t;
  }

  function Parser(tokens) { this.t = tokens; this.i = 0; this.n = tokens.length; }
  Parser.prototype.peek = function () { return this.t[this.i] || null; };
  Parser.prototype.take = function () { return this.t[this.i++] || null; };
  Parser.prototype.expect = function (k, what) {
    var x = this.take();
    if (!x || x.k !== k) throw error("expected " + (what || k), x ? x.pos : undefined);
    return x;
  };
  Parser.prototype.word = function () { return this.expect("word", "a word").v; };

  Parser.prototype.block = function () {
    this.expect("[", "'['");
    var branches = [[]];
    while (true) {
      var x = this.peek();
      if (!x) throw error("unterminated rule block");
      if (x.k === "]") { this.take(); break; }
      if (x.k === "|") { this.take(); branches.push([]); continue; }
      branches[branches.length - 1].push(this.element());
    }
    if (branches.length === 1) return {k:"seq", v:branches[0]};
    return {k:"alt", v:branches};
  };

  Parser.prototype.element = function () {
    var x = this.peek();
    if (!x) throw error("expected a rule");
    if (x.k === "str") { this.take(); return {k:"str", v:x.v}; }
    if (x.k === "[") return this.block();
    if (x.k === "num") {
      var min = this.take().v, max = min;
      if (this.peek() && this.peek().k === "num") max = this.take().v;
      if (max < min) throw error("repetition maximum is less than minimum");
      return {k:"repeat", min:min, max:max, rule:this.element()};
    }
    if (x.k !== "word") throw error("expected a rule", x.pos);
    var w = this.take().v;
    if (w === "some" || w === "any" || w === "opt") {
      return {k:"op", op:w, rule:this.element()};
    }
    if (w === "skip") return {k:"skip"};
    if (w === "end") return {k:"end"};
    if (w === "none") return {k:"none"};
    if (w === "to" || w === "thru") {
      var target = this.element();
      if (target.k !== "str") throw error(w + " requires a quoted string target");
      return {k:w, target:target.v};
    }
    if (w === "charset") {
      var set = this.element();
      if (set.k !== "str") throw error("charset requires a quoted string");
      return {k:"charset", v:set.v};
    }
    if (w === "copy") {
      var name = this.word();
      return {k:"copy", name:name, rule:this.element()};
    }
    if (w === "set" || w === "ahead" || w === "not") {
      throw error("unsupported operator: " + w, x.pos);
    }
    return {k:"name", v:w};
  };

  Parser.prototype.rule = function () {
    var x = this.peek();
    if (!x) throw error("empty rule");
    if (x.k === "[") return this.block();
    if (x.k === "str") { this.take(); return {k:"str", v:x.v}; }
    if (x.k === "word") { this.take(); return {k:"name", v:x.v}; }
    throw error("top rule must be a block, string or named rule", x.pos);
  };

  function stringExpr(bytes) { return "mk-string [ " + bytes.join(" ") + " ]"; }
  function charsetPairs(bytes) {
    var out = [];
    for (var i = 0; i < bytes.length;) {
      if (i + 2 < bytes.length && bytes[i + 1] === 45) {
        if (bytes[i] > bytes[i + 2]) throw error("charset range has descending endpoints");
        out.push(bytes[i], bytes[i + 2]); i += 3;
      } else { out.push(bytes[i], bytes[i]); i++; }
    }
    return out;
  }
  function fail(node) { throw error("unsupported rule form"); }

  function elementExpr(node) {
    switch (node.k) {
      case "str": return stringExpr(node.v);
      case "name": return "'" + node.v;
      case "seq": return "reduce [ " + node.v.map(elementExpr).join(" ") + " ]";
      case "alt": {
        var parts = [];
        node.v.forEach(function (branch, bi) {
          if (bi) parts.push("0");
          branch.forEach(function (e) { parts.push(elementExpr(e)); });
        });
        return "reduce [ " + parts.join(" ") + " ]";
      }
      case "repeat": {
        var code = node.min === node.max ? 20 : 21;
        var nums = node.min === node.max ? String(node.min) : node.min + " " + node.max;
        return "reduce [ " + code + " " + nums + " " + elementExpr(node.rule) + " ]";
      }
      case "op": return (node.op === "some" ? "1" : node.op === "any" ? "2" : "3") + " " + elementExpr(node.rule);
      case "skip": return "reduce [ 4 ]";
      case "end": return "reduce [ 7 ]";
      case "none": return "'none";
      case "to": return "reduce [ 5 " + stringExpr(node.target) + " ]";
      case "thru": return "reduce [ 6 " + stringExpr(node.target) + " ]";
      case "charset": return "reduce [ 8 " + stringExpr(charsetPairs(node.v)) + " ]";
      case "copy": return "reduce [ 9 " + stringExpr(Array.from(new TextEncoder().encode(node.name))) + " " + elementExpr(node.rule) + " ]";
      default: return fail(node);
    }
  }

  function topExpr(node) {
    if (node.k === "seq" || node.k === "alt" || node.k === "str" || node.k === "name") return elementExpr(node);
    return "reduce [ " + elementExpr(node) + " ]";
  }

  function parseDefinitions(text) {
    if (!text || !text.trim()) return [];
    var p = new Parser(tokenize(text)), defs = [];
    while (p.peek()) {
      var name = p.word();
      p.expect(":", "':' after rule name");
      if (!p.peek() || p.peek().k !== "[") throw error("rule " + name + " must have a [...] block");
      defs.push({name:name, rule:p.block()});
    }
    return defs;
  }
  function parseTop(text) {
    if (!text || !text.trim()) throw error("top rule is empty");
    var p = new Parser(tokenize(text)), r = p.rule();
    if (p.peek()) throw error("unexpected text after top rule", p.peek().pos);
    return r;
  }
  function lower(definitionsText, topText) {
    var defs = parseDefinitions(definitionsText);
    var top = parseTop(topText);
    var parts = [];
    defs.forEach(function (d) { parts.push("'" + d.name, topExpr(d.rule)); });
    return {
      definitions: defs.map(function (d) { return d.name; }),
      registry: parts.length ? "reduce [ " + parts.join(" ") + " ]" : "[]",
      rule: topExpr(top)
    };
  }
  return { tokenize:tokenize, lower:lower, error:error };
});
