#!/usr/bin/env python3
"""Original-path file replacement acceptance tests.

The expected result is produced independently by Python's bytes.replace(old,
new, 1), never by Glon.  The tests exercise the optional file module, the
reusable file/replace-first operation and the integrated MASM-1 search and
MASM-2 request encoder.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MASM = (
    ROOT / "experiments/masm-vocabulary/find.glon",
    ROOT / "experiments/masm2/u32.glon",
    ROOT / "experiments/masm-vocabulary/file-stream-masm.glon",
)
ATOMIC = ROOT / "modules/file-atomic.glon"
REPLACE = ROOT / "modules/file-replace.glon"

passed = 0
failed = 0


def check(cond, name, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print(f"PASS {name}")
    else:
        failed += 1
        print(f"FAIL {name}" + (f": {detail}" if detail else ""))


def bytes_expr(b):
    if not b:
        return '""'
    parts = [b[i:i + 400] for i in range(0, len(b), 400)]
    exprs = ["mk-string [ " + " ".join(str(x) for x in p) + " ]" for p in parts]
    if len(exprs) == 1:
        return exprs[0]
    return ("s/+ " * (len(exprs) - 1)) + " ".join(exprs)


def program(old, new, path="doc.txt"):
    return f"""[ oldv: {bytes_expr(old)}
newv: {bytes_expr(new)}
file/replace-first "{path}" oldv newv
    func [x] [ s/print "REPLACE_DONE" ]
    func [e] [ raise e ]
]
"""


def run(glon, root, prog, flags=(), libs=MASM, timeout=300,
        grants=("read", "write")):
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-prog.")
    prog_path = Path(td.name) / "run.glon"
    prog_path.write_text(prog)
    grant_args = []
    if "read" in grants:
        grant_args.append("--allow-file-read")
    if "write" in grants:
        grant_args.append("--allow-file-write")
    cmd = [str(glon), "--io-root", str(root), *grant_args,
           *map(str, flags), *map(str, libs), str(ATOMIC), str(REPLACE), str(prog_path)]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
    finally:
        td.cleanup()
    return r


def temps(root):
    return [p.name for p in Path(root).iterdir() if p.name.startswith(".glon-tmp-")]


def case(glon, name, data, old, new, flags=(), libs=MASM, expect_ok=True,
         expect_error_word=None, timeout=300, path="doc.txt", gc=False,
         create=True, grants=("read", "write")):
    td = tempfile.TemporaryDirectory(prefix="glon-inplace.")
    root = Path(td.name)
    if create:
        (root / path).write_bytes(data)
    flags = list(flags)
    if gc:
        flags.append("--gc-report")
    r = run(glon, root, program(old, new, path), flags, libs, timeout, grants)
    got = (root / path).read_bytes() if (root / path).exists() else None
    if expect_ok:
        expected = data.replace(old, new, 1)
        ok = r.returncode == 0 and b"REPLACE_DONE" in r.stdout and got == expected
        detail = (f"rc={r.returncode} got={(got[:80] if got is not None else None)!r} expected={expected[:80]!r} "
                  f"stderr={r.stderr[-300:]!r}")
    else:
        expected = data if create else None
        word = expect_error_word.encode() if expect_error_word else b""
        ok = (r.returncode != 0 and got == expected and
              (not word or word in r.stderr))
        detail = (f"rc={r.returncode} got={(got[:80] if got is not None else None)!r} "
                  f"stderr={r.stderr[-400:]!r}")
    ok = ok and not temps(root)
    check(ok, name, detail)
    td.cleanup()
    return r



def mem_case(glon, name, text, old, new, libs=MASM):
    prog = (f"[ s/print s/replace-first {bytes_expr(text)} {bytes_expr(old)} "
            f"{bytes_expr(new)} ]\n")
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-mem.")
    prog_path = Path(td.name) / "run.glon"
    prog_path.write_text(prog)
    try:
        r = subprocess.run([str(glon), *map(str, libs), str(ATOMIC), str(REPLACE),
                            str(prog_path)], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=120)
    finally:
        td.cleanup()
    expected = text.replace(old, new, 1) + b"\n"
    check(r.returncode == 0 and r.stdout.endswith(expected), "in-memory " + name,
          f"rc={r.returncode} out={r.stdout[:80]!r} expected={expected[:80]!r}")


def stream_to_case(glon, name, data, old, new, libs=MASM):
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-streamto.")
    root = Path(td.name)
    (root / "in.txt").write_bytes(data)
    prog = """[ io/open "in.txt" 'read
    func [in] [
        io/open "out.txt" 'write
            func [out] [
                stream/replace-first-to in out %s %s
                    func [x] [ io/close out func [y] [ s/print "STREAM_TO_DONE" ] func [e] [ raise e ] ]
                    func [e] [ io/close out func [y] [ raise e ] func [z] [ raise e ] ]
            ]
            func [e] [ io/close in func [x] [ raise e ] func [y] [ raise e ] ]
    ]
    func [e] [ raise e ]
]
""" % (bytes_expr(old), bytes_expr(new))
    prog_path = Path(td.name) / "run.glon"
    prog_path.write_text(prog)
    r = run(glon, root, prog, libs=libs)
    got = (root / "out.txt").read_bytes() if (root / "out.txt").exists() else None
    expected = data.replace(old, new, 1)
    check(r.returncode == 0 and b"STREAM_TO_DONE" in r.stdout and got == expected
          and not temps(root), "streaming-to " + name,
          f"rc={r.returncode} got={got[:80] if got else None!r} stderr={r.stderr[-300:]!r}")
    td.cleanup()


def main():
    global passed, failed
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    args = ap.parse_args()
    glon = args.glon.resolve()

    # Original Cookbook shape and first-occurrence semantics.
    case(glon, "cookbook web -> rebol", b"the web is wide", b"web", b"rebol")
    case(glon, "match at beginning", b"web site", b"web", b"rebol")
    case(glon, "match at end", b"the web", b"web", b"rebol")
    case(glon, "multiple matches: first only", b"web web web", b"web", b"rebol")
    case(glon, "no match", b"the internet", b"web", b"rebol")
    case(glon, "empty file", b"", b"web", b"rebol")
    case(glon, "empty replacement", b"a web b web", b"web", b"")
    case(glon, "shorter replacement", b"the world wide web", b"world wide web", b"web")
    case(glon, "longer replacement", b"a web b", b"web", b"rebol-rebol-rebol")
    case(glon, "replacement contains the pattern", b"aaa", b"a", b"aa")

    # Empty search is rejected by contract; original is untouched.
    case(glon, "empty search pattern rejected", b"web site", b"", b"x",
         expect_ok=False, expect_error_word="empty-pattern")

    # Arbitrary binary content, including NUL and high bytes.
    case(glon, "binary content around match", b"\x00web\x80\xff tail", b"web", b"rebol")
    case(glon, "binary pattern and replacement",
         b"left\x00\x01right\x00\x01tail", b"\x00\x01", b"\xff\xfe")
    case(glon, "NUL and CR/LF preservation",
         b"\x00\r\nweb\r\n\x00", b"web", b"REBOL")

    # Match crossing the 400-byte stream boundary.
    case(glon, "match crossing chunk boundary",
         b"a" * 398 + b"web" + b"b" * 100, b"web", b"rebol")
    case(glon, "match entirely in the carried tail",
         b"a" * 399 + b"web" + b"b" * 100, b"web", b"rebol")

    # Short reads/writes must loop rather than truncate.
    case(glon, "short reads and writes",
         b"prefix web suffix " + b"z" * 1200, b"web", b"rebol",
         flags=("--io-short", "3"))

    # Failure cases preserve the original and leave no temp file.
    case(glon, "missing file", b"", b"web", b"rebol", path="missing.txt",
         create=False, expect_ok=False, expect_error_word="not-found")
    case(glon, "write failure via host cap", b"x" * 100, b"x", b"y",
         flags=("--io-max-file", "10"), expect_ok=False,
         expect_error_word="too-large")
    case(glon, "injected close failure", b"the web", b"web", b"rebol",
         flags=("--io-fail-close",), expect_ok=False,
         expect_error_word="io-error")
    case(glon, "injected rename failure", b"the web", b"web", b"rebol",
         flags=("--io-fail-rename",), expect_ok=False,
         expect_error_word="io-error")
    case(glon, "cancellation before reply", b"the web", b"web", b"rebol",
         flags=("--io-cancel-before-reply",), expect_ok=False,
         expect_error_word="cancel")

    # Capability and confinement failures.
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-perm.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"the web")
    r = run(glon, root, program(b"web", b"rebol"), grants=("write",))
    check(r.returncode != 0 and (root / "doc.txt").read_bytes() == b"the web"
          and not temps(root), "read capability denied",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    td = tempfile.TemporaryDirectory(prefix="glon-inplace-wperm.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"the web")
    r = run(glon, root, program(b"web", b"rebol"), grants=("read",))
    check(r.returncode != 0 and (root / "doc.txt").read_bytes() == b"the web"
          and not temps(root), "write capability denied",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    td = tempfile.TemporaryDirectory(prefix="glon-inplace-escape.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"the web")
    r = run(glon, root, program(b"web", b"rebol", path="../doc.txt"))
    check(r.returncode != 0 and (root / "doc.txt").read_bytes() == b"the web"
          and not temps(root), "out-of-root path rejected",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    # Temp creation failure: read-only directory, source readable.
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-tempfail.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"the web")
    os.chmod(root, 0o500)
    try:
        r = run(glon, root, program(b"web", b"rebol"))
    finally:
        os.chmod(root, 0o700)
    check(r.returncode != 0 and (root / "doc.txt").read_bytes() == b"the web"
          and not temps(root), "temporary creation failure preserves original",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    # Symlink and hard-link behaviour.
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-link.")
    root = Path(td.name)
    real = root / "real.txt"
    real.write_bytes(b"the web")
    (root / "doc.txt").symlink_to("real.txt")
    r = run(glon, root, program(b"web", b"rebol"))
    check(r.returncode != 0 and real.read_bytes() == b"the web"
          and (root / "doc.txt").is_symlink() and not temps(root),
          "symlink source rejected, target untouched",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    td = tempfile.TemporaryDirectory(prefix="glon-inplace-hard.")
    root = Path(td.name)
    original = root / "original.txt"
    original.write_bytes(b"the web")
    os.link(original, root / "doc.txt")
    r = run(glon, root, program(b"web", b"rebol"))
    check(r.returncode == 0 and (root / "doc.txt").read_bytes() == b"the rebol"
          and original.read_bytes() == b"the web" and not temps(root),
          "hard-linked source: original path replaced, other link unchanged",
          f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    # Concurrent source modification must be detected at commit.
    td = tempfile.TemporaryDirectory(prefix="glon-inplace-stale.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"a" * 2000 + b"web" + b"b" * 2000)
    prog = Path(td.name) / "run.glon"
    prog.write_text(program(b"web", b"rebol"))
    cmd = [str(glon), "--io-root", str(root), "--allow-file-read",
           "--allow-file-write", "--io-delay-ms", "20",
           *map(str, MASM), str(ATOMIC), str(REPLACE), str(prog)]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    deadline = time.time() + 10
    seen = False
    while time.time() < deadline:
        if temps(root):
            seen = True
            break
        if proc.poll() is not None:
            break
        time.sleep(0.005)
    if seen:
        (root / "doc.txt").write_bytes(b"concurrent change")
    out, err = proc.communicate(timeout=120)
    check(seen and proc.returncode != 0 and b"stale" in err
          and (root / "doc.txt").read_bytes() == b"concurrent change"
          and not temps(root),
          "concurrent modification detected, source preserved",
          f"seen={seen} rc={proc.returncode} stderr={err[-400:]!r}")
    td.cleanup()

    # Reusable in-memory and streaming-to layers, not just the in-place wrapper.
    mem_case(glon, "cookbook", b"the web is wide", b"web", b"rebol")
    mem_case(glon, "multiple first only", b"web web", b"web", b"rebol")
    mem_case(glon, "binary", b"\x00web\xff", b"web", b"rebol")
    stream_to_case(glon, "distinct output", b"the web is wide", b"web", b"rebol")
    stream_to_case(glon, "boundary", b"a" * 398 + b"web" + b"b" * 50,
                   b"web", b"rebol")

    # Representative sizes (the 1 MiB and 4 MiB sizes are run separately by
    # the benchmark driver to keep this acceptance run quick).
    for size in (1024, 65536):
        data = b"a" * (size - 6) + b"web" + b"bbb"
        case(glon, f"{size}-byte file", data, b"web", b"rebol", timeout=300)

    print(f"{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
