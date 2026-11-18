#!/usr/bin/env python3
"""Generate and verify shared s/find cases for Glon reference, MASM, and C."""
import argparse
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def glon_repr(data: bytes) -> str:
    return "mk-string [ " + " ".join(str(b) for b in data) + " ]"


def expected(text: bytes, pat: bytes) -> int:
    if not pat:
        return 0
    return text.find(pat)


def run_glon(glon: Path, cases, impl: str):
    src = ["["]
    for text, pat in cases:
        src.append(f"print {impl} {glon_repr(text)} {glon_repr(pat)}")
    src.append("]")
    fd, path = tempfile.mkstemp(prefix="find-cases.", suffix=".glon")
    os.close(fd)
    Path(path).write_text("\n".join(src) + "\n")
    try:
        r = subprocess.run(
            [str(glon), str(ROOT / "glon-lib" / "strings-ext.glon"),
             str(HERE / "find.glon"), path],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    finally:
        Path(path).unlink(missing_ok=True)
    return r


def parse_ints(out: bytes):
    vals = []
    for line in out.splitlines():
        line = line.strip()
        # loader prints #[func] for library definitions; skip non-integers
        try:
            vals.append(int(line))
        except ValueError:
            pass
    return vals


def run_c(cbin: Path, text: bytes, pat: bytes) -> int:
    with tempfile.TemporaryDirectory(prefix="find-c.") as d:
        t = Path(d) / "t"; p = Path(d) / "p"
        t.write_bytes(text); p.write_bytes(pat)
        r = subprocess.run([str(cbin), str(t), str(p)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=30)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace"))
    return int(r.stdout.split()[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--c", required=True, type=Path)
    ap.add_argument("--random", type=int, default=200)
    args = ap.parse_args()
    args.glon = args.glon.resolve()
    args.c = args.c.resolve()

    cases = []
    # Hand-written edge cases.
    cases += [
        (b"web site", b"web"),
        (b"the web is wide", b"web"),
        (b"the web", b"web"),
        (b"the internet", b"web"),
        (b"web web web", b"web"),
        (b"", b""),
        (b"", b"web"),
        (b"web", b""),
        (b"abc", b"abcd"),
        (b"aaaa", b"aa"),
        (b"aaaab", b"aab"),
        (b"a", b"a"),
        (b"ba", b"a"),
        (b"ab", b"a"),
        (b"xxabc", b"abc"),
        (b"\x00web\x80\xff", b"web"),
        (b"\x00web\x80\xff", b"\x80\xff"),
        (b"abcabc", b"bca"),
        (b"abcabc", b"abc"),
        (b"a" * 40 + b"needle" + b"b" * 40, b"needle"),
        (b"a" * 399 + b"z", b"z"),       # last valid start, 400-byte text
    ]
    # Random differential cases, including binary bytes and empty patterns.
    rng = random.Random(0x4D41534D)
    for _ in range(args.random):
        n = rng.randrange(0, 81)
        m = rng.randrange(0, 13)
        text = bytes(rng.randrange(0, 256) for _ in range(n))
        pat = bytes(rng.randrange(0, 256) for _ in range(m))
        cases.append((text, pat))

    # Batch the Glon sources; the parser has a per-block form bound.
    expected_vals = [expected(t, p) for t, p in cases]
    for impl in ("s/find-masm", "s/find-glon"):
        got = []
        for start in range(0, len(cases), 40):
            batch = cases[start:start + 40]
            r = run_glon(args.glon, batch, impl)
            if r.returncode != 0:
                print(r.stderr.decode(errors="replace"))
                return 1
            got.extend(parse_ints(r.stdout))
        if got != expected_vals:
            for i, (a, b) in enumerate(zip(got, expected_vals)):
                if a != b:
                    print(f"FAIL {impl} case {i}: got {a}, expected {b}, "
                          f"text={cases[i][0]!r} pattern={cases[i][1]!r}")
                    break
            return 1
        print(f"PASS {impl}: {len(cases)} cases")

    # C reference over a representative subset (process-per-case is enough).
    sample = list(range(len(cases)))
    for i in sample:
        got = run_c(args.c, *cases[i])
        if got != expected_vals[i]:
            print(f"FAIL c_find case {i}: got {got}, expected {expected_vals[i]}")
            return 1
    print(f"PASS c_find: {len(sample)} cases")

    # Input immutability: print the inputs after a search.  Keep these cases
    # newline-free so the line-oriented capture is unambiguous.
    text = b"\x00abc\x80\xff"
    pat = b"abc"
    src = ("[" + f"r: s/find {glon_repr(text)} {glon_repr(pat)} "
           f"s/print {glon_repr(text)} s/print {glon_repr(pat)} ]")
    fd, path = tempfile.mkstemp(prefix="find-immut.", suffix=".glon")
    os.close(fd); Path(path).write_text(src + "\n")
    try:
        r = subprocess.run([str(args.glon), str(ROOT / "glon-lib" / "strings-ext.glon"),
                            str(HERE / "find.glon"), path],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    finally:
        Path(path).unlink(missing_ok=True)
    lines = r.stdout.splitlines()
    try:
        ti = lines.index(text); pi = lines.index(pat, ti + 1)
    except ValueError:
        print("FAIL input immutability: printed inputs not found")
        return 1
    print("PASS input immutability")

    # Invalid argument type: both implementations must raise SIN! 'type.
    src = ("[ e: judge [ s/find 42 \"x\" ] values [ e ] ]")
    fd, path = tempfile.mkstemp(prefix="find-invalid.", suffix=".glon")
    os.close(fd); Path(path).write_text(src + "\n")
    try:
        r = subprocess.run([str(args.glon), str(ROOT / "glon-lib" / "strings-ext.glon"),
                            str(HERE / "find.glon"), path],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    finally:
        Path(path).unlink(missing_ok=True)
    if b"SIN!" not in r.stdout or b"type" not in r.stdout:
        print("FAIL invalid type: no SIN! type result")
        return 1
    print("PASS invalid type")
    print(f"ALL PASS ({len(cases)} differential cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
