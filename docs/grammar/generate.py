#!/usr/bin/env python3
"""Generate deterministic Glon railroad diagrams, manifest and HTML reference.

Canonical source: docs/grammar/glon-grammar.ebnf
Outputs:
  docs/grammar/railroad/<production>.svg
  docs/grammar/railroad/manifest.json
  docs/grammar/index.html

Python 3 standard library only. No network access, no timestamps, no random IDs.
"""
from __future__ import annotations

import hashlib
import html
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape as xml_escape

ROOT = Path(__file__).resolve().parent
GRAMMAR_PATH = ROOT / "glon-grammar.ebnf"
RAILROAD_DIR = ROOT / "railroad"
INDEX_PATH = ROOT / "index.html"
MANIFEST_PATH = RAILROAD_DIR / "manifest.json"
GENERATOR_VERSION = 1

# Production classification used by the HTML navigation.
LEXICAL = {
    "ws", "digit", "any-byte", "non-quote-byte", "token-byte", "token",
    "word", "word-value", "string", "integer", "masm-tokens",
}
STRUCTURAL = {
    "source", "trailing-tail", "top-item", "block", "block-item", "form",
    "func-form", "masm-form", "masm-block",
}
ALL_PRODUCTIONS = LEXICAL | STRUCTURAL

PRODUCTION_NOTES = {
    "source": "If the first non-space byte is an open bracket, that one block is parsed and all trailing source bytes are ignored. An unwrapped source is a sequence of top-items.",
    "trailing-tail": "Not a Glon value; it is the parser's ignored remainder after a leading block. `[1] 2` parses as `[1]`.",
    "top-item": "The unwrapped top-level loop recognises strings, masm/raw, and basic forms. It does not special-case func.",
    "block": "A block may be empty. The reader stops at the matching close bracket. A string literal contributes two forms.",
    "block-item": "Inside a block, func is special-cased before ordinary form parsing; masm/raw and strings are handled at item start.",
    "form": "Parse_form accepts only a block, an integer, or a word token. It does not recognise a string literal, func, or masm.",
    "func-form": "Block-only. The spec is parsed with parse_form; a directly following block is the literal body. A computed body is left in the stream. At run time the body must evaluate to a block.",
    "masm-form": "The keyword must be the whole maximal token. The optional arity is an unsigned decimal integer. The masm block is a private S1 sublanguage.",
    "masm-block": "Opaque to Glon item parsing. Limits: 511 tokens, 64 labels, 128 label references, and the code-space bound.",
    "string": "Recognised at item start only. No escape sequences. At most 512 bytes. Expands to mk-string plus a block of byte integers.",
    "integer": "Optional minus sign followed by decimal digits. No plus sign, decimal point, exponent, or overflow check.",
    "word": "A word is a maximal token; see word-value for the source-form interpretation.",
    "word-value": "After reading and truncating a token, apply the first matching rule: exact none; trailing colon (SET-WORD); leading colon (GET-WORD); leading apostrophe (LIT-WORD); otherwise WORD.",
    "token": "Maximal run excluding whitespace and brackets. The reader keeps the first 63 bytes; longer tokens can therefore collide.",
    "token-byte": "Any byte except whitespace, open bracket, or close bracket.",
    "non-quote-byte": "Any byte except a double quote. A backslash is an ordinary byte; there are no string escapes.",
    "any-byte": "Any byte 0..255.",
    "masm-tokens": "Private S1 macro-assembly tokens. They are not interned and do not participate in Glon binding.",
    "digit": "Decimal digit.",
    "ws": "C isspace set: space, tab, LF, CR, vertical tab, form feed.",
}

DOC_LINKS = [
    ("Language reference hub", "../language/"),
    ("Language specification", "../language/spec.md"),
    ("Alpha language laws (normative)", "../language/laws.md"),
    ("Evaluation model", "../language/evaluation.md"),
    ("Syntax diagrams (verified prose)", "../language/syntax-diagrams.md"),
    ("Specification verification", "../language/verification.md"),
    ("Host ABI", "../language/host-abi.md"),
    ("Original WASM ABI", "../language/wasm-abi.md"),
    ("Implementer reading guide", "../language/implementer-guide.md"),
    ("AI authoring guide", "../language/ai-authoring.md"),
]


# ----------------------------- AST -----------------------------------------

