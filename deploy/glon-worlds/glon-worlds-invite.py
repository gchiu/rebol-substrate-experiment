#!/usr/bin/env python3
"""Create a single-use Glon Worlds pilot invitation code.

The raw code is printed once. Only its SHA-256 hex digest is stored in SQLite.
Run this on the VPS as root or the glon-worlds service user against the live
users.db. This script does not create users or change the running service.
"""

import argparse
import datetime as dt
import hashlib
import os
import secrets
import sqlite3
import sys
import time

DEFAULT_DB = os.environ.get("GLON_WORLDS_DB", "/var/lib/glon-worlds/users.db")
SCHEMA = (
    "CREATE TABLE IF NOT EXISTS auth_invites("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "code_hash TEXT NOT NULL UNIQUE, "
    "label TEXT NOT NULL, "
    "created_at INTEGER NOT NULL, "
    "expires_at INTEGER NOT NULL, "
    "used_at INTEGER, "
    "used_by INTEGER)"
)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--db", default=DEFAULT_DB, help="users.db path")
    ap.add_argument("--label", default="pilot", help="operator label")
    ap.add_argument("--days", type=int, default=7, help="validity in days")
    ap.add_argument("--code", help="explicit code for tests (otherwise generated)")
    args = ap.parse_args()
    if args.days < 1 or args.days > 365:
        ap.error("--days must be 1..365")
    code = args.code or secrets.token_hex(16)
    if len(code) < 8 or len(code) > 128:
        ap.error("code must be 8..128 bytes")
    now = int(time.time())
    expires = now + args.days * 86400
    code_hash = hashlib.sha256(code.encode("utf-8")).hexdigest()
    con = sqlite3.connect(args.db)
    try:
        con.execute("PRAGMA busy_timeout=5000")
        con.execute(SCHEMA)
        con.execute(
            "INSERT INTO auth_invites(code_hash,label,created_at,expires_at,used_at,used_by) "
            "VALUES (?,?,?,?,NULL,NULL)",
            (code_hash, args.label[:64], now, expires),
        )
        con.commit()
    except sqlite3.IntegrityError:
        print("FAIL: this invitation code already exists", file=sys.stderr)
        return 1
    finally:
        con.close()
    when = dt.datetime.fromtimestamp(expires, dt.timezone.utc).isoformat()
    print(code)
    print(f"label={args.label!r} expires={when}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
