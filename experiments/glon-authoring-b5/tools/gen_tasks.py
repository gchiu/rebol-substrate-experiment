#!/usr/bin/env python3
"""Generate the B4 test fixtures in three representations.

This is fixture-authoring tooling, not part of any measured compiler. It takes
a small canonical AST and emits:
  <task>.glon  textual Glon subset for A
  <task>.b4b   semantically tagged words for B
  <task>.b4c   direct machine words for C
All encodings are deterministic and little-endian.
"""
import os
import struct

OUT = os.path.join(os.path.dirname(__file__), "..", "tasks")

# --- canonical AST -------------------------------------------------------
# expr: ('int', n) | ('param', i) | ('call', name, [args])
#       | ('bin', op, a, b) | ('either', cond, then_seq, else_seq)
# seq: list of expressions; func: (name, [params], body_seq)

TASKS = [
    dict(
        name="tri",
        spec="Return the triangular number 1+2+...+n by recursion (0 when n <= 0).",
        funcs=[("tri", ["n"], [
            ("either", ("bin", "<=", ("param", 0), ("int", 0)),
             [("int", 0)],
             [("bin", "+", ("param", 0),
               ("call", "tri", [("bin", "-", ("param", 0), ("int", 1))]))])
        ])],
        top=[("call", "tri", [("int", 10)])],
        expected=55,
    ),
    dict(
        name="pow2",
        spec="Return 2^n by recursion (1 when n = 0).",
        funcs=[("pow2", ["n"], [
            ("either", ("bin", "=", ("param", 0), ("int", 0)),
             [("int", 1)],
             [("bin", "*", ("int", 2),
               ("call", "pow2", [("bin", "-", ("param", 0), ("int", 1))]))])
        ])],
        top=[("call", "pow2", [("int", 10)])],
        expected=1024,
    ),
    dict(
        name="min3",
        spec="Return the minimum of three integers using comparisons and nested either.",
        funcs=[("min3", ["a", "b", "c"], [
            ("either", ("bin", "<", ("param", 0), ("param", 1)),
             [("either", ("bin", "<", ("param", 0), ("param", 2)),
               [("param", 0)], [("param", 2)])],
             [("either", ("bin", "<", ("param", 1), ("param", 2)),
               [("param", 1)], [("param", 2)])])
        ])],
        top=[("call", "min3", [("int", 9), ("int", 4), ("int", 7)])],
        expected=4,
    ),
    dict(
        name="clamp",
        spec="Clamp x into [lo,hi]: return lo if x<lo, hi if x>hi, else x.",
        funcs=[("clamp", ["x", "lo", "hi"], [
            ("either", ("bin", "<", ("param", 0), ("param", 1)),
             [("param", 1)],
             [("either", ("bin", ">", ("param", 0), ("param", 2)),
               [("param", 2)], [("param", 0)])])
        ])],
        top=[("call", "clamp", [("int", 15), ("int", 3), ("int", 10)])],
        expected=10,
    ),
    dict(
        name="sign_neg",
        spec="Return -1 for a negative input, 0 for zero, 1 for positive.",
        funcs=[("sign", ["x"], [
            ("either", ("bin", "<", ("param", 0), ("int", 0)),
             [("bin", "-", ("int", 0), ("int", 1))],
             [("either", ("bin", "=", ("param", 0), ("int", 0)),
               [("int", 0)], [("int", 1)])])
        ])],
        top=[("call", "sign", [("bin", "-", ("int", 0), ("int", 7))])],
        expected=-1,
    ),
    dict(
        name="range_sum",
        spec="Return lo+(lo+1)+...+hi by recursion; return 0 when lo>hi.",
        funcs=[("range_sum", ["lo", "hi"], [
            ("either", ("bin", ">", ("param", 0), ("param", 1)),
             [("int", 0)],
             [("bin", "+", ("param", 0),
               ("call", "range_sum", [
                   ("bin", "+", ("param", 0), ("int", 1)),
                   ("param", 1)]))])
        ])],
        top=[("call", "range_sum", [("int", 3), ("int", 9)])],
        expected=42,
    ),
    dict(
        name="choose",
        spec="Return the binomial coefficient C(n,k) by recursion, for non-negative n,k.",
        funcs=[("choose", ["n", "k"], [
            ("either", ("bin", "=", ("param", 1), ("int", 0)),
             [("int", 1)],
             [("either", ("bin", "=", ("param", 0), ("param", 1)),
               [("int", 1)],
               [("bin", "+",
                 ("call", "choose", [
                     ("bin", "-", ("param", 0), ("int", 1)),
                     ("bin", "-", ("param", 1), ("int", 1))]),
                 ("call", "choose", [
                     ("bin", "-", ("param", 0), ("int", 1)),
                     ("param", 1)]))])])
        ])],
        top=[("call", "choose", [("int", 6), ("int", 2)])],
        expected=15,
    ),
    dict(
        name="is_even",
        spec="Return 1 if n is even, 0 if n is odd, using recursion and subtraction by 2.",
        funcs=[("is_even", ["n"], [
            ("either", ("bin", "=", ("param", 0), ("int", 0)),
             [("int", 1)],
             [("either", ("bin", "=", ("param", 0), ("int", 1)),
               [("int", 0)],
               [("call", "is_even", [("bin", "-", ("param", 0), ("int", 2))])])])
        ])],
        top=[("call", "is_even", [("int", 10)])],
        expected=1,
    ),
    dict(
        name="ack",
        spec="Return Ackermann(m,n) for non-negative m,n using at most small inputs.",
        funcs=[("ack", ["m", "n"], [
            ("either", ("bin", "=", ("param", 0), ("int", 0)),
             [("bin", "+", ("param", 1), ("int", 1))],
             [("either", ("bin", "=", ("param", 1), ("int", 0)),
               [("call", "ack", [
                   ("bin", "-", ("param", 0), ("int", 1)),
                   ("int", 1)])],
               [("call", "ack", [
                   ("bin", "-", ("param", 0), ("int", 1)),
                   ("call", "ack", [
                       ("param", 0),
                       ("bin", "-", ("param", 1), ("int", 1))])])])])
        ])],
        top=[("call", "ack", [("int", 2), ("int", 3)])],
        expected=9,
    ),
    dict(
        name="quad",
        spec="quad(x) = double(double(x)); double is defined after quad (forward reference).",
        funcs=[
            ("quad", ["x"], [("call", "double", [("call", "double", [("param", 0)])])]),
            ("double", ["x"], [("bin", "+", ("param", 0), ("param", 0))]),
        ],
        top=[("call", "quad", [("int", 5)])],
        expected=20,
    ),
    dict(
        name="median3",
        spec="Return the median of three integers using comparisons and nested either.",
        funcs=[("median3", ["a", "b", "c"], [
            ("either", ("bin", "<", ("param", 0), ("param", 1)),
             [("either", ("bin", "<", ("param", 1), ("param", 2)),
               [("param", 1)],
               [("either", ("bin", "<", ("param", 0), ("param", 2)),
                 [("param", 2)], [("param", 0)])])],
             [("either", ("bin", "<", ("param", 0), ("param", 2)),
               [("param", 0)],
               [("either", ("bin", "<", ("param", 1), ("param", 2)),
                 [("param", 2)], [("param", 1)])])])
        ])],
        top=[("call", "median3", [("int", 9), ("int", 2), ("int", 7)])],
        expected=7,
    ),
    dict(
        name="max_neg",
        spec="Return the maximum of three negative integers using comparisons and nested either.",
        funcs=[("max_neg", ["a", "b", "c"], [
            ("either", ("bin", ">", ("param", 0), ("param", 1)),
             [("either", ("bin", ">", ("param", 0), ("param", 2)),
               [("param", 0)], [("param", 2)])],
             [("either", ("bin", ">", ("param", 1), ("param", 2)),
               [("param", 1)], [("param", 2)])])
        ])],
        top=[("call", "max_neg", [
            ("bin", "-", ("int", 0), ("int", 9)),
            ("bin", "-", ("int", 0), ("int", 4)),
            ("bin", "-", ("int", 0), ("int", 7))])],
        expected=-4,
    ),
]