@dataclass(frozen=True)
class Terminal:
    label: str
    kind: str = "literal"  # literal | special


@dataclass(frozen=True)
class Nonterminal:
    name: str


@dataclass(frozen=True)
class Seq:
    items: tuple


@dataclass(frozen=True)
class Alt:
    items: tuple


@dataclass(frozen=True)
class Opt:
    item: object


@dataclass(frozen=True)
class Star:
    item: object


@dataclass(frozen=True)
class Plus:
    item: object


@dataclass(frozen=True)
class Empty:
    pass


@dataclass(frozen=True)
class Production:
    name: str
    ast: object
    rhs: str


# ----------------------------- parser --------------------------------------

class GrammarError(ValueError):
    pass


def _strip_comments(text: str) -> str:
    # EBNF comments in this project are simple, non-nested (* ... *).
    return re.sub(r"\(\*.*?\*\)", " ", text, flags=re.S)


class Parser:
    def __init__(self, text: str):
        self.s = _strip_comments(text)
        self.i = 0

    def error(self, message: str):
        raise GrammarError(f"{message} at offset {self.i}")

    def skip(self):
        while self.i < len(self.s) and self.s[self.i].isspace():
            self.i += 1

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def expect(self, ch: str):
        self.skip()
        if self.peek() != ch:
            self.error(f"expected {ch!r}")
        self.i += 1

    def parse_ident(self) -> str:
        self.skip()
        start = self.i
        if not (self.peek().isalpha() or self.peek() == "_"):
            self.error("expected production name")
        self.i += 1
        while self.i < len(self.s) and (self.s[self.i].isalnum() or self.s[self.i] in "_-"):
            self.i += 1
        return self.s[start:self.i]

    def parse(self) -> list[Production]:
        productions = []
        while True:
            self.skip()
            if self.i >= len(self.s):
                break
            name = self.parse_ident()
            self.expect("=")
            start = self.i
            ast = self.parse_expr({";"})
            end = self.i
            self.skip()
            self.expect(";")
            rhs = re.sub(r"\s+", " ", self.s[start:end]).strip()
            productions.append(Production(name, ast, rhs))
        return productions

    def parse_expr(self, stops: set[str]):
        alts = [self.parse_seq(stops | {"|"})]
        while True:
            self.skip()
            if self.peek() != "|":
                break
            self.i += 1
            alts.append(self.parse_seq(stops | {"|"}))
        return alts[0] if len(alts) == 1 else Alt(tuple(alts))

    def parse_seq(self, stops: set[str]):
        items = []
        while True:
            self.skip()
            c = self.peek()
            if not c or c in stops:
                break
            items.append(self.parse_factor())
        if not items:
            return Empty()
        return items[0] if len(items) == 1 else Seq(tuple(items))

    def parse_factor(self):
        atom = self.parse_atom()
        while True:
            self.skip()
            c = self.peek()
            if c == "?":
                self.i += 1
                atom = Opt(atom)
            elif c == "*":
                self.i += 1
                atom = Star(atom)
            elif c == "+":
                self.i += 1
                atom = Plus(atom)
            else:
                break
        return atom

    def parse_atom(self):
        self.skip()
        c = self.peek()
        if c == "?":
            self.i += 1
            end = self.s.find("?", self.i)
            if end < 0:
                self.error("unterminated ? special terminal")
            text = self.s[self.i:end].strip()
            self.i = end + 1
            if not text:
                self.error("empty ? special terminal")
            return Terminal(text, "special")
        if c in ("'", '"'):
            return self.parse_quoted()
        if c == "(":
            self.i += 1
            expr = self.parse_expr({")"})
            self.expect(")")
            return expr
        if c == "[":
            self.i += 1
            expr = self.parse_expr({"]"})
            self.expect("]")
            return Opt(expr)
        name = self.parse_ident()
        return Nonterminal(name)

    def parse_quoted(self):
        quote = self.peek()
        self.i += 1
        out = []
        while self.i < len(self.s):
            c = self.s[self.i]
            if c == quote:
                self.i += 1
                return Terminal("".join(out), "literal")
            if c == "\\":
                self.i += 1
                if self.i >= len(self.s):
                    self.error("unterminated escape")
                esc = self.s[self.i]
                mapping = {
                    "\\": "\\", '"': '"', "'": "'",
                    "n": "\n", "t": "\t", "r": "\r", "v": "\v", "f": "\f",
                }
                if esc not in mapping:
                    self.error(f"unsupported escape \\{esc}")
                out.append(mapping[esc])
                self.i += 1
            else:
                out.append(c)
                self.i += 1
        self.error("unterminated quoted terminal")


