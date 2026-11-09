"""experiments/glon-deflate/tests/demo.py -- the ASCII progress demonstration.

Compresses a generated text with Python's zlib (raw DEFLATE, windowBits -10),
then lets the Glon decoder inflate it through build/dzhost with progress on:
  - with a known total (the compressed file's size): a bar and a percentage;
  - with no total (-u): byte counters only, no invented percentage.
On a terminal the bar is redrawn in place (ANSI cursor-up, clear-line, CR);
otherwise, or with --lines, every update is its own line. The decompressed
bytes are checked against the original afterwards (a separate data run).

    python3 -I tests/demo.py [--lines] [--size N] [--host PATH]
"""
import argparse
import os
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fixtures  # noqa: E402

EXP = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(EXP))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lines", action="store_true", help="one line per update (no ANSI)")
    ap.add_argument("--size", type=int, default=12000)
    ap.add_argument("--host", default=os.path.join(EXP, "build", "dzhost"))
    a = ap.parse_args()
    data = fixtures.text(a.size, 42)
    comp = fixtures.deflate(data, 9, wbits=-10, mem=4)
    os.makedirs(os.path.join(EXP, "build"), exist_ok=True)
    src = os.path.join(EXP, "build", "demo.deflate")
    with open(src, "wb") as f:
        f.write(comp)
    mode = "1" if a.lines or not sys.stdout.isatty() else "2"
    print("demo: %d bytes of text, %d compressed (raw DEFLATE, zlib windowBits -10)" % (len(data), len(comp)))
    print("-- known total (compressed size), reading 512 bytes per request, feeding 128-byte chunks")
    sys.stdout.flush()
    r1 = subprocess.run([a.host, "-L", ROOT, "-p", mode, "-c", "128", "-r", "512", src])
    print("-- unknown total: counters only")
    sys.stdout.flush()
    r2 = subprocess.run([a.host, "-L", ROOT, "-p", "1", "-u", "-c", "128", "-r", "4096", src])
    out = os.path.join(EXP, "build", "demo.out")
    r3 = subprocess.run([a.host, "-L", ROOT, "-c", "128", "-r", "512", "-o", out, src])
    same = r3.returncode == 0 and open(out, "rb").read() == data
    print("-- data run: output %s the original (%d bytes)" % ("EQUALS" if same else "DIFFERS FROM", len(data)))
    sys.exit(0 if (r1.returncode == 0 and r2.returncode == 0 and same) else 1)


if __name__ == "__main__":
    main()