# --- A: textual Glon -----------------------------------------------------
def text_expr(e, params):
    k = e[0]
    if k == "int": return str(e[1])
    if k == "param": return params[e[1]]
    if k == "call": return e[1] + "".join(" " + text_expr(a, params) for a in e[2])
    if k == "bin": return e[1] + " " + text_expr(e[2], params) + " " + text_expr(e[3], params)
    if k == "either":
        return ("either " + text_expr(e[1], params) +
                " [ " + text_seq(e[2], params) + " ]" +
                " [ " + text_seq(e[3], params) + " ]")
    raise ValueError(k)

def text_seq(seq, params):
    return " ".join(text_expr(e, params) for e in seq)

def emit_text(task):
    lines = []
    for name, params, body in task["funcs"]:
        lines.append(f"{name}: func [ {' '.join(params)} ] [ {text_seq(body, params)} ]")
    lines.append(text_seq(task["top"], []))
    return ("\n".join(lines) + "\n").encode()

# --- B: semantically tagged 32-bit words ---------------------------------
B_MAGIC = 0x42344231
B_FUNC, B_SEQ_BEGIN, B_SEQ_END, B_TOP_BEGIN, B_TOP_END = range(1, 6)
B_INT, B_PARAM, B_CALL, B_BIN, B_EITHER = range(6, 11)
B_THEN_BEGIN, B_THEN_END, B_ELSE_BEGIN, B_ELSE_END = range(11, 15)
BIN_OP = {"+": 0, "-": 1, "*": 2, "<": 3, "<=": 4, "=": 5, ">": 6, ">=": 7}