def parse_grammar(path: Path = GRAMMAR_PATH) -> list[Production]:
    productions = Parser(path.read_text(encoding="utf-8")).parse()
    names = [p.name for p in productions]
    if len(names) != len(set(names)):
        raise GrammarError("duplicate production name")
    known = set(names)
    referenced = set()

    def collect(node):
        if isinstance(node, Nonterminal):
            referenced.add(node.name)
        for attr in ("item",):
            if hasattr(node, attr):
                collect(getattr(node, attr))
        for attr in ("items",):
            if hasattr(node, attr):
                for child in getattr(node, attr):
                    collect(child)

    for p in productions:
        collect(p.ast)
    missing = sorted(referenced - known)
    if missing:
        raise GrammarError(f"undefined nonterminals: {', '.join(missing)}")
    return productions


# ----------------------------- layout --------------------------------------

MARGIN = 24
STUB = 10
ROW_GAP = 12
SEQ_GAP = 18
BRANCH = 18
REPEAT_MARGIN = 14
LOOP_OFFSET = 10


def _fmt(v: float) -> str:
    if abs(v - round(v)) < 1e-9:
        return str(int(round(v)))
    return f"{v:.2f}".rstrip("0").rstrip(".")


def _line(x1, y1, x2, y2, dash=False) -> str:
    extra = ' stroke-dasharray="4 4"' if dash else ""
    return (
        f'<path d="M{_fmt(x1)} {_fmt(y1)} L{_fmt(x2)} {_fmt(y2)}"'
        f' fill="none" stroke="#404040" stroke-width="1.5"'
        f' stroke-linecap="round" stroke-linejoin="round"{extra}/>'
    )


def _rect(x, y, w, h, rx, fill, dash=False) -> str:
    extra = ' stroke-dasharray="5 4"' if dash else ""
    return (
        f'<rect x="{_fmt(x)}" y="{_fmt(y)}" width="{_fmt(w)}" height="{_fmt(h)}"'
        f' rx="{_fmt(rx)}" fill="{fill}" stroke="#303030" stroke-width="1.5"{extra}/>'
    )


def _text(cx, cy, label: str, special=False) -> str:
    weight = ' font-weight="600"' if special else ""
    return (
        f'<text x="{_fmt(cx)}" y="{_fmt(cy + 4)}" text-anchor="middle"'
        f' font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"'
        f' font-size="13"{weight} fill="#111">{xml_escape(label)}</text>'
    )


def _text_width(label: str) -> float:
    # A deliberately simple, deterministic monospace metric.
    return max(24.0, len(label) * 7.7)


def _display_literal(label: str) -> str:
    if label == " ":
        return "space"
    if label == "\t":
        return "tab"
    if label == "\n":
        return "LF"
    if label == "\r":
        return "CR"
    if label == "\v":
        return "VT"
    if label == "\f":
        return "FF"
    if label == "":
        return '""'
    return label


class Node:
    def __init__(self, w: float, h: float):
        self.w = float(w)
        self.h = float(h)

    def draw(self, x: float, yc: float) -> list[str]:
        raise NotImplementedError


class EmptyNode(Node):
    def __init__(self):
        super().__init__(30, 20)

    def draw(self, x, yc):
        return [_line(x, yc, x + self.w, yc)]


class TerminalNode(Node):
    def __init__(self, label: str, kind: str):
        self.label = _display_literal(label) if kind == "literal" else label
        self.kind = kind
        box_w = max(44.0, _text_width(self.label) + 20)
        self.box_w = box_w
        super().__init__(box_w + 2 * STUB, 32)

    def draw(self, x, yc):
        bx = x + STUB
        by = yc - self.h / 2
        dash = self.kind == "special"
        fill = "#fbfbfb" if dash else "#ffffff"
        return [
            _line(x, yc, bx, yc),
            _rect(bx, by, self.box_w, self.h, 4, fill, dash),
            _line(bx + self.box_w, yc, x + self.w, yc),
            _text(bx + self.box_w / 2, yc, self.label, special=dash),
        ]


