#!/usr/bin/env python3
"""Create B8 input variants by changing only the top-level call of existing
Glon sources. Function bodies are copied verbatim from B4/B7 artifacts."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent.parent/"workloads"
B4=ROOT/"experiments/glon-source-b4/tasks"
B7=ROOT/"experiments/glon-authoring-b7/trials/AE-seed-20261115"

def strip_call(src, call):
    # The existing B4/B7 task artifacts end with exactly one top-level call.
    # Remove only that last line; preserve all function bodies verbatim.
    lines=src.rstrip().splitlines()
    if lines:
        lines.pop()
    return "\n".join(lines).rstrip()+"\n"

fib= (B4/"fib.glon").read_text()
sum_src=(B4/"sum.glon").read_text()
ack= (B7/"15-ack-A/artifact").read_text()
choose=(B7/"04-choose-A/artifact").read_text()
for name,base,call in [
 ("fib_10",fib,"fib 10"),("fib_15",fib,"fib 15"),("fib_18",fib,"fib 18"),
 ("ack_22",ack,"ack 2 2"),("ack_23",ack,"ack 2 3"),("ack_33",ack,"ack 3 3"),
 ("sum_200",sum_src,"sum 200 0"),("sum_500",sum_src,"sum 500 0"),
 ("choose_6_2",choose,"choose 6 2"),("choose_10_5",choose,"choose 10 5"),
]:
    body=strip_call(base,call)
    (OUT/(name+".glon")).write_text(body+call+"\n")
print("workloads:", len(list(OUT.glob('*.glon'))))
