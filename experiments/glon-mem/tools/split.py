"""tools/split.py LIB OUTDIR -- write each top-level definition of a Glon
library (a line starting `name:` at column 0, to its balanced end) into its
own file, in order, for per-definition cost accounting (tools/profile.c).
masm definitions are wrapped in [ ] as the loader requires."""
import os, sys
src = open(sys.argv[1]).read()
src = "\n".join(l.split(";;")[0] if ";;" in l else l for l in src.splitlines())
lines = src.splitlines()
defs, cur, depth = [], None, 0
for l in lines:
    if cur is None and l[:1] not in ("", " ", "[", "]") and ":" in l.split(" ")[0]:
        cur, depth = [l], l.count("[") - l.count("]")
    elif cur is not None:
        cur.append(l); depth += l.count("[") - l.count("]")
    if cur is not None and depth <= 0:
        defs.append("\n".join(cur)); cur = None
os.makedirs(sys.argv[2], exist_ok=True)
for i, d in enumerate(defs):
    name = d.split(":")[0]
    body = "[ " + d + " ]" if " masm " in d.split("\n")[0] else d
    open(os.path.join(sys.argv[2], "%02d.glon" % i), "w").write(body + "\n")
    print("%02d %s" % (i, name))