class NonterminalNode(Node):
    def __init__(self, name: str):
        self.label = name
        self.box_w = max(52.0, _text_width(name) + 22)
        super().__init__(self.box_w + 2 * STUB, 32)

    def draw(self, x, yc):
        bx = x + STUB
        by = yc - self.h / 2
        return [
            _line(x, yc, bx, yc),
            _rect(bx, by, self.box_w, self.h, 12, "#eef4ff"),
            _line(bx + self.box_w, yc, x + self.w, yc),
            _text(bx + self.box_w / 2, yc, self.label, special=True),
        ]


class SeqNode(Node):
    def __init__(self, items):
        self.items = list(items)
        self.w = sum(n.w for n in self.items) + SEQ_GAP * (len(self.items) - 1)
        self.h = max(n.h for n in self.items)

    def draw(self, x, yc):
        parts = []
        cx = x
        prev_end = None
        for node in self.items:
            if prev_end is not None:
                parts.append(_line(prev_end, yc, cx, yc))
            parts.extend(node.draw(cx, yc))
            prev_end = cx + node.w
            cx += node.w + SEQ_GAP
        return parts


class AltNode(Node):
    def __init__(self, items):
        self.items = list(items)
        self.maxw = max(n.w for n in self.items)
        self.h = sum(n.h for n in self.items) + ROW_GAP * (len(self.items) - 1)
        self.w = 2 * BRANCH + self.maxw

    def draw(self, x, yc):
        parts = []
        cx = x + BRANCH
        parts.append(_line(x, yc, cx, yc))
        ys = []
        top = 0.0
        for node in self.items:
            ys.append(top + node.h / 2)
            top += node.h + ROW_GAP
        for y in ys:
            parts.append(_line(cx, yc, cx, y))
        right = cx + self.maxw
        for node, y in zip(self.items, ys):
            parts.extend(node.draw(cx, y))
            if node.w < self.maxw:
                parts.append(_line(cx + node.w, y, right, y))
        for y in ys:
            parts.append(_line(right, y, right, yc))
        parts.append(_line(right, yc, x + self.w, yc))
        return parts


class OptNode(Node):
    def __init__(self, item):
        self.item = item
        self.w = 2 * BRANCH + item.w
        self.h = item.h + 2 * REPEAT_MARGIN

    def draw(self, x, yc):
        cx = x + BRANCH
        right = cx + self.item.w
        bypass = yc - self.item.h / 2 - REPEAT_MARGIN
        parts = [
            _line(x, yc, cx, yc),
            _line(cx, yc, cx, bypass),
            _line(cx, bypass, right, bypass),
            _line(right, bypass, right, yc),
        ]
        parts.extend(self.item.draw(cx, yc))
        parts.append(_line(right, yc, x + self.w, yc))
        return parts


class RepeatNode(Node):
    def __init__(self, item, optional_zero: bool):
        self.item = item
        self.optional_zero = optional_zero
        self.w = 2 * BRANCH + item.w
        self.h = item.h + 2 * REPEAT_MARGIN

    def draw(self, x, yc):
        cx = x + BRANCH
        right = cx + self.item.w
        loop_y = yc + self.item.h / 2 + LOOP_OFFSET
        parts = [_line(x, yc, cx, yc)]
        if self.optional_zero:
            bypass = yc - self.item.h / 2 - LOOP_OFFSET
            parts.extend([
                _line(cx, yc, cx, bypass),
                _line(cx, bypass, right, bypass),
                _line(right, bypass, right, yc),
            ])
        parts.extend(self.item.draw(cx, yc))
        parts.extend([
            _line(right, yc, right, loop_y),
            _line(right, loop_y, cx, loop_y),
            _line(cx, loop_y, cx, yc),
            _line(right, yc, x + self.w, yc),
        ])
        return parts


