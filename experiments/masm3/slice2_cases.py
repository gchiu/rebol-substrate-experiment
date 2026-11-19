#!/usr/bin/env python3
"""MASM-3 s/slice2 differential, boundary, GC/lifetime and task tests."""
import argparse
from pathlib import Path
import random
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FULL = HERE / "slice2.glon"
MIN = HERE / "slice2-min.glon"
COOP = HERE / "coop-core.glon"


def run(glon, src, extra=(), flags=()):
    with tempfile.TemporaryDirectory(prefix="masm3-slice2.") as d:
        path = Path(d) / "t.glon"
        path.write_text(src)
        return subprocess.run([str(glon), *flags, *extra, str(path)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=300)


def block(b):
    if not b:
        return '""'
    parts = [b[i:i + 500] for i in range(0, len(b), 500)]
    exprs = ["mk-string [ " + " ".join(str(x) for x in p) + " ]" for p in parts]
    if len(exprs) == 1:
        return exprs[0]
    return ("s/+ " * (len(exprs) - 1)) + " ".join(exprs)


def cases():
    out = []
    def add(a, b, start, n, exp):
        out.append((a, b, start, n, exp))
    # empty / single-byte / n = 0
    add(b"", b"", 0, 0, b"")
    add(b"", b"a", 0, 1, b"a")
    add(b"a", b"", 0, 1, b"a")
    add(b"a", b"b", 0, 2, b"ab")
    add(b"abc", b"def", 3, 0, b"")
    add(b"abc", b"def", 6, 0, b"")
    # every split position
    a, b = b"abcdefgh", b"ijklmnop"
    for s in range(len(a) + len(b) + 1):
        add(a, b, s, 0, b"")
        if s < len(a) + len(b):
            add(a, b, s, 1, (a + b)[s:s + 1])
            add(a, b, s, len(a) + len(b) - s, (a + b)[s:])
    # boundaries around 16-cell allocation rounding
    for n in (14, 15, 16, 17, 31, 32, 33, 399, 400, 401):
        a = bytes((i * 7 + 3) & 255 for i in range(n))
        b = bytes((i * 11 + 5) & 255 for i in range(n))
        add(a, b, 0, n, a)
        add(a, b, n, n, b)
        add(a, b, n - 1, 2, (a + b)[n - 1:n + 1])
        add(a, b, 3, n, (a + b)[3:3 + n])
    # arbitrary binary content containing NUL and high bytes
    a = bytes(range(256))
    b = bytes(reversed(range(256)))
    for s, n in ((0, 512), (255, 2), (256, 1), (100, 300), (511, 1)):
        add(a, b, s, n, (a + b)[s:s + n])
    # lengths that cross the payload rounding and several workspace cells
    for total in (384, 512, 768):
        a = bytes((i * 13 + 1) & 255 for i in range(total // 2))
        b = bytes((i * 17 + 9) & 255 for i in range(total - len(a)))
        add(a, b, 0, total, a + b)
        add(a, b, 1, total - 2, (a + b)[1:-1])
        add(a, b, len(a) - 1, 3, (a + b)[len(a) - 1:len(a) + 2])
    # randomised
    rng = random.Random(0x4D41534D33)
    for _ in range(60):
        la = rng.randrange(0, 120)
        lb = rng.randrange(0, 120)
        a = bytes(rng.randrange(256) for _ in range(la))
        b = bytes(rng.randrange(256) for _ in range(lb))
        total = la + lb
        s = rng.randrange(0, total + 1)
        n = rng.randrange(0, total - s + 1)
        add(a, b, s, n, (a + b)[s:s + n])
    return out


def run_differential(glon):
    all_cases = cases()
    batches = []
    part, cells = [], 0
    for c in all_cases:
        need = len(c[0]) + len(c[1]) + len(c[4]) + 20
        if part and cells + need > 600:
            batches.append(part); part, cells = [], 0
        part.append(c); cells += need
    if part: batches.append(part)
    checked = 0
    for bi, part in enumerate(batches):
        lines = ["["]
        lines.append("chk: func [a b s n e] ["
                     " ra: s/slice2-glon a b s n"
                     " rb: s/slice2-masm a b s n"
                     " either s/= ra e ["
                     "   either s/= rb e [ 1 ] [ raise create-sin 'test 'masm-mismatch e ]"
                     " ] [ raise create-sin 'test 'glon-mismatch e ] ]")
        for j, (a, b, s, n, e) in enumerate(part):
            lines.append(f"a{j}: {block(a)}")
            lines.append(f"b{j}: {block(b)}")
            lines.append(f"e{j}: {block(e)}")
            lines.append(f"chk a{j} b{j} {s} {n} e{j}")
        lines += ['s/print "SLICE2_OK"', "]"]
        r = run(glon, "\n".join(lines) + "\n", extra=(FULL,))
        if r.returncode != 0 or b"SLICE2_OK" not in r.stdout:
            print(r.stdout.decode(errors="replace"))
            print(r.stderr.decode(errors="replace")[-2000:])
            return 1
        checked += len(part)
    print(f"PASS s/slice2 differential: {checked} cases")

    # selected alias is the MASM implementation
    src = """[
r: s/slice2 "Hello " "world" 6 5
s/print either s/= r "world" [ "ALIAS_OK" ] [ "ALIAS_BAD" ]
]
"""
    r = run(glon, src, extra=(FULL,))
    ok = r.returncode == 0 and b"ALIAS_OK" in r.stdout
    print(("PASS" if ok else "FAIL") + " selected s/slice2 alias")
    return 0 if ok else 1


def run_errors(glon):
    src = """[
ex: masm 1 [ LIT 16 MOD LIT 14 EQ LIT 16 MUL ARITY 1 EXIT ]
chkerr: func [v] [ either ex v [ 1 ] [ raise create-sin 'test 'expected-error v ] ]
chkerr judge [ s/slice2-masm 5 "abc" 0 1 ]
chkerr judge [ s/slice2-masm "abc" 5 0 1 ]
chkerr judge [ s/slice2-masm "abc" "def" 'x 1 ]
chkerr judge [ s/slice2-masm "abc" "def" 0 "x" ]
chkerr judge [ s/slice2-masm "abc" "def" -1 1 ]
chkerr judge [ s/slice2-masm "abc" "def" 0 -1 ]
chkerr judge [ s/slice2-masm "abc" "def" 7 1 ]
chkerr judge [ s/slice2-masm "abc" "def" 6 1 ]
chkerr judge [ s/slice2-masm "abc" "def" 0 7 ]
chkerr judge [ s/slice2-masm "abc" "def" 3 4 ]
chkerr judge [ s/slice2-masm "abc" "def" 0 999999999 ]
chkerr judge [ s/slice2-glon 5 "abc" 0 1 ]
chkerr judge [ s/slice2-glon "abc" "def" 7 1 ]
s/print "ERRORS_OK"
]
"""
    r = run(glon, src, extra=(FULL,))
    ok = r.returncode == 0 and b"ERRORS_OK" in r.stdout
    print(("PASS" if ok else "FAIL") + " s/slice2 invalid arguments raise SIN!")
    if not ok:
        print(r.stdout.decode(errors="replace"))
        print(r.stderr.decode(errors="replace")[-2000:])
    return 0 if ok else 1


def run_max_size(glon):
    total = 2048
    a = bytes((i * 3 + 1) & 255 for i in range(total // 2))
    b = bytes((i * 5 + 2) & 255 for i in range(total - len(a)))
    src = "[\n" + f"a: {block(a)}\nb: {block(b)}\n" + f"r: s/slice2-masm a b 0 {total}\n"
    src += "s/print s/from-int s/length r\ns/print either = 1 s/at r 0 [ \"MAX_OK\" ] [ \"MAX_BYTE_BAD\" ]\n]\n"
    r = run(glon, src, extra=(FULL,))
    ok = r.returncode == 0 and b"MAX_OK" in r.stdout
    print(("PASS" if ok else "FAIL") + f" s/slice2 max supported probe ({total} bytes)")
    if not ok:
        print(r.stdout.decode(errors="replace")); print(r.stderr.decode(errors="replace")[-1000:])
    return 0 if ok else 1


def run_lifetime(glon):
    # Keep a result and an unrelated string across heavy allocation pressure.
    src = """[
a: "0123456789abcdefghij"
b: "ABCDEFGHIJKLMNOPQRST"
keep: s/slice2-masm a b 5 20
sentinel: "sentinel"
"""
    src += "\n".join('x: mk-string [ 65 66 67 68 ]' for _ in range(120))
    src += """
s/print either s/= keep "56789abcdefghijABCDE" [ "LIVE_OK" ] [ "LIVE_BAD" ]
s/print either s/= sentinel "sentinel" [ "SENTINEL_OK" ] [ "SENTINEL_BAD" ]
s/print either s/= a "0123456789abcdefghij" [ "A_OK" ] [ "A_BAD" ]
s/print either s/= b "ABCDEFGHIJKLMNOPQRST" [ "B_OK" ] [ "B_BAD" ]
]
"""
    r = run(glon, src, extra=(FULL,), flags=("--gc-report",))
    required = (b"LIVE_OK", b"SENTINEL_OK", b"A_OK", b"B_OK")
    ok = r.returncode == 0 and all(x in r.stdout for x in required)
    collected = b"collect=" in r.stderr
    print(("PASS" if ok else "FAIL") + " s/slice2 result survives allocation pressure"
          + (" (GC ran)" if collected else " (no GC observed)"))
    if not ok:
        print(r.stdout.decode(errors="replace"))
        print(r.stderr.decode(errors="replace")[-2000:])
    return 0 if ok else 1


def run_coop(glon):
    src = """[
reset-tasks
s1: "0123456789abcdefghij"
s2: "ABCDEFGHIJKLMNOPQRST"
ra: "" rb: ""
task-a: spawn [
    ra: s/slice2 s1 s2 5 20
    yield
"""
    src += "\n".join('    x: mk-string [ 120 120 120 120 120 120 120 120 ]' for _ in range(60))
    src += """
    yield
    ra: s/+ ra ""
]
task-b: spawn [
    rb: s/slice2 s1 s2 0 30
    yield
"""
    src += "\n".join('    x: mk-string [ 121 121 121 121 121 121 121 121 ]' for _ in range(30))
    src += """
    yield
    rb: s/+ rb ""
]
run-tasks
s/print either s/= ra "56789abcdefghijABCDE" [ "COOP_A_OK" ] [ "COOP_A_BAD" ]
s/print either s/= rb "0123456789abcdefghijABCDEFGHIJ" [ "COOP_B_OK" ] [ "COOP_B_BAD" ]
]
"""
    r = run(glon, src, extra=(MIN, COOP), flags=("--gc-report",))
    ok = r.returncode == 0 and b"COOP_A_OK" in r.stdout and b"COOP_B_OK" in r.stdout
    print(("PASS" if ok else "FAIL") + " s/slice2 under cooperative tasks/yield")
    if not ok:
        print(r.stdout.decode(errors="replace"))
        print(r.stderr.decode(errors="replace")[-2000:])
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    args = ap.parse_args()
    glon = args.glon.resolve()
    failures = 0
    failures += run_differential(glon)
    failures += run_errors(glon)
    failures += run_max_size(glon)
    failures += run_lifetime(glon)
    failures += run_coop(glon)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
