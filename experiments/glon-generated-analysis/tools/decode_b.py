#!/usr/bin/env python3
"""Decode a B4-B tagged hex artifact into an indented role/operand tree.
This is an explanation of the original model output, not a reconstruction."""
import sys

ROLES = {1:"FUNC",2:"SEQ_BEGIN",3:"SEQ_END",4:"TOP_BEGIN",5:"TOP_END",
         6:"INT",7:"PARAM",8:"CALL",9:"BIN",10:"EITHER",
         11:"THEN_BEGIN",12:"THEN_END",13:"ELSE_BEGIN",14:"ELSE_END"}
BIN = {0:"+",1:"-",2:"*",3:"<",4:"<=",5:"=",6:">",7:">="}

def signed24(v):
    return v - (1 << 24) if v & 0x800000 else v

def main():
    words=[]
    for line in open(sys.argv[1]):
        for tok in line.split():
            words.append(int(tok,16) & 0xFFFFFFFF)
    if not words or words[0] != 0x42344231:
        print("bad magic", file=sys.stderr); return 2
    pos=1; depth=0
    def emit(text): print("  "*depth + text)
    def take():
        nonlocal pos
        w=words[pos]; pos+=1; return (w>>24)&0xFF, w & 0xFFFFFF
    while pos < len(words):
        role,arg=take()
        if role == 1:
            emit(f"FUNC id={arg>>8} arity={arg&0xff}")
        elif role in (2,4,11,13):
            emit(ROLES[role]); depth+=1
        elif role in (3,5,12):
            depth-=1; emit(ROLES[role])
        elif role == 14:       # ELSE_END also closes the EITHER
            depth-=1; emit(ROLES[role]); depth-=1
        elif role == 6:
            emit(f"INT {signed24(arg)} (operand 0x{arg:06x})")
        elif role == 7:
            emit(f"PARAM {arg}")
        elif role == 8:
            emit(f"CALL id={arg>>8} arity={arg&0xff}")
        elif role == 9:
            emit(f"BIN {BIN.get(arg,arg)}")
        elif role == 10:
            emit("EITHER"); depth+=1
        else:
            emit(f"UNKNOWN role={role} arg=0x{arg:06x}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