def bword(role, operand=0):
    return ((role & 0xFF) << 24) | (operand & 0xFFFFFF)

def tagged_expr(e, fids, params):
    k = e[0]
    if k == "int":
        v = e[1] & 0xFFFFFF
        return [bword(B_INT, v)]
    if k == "param":
        return [bword(B_PARAM, e[1])]
    if k == "bin":
        out = [bword(B_BIN, BIN_OP[e[1]])]
        out += tagged_expr(e[2], fids, params)
        out += tagged_expr(e[3], fids, params)
        return out
    if k == "call":
        fid = fids[e[1]]
        arity = len(e[2])
        out = [bword(B_CALL, (fid << 8) | arity)]
        for a in e[2]: out += tagged_expr(a, fids, params)
        return out
    if k == "either":
        out = [bword(B_EITHER)]
        out += tagged_expr(e[1], fids, params)
        out += [bword(B_THEN_BEGIN)]
        for x in e[2]: out += tagged_expr(x, fids, params)
        out += [bword(B_THEN_END), bword(B_ELSE_BEGIN)]
        for x in e[3]: out += tagged_expr(x, fids, params)
        out += [bword(B_ELSE_END)]
        return out
    raise ValueError(k)

def emit_tagged(task):
    fids = {name: i + 1 for i, (name, _, _) in enumerate(task["funcs"])}
    words = [B_MAGIC]
    for name, params, body in task["funcs"]:
        words.append(bword(B_FUNC, (fids[name] << 8) | len(params)))
        words.append(bword(B_SEQ_BEGIN))
        for e in body: words += tagged_expr(e, fids, params)
        words.append(bword(B_SEQ_END))
    words.append(bword(B_TOP_BEGIN))
    for e in task["top"]: words += tagged_expr(e, fids, [])
    words.append(bword(B_TOP_END))
    return b"".join(struct.pack("<I", w) for w in words)