def build_node(ast) -> Node:
    if isinstance(ast, Terminal):
        return TerminalNode(ast.label, ast.kind)
    if isinstance(ast, Nonterminal):
        return NonterminalNode(ast.name)
    if isinstance(ast, Empty):
        return EmptyNode()
    if isinstance(ast, Seq):
        return SeqNode([build_node(x) for x in ast.items])
    if isinstance(ast, Alt):
        return AltNode([build_node(x) for x in ast.items])
    if isinstance(ast, Opt):
        return OptNode(build_node(ast.item))
    if isinstance(ast, Star):
        return RepeatNode(build_node(ast.item), optional_zero=True)
    if isinstance(ast, Plus):
        return RepeatNode(build_node(ast.item), optional_zero=False)
    raise TypeError(f"unknown AST node {ast!r}")


def render_svg(name: str, ast) -> str:
    node = build_node(ast)
    width = int(math.ceil(node.w + 2 * MARGIN))
    height = int(math.ceil(node.h + 2 * MARGIN))
    body = node.draw(MARGIN, MARGIN + node.h / 2)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}"'
        f' width="{width}" height="{height}" role="img"'
        f' aria-label="Railroad diagram for production {xml_escape(name)}">\n'
        f'<title>{xml_escape(name)}</title>\n'
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="#ffffff"/>\n'
        + "\n".join(body)
        + "\n</svg>\n"
    )


# ----------------------------- HTML ---------------------------------------

def _production_section(p: Production, svg: str) -> str:
    name = html.escape(p.name)
    note = PRODUCTION_NOTES.get(p.name, "")
    note_html = f'<p class="note">{html.escape(note)}</p>' if note else ""
    return f'''<section class="production" id="prod-{name}" data-production="{name}">
  <div class="prod-head">
    <h3><a class="self" href="#prod-{name}">{name}</a></h3>
    <a class="svg-link" href="railroad/{name}.svg">standalone SVG</a>
  </div>
  <pre class="ebnf"><code>{html.escape(p.name)} = {html.escape(p.rhs)} ;</code></pre>
  {note_html}
  <div class="zoom" aria-label="Zoom controls for {name}">
    <button type="button" data-target="diagram-{name}" data-zoom="0.8">-</button>
    <button type="button" data-target="diagram-{name}" data-zoom="1">reset</button>
    <button type="button" data-target="diagram-{name}" data-zoom="1.25">+</button>
  </div>
  <div class="rail" id="diagram-{name}">
    {svg}
  </div>
</section>'''


def build_html(productions: list[Production], svgs: dict[str, str]) -> str:
    names = [p.name for p in productions]
    lexical = [p for p in productions if p.name in LEXICAL]
    structural = [p for p in productions if p.name in STRUCTURAL]
    other = [p for p in productions if p.name not in ALL_PRODUCTIONS]
    if other:
        raise GrammarError(f"unclassified productions: {', '.join(p.name for p in other)}")

    def nav_group(title: str, items: list[Production]) -> str:
        links = "\n".join(
            f'<li><a href="#prod-{html.escape(p.name)}">{html.escape(p.name)}</a></li>'
            for p in items
        )
        return f'<h2>{html.escape(title)}</h2>\n<ul>\n{links}\n</ul>'

    lexical_html = "\n".join(_production_section(p, svgs[p.name]) for p in lexical)
    structural_html = "\n".join(_production_section(p, svgs[p.name]) for p in structural)
    doc_links = "\n".join(
        f'<li><a href="{html.escape(path)}">{html.escape(label)}</a></li>'
        for label, path in DOC_LINKS
    )
    nav = "\n".join([
        '<li><a href="#overview">Overview</a></li>',
        '<li><a href="#lexical">Lexical grammar</a></li>',
        nav_group("Lexical productions", lexical),
        '<li><a href="#structural">Structural grammar</a></li>',
        nav_group("Structural productions", structural),
        '<li><a href="#grouping">Arity-driven grouping</a></li>',
        '<li><a href="#implementation">Implementation-specific parsing</a></li>',
        '<li><a href="#unresolved">Unresolved cases</a></li>',
        '<li><a href="#documents">Related documents</a></li>',
    ])

    return f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Glon Grammar and Railroad Diagrams</title>
<style>
:root {{
  --ink: #17202a;
  --muted: #5b6773;
  --line: #d7dde3;
  --panel: #f6f8fa;
  --accent: #1f5f8b;
  --code: #20262e;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{
  margin: 0;
  color: var(--ink);
  background: #fff;
  font: 16px/1.55 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}}
