#!/usr/bin/env python3
"""B6 verifier: A uses B5's verifier; D adds structured textual validation.

D validation is post-generation feedback only. It never rewrites the artifact.
On a D validation error the model receives category/location/expected/observed
diagnostics. If validation passes, B5's compiler/runner checks semantics.
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
B5BIN = BASE.parent / "glon-authoring-b5" / "build" / "b5"

BINOPS = {"+", "-", "*", "<", "<=", "=", ">", ">="}


class Tok:
    __slots__ = ("kind", "text", "line", "col")
    def __init__(self, kind, text, line, col):
        self.kind, self.text, self.line, self.col = kind, text, line, col


def tokenize(src):
    toks = []
    i = 0
    line = 1
    col = 1
    n = len(src)
    while i < n:
        c = src[i]
        if c == '\n':
            i += 1; line += 1; col = 1; continue
        if c in ' \t\r':
            i += 1; col += 1; continue
        if c == ';':
            while i < n and src[i] != '\n':
                i += 1; col += 1
            continue
        if c in '[]':
            toks.append(Tok('LBRACK' if c == '[' else 'RBRACK', c, line, col))
            i += 1; col += 1; continue
        if c.isdigit() or (c == '-' and i + 1 < n and src[i+1].isdigit()):
            start = i
            if c == '-':
                i += 1; col += 1
            while i < n and src[i].isdigit():
                i += 1; col += 1
            toks.append(Tok('INT', src[start:i], line, col - (i - start)))
            continue
        start = i
        startcol = col
        while i < n and src[i] not in ' \t\r\n[];':
            i += 1; col += 1
        if i == start:
            toks.append(Tok('WORD', c, line, col)); i += 1; col += 1
        else:
            toks.append(Tok('WORD', src[start:i], line, startcol))
    toks.append(Tok('EOF', '<end of file>', line, col))
    return toks


class Validator:
    def __init__(self, src):
        self.toks = tokenize(src)
        self.pos = 0
        self.funcs = {}       # name -> arity
        self.params = []
        self.top_start = 0
        self.error = None

    def tok(self):
        return self.toks[self.pos]

    def at(self, kind):
        return self.tok().kind == kind

    def take(self):
        t = self.tok()
        if t.kind != 'EOF':
            self.pos += 1
        return t

    def fail(self, category, token, expected, observed, message):
        if self.error is None:
            self.error = {
                "category": category,
                "line": token.line,
                "column": token.col,
                "expected": expected,
                "observed": observed,
                "message": message,
            }
        return None

    def expect(self, kind, expected, category="malformed_block"):
        t = self.tok()
        if t.kind != kind:
            self.fail(category, t, expected, t.text,
                      f"expected {expected}, found {t.text}")
            return False
        self.take()
        return True

    def prescan(self):
        # Top-level definitions: name: func [params] [body]
        while True:
            t = self.tok()
            if t.kind != 'WORD' or not t.text.endswith(':'):
                break
            name_tok = self.take()
            name = name_tok.text[:-1]
            if not name:
                self.fail("invalid_definition", name_tok, "function name",
                          name_tok.text, "empty function name"); return False
            if name in self.funcs:
                self.fail("invalid_definition", name_tok, "unique function name",
                          name, f"duplicate function '{name}'"); return False
            if not self.at('WORD') or self.tok().text != 'func':
                self.fail("invalid_definition", self.tok(), "func",
                          self.tok().text, "expected 'func' after function name")
                return False
            self.take()
            if not self.expect('LBRACK', "'[' before parameters"):
                return False
            params = []
            while not self.at('RBRACK'):
                if self.at('EOF'):
                    self.fail("unbalanced_delimiter", self.tok(), "']'", "end of file",
                              "unterminated parameter list"); return False
                if not self.at('WORD'):
                    self.fail("invalid_definition", self.tok(), "parameter word",
                              self.tok().text, "parameter must be a word"); return False
                p = self.take()
                if p.text in params:
                    self.fail("invalid_definition", p, "unique parameter",
                              p.text, f"duplicate parameter '{p.text}'"); return False
                if len(params) >= 8:
                    self.fail("invalid_definition", p, "at most 8 parameters",
                              str(len(params) + 1), "too many parameters"); return False
                params.append(p.text)
            self.take()  # ]
            if not self.expect('LBRACK', "'[' before function body"):
                return False
            body_start = self.pos
            depth = 1
            while self.pos < len(self.toks) and depth:
                t = self.take()
                if t.kind == 'LBRACK': depth += 1
                elif t.kind == 'RBRACK': depth -= 1
                elif t.kind == 'EOF':
                    self.fail("unbalanced_delimiter", t, "']'", "end of file",
                              f"unterminated body for '{name}'"); return False
            if depth:
                self.fail("unbalanced_delimiter", self.tok(), "']'", "end of file",
                          f"unterminated body for '{name}'"); return False
            self.funcs[name] = (len(params), body_start - 1)
        self.top_start = self.pos
        return True

    def parse_expr(self):
        t = self.tok()
        if t.kind == 'INT':
            self.take(); return True
        if t.kind != 'WORD':
            if t.kind == 'LBRACK':
                self.fail("malformed_block", t, "expression", "'['",
                          "a block is not an expression here"); return False
            if t.kind == 'RBRACK':
                self.fail("expected_expression", t, "expression", "']'",
                          "expected an expression before the closing delimiter"); return False
            self.fail("missing_expression", t, "expression", t.text,
                      "expected an expression"); return False
        if t.text in self.params:
            self.take(); return True
        if t.text == 'either':
            return self.parse_either()
        if t.text in BINOPS:
            self.take()
            if not self.parse_expr(): return False
            if not self.parse_expr(): return False
            return True
        if t.text in self.funcs:
            arity = self.funcs[t.text][0]
            self.take()
            for i in range(arity):
                if self.at('EOF') or self.at('RBRACK'):
                    self.fail("argument_count", t, f"{arity} argument(s)",
                              f"{i}", f"call to '{t.text}' expects {arity} argument(s)")
                    return False
                if not self.parse_expr(): return False
            return True
        if t.text in ('if', 'func'):
            self.fail("invalid_control_flow", t, "supported expression",
                      t.text, f"'{t.text}' is not supported in this subset")
            return False
        self.fail("invalid_function_reference", t, "parameter, function or supported operator",
                  t.text, f"unknown word or invalid function reference '{t.text}'")
        return False

    def parse_either(self):
        start = self.take()  # either
        if not self.parse_expr(): return False
        if not self.expect('LBRACK', "'[' before the then block", "invalid_control_flow"):
            return False
        if not self.parse_seq(True, "then"): return False
        if not self.expect('RBRACK', "']' after the then block", "invalid_control_flow"):
            return False
        if not self.expect('LBRACK', "'[' before the else block", "invalid_control_flow"):
            return False
        if not self.parse_seq(True, "else"): return False
        if not self.expect('RBRACK', "']' after the else block", "invalid_control_flow"):
            return False
        return True

    def parse_seq(self, in_block, label=""):
        start = self.pos
        while True:
            if self.at('EOF'):
                if in_block:
                    self.fail("unbalanced_delimiter", self.tok(), "']'", "end of file",
                              f"unterminated {label} block"); return False
                break
            if self.at('RBRACK'):
                if in_block: break
                self.fail("unexpected_token", self.tok(), "expression", "']'",
                          "unexpected closing delimiter"); return False
            if not self.parse_expr(): return False
        if self.pos == start and in_block:
            self.fail("empty_block", self.tok(), "at least one expression", "']'",
                      f"empty {label} block"); return False
        return True

    def run(self):
        if not self.prescan(): return self.error
        if self.at('EOF'):
            self.fail("missing_top_level", self.tok(), "top-level expression",
                      "end of file", "no top-level expression after function definitions")
            return self.error
        # Parse each function body with its parameter scope.
        for name, (arity, body_open) in self.funcs.items():
            self.pos = body_open + 1
            self.params = list(self._params_of(name))
            if not self.parse_seq(True, f"function '{name}'"): return self.error
            if not self.expect('RBRACK', "']' after function body"):
                return self.error
        # Top-level.
        self.pos = self.top_start
        self.params = []
        if not self.parse_seq(False): return self.error
        return None

    def _params_of(self, name):
        # Recover parameters from the definition header without storing them twice.
        # body_open is stored; walk backwards to the preceding ']'.
        i = self.funcs[name][1] - 1
        while i >= 0 and self.toks[i].kind != 'RBRACK':
            i -= 1
        j = i - 1
        while j >= 0 and self.toks[j].kind != 'LBRACK':
            j -= 1
        return [self.toks[k].text for k in range(j + 1, i) if self.toks[k].kind == 'WORD']



def validate_text(src):
    return Validator(src).run()


def run_b5(task, rep, path):
    t0 = time.perf_counter()
    p = subprocess.run([str(B5BIN), "--verify", task, rep, str(path)],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr, time.perf_counter() - t0


def write_meta(record):
    path = os.environ.get("B6_VERIFY_META")
    if path:
        with open(path, "a") as f:
            f.write(json.dumps(record) + "\n")


def main():
    if len(sys.argv) != 4:
        print("usage: b6_verify.py TASK A|D FILE", file=sys.stderr)
        return 2
    task, rep, path = sys.argv[1:4]
    if rep not in ("A", "D"):
        print("B6 verifier supports A and D only", file=sys.stderr)
        return 2
    if not B5BIN.exists():
        print(f"missing B5 verifier: {B5BIN}", file=sys.stderr)
        return 2

    validation_s = 0.0
    category = None
    if rep == "D":
        src = Path(path).read_text(errors="replace")
        t0 = time.perf_counter()
        err = validate_text(src)
        validation_s = time.perf_counter() - t0
        if err is not None:
            category = err["category"]
            print("D-VALIDATION ERROR")
            print(f"category: {err['category']}")
            print(f"location: line {err['line']}, column {err['column']}")
            print(f"expected: {err['expected']}")
            print(f"observed: {err['observed']}")
            print(f"message: {err['message']}")
            write_meta({"task": task, "rep": rep, "category": category,
                        "validation_seconds": round(validation_s, 6),
                        "verify_seconds": 0.0, "returncode": 1})
            return 1

    rc, out, verify_s = run_b5(task, "A", path)
    if rc == 0:
        category = "pass"
        print(out, end="")
    else:
        category = "compiler_or_semantics"
        print(out, end="")
        if rep == "D":
            print("D-VALIDATION: passed structural checks; failure is in the compiler/semantic stage.")
    write_meta({"task": task, "rep": rep, "category": category,
                "validation_seconds": round(validation_s, 6),
                "verify_seconds": round(verify_s, 6), "returncode": rc})
    return rc


if __name__ == "__main__":
    sys.exit(main())