# --- C: direct machine words ---------------------------------------------
C_MAGIC = 0x43344231
(C_ENTRY, C_LABEL, C_LIT, C_DUP, C_DROP, C_FETCH, C_STORE, C_ZBRANCH,
 C_HOST, C_CALL, C_RET, C_TO_R, C_FROM_R, C_R_FETCH, C_BRANCH, C_HALT) = range(1, 17)
HOST = {"+": 0, "-": 1, "*": 2, "/": 3, "=": 6, "<": 8, ">": 9, "<=": 10, ">=": 11}
REG_RP = 2

def cword(op, operand=0):
    return ((op & 0xFF) << 24) | (operand & 0xFFFFFF)

class Lower:
    def __init__(self, task):
        self.fids = {name: i + 1 for i, (name, _, _) in enumerate(task["funcs"])}
        self.entry = len(task["funcs"]) + 1
        self.next_label = self.entry + 1
        self.out = []
    def label(self):
        x = self.next_label; self.next_label += 1; return x
    def op(self, op, arg=0): self.out.append(cword(op, arg))
    def expr(self, e, params):
        k = e[0]
        if k == "int":
            self.op(C_LIT, (e[1] * 16) & 0xFFFFFF); return
        if k == "param":
            i = e[1]
            if i == 0:
                self.op(C_R_FETCH)
            else:
                self.op(C_LIT, REG_RP); self.op(C_FETCH)
                self.op(C_LIT, i); self.op(C_HOST, 0); self.op(C_FETCH)
            return
        if k == "call":
            for a in e[2]: self.expr(a, params)
            self.op(C_CALL, self.fids[e[1]]); return
        if k == "bin":
            op = e[1]
            if op == "*":
                self.expr(e[2], params); self.op(C_LIT, 16); self.op(C_HOST, 3)
                self.expr(e[3], params); self.op(C_LIT, 16); self.op(C_HOST, 3)
                self.op(C_HOST, 2); self.op(C_LIT, 16); self.op(C_HOST, 2)
                return
            self.expr(e[2], params); self.expr(e[3], params)
            self.op(C_HOST, HOST[op])
            if op not in ("+", "-"):
                self.op(C_LIT, 16); self.op(C_HOST, 2)
            return
        if k == "either":
            else_l = self.label(); end_l = self.label()
            self.expr(e[1], params); self.op(C_ZBRANCH, else_l)
            for x in e[2]: self.expr(x, params)
            self.op(C_BRANCH, end_l); self.op(C_LABEL, else_l)
            for x in e[3]: self.expr(x, params)
            self.op(C_LABEL, end_l); return
        raise ValueError(k)
    def seq(self, seq, params):
        for i, e in enumerate(seq):
            self.expr(e, params)
            if i + 1 < len(seq): self.op(C_DROP)

def emit_direct(task):
    lo = Lower(task)
    lo.op(C_ENTRY, lo.entry)
    for name, params, body in task["funcs"]:
        lo.op(C_LABEL, lo.fids[name])
        for _ in params: lo.op(C_TO_R)
        lo.seq(body, params)
        for _ in params: lo.op(C_FROM_R); lo.op(C_DROP)
        lo.op(C_RET)
    lo.op(C_LABEL, lo.entry)
    lo.seq(task["top"], [])
    lo.op(C_HALT)
    words = [C_MAGIC] + lo.out
    return b"".join(struct.pack("<I", w) for w in words)

def main():
    os.makedirs(OUT, exist_ok=True)
    for t in TASKS:
        base = os.path.join(OUT, t["name"])
        open(base + ".glon", "wb").write(emit_text(t))
        open(base + ".b4b", "wb").write(emit_tagged(t))
        open(base + ".b4c", "wb").write(emit_direct(t))
        with open(base + ".md", "w") as f:
            call = text_seq(t["top"], [])
            f.write(f"# {t['name']}\n\n{t['spec']}\n\n"
                    f"Top-level call: `{call}`\n\n"
                    f"Expected result: `{t['expected']}`.\n")
    print(f"wrote {len(TASKS)} tasks to {OUT}")

if __name__ == "__main__":
    main()