a {{ color: var(--accent); text-decoration: none; }}
a:hover {{ text-decoration: underline; }}
header {{
  padding: 2rem clamp(1rem, 4vw, 3rem) 1.2rem;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(180deg, #f9fbfd, #fff);
}}
header h1 {{ margin: 0 0 .4rem; font-size: clamp(1.6rem, 4vw, 2.4rem); }}
header p {{ max-width: 72ch; margin: .35rem 0; color: var(--muted); }}
.breadcrumb {{ font-size: .85rem; margin: 0 0 .55rem; }}
.layout {{
  display: grid;
  grid-template-columns: 280px minmax(0, 1fr);
  gap: 2rem;
  max-width: 1500px;
  margin: 0 auto;
  padding: 1.5rem clamp(1rem, 3vw, 2rem) 4rem;
}}
nav {{
  align-self: start;
  position: sticky;
  top: 1rem;
  max-height: calc(100vh - 2rem);
  overflow: auto;
  border: 1px solid var(--line);
  border-radius: 10px;
  background: var(--panel);
  padding: 1rem;
  font-size: .92rem;
}}
nav h2 {{ font-size: .8rem; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); margin: 1rem 0 .35rem; }}
nav ul {{ list-style: none; margin: 0; padding: 0; }}
nav li {{ margin: .15rem 0; }}
nav > ul > li > a {{ font-weight: 600; }}
main {{ min-width: 0; }}
section.doc {{ margin: 0 0 2.5rem; }}
section.doc h2 {{ border-bottom: 1px solid var(--line); padding-bottom: .35rem; }}
.production {{
  border-top: 1px solid var(--line);
  padding: 1.3rem 0 1.6rem;
}}
.prod-head {{ display: flex; align-items: baseline; justify-content: space-between; gap: 1rem; flex-wrap: wrap; }}
.prod-head h3 {{ margin: 0; font-size: 1.12rem; }}
.prod-head h3 a.self {{ color: var(--ink); }}
.svg-link {{ font-size: .82rem; white-space: nowrap; }}
pre.ebnf {{
  margin: .7rem 0;
  padding: .7rem .85rem;
  border-radius: 8px;
  overflow: auto;
  background: var(--code);
  color: #e8edf2;
  font: 13px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}}
