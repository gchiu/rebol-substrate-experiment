#!/usr/bin/env python3
"""Focused Glon SQLite acceptance: example, restart, transaction, sandbox."""
import os
import pathlib
import shutil
import sqlite3
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
GLON = ROOT / "glon"
EXAMPLES = ROOT / "examples" / "sqlite-g3d"
TESTS = ROOT / "tests" / "sqlite-g3d"


def run(script, root):
    p = subprocess.run([str(GLON), "--io-root", str(root),
                        "--allow-file-read", "--allow-file-write", str(script)],
                       cwd=ROOT, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if p.returncode != 0:
        print(p.stdout, end="")
        print(p.stderr, end="", file=sys.stderr)
        raise SystemExit(f"command failed: {script} (rc={p.returncode})")
    return p.stdout


def main():
    if not GLON.exists():
        sys.exit(f"missing binary: {GLON}")
    base = pathlib.Path(tempfile.mkdtemp(prefix="glon-sqlite-accept-"))
    root = base / "root"
    root.mkdir()
    try:
        out = run(EXAMPLES / "example.glon", root)
        assert "SQLITE_EXAMPLE_OK" in out, out
        out = run(TESTS / "restart.glon", root)
        assert "SQLITE_RESTART_COUNT=2" in out, out
        out = run(TESTS / "transaction.glon", root)
        assert "SQLITE_TX_OK" in out, out
        out = run(TESTS / "badpath.glon", root)
        assert "SQLITE_BADPATH_REJECTED" in out, out
        assert not (base / "escape.db").exists(), "sandbox escape created a file"

        c = sqlite3.connect(str(root / "example.db"))
        rows = c.execute("select id,name,score,payload from people order by id").fetchall()
        c.close()
        assert rows[0][1] == "Ada" and rows[0][2] == 42 and rows[0][3] == b"\x00\x01\xff", rows
        assert rows[1][1] == "Grace" and rows[1][2] == 99 and rows[1][3] is None, rows

        c = sqlite3.connect(str(root / "transaction.db"))
        names = [r[0] for r in c.execute("select name from tx order by id").fetchall()]
        c.close()
        assert names == ["kept", "committed"], names

        print("PASS SQLite example/open/close")
        print("PASS integer/text/NULL/blob round trips")
        print("PASS restart persistence")
        print("PASS transaction rollback/commit")
        print("PASS sandbox rejects ../ path")
    finally:
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    main()
