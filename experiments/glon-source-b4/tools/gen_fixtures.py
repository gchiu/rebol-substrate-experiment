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
        name="fib",
        spec="Return the n-th Fibonacci number by naive recursion.",
        funcs=[("fib", ["n"], [
            ("either", ("bin", "<", ("param", 0), ("int", 2)),
             [("param", 0)],
             [("bin", "+",
               ("call", "fib", [("bin", "-", ("param", 0), ("int", 1))]),
               ("call", "fib", [("bin", "-", ("param", 0), ("int", 2))]))])
        ])],
        top=[("call", "fib", [("int", 18)])],
        expected=2584,
    ),
    dict(
        name="sum",
        spec="Sum 0..n with a two-parameter recursive accumulator.",
        funcs=[("sum", ["n", "acc"], [
            ("either", ("bin", "=", ("param", 0), ("int", 0)),
             [("param", 1)],
             [("call", "sum", [
                 ("bin", "-", ("param", 0), ("int", 1)),
                 ("bin", "+", ("param", 1), ("param", 0))])])
        ])],
        top=[("call", "sum", [("int", 500), ("int", 0)])],
        expected=125250,
    ),
    dict(
        name="max3",
        spec="Return the maximum of three integers using comparisons and nested either.",
        funcs=[("max3", ["a", "b", "c"], [
            ("either", ("bin", ">", ("param", 0), ("param", 1)),
             [("either", ("bin", ">", ("param", 0), ("param", 2)),
               [("param", 0)], [("param", 2)])],
             [("either", ("bin", ">", ("param", 1), ("param", 2)),
               [("param", 1)], [("param", 2)])])
        ])],
        top=[("call", "max3", [("int", 7), ("int", 3), ("int", 5)])],
        expected=7,
    ),
    dict(
        name="fact",
        spec="Return n! by recursion, exercising multiplication.",
        funcs=[("fact", ["n"], [
            ("either", ("bin", "<", ("param", 0), ("int", 2)),
             [("int", 1)],
             [("bin", "*", ("param", 0),
               ("call", "fact", [("bin", "-", ("param", 0), ("int", 1))]))])
        ])],
        top=[("call", "fact", [("int", 10)])],
        expected=3628800,
    ),
    dict(
        name="diff",
        spec="Return the absolute difference of two integers.",
        funcs=[("diff", ["a", "b"], [
            ("either", ("bin", "<", ("param", 0), ("param", 1)),
             [("bin", "-", ("param", 1), ("param", 0))],
             [("bin", "-", ("param", 0), ("param", 1))])
        ])],
        top=[("call", "diff", [("int", 17), ("int", 42)])],
        expected=25,
    ),
    dict(
        name="nested",
        spec="Exercise nested calls and two functions.",
        funcs=[
            ("inc", ["x"], [("bin", "+", ("param", 0), ("int", 1))]),
            ("twice", ["x"], [("call", "inc", [("call", "inc", [("param", 0)])])]),
        ],
        top=[("call", "twice", [("int", 5)])],
        expected=7,
    ),
    dict(
        name="forward",
        spec="Exercise a forward reference: a calls b, and b is defined later.",
        funcs=[
            ("a", ["x"], [("call", "b", [("param", 0)])]),
            ("b", ["x"], [("bin", "+", ("param", 0), ("int", 1))]),
        ],
        top=[("call", "a", [("int", 41)])],
        expected=42,
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
            f.write(f"# {t['name']}\n\n{t['spec']}\n\nExpected result: `{t['expected']}`.\n")
    print(f"wrote {len(TASKS)} tasks to {OUT}")

if __name__ == "__main__":
    main()
