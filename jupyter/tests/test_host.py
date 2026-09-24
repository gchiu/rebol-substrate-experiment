"""jupyter/tests/test_host.py -- host-level tests for glon-kernel-host.

No Jupyter: the host is driven through glon_kernel.host.Host (stdlib only), which
frames requests and reads structured results. Every expectation below is about
the host's structured outcome ("status", "detail", "values", "sin") or the
exact bytes a cell printed; nothing parses rendered result text.

Run: python3 jupyter/tests/test_host.py   (or: make host-test)
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # jupyter/
from glon_kernel.host import Host, HostDied, ROOT  # noqa: E402  (stdlib only; no Jupyter)

failures = 0


def check(cond, msg):
    global failures
    print(("  ok: " if cond else "  FAIL: ") + msg)
    if not cond:
        failures += 1


def ok(res, *values):
    return res["status"] == "OK" and res["values"] == list(values)


def main():
    print("glon-kernel-host: persistent session host tests")

    # ---- prelude: verbatim copies of common.glon definitions -----------------
    common = open(os.path.join(ROOT, "demo", "shop", "common.glon"), encoding="utf-8").read()
    prelude = open(os.path.join(ROOT, "jupyter", "prelude.glon"), encoding="utf-8").read()
    lines = prelude.split("\n")
    body = "\n".join(lines[lines.index("[") + 1:len(lines) - 1 - lines[::-1].index("]")])  # inside the outer [ ]
    defs = [d.strip("\n") for d in re.split(r"\n\s*\n", body) if d.strip()]
    names = [d.split(":", 1)[0] for d in defs]
    check(names == ["mk-string", "str-eq", "get", "lambda", "does", "block-len", "block-at",
                    "select", "select-at"] and all(d in common for d in defs),
          "P0: the prelude is exactly 9 definitions, each a verbatim copy from common.glon")

    h = Host()
    check(h.ready["status"] == "OK", "A0: the host starts and loads prelude + case (%s)" % h.ready["session"])

    # ---- A-C: execution and persistence ---------------------------------------
    check(ok(h.execute("1"), "1"), "A: `1` returns one result, 1")
    check(ok(h.execute("x: 21"), "21"), "B1: cell 1 `x: 21`")
    check(ok(h.execute("* x 2"), "42"), "B2: cell 2 `* x 2` -> 42 (same runtime, not reinitialised)")
    check(ok(h.execute("counter: func [] [ n: 0  func [] [ n: + n 1 ] ]  c: counter  c"), "1"),
          "C1: a closure counter is created (1)")
    check(ok(h.execute("c"), "2") and ok(h.execute("c"), "3"), "C2: later cells call it: its state persists (2, 3)")

    # ---- prelude words work ------------------------------------------------------
    check(ok(h.execute('str-eq "tea" "tea"'), "1"), "P1: string literals + str-eq (prelude)")
    check(ok(h.execute("size: func [n] [ case [ [< n 10] [ 'small ] [1] [ 'large ] ] ]  size 50"), "large"),
          "P2: CASE (case.glon)")
    check(ok(h.execute("g: does [ x ]  g"), "21"), "P3: does / lambda (prelude)")

    # ---- D: print goes to stdout, attributed to its cell --------------------------
    r = h.execute("print 7  print 8  9")
    check(ok(r, "9") and r["stdout"] == b"7\n8\n", "D: print writes to stdout (7, 8), separate from the result (9)")
    check(h.execute("10")["stdout"] == b"", "D2: the next cell's stdout is empty (exact attribution)")

    # ---- E-F: SIN! -----------------------------------------------------------------
    r = h.execute("raise create-sin 'demo 'oops 7")
    check(r["status"] == "UNCAUGHT_SIN" and r["sin"] == {"type": "demo", "id": "oops", "arg": "7"} and not r["values"],
          "E: an uncaught SIN! is structured UNCAUGHT_SIN {type demo, id oops, arg 7}")
    check(ok(h.execute("* x 2"), "42"), "E2: earlier state still works after it")
    r = h.execute("judge [ raise create-sin 'demo 'oops 7 ]")
    check(ok(r, "#[SIN! demo oops 7]") and r["sin"] is None, "F: a judged SIN! is an ordinary OK result")

    # ---- D-parse: parse error ----------------------------------------------------
    before = h.execute("0")["session"]
    r = h.execute("y: [ unclosed")
    check(r["status"] == "PARSE_ERROR" and r["detail"] == "syntax", "PE1: a malformed cell is PARSE_ERROR / syntax")
    check(r["session"]["loader_free"] == before["loader_free"] and r["session"]["symbols"] == before["symbols"],
          "PE2: it consumed no loader heap and no symbols")
    check(ok(h.execute("* x 2"), "42") and ok(h.execute("c"), "4"), "PE3: earlier state still works (42, c -> 4)")

    # ---- K: generic machine halt ---------------------------------------------------
    r = h.execute("undefined-word")
    check(r["status"] == "HALT" and r["detail"] == "machine" and r["stderr_bytes"] > 0,
          "K1: an unset word is HALT / machine, with diagnostics on stderr")
    check(ok(h.execute("x"), "21"), "K2: the session continues after a halt (x -> 21)")

    # ---- empty / whitespace / comment-only / multi-line -----------------------------
    check(ok(h.execute(""), "none") and ok(h.execute("   \n\t "), "none")
          and ok(h.execute(";; just a comment"), "none"),
          "N0: empty, whitespace-only and comment-only cells are OK; like any empty Glon program they yield none")
    check(ok(h.execute("double: func [x] [\n    * x 2\n]\n;; a comment\ndouble 21\n"), "42"),
          "N1: a multi-line cell with comments runs as one cell (42)")
    h.quit()

    # ---- G: context full -----------------------------------------------------------
    h = Host()
    h.execute("keep: 21  kf: func [n] [ * n 2 ]")
    r = None
    for k in range(40):
        r = h.execute(" ".join("cg%d-%d: %d" % (k, j, j) for j in range(10)))
        if r["status"] != "OK":
            break
    check(r["status"] == "RESOURCE_ERROR" and r["detail"] == "context_full",
          "G1: filling the global context ends in RESOURCE_ERROR / context_full")
    check(r["session"]["globals"] == r["session"]["global_capacity"] == 256,
          "G2: the context is exactly full (256 of 256), not overrun")
    check(ok(h.execute("kf keep"), "42") and ok(h.execute("keep: 5  keep"), "5"),
          "G3: earlier bindings still work and can be updated")
    check(h.execute("brand-new: 1")["detail"] == "context_full", "G4: further new bindings keep failing cleanly")
    h.quit()

    # ---- H: symbol table full ------------------------------------------------------
    h = Host()
    h.execute("keep: 21")
    r = None
    for k in range(40):
        r = h.execute("[ %s ] 0" % " ".join("sym%d-%d" % (k, j) for j in range(40)))
        if r["status"] != "OK":
            break
    check(r["status"] == "RESOURCE_ERROR" and r["detail"] == "symbol_table_full",
          "H1: exhausting the symbol table is RESOURCE_ERROR / symbol_table_full (no crash)")
    check(ok(h.execute("keep"), "21") and h.proc.poll() is None, "H2: the host is alive and earlier state works")
    h.quit()

    # ---- I: loader exhaustion ------------------------------------------------------
    h = Host()
    h.execute("x: 21  mk: func [n] [ func [] [ n: + n 1 ] ]  c: mk 0")
    before = h.execute("c")["session"]
    big = " ".join("[ " + "[] " * 500 + "]" for _ in range(7))      # 3500 empty blocks
    r = h.execute(big)
    check(r["status"] == "RESOURCE_ERROR" and r["detail"] == "loader_exhausted",
          "I1: an oversized cell is RESOURCE_ERROR / loader_exhausted")
    check(r["session"]["loader_free"] == before["loader_free"], "I2: the loader heap is rolled back exactly")
    check(ok(h.execute("* x 2"), "42") and ok(h.execute("c"), "2") and ok(h.execute("y: 5  + x y"), "26"),
          "I3: the session resumes: globals, closure state and new definitions all work")
    h.quit()

    # ---- J: site-table exhaustion --------------------------------------------------
    h = Host()
    h.execute("x: 21")
    before = h.execute("0")["session"]
    sites = " ".join("[ " + "func [] 1 " * 140 + "]" for _ in range(5))  # 700 func sites
    r = h.execute(sites)
    check(r["status"] == "RESOURCE_ERROR" and r["detail"] == "site_table_full"
          and r["session"]["loader_free"] == before["loader_free"],
          "J1: 700 func sites is RESOURCE_ERROR / site_table_full, rolled back")
    check(ok(h.execute("f: func [n] [ + n 1 ]  f x"), "22"), "J2: a new func still parses and runs afterwards (22)")
    h.quit()

    # ---- L: restart ----------------------------------------------------------------
    h = Host()
    h.execute("gone: 99")
    check(ok(h.execute("gone"), "99"), "L1: a definition before restart")
    rr = h.restart()
    check(rr["op"] == "restart" and rr["status"] == "OK", "L2: restart reinitialises and reloads the libraries")
    r = h.execute("gone")
    check(r["status"] == "HALT", "L3: after restart the old definition no longer exists")
    check(ok(h.execute('str-eq "a" "a"'), "1"), "L4: the prelude is available again after restart")
    h.quit()

    # ---- M: independent hosts ------------------------------------------------------
    h1, h2 = Host(), Host()
    h1.execute("only-in-one: 1")
    check(h2.execute("only-in-one")["status"] == "HALT" and ok(h1.execute("only-in-one"), "1"),
          "M: two host processes do not share state")
    h1.quit()
    h2.quit()

    # ---- N: large stdout cannot deadlock --------------------------------------------
    h = Host()
    # four nested levels of 16 calls (call depth stays ~64: deep recursion
    # exhausts the return stack at ~100): 16^4 = 65,536 printed lines
    h.execute("p4: func [n] [ either > n 0 [ print n  p4 - n 1 ] [ 0 ] ]"
              "  p3: func [n] [ either > n 0 [ p4 16  p3 - n 1 ] [ 0 ] ]"
              "  p2: func [n] [ either > n 0 [ p3 16  p2 - n 1 ] [ 0 ] ]"
              "  p1: func [n] [ either > n 0 [ p2 16  p1 - n 1 ] [ 0 ] ]")
    t0 = time.time()
    r = h.execute("p1 16  7")
    lines = r["stdout"].split(b"\n")
    check(ok(r, "7") and r["stdout_bytes"] == len(r["stdout"]) > 128 * 1024
          and lines[0] == b"16" and lines[-2] == b"1" and len(lines) == 65537,
          "N: 65,536 printed lines (%d KB, several pipe buffers) arrive complete, in %.1fs"
          % (r["stdout_bytes"] // 1024, time.time() - t0))
    check(ok(h.execute("x: 3  x"), "3"), "N2: the host is responsive afterwards")
    h.quit()

    # ---- host killed mid-session is detected -----------------------------------------
    h = Host()
    h.execute("x: 1")
    h.proc.kill()
    try:
        h.execute("x")
        died = False
    except HostDied:
        died = True
    check(died, "S: a killed host is reported as HostDied, not a hang")
    h.close()

    if failures == 0:
        print("all host tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
