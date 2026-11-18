#!/usr/bin/env python3
"""Acceptance tests for the optional glon-io-file module.

The module binary is required.  An optional minimal binary proves that the
same source does not gain file vocabulary without MODULES=file.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURES = HERE / "fixtures"
RECIPE = ROOT / "modules" / "recipe-0004.glon"
IN_MEMORY = FIXTURES / "in_memory.glon"

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


def run(glon, args, cwd=None):
    return subprocess.run([str(glon), *map(str, args)], cwd=cwd,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def with_root(data, flags, source, extra=(), allow=None):
    td = tempfile.TemporaryDirectory(prefix="glon-io-test.")
    root = Path(td.name)
    if data is not None:
        (root / "doc.txt").write_bytes(data)
    grants = []
    if allow is None:
        grants = ["--allow-file-read", "--allow-file-write"]
    else:
        if "read" in allow:
            grants.append("--allow-file-read")
        if "write" in allow:
            grants.append("--allow-file-write")
    r = run(GLON, ["--io-root", root, *grants, *extra, *flags, source])
    return td, root, r


def recipe_case(name, data, expected, flags=(), extra=()):
    td, root, r = with_root(data, flags, RECIPE, extra)
    got = (root / "doc.txt").read_bytes() if (root / "doc.txt").exists() else None
    check(r.returncode == 0 and got == expected and b"RECIPE_DONE" in r.stdout,
          name, f"rc={r.returncode} got={got!r} stderr={r.stderr[-300:]!r}")
    td.cleanup()


def failure_case(name, data, flags, source, needle, extra=(), allow=None):
    td, root, r = with_root(data, flags, source, extra, allow)
    check(r.returncode != 0 and needle.encode() in r.stderr,
          name, f"rc={r.returncode} stderr={r.stderr[-500:]!r}")
    td.cleanup()
    return root, r


def main():
    global GLON
    ap = argparse.ArgumentParser()
    ap.add_argument("--glon", required=True, type=Path)
    ap.add_argument("--minimal", type=Path)
    args = ap.parse_args()
    GLON = args.glon.resolve()
    minimal = args.minimal.resolve() if args.minimal else None

    # The six original in-memory replacement cases remain independent of I/O.
    td = tempfile.TemporaryDirectory(prefix="glon-io-mem.")
    mem = run(GLON, ["--io-root", td.name, "--allow-file-read",
                     "--allow-file-write", IN_MEMORY])
    expected_mem = (b"rebol site\nthe rebol is wide\nthe rebol\n"
                    b"the internet\nrebol web web\n\n")
    check(mem.returncode == 0 and mem.stdout == expected_mem,
          "in-memory six replacement cases", f"rc={mem.returncode} out={mem.stdout!r}")
    td.cleanup()

    # Recipe 0004 via real file read -> Glon replace-first -> file write.
    recipe_case("beginning", b"web site", b"rebol site")
    recipe_case("middle", b"the web is wide", b"the rebol is wide")
    recipe_case("end", b"the web", b"the rebol")
    recipe_case("no match", b"the internet", b"the internet")
    recipe_case("multiple: first only", b"web web web", b"rebol web web")
    recipe_case("empty file", b"", b"")
    recipe_case("binary preservation", b"\x00web\x80\xff", b"\x00rebol\x80\xff")
    recipe_case("exact bytes outside replacement",
                b"left\x00right web tail\xff", b"left\x00right rebol tail\xff")
    recipe_case("multi-chunk file", b"web" + b"z" * 1000,
                b"rebol" + b"z" * 1000)
    recipe_case("whole-file safe boundary", b"web" + b"z" * 1019,
                b"rebol" + b"z" * 1019)
    failure_case("whole-file cap exceeded", b"x" * 1025, (),
                 RECIPE, "too-large")
    failure_case("unsafe configured whole-file cap rejected", b"web site", (),
                 RECIPE, "exceeds measured safe limit",
                 extra=("--io-max-file", "2048"))

    # Short reads/writes must loop, not truncate or duplicate bytes.
    recipe_case("short reads and writes", b"web site and more text",
                b"rebol site and more text", extra=("--io-short", "3"))

    # Errors are recoverable SIN!s reported by the Glon failure callback.
    failure_case("missing file", None, (), RECIPE, "not-found")
    failure_case("read permission denied", b"web site",
                 (), RECIPE, "permission", allow=("write",))
    td, root, r = with_root(b"web site", (), RECIPE, allow=("read",))
    check(r.returncode != 0 and b"permission" in r.stderr and
          (root / "doc.txt").read_bytes() == b"web site",
          "write permission denied", f"rc={r.returncode} stderr={r.stderr[-500:]!r}")
    check((root / "doc.txt").read_bytes() == b"web site",
          "write denial leaves file unchanged")
    td.cleanup()

    failure_case("path traversal rejected", b"web site", (),
                 FIXTURES / "traversal.glon", "permission")
    failure_case("absolute path rejected", b"web site", (),
                 FIXTURES / "absolute.glon", "permission")

    td = tempfile.TemporaryDirectory(prefix="glon-io-link.")
    root = Path(td.name)
    (root / "doc.txt").write_bytes(b"web site")
    os.symlink("/etc/passwd", root / "link")
    r = run(GLON, ["--io-root", root, "--allow-file-read", "--allow-file-write",
                   FIXTURES / "symlink.glon"])
    check(r.returncode != 0 and b"permission" in r.stderr,
          "symlink escape rejected", f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
    td.cleanup()

    failure_case("stale reply id rejected", b"web site", (),
                 FIXTURES / "stale_reply.glon", "stale-reply")

    failure_case("malformed request is a protocol failure", b"web site", (),
                 FIXTURES / "bad_frame.glon", "bad request magic")

    failure_case("invalid handle", b"web site", (),
                 FIXTURES / "invalid_handle.glon", "invalid-handle")
    failure_case("maximum handles", b"web site", (),
                 FIXTURES / "two_handles.glon", "too-large",
                 extra=("--io-max-handles", "1", "--io-report"))
    failure_case("maximum file", b"x" * 1000, (),
                 RECIPE, "too-large", extra=("--io-max-file", "800"))

    # Cleanup after a job leaves a handle open.
    td, root, r = with_root(b"web site", (), FIXTURES / "left_open.glon",
                             extra=("--io-report",))
    check(r.returncode == 0 and b"LEFT_OPEN" in r.stdout and
          b"handles_open=1 cleanup_closed=1" in r.stderr,
          "job-end cleanup closes open handle", f"rc={r.returncode} stderr={r.stderr!r}")
    td.cleanup()

    # Cancellation discards a reply and still shuts the job down.
    td, root, r = with_root(b"web site", (), FIXTURES / "left_open.glon",
                             extra=("--io-delay-ms", "150",
                                    "--io-cancel-before-reply", "--io-report"))
    check(r.returncode != 0 and b"cancelling request" in r.stderr and
          b"handles_open=0" in r.stderr,
          "request cancellation path", f"rc={r.returncode} stderr={r.stderr!r}")
    td.cleanup()

    # Async progress: a second host-dispatched Glon job runs while request 1 is
    # deliberately pending.  This is host-level concurrency, not M1 suspension.
    td, root, r = with_root(b"web site", (), RECIPE,
                             extra=("--io-delay-ms", "150",
                                    "--io-progress", FIXTURES / "progress.glon",
                                    "--io-report"))
    out = r.stdout
    err = r.stderr
    progress_at = out.find(b"PROGRESS_DONE")
    recipe_at = out.find(b"RECIPE_DONE")
    pending_log = err.find(b"progress job completed while request 1 was pending")
    reply_log = err.find(b"reply 1 status")
    check(r.returncode == 0 and progress_at >= 0 and recipe_at > progress_at and
          0 <= pending_log < reply_log,
          "progress job runs during pending I/O",
          f"rc={r.returncode} out={out!r} err={err!r}")
    td.cleanup()

    if minimal:
        td = tempfile.TemporaryDirectory(prefix="glon-io-min.")
        root = Path(td.name)
        (root / "doc.txt").write_bytes(b"web site")
        r = run(minimal, [RECIPE])
        check(r.returncode != 0 and b"RECIPE_DONE" not in r.stdout and
              (root / "doc.txt").read_bytes() == b"web site",
              "minimal build has no file vocabulary",
              f"rc={r.returncode} stderr={r.stderr[-300:]!r}")
        td.cleanup()

    print(f"\n{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