.note {{ color: var(--muted); max-width: 85ch; margin: .35rem 0 .8rem; }}
.zoom {{ display: flex; gap: .4rem; margin: .4rem 0 .7rem; }}
.zoom button {{
  border: 1px solid var(--line);
  background: #fff;
  color: var(--ink);
  border-radius: 6px;
  padding: .25rem .55rem;
  cursor: pointer;
  font: inherit;
  font-size: .82rem;
}}
.zoom button:hover {{ background: var(--panel); }}
.rail {{
  overflow: auto;
  border: 1px solid var(--line);
  border-radius: 10px;
  background: #fff;
  padding: .6rem;
  -webkit-overflow-scrolling: touch;
}}
.rail svg {{ display: block; width: 100%; height: auto; min-width: 320px; }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 1rem; }}
.card {{ border: 1px solid var(--line); border-radius: 10px; background: var(--panel); padding: 1rem; }}
.card h3 {{ margin-top: 0; }}
code {{ font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: .92em; }}
table {{ border-collapse: collapse; width: 100%; margin: .7rem 0 1rem; }}
th, td {{ border: 1px solid var(--line); padding: .45rem .55rem; text-align: left; vertical-align: top; }}
th {{ background: var(--panel); }}
.status {{ display: inline-block; font-size: .72rem; font-weight: 700; letter-spacing: .04em; border-radius: 999px; padding: .1rem .45rem; border: 1px solid var(--line); }}
.verified {{ color: #165c35; background: #edf8f1; }}
.unresolved {{ color: #7a2e12; background: #fff3ec; }}
footer {{ border-top: 1px solid var(--line); padding: 1.2rem clamp(1rem, 4vw, 3rem); color: var(--muted); font-size: .85rem; }}
@media (max-width: 900px) {{
  .layout {{ display: block; }}
  nav {{ position: static; max-height: none; margin-bottom: 1.5rem; }}
}}
</style>
</head>
<body>
<header>
  <p class="breadcrumb"><a href="../">Glon</a> / <a href="../language/">Language Reference</a> / Grammar</p>
  <h1>Glon Grammar and Railroad Diagrams</h1>
  <p>Generated from the verified reader EBNF in <code>glon-grammar.ebnf</code>. The EBNF is the canonical machine-readable source; the SVG diagrams and this page are deterministic generated outputs.</p>
  <p><span class="status verified">VERIFIED</span> Reader acceptance only. This page does not claim that unresolved semantic behaviour has been verified.</p>
</header>
<div class="layout">
<nav aria-label="Grammar navigation">
{nav}
</nav>
<main>
<section class="doc" id="overview">
  <h2>Overview</h2>
  <p>This reference separates three concerns that are often mixed together:</p>
  <div class="grid">
    <div class="card">
      <h3>Lexical grammar</h3>
      <p>Bytes, whitespace, tokens, word forms, strings, integers and the private masm token stream. These productions decide what the reader accepts.</p>
      <p><a href="#lexical">Go to lexical grammar</a></p>
    </div>
    <div class="card">
      <h3>Structural grammar</h3>
      <p>Source envelopes, blocks, items, forms, <code>func</code> and <code>masm</code> forms. These decide how accepted forms nest.</p>
      <p><a href="#structural">Go to structural grammar</a></p>
    </div>
    <div class="card">
      <h3>Arity-driven grouping</h3>
      <p>There is no lexical grouping grammar. A callable consumes its own arity of following expressions; parentheses and commas do not exist.</p>
      <p><a href="#grouping">Go to grouping</a></p>
    </div>
  </div>
  <p>The canonical grammar file is <a href="glon-grammar.ebnf">glon-grammar.ebnf</a>. Run <code>python3 generate.py</code> to regenerate this page and the SVG files; run <code>python3 verify.py</code> to verify them.</p>
</section>

<section class="doc" id="lexical">
  <h2>Lexical grammar</h2>
  <p>Terminals are shown as boxes. <code>? ... ?</code> denotes a character-class or private terminal, not a Glon value.</p>
  {lexical_html}
</section>

<section class="doc" id="structural">
  <h2>Structural grammar</h2>
  <p>An item is what a block or program loop recognises; a form is what <code>parse_form</code> recognises. This distinction is load-bearing for strings, <code>func</code> and <code>masm</code>.</p>
  {structural_html}
</section>

<section class="doc" id="grouping">
  <h2>Arity-driven expression grouping</h2>
  <p><span class="status verified">NOT EBNF</span> Grouping is an evaluation rule, not a lexical production. The reader emits a flat stream of values; the evaluator gives each callable its fixed number of following complete expressions.</p>
  <table>
    <thead><tr><th>Expression</th><th>Grouping</th><th>Result</th></tr></thead>
    <tbody>
      <tr><td><code>+ 1 * 2 3</code></td><td><code>+ 1 (* 2 3)</code></td><td><code>7</code></td></tr>
      <tr><td><code>double double 5</code></td><td><code>double (double 5)</code></td><td><code>20</code> for the primer's <code>double</code></td></tr>
      <tr><td><code>values [10 20]</code></td><td>one expression yielding multiple results</td><td><code>10 20</code></td></tr>
    </tbody>
  </table>
  <p>See <a href="../language/evaluation.md">GLON-EVALUATION-MODEL.md</a> for the evaluation diagrams and <a href="../language/spec.md">GLON-LANGUAGE-SPEC.md</a> §3 for fixed-arity rules. Non-block <code>do</code>/<code>either</code> and missing arguments are unresolved, not groupable syntax.</p>
</section>

<section class="doc" id="implementation">
  <h2>Implementation-specific parsing behaviour</h2>
  <p>These facts are implemented and documented; they are not additional syntax.</p>
  <ul>
    <li><span class="status verified">IMPLEMENTED</span> A source beginning with a block ignores all trailing bytes.</li>
    <li><span class="status verified">IMPLEMENTED</span> <code>func</code> is special-cased inside a block, not by the unwrapped top-level program loop.</li>
    <li><span class="status verified">IMPLEMENTED</span> A string at <code>form</code> position (for example a <code>func</code> spec) is a word token, not a string literal.</li>
    <li><span class="status verified">IMPLEMENTED</span> Tokens are truncated to 63 bytes before interning.</li>
    <li><span class="status verified">IMPLEMENTED</span> <code>;;</code> comments are host pre-processing, not reader syntax; the stripper is not string-aware.</li>
    <li><span class="status verified">IMPLEMENTED</span> A string literal expands to <code>mk-string</code> plus a byte block and needs the library binding at run time.</li>
    <li><span class="status verified">IMPLEMENTED</span> <code>masm</code>/<code>raw</code> is a private S1 sublanguage, not ordinary Glon vocabulary.</li>
  </ul>
</section>

<section class="doc" id="unresolved">
  <h2>Unresolved cases</h2>
  <p><span class="status unresolved">UNRESOLVED</span> The documentation and implementation do not define or test all of the following. They are listed so an implementer does not mistake a guess for a contract.</p>
  <ul>
    <li>Non-integer arithmetic and <code>print</code>; division/modulo by zero.</li>
    <li>Non-block <code>do</code>/<code>either</code>; missing or extra call arguments.</li>
    <li>Malformed <code>func</code> specs (non-block, duplicate parameters, more than 16 parameters).</li>
    <li><code>return</code> with no following expression.</li>
    <li><code>values</code> zero/multi-result forms and first-result collapse in argument position.</li>
    <li>Token-truncation collisions; malformed <code>masm</code> empty blocks; some host/comment boundary cases.</li>
  </ul>
  <p>See <a href="../language/verification.md">GLON-SPEC-VERIFICATION.md</a> §7 and <a href="../language/spec.md">GLON-LANGUAGE-SPEC.md</a> §11.3 for the prioritised list.</p>
</section>

<section class="doc" id="documents">
  <h2>Related documents</h2>
  <ul>
{doc_links}
  </ul>
</section>
</main>
</div>
<footer>Generated by <code>docs/grammar/generate.py</code> from <code>docs/grammar/glon-grammar.ebnf</code>. Do not edit generated SVG or HTML by hand. No network or CDN dependency.</footer>
<script>
(function () {{
  const defaults = new WeakMap();
  function setZoom(target, factor) {{
    const rail = document.getElementById(target);
    if (!rail) return;
    const svg = rail.querySelector('svg');
    if (!svg) return;
    let current = defaults.get(svg) || 1;
    if (factor === 1) current = 1;
    else current = Math.max(0.5, Math.min(3, current * factor));
    defaults.set(svg, current);
    svg.style.width = (current * 100) + '%';
  }}
  document.querySelectorAll('button[data-target]').forEach(function (button) {{
    button.addEventListener('click', function () {{
      const target = button.getAttribute('data-target');
      const zoom = parseFloat(button.getAttribute('data-zoom'));
      setZoom(target, zoom);
    }});
  }});
}})();
</script>
</body>
</html>
'''


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def generate_outputs() -> tuple[list[Production], dict[str, bytes]]:
    productions = parse_grammar()
    svgs = {p.name: render_svg(p.name, p.ast) for p in productions}
    index = build_html(productions, svgs)
    index_bytes = index.encode("utf-8")
    manifest = {
        "generator": "docs/grammar/generate.py",
        "generator_version": GENERATOR_VERSION,
        "grammar": "glon-grammar.ebnf",
        "grammar_sha256": _sha256(GRAMMAR_PATH.read_bytes()),
        "production_count": len(productions),
        "productions": [
            {
                "name": p.name,
                "svg": f"{p.name}.svg",
                "sha256": _sha256(svgs[p.name].encode("utf-8")),
                "bytes": len(svgs[p.name].encode("utf-8")),
            }
            for p in productions
        ],
        "index_sha256": _sha256(index_bytes),
        "index_bytes": len(index_bytes),
    }
    outputs: dict[str, bytes] = {
        f"railroad/{name}.svg": svg.encode("utf-8") for name, svg in svgs.items()
    }
    outputs["railroad/manifest.json"] = (
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    outputs["index.html"] = index_bytes
    return productions, outputs


def write_outputs(outputs: dict[str, bytes]) -> None:
    RAILROAD_DIR.mkdir(parents=True, exist_ok=True)
    for old in RAILROAD_DIR.glob("*.svg"):
        old.unlink()
    for rel, data in outputs.items():
        path = ROOT / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main() -> int:
    productions, outputs = generate_outputs()
    write_outputs(outputs)
    print(f"generated {len(productions)} productions, {len(outputs)} files")
    print(f"html: {INDEX_PATH}")
    print(f"manifest: {MANIFEST_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
