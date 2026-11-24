#!/usr/bin/env python3
"""Run the Glon-level sec vocabulary test against a built ./glon.

Build the binary first, for example:
  make glon MODULES=sec OPENSSL_CFLAGS=... OPENSSL_LIBS=...
The test deliberately does not rebuild or choose OpenSSL flags.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GLON = ROOT / "glon"
TEST = ROOT / "tests" / "security-g3c" / "primitives.glon"

def main():
    if not GLON.exists():
        sys.exit(f"missing {GLON}")
    p = subprocess.run([str(GLON), str(TEST)], cwd=ROOT,
                       text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if p.returncode != 0 or "G3C_SEC_GLON_OK" not in p.stdout:
        print(p.stdout, end="")
        print(p.stderr, end="", file=sys.stderr)
        sys.exit(f"sec Glon test failed (rc={p.returncode})")
    print("PASS Glon sec SHA-256/scrypt/random/ct-eq/time")
    print(p.stdout.strip())

if __name__ == "__main__":
    main()
